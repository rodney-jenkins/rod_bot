#!/usr/bin/env python3

'''
rod_mux.py — Encode any video file into the .rod LED-matrix format.

.rod file layout
  RodHeader                         4186 bytes  (fixed)
  FrameHeader + PCM + pixels  * N   variable    (one record per frame)
  uint64_t[N]                       8*N bytes   (frame index table, at index_offset)

Pixel data within each frame record:  audio PCM first, then video pixels.
Audio samples are per-channel (mono: N*int16, stereo: N*2*int16 interleaved L/R).

Usage
  python rod_mux.py input.mp4 output.rod
  python rod_mux.py input.mp4 output.rod --fps 24 --sample-rate 44100 --channels 1 --format rgb565 --title "My Movie"
  python rod_mux_v2.py input.mp4 output.rod --no-index   # disable seeking
'''

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path

import numpy as np
from PIL import Image

#--- Rod Format ----------------------------------------------------------------------------------#

ROD_MAGIC   = 0x524F4421   # "ROD!"
ROD_VERSION = 2

PIXFMT_RGB332 = 0
PIXFMT_RGB565 = 1
PIXFMT_RGB888 = 2

TITLE_LEN  = 32
THUMB_LEN  = 2048          # 32 × 32 × 2 bytes  (RGB565 thumbnail)
HEADER_PAD = 2071

THUMB_W = 32
THUMB_H = 32

# RodHeader packed struct (little-endian, matches C __attribute__((packed))):
#   magic(u32) version(u8) panel_w(u16) panel_h(u16) frame_count(u32)
#   fps_num(u16) fps_den(u16) sample_rate(u32) channels(u8) pixel_format(u8)
#   duration_ms(u32) index_offset(u64) title(32s) thumbnail(2048s) pad(2071s)
HEADER_FMT  = "<I B H H I H H I B B I Q 32s 2048s 2071s"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 4186, f"RodHeader size mismatch: {HEADER_SIZE}"

# FrameHeader packed struct:  frame_number(u32) audio_sample_offset(u32) audio_samples(u16)
FRAME_HDR_FMT  = "<I I H"
FRAME_HDR_SIZE = struct.calcsize(FRAME_HDR_FMT)
assert FRAME_HDR_SIZE == 10, f"FrameHeader size mismatch: {FRAME_HDR_SIZE}"

BYTES_PER_PIXEL = {PIXFMT_RGB332: 1, PIXFMT_RGB565: 2, PIXFMT_RGB888: 3}

# Maximum audio samples per channel per frame (uint16 field, and matches config.h)
MAX_AUDIO_SAMPLES = 4096


#--- Pixel conversion ----------------------------------------------------------------------------#

def _to_rgb565(arr: np.ndarray) -> bytes:
    """H*W*3 uint8 ndarray → little-endian RGB565 bytes."""
    r = arr[:, :, 0].astype(np.uint16)
    g = arr[:, :, 1].astype(np.uint16)
    b = arr[:, :, 2].astype(np.uint16)
    packed = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return packed.astype("<u2").tobytes()


def _to_rgb888(arr: np.ndarray) -> bytes:
    return arr.tobytes()


def _to_rgb332(arr: np.ndarray) -> bytes:
    r = arr[:, :, 0].astype(np.uint8)
    g = arr[:, :, 1].astype(np.uint8)
    b = arr[:, :, 2].astype(np.uint8)
    return ((r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)).tobytes()


CONVERTERS = {
    PIXFMT_RGB332: _to_rgb332,
    PIXFMT_RGB565: _to_rgb565,
    PIXFMT_RGB888: _to_rgb888,
}


#--- ffprobe -------------------------------------------------------------------------------------#

def _check_tool(name: str) -> None:
    try:
        subprocess.run([name, "-version"], capture_output=True, check=True)
    except FileNotFoundError:
        sys.exit(
            f"'{name}' not found on PATH.\n"
            "Install ffmpeg: https://ffmpeg.org/download.html"
        )


def probe( path: Path ) -> dict:
    """Return fps_num, fps_den, duration_s, has_audio from ffprobe."""
    cmd = [
        "ffprobe", "-v", "quiet",
        "-print_format", "json",
        "-show_streams", "-show_format",
        str( path ),
    ]
    result = subprocess.run( cmd, capture_output=True, text=True )
    if result.returncode != 0:
        sys.exit( f"ffprobe failed on {path}:\n{result.stderr}" )
    data = json.loads( result.stdout )

    fps_num, fps_den = 24, 1
    duration_s = 0.0
    has_audio = False

    for stream in data.get( "streams", [] ):
        ct = stream.get( "codec_type", "" )
        if ct == "video":
            parts = stream.get( "r_frame_rate", "24/1" ).split( "/" )
            fps_num = int( parts[0] )
            fps_den = int( parts[1] ) if len(parts) > 1 else 1
            if "duration" in stream:
                duration_s = float( stream["duration"] )
        elif ct == "audio":
            has_audio = True

    # Fall back to container duration if the video stream didn't expose one.
    if duration_s == 0.0:
        duration_s = float( data.get( "format", {} ).get( "duration", 0.0 ) )

    return dict( fps_num=fps_num, fps_den=fps_den, duration_s=duration_s, has_audio=has_audio )


