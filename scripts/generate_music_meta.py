"""
generate_music_meta.py — Generate .meta and .art files for RodBox radio app.

For each audio file in /music/<playlist>/, this script:
  1. Reads embedded metadata (artist, album) via mutagen.
  2. Extracts embedded album art, resizes to 50x50, converts to raw RGB565,
     and writes a .art file alongside the audio file.
  3. Writes a .meta text file with artist, album, and art filename.

Usage:
    python generate_music_meta.py <music_folder> [--force]

    music_folder   Path to the music directory (e.g. an SD card's /music folder).
    --force        Overwrite existing .meta and .art files.

Requirements:
    pip install mutagen Pillow
"""

import argparse
import os
import struct
import sys

from mutagen import File as MutagenFile
from PIL import Image
import io

AUDIO_EXTS = { ".mp3", ".wav", ".m4a", ".aac", ".flac", ".ogg" }
ART_SIZE   = 50  # 50x50 pixels

def rgb888_to_rgb565( r, g, b ):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def extract_metadata( filepath ):
    """Return (artist, album, art_bytes) from an audio file's tags."""
    audio = MutagenFile( filepath )
    artist = ""
    album  = ""
    art_bytes = None

    if audio is None:
        return artist, album, art_bytes

    # MP3 (ID3)
    if hasattr( audio, "tags" ) and audio.tags is not None:
        tags = audio.tags

        # ID3 tags (MP3)
        if hasattr( tags, "getall" ):
            # Artist
            for tag in ( "TPE1", "TPE2" ):
                frames = tags.getall( tag )
                if frames:
                    artist = str( frames[0] )
                    break
            # Album
            frames = tags.getall( "TALB" )
            if frames:
                album = str( frames[0] )
            # Art
            apic_frames = tags.getall( "APIC" )
            if apic_frames:
                art_bytes = apic_frames[0].data

        # MP4/M4A tags
        elif hasattr( tags, "get" ):
            artist = str( tags.get( "\xa9ART", [ "" ] )[0] )
            album  = str( tags.get( "\xa9alb", [ "" ] )[0] )
            covers = tags.get( "covr" )
            if covers:
                art_bytes = bytes( covers[0] )

        # FLAC / Vorbis
        if art_bytes is None and hasattr( audio, "pictures" ):
            pics = audio.pictures
            if pics:
                art_bytes = pics[0].data

    return artist, album, art_bytes

def make_art_file( art_bytes, out_path ):
    """Resize album art to 50x50 and write raw RGB565 binary."""
    img = Image.open( io.BytesIO( art_bytes ) ).convert( "RGB" )
    img = img.resize( ( ART_SIZE, ART_SIZE ), Image.LANCZOS )

    with open( out_path, "wb" ) as f:
        for y in range( ART_SIZE ):
            for x in range( ART_SIZE ):
                r, g, b = img.getpixel( ( x, y ) )
                f.write( struct.pack( "<H", rgb888_to_rgb565( r, g, b ) ) )

    print( f"  Art -> {os.path.basename( out_path )}" )

def make_meta_file( meta_path, artist, album, art_filename ):
    """Write a simple key=value .meta file."""
    with open( meta_path, "w", encoding="utf-8" ) as f:
        f.write( f"artist={artist}\n" )
        f.write( f"album={album}\n" )
        if art_filename:
            f.write( f"art={art_filename}\n" )

    print( f"  Meta -> {os.path.basename( meta_path )}" )

def process_file( filepath, force ):
    base, ext = os.path.splitext( filepath )
    meta_path = base + ".meta"
    art_path  = base + ".art"

    if not force and os.path.exists( meta_path ):
        print( f"  Skipping (meta exists): {os.path.basename( filepath )}" )
        return

    artist, album, art_bytes = extract_metadata( filepath )

    art_filename = ""
    if art_bytes:
        make_art_file( art_bytes, art_path )
        art_filename = os.path.basename( art_path )
    else:
        print( f"  No embedded art: {os.path.basename( filepath )}" )

    make_meta_file( meta_path, artist, album, art_filename )

def main():
    parser = argparse.ArgumentParser( description="Generate .meta/.art files for RodBox radio." )
    parser.add_argument( "music_folder", help="Path to the music directory on the SD card." )
    parser.add_argument( "--force", action="store_true", help="Overwrite existing .meta/.art files." )
    args = parser.parse_args()

    music_dir = args.music_folder
    if not os.path.isdir( music_dir ):
        print( f"Error: '{music_dir}' is not a directory." )
        sys.exit( 1 )

    count = 0
    for dirpath, dirnames, filenames in os.walk( music_dir ):
        for fname in sorted( filenames ):
            _, ext = os.path.splitext( fname )
            if ext.lower() in AUDIO_EXTS:
                filepath = os.path.join( dirpath, fname )
                print( f"Processing: {filepath}" )
                process_file( filepath, args.force )
                count += 1

    print( f"\nDone. Processed {count} audio file(s)." )

if __name__ == "__main__":
    main()
