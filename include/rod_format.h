#pragma once

// rod_format.h — binary layout of the .rod LED-matrix video format.
// Shared by the player, any future encoder, and file-validation utilities.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

static constexpr uint32_t ROD_MAGIC  = 0x524F4421; // "ROD!"
static constexpr size_t   TITLE_LEN  = 32;
static constexpr size_t   THUMB_LEN  = 2048;
static constexpr size_t   HEADER_PAD = 2079;

// .rod files support several different color formats
enum PixelFormat : uint8_t 
{
    PIXFMT_RGB332 = 0,  // 1 byte/px
    PIXFMT_RGB565 = 1,  // 2 bytes/px
    PIXFMT_RGB888 = 2,  // 3 bytes/px
};


/*--- TYPES -------------------------------------------------------------------------------------*/

// The .rod file header format
struct __attribute__( (packed) ) RodHeader 
{
    uint32_t magic;
    uint8_t  version;
    uint16_t panel_w;
    uint16_t panel_h;
    uint32_t frame_count;
    uint16_t fps_num;
    uint16_t fps_den;
    uint32_t sample_rate;
    uint8_t  channels;        // 1 = mono, 2 = stereo
    uint8_t  pixel_format;    // see PixelFormat below
    uint32_t duration_ms;
    char     title[TITLE_LEN];
    uint8_t  thumbnail[THUMB_LEN]; // RGB565, 32×32
    uint8_t  _pad[HEADER_PAD];
};
static_assert( sizeof(RodHeader) == 4186, "RodHeader size mismatch" );

// The .rod header per frame
struct __attribute__( (packed) ) FrameHeader
{
    uint32_t frame_number;
    uint32_t audio_sample_offset; // absolute sample index at which to display this frame
    uint16_t audio_samples;       // PCM samples per channel in this frame (varies ±1 due to fractional fps)
};
static_assert( sizeof(FrameHeader) == 10, "FrameHeader size mismatch" );
// NOTE: within each frame record on disk, audio PCM comes BEFORE video pixels.


/*--- MACROS ------------------------------------------------------------------------------------*/

inline uint8_t rod_bytes_per_pixel( PixelFormat fmt )
{
    switch( fmt )
    {
        case PIXFMT_RGB332: return 1;
        case PIXFMT_RGB888: return 3;
        case PIXFMT_RGB565:
        default:            return 2;
    }
}