#--- fps argument parsing ------------------------------------------------------------------------#

def parse_fps( s: str ):
    """Accept '24', '23.976', '24000/1001' → (fps_num, fps_den) as ints."""
    if "/" in s:
        n, d = s.split( "/", 1 )
        return int( n ), int( d )
    f = float( s )
    if f == int( f ):
        return int( f ), 1
    # Try common NTSC fractions first.
    for num, den in [(24000, 1001), (30000, 1001), (60000, 1001), (48000, 1001)]:
        if abs( f - num / den ) < 0.001:
            return num, den
    # General rational approximation (denominator ≤ 1001).
    frac = Fraction( f ).limit_denominator( 1001 )
    return frac.numerator, frac.denominator


#--- audio extraction ----------------------------------------------------------------------------#

def extract_audio( input_path: Path, tmp_dir: str, sample_rate: int, channels: int ):
    """Decode audio to a raw s16le PCM temp file.  Returns path or None."""
    out = os.path.join( tmp_dir, "audio.raw" )
    cmd = [
        "ffmpeg", "-y", "-i", str( input_path ),
        "-vn",
        "-ac", str( channels ),
        "-ar", str( sample_rate ),
        "-f",  "s16le",
        out,
    ]
    r = subprocess.run( cmd, capture_output=True )
    if r.returncode != 0 or not os.path.exists( out ) or os.path.getsize( out ) == 0:
        return None
    return out


#--- thumbnail -----------------------------------------------------------------------------------#

def make_thumbnail_from_image( path: Path ) -> bytes:
    """Load image, resize to 32*32, return RGB565 bytes."""
    img = Image.open( path ).convert( "RGB" ).resize( ( THUMB_W, THUMB_H ), Image.LANCZOS )
    data = _to_rgb565( np.array( img, dtype=np.uint8 ) )
    assert len( data ) == THUMB_LEN
    return data


def make_red_thumbnail() -> bytes:
    """Generate solid red 32×32 thumbnail."""
    arr = np.zeros( ( THUMB_H, THUMB_W, 3 ), dtype=np.uint8 )
    arr[:, :, 0] = 255  # red channel
    return _to_rgb565( arr )


#--- main ----------------------------------------------------------------------------------------#

def main():
    ap = argparse.ArgumentParser(
        description="Encode a video file into the .rod LED-matrix format v2.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument( "input",  help="Input video (any ffmpeg-supported format)" )
    ap.add_argument( "output", help="Output .rod file path" )
    ap.add_argument(
        "--fps", default=None,
        help="Output frame rate: 24, 23.976, 24000/1001, etc.  (default: match source)",
    )
    ap.add_argument( "--sample-rate", type=int, default=44100,
                     help="Audio sample rate in Hz (default: 44100)" )
    ap.add_argument( "--channels", type=int, default=1, choices=[1, 2],
                     help="Audio channels — 1=mono, 2=stereo (default: 1)" )
    ap.add_argument( "--format", default="rgb565", choices=["rgb332", "rgb565", "rgb888"],
                     help="Pixel format (default: rgb565)" )
    ap.add_argument( "--width", type=int, default=128, help="Panel width  (default: 128)" )
    ap.add_argument( "--height", type=int, default=64,  help="Panel height (default: 64)" )
    ap.add_argument( "--title", default=None,
                     help="Video title string (default: input filename without extension)" )
    ap.add_argument( "--letterbox", action="store_true",
                     help="Preserve aspect ratio with black bars (default: stretch to fill)" )
    ap.add_argument( "--no-index", action="store_true",
                     help="Omit the frame index table — disables in-player seeking" )
    ap.add_argument( "--thumbnail",
                     help="Path to PNG thumbnail (will be resized to 32x32). Default: solid red" )
    args = ap.parse_args()

    input_path  = Path( args.input )
    output_path = Path( args.output )

    if not input_path.exists():
        sys.exit( f"Input file not found: {input_path}" )

    _check_tool( "ffmpeg" )
    _check_tool( "ffprobe" )

    #--- Probe source --------------------------------------------------------#
    print( f"Probing {input_path.name}…" )
    info = probe( input_path )
    print( f"  Source fps:      {info['fps_num']}/{info['fps_den']}"
           f"  ({info['fps_num']/info['fps_den']:.3f})" )
    print( f"  Source duration: {info['duration_s']:.1f} s"
           f"  ({int(info['duration_s']//60)}:{int(info['duration_s']%60):02d})" )
    print( f"  Has audio:       {info['has_audio']}" )

    #--- Resolve parameters --------------------------------------------------#
    if args.fps:
        fps_num, fps_den = parse_fps(args.fps)
    else:
        fps_num, fps_den = info["fps_num"], info["fps_den"]

    # fps_num/fps_den must fit in uint16 (RodHeader)
    if fps_num > 0xFFFF or fps_den > 0xFFFF:
        sys.exit(
            f"fps {fps_num}/{fps_den} does not fit in uint16.  "
            "Use a simplified fraction, e.g. --fps 24000/1001."
        )

    pixel_fmt   = {"rgb332": PIXFMT_RGB332, "rgb565": PIXFMT_RGB565, "rgb888": PIXFMT_RGB888}[args.format]
    sample_rate = args.sample_rate
    channels    = args.channels
    panel_w     = args.width
    panel_h     = args.height
    title       = (args.title or input_path.stem)
    convert     = CONVERTERS[pixel_fmt]
    bpp         = BYTES_PER_PIXEL[pixel_fmt]
    write_index = not args.no_index
    fps_f       = fps_num / fps_den

    # Estimated frame count for progress display (may be slightly off).
    est_frames  = int(info["duration_s"] * fps_f) if info["duration_s"] > 0 else None

    print( f"\nEncoding:" )
    print( f"  Output fps:    {fps_num}/{fps_den}  ({fps_f:.4f})" )
    print( f"  Resolution:    {panel_w}*{panel_h}  {args.format.upper()}  ({bpp} B/px)" ) 
    print( f"  Audio:         {sample_rate} Hz  {channels}ch" )
    print( f"  Frame index:   {'yes' if write_index else 'no — seeking disabled'}" )
    if est_frames:
        index_mb = est_frames * 8 / 1024 / 1024
        est_mb   = (est_frames * (FRAME_HDR_SIZE
                    + math.floor(sample_rate / fps_f) * channels * 2
                    + panel_w * panel_h * bpp)
                    + HEADER_SIZE + (est_frames * 8 if write_index else 0)) / 1024 / 1024
        print( f"  Est. frames:   {est_frames}  (~{est_mb:.0f} MB"
              + (f", index ~{index_mb:.1f} MB" if write_index else "") + ")" )

    #--- extract and encode --------------------------------------------------#
    with tempfile.TemporaryDirectory() as tmp:

        # Audio: extract entire stream to a raw temp file for sequential reading.
        audio_file = None
        if info["has_audio"]:
            print("\nExtracting audio…")
            audio_path = extract_audio(input_path, tmp, sample_rate, channels)
            if audio_path:
                audio_bytes = os.path.getsize(audio_path)
                total_samples = audio_bytes // (channels * 2)
                print( f"  {total_samples:,} samples  ({audio_bytes/1024/1024:.1f} MB raw)" )
                audio_file = open( audio_path, "rb" )
            else:
                print( "  Warning: audio extraction failed — output will be silent." )
        else:
            print( "\nNo audio stream — output will be silent." )

        # Video: stream raw RGB24 frames out of ffmpeg one at a time.
        if args.letterbox:
            scale_filter = (
                f"scale={panel_w}:{panel_h}:force_original_aspect_ratio=decrease,"
                f"pad={panel_w}:{panel_h}:(ow-iw)/2:(oh-ih)/2:black"
            )
        else:
            scale_filter = f"scale={panel_w}:{panel_h}"

        ffmpeg_cmd = [
            "ffmpeg", "-i", str(input_path),
            "-vf",  scale_filter,
            "-r",   f"{fps_num}/{fps_den}",
            "-an",                     # no audio in this pipe; handled separately
            "-f",   "rawvideo",
            "-pix_fmt", "rgb24",
            "-",
        ]
        raw_frame_size = panel_w * panel_h * 3   # always rgb24 from ffmpeg

        print( "\nEncoding frames…" )
        video_proc = subprocess.Popen(
            ffmpeg_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )

        # Per-frame state
        frame_index        = []          # uint64 file offsets, one entry per frame
        frame_count        = 0
        audio_sample_cursor = 0          # cumulative samples written so far

        if args.thumbnail:
            thumb_path = Path( args.thumbnail )
            if not thumb_path.exists():
                sys.exit( f"Thumbnail not found: {thumb_path}" )
            print( f"Using custom thumbnail: {thumb_path}" )
            thumbnail = make_thumbnail_from_image( thumb_path )
        else:
            print( "Using default solid red thumbnail" )
            thumbnail = make_red_thumbnail()

        try:
            with open( output_path, "wb" ) as out:

                # Write a zeroed placeholder header — seeked back and filled at end.
                out.write( bytes( HEADER_SIZE ) )

                while True:
                    raw = video_proc.stdout.read( raw_frame_size )
                    if len( raw ) < raw_frame_size:
                        break   # clean end of stream or ffmpeg error

                    arr = np.frombuffer( raw, dtype=np.uint8 ).reshape( panel_h, panel_w, 3 )

                    # Compute audio samples for this frame.
                    # Uses floor() on cumulative positions to avoid drift — identical
                    # to what the player expects when reconstructing frame timing.
                    next_cursor   = math.floor( (frame_count + 1) * sample_rate * fps_den / fps_num )
                    audio_n       = next_cursor - audio_sample_cursor
                    audio_n       = min( audio_n, MAX_AUDIO_SAMPLES )     # guard uint16 and pcm_tmp
                    audio_n       = min( audio_n, 0xFFFF )                # fits FrameHeader uint16

                    # Read PCM from temp file; pad with silence if audio ends early.
                    want_bytes = audio_n * channels * 2
                    if audio_file:
                        pcm = audio_file.read( want_bytes )
                        if len( pcm ) < want_bytes:
                            pcm += bytes( want_bytes - len( pcm ) )
                    else:
                        pcm = bytes( want_bytes )

                    # Record this frame's absolute file position before writing.
                    frame_index.append( out.tell() )

                    # FrameHeader
                    out.write( struct.pack( FRAME_HDR_FMT,
                        frame_count,           # frame_number
                        audio_sample_cursor,   # audio_sample_offset (uint32 — fine up to ~27h)
                        audio_n,               # audio_samples per channel
                    ) )
                    # Audio PCM then video pixels (format matches player's readahead_task)
                    out.write( pcm )
                    out.write( convert( arr ) )

                    audio_sample_cursor = next_cursor
                    frame_count += 1

                    # Progress
                    if frame_count % 24 == 0:
                        if est_frames:
                            pct = frame_count / est_frames * 100
                            bar_w  = 30
                            filled = int( bar_w * frame_count / est_frames )
                            bar    = "█" * filled + "░" * ( bar_w - filled )
                            print( f"\r  [{bar}] {pct:5.1f}%  frame {frame_count}/{est_frames}",
                                  end="", flush=True )
                        else:
                            print( f"\r  Frame {frame_count}", end="", flush=True )

                print( f"\r  {'█'*30}  100.0%  frame {frame_count}/{frame_count}  ", flush=True )

                #--- write frame index table ---------------------------------#
                if write_index and frame_index:
                    index_offset = out.tell()
                    for offset in frame_index:
                        out.write( struct.pack( "<Q", offset ) )
                else:
                    index_offset = 0

                #--- seek back to position 0 and write the real RodHeader ----#
                duration_ms  = int( frame_count * fps_den * 1000 / fps_num ) if fps_num else 0
                title_bytes  = title.encode("utf-8")[:TITLE_LEN - 1]   # leave room for NUL
                title_padded = title_bytes.ljust( TITLE_LEN, b"\x00" )

                header = struct.pack(
                    HEADER_FMT,
                    ROD_MAGIC,
                    ROD_VERSION,
                    panel_w,
                    panel_h,
                    frame_count,
                    fps_num,
                    fps_den,
                    sample_rate,
                    channels,
                    pixel_fmt,
                    duration_ms,
                    index_offset,    # 0 if --no-index
                    title_padded,
                    thumbnail,
                    bytes( HEADER_PAD ),
                )
                out.seek( 0 )
                out.write( header )

        except KeyboardInterrupt:
            print( "\n\nInterrupted — removing partial output." )
            output_path.unlink( missing_ok=True )
            sys.exit( 1 )

        finally:
            video_proc.stdout.close()
            video_proc.wait()
            if audio_file:
                audio_file.close()

    #--- Summary -------------------------------------------------------------#
    if output_path.exists():
        file_size  = output_path.stat().st_size
        duration_s = frame_count / fps_f if fps_f else 0
        print( f"\nDone." )
        print( f"  Frames:    {frame_count:,}" )
        print( f"  Duration:  {int(duration_s // 3600)}h "
              f"{int((duration_s % 3600) // 60)}m "
              f"{int(duration_s % 60)}s" )
        print( f"  File size: {file_size / 1024 / 1024:.1f} MB" )
        if write_index:
            print( f"  Index:     {frame_count * 8 / 1024:.1f} KB"
                   f"  (@offset {index_offset:,})" )
        print( f"  Output:    {output_path}" )


if __name__ == "__main__":
    main()
