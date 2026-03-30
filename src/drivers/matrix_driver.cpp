// drivers/matrix_driver.cpp — HUB75 LED matrix driver.
// Thin wrapper around ESP32-HUB75-MatrixPanel-I2S-DMA.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"
#include "rod_format.h"
#include "drivers/matrix_driver.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

static MatrixPanel_I2S_DMA *s_panel = nullptr;


/*--- HELPERS -----------------------------------------------------------------------------------*/

// Expand a 5-bit channel value to 8-bit by replicating the top bits into the vacated lsbs.
// This gives a fuller range (31 → 255) rather than the truncated range (31 → 248) of a left shift.
static inline uint8_t expand5( uint8_t v ) { return ( v << 3 ) | ( v >> 2 ); }
static inline uint8_t expand6( uint8_t v ) { return ( v << 2 ) | ( v >> 4 ); }


/*--- PUBLIC API --------------------------------------------------------------------------------*/

void matrix_init()
{
    HUB75_I2S_CFG cfg
    (
        PANEL_W / 2,  // module width per panel (64 px each)
        PANEL_H,      // module height (pixels)
        2             // chain length  (2 panels side-by-side → 128 px wide)
    );

    // Apply pin assignments from config.h.
    // Three signals are remapped away from the library defaults to avoid
    // clashing with the I2S audio peripheral (see comments in config.h).
    cfg.gpio.r1  = HUB75_R1;
    cfg.gpio.g1  = HUB75_G1;
    cfg.gpio.b1  = HUB75_B1;
    cfg.gpio.r2  = HUB75_R2;
    cfg.gpio.g2  = HUB75_G2;
    cfg.gpio.b2  = HUB75_B2;
    cfg.gpio.a   = HUB75_A;
    cfg.gpio.b   = HUB75_B;
    cfg.gpio.c   = HUB75_C;
    cfg.gpio.d   = HUB75_D;
    cfg.gpio.e   = HUB75_E;
    cfg.gpio.lat = HUB75_LAT;
    cfg.gpio.oe  = HUB75_OE;
    cfg.gpio.clk = HUB75_CLK;

    s_panel = new MatrixPanel_I2S_DMA( cfg );
    s_panel->begin();
    s_panel->setBrightness8( 64 );  // ~25% — comfortable default; adjust as needed
    s_panel->clearScreen();
}

MatrixPanel_I2S_DMA *matrix_panel()
{
    return s_panel;
}

void matrix_render_frame( const uint8_t *video, uint16_t w, uint16_t h, PixelFormat fmt )
{
    if( !s_panel || !video ) return;

    const uint8_t *src = video;

    for( uint16_t y = 0; y < h; y++ )
    {
        for( uint16_t x = 0; x < w; x++ )
        {
            switch( fmt )
            {
                case PIXFMT_RGB565:
                {
                    // Source is already RGB565 — pass directly to drawPixel to avoid
                    // a needless RGB565 → RGB888 → RGB565 round-trip.
                    uint16_t color = (uint16_t)src[0] | ( (uint16_t)src[1] << 8 );
                    src += 2;
                    s_panel->drawPixel( x, y, color );
                    break;
                }

                case PIXFMT_RGB888:
                {
                    uint8_t r = *src++;
                    uint8_t g = *src++;
                    uint8_t b = *src++;
                    s_panel->drawPixelRGB888( x, y, r, g, b );
                    break;
                }

                case PIXFMT_RGB332:
                {
                    uint8_t px = *src++;
                    uint8_t r3 = ( px >> 5 ) & 0x07;
                    uint8_t g3 = ( px >> 2 ) & 0x07;
                    uint8_t b2 =   px        & 0x03;
                    uint8_t r  = r3 * 255 / 7;
                    uint8_t g  = g3 * 255 / 7;
                    uint8_t b  = b2 * 255 / 3;
                    s_panel->drawPixelRGB888( x, y, r, g, b );
                    break;
                }
            }
        }
    }
}

void matrix_render_thumbnail( const uint8_t *thumb_rgb565 )
{
    if( !s_panel || !thumb_rgb565 ) return;

    // The thumbnail is always 32×32 RGB565 (per RodHeader::thumbnail).
    // The panel is PANEL_W×PANEL_H (64×32).  Scale with nearest-neighbour:
    //   x: 64/32 = 2×  →  thumb_x = panel_x >> 1
    //   y: 32/32 = 1×  →  thumb_y = panel_y
    static constexpr uint16_t THUMB_W = 32;
    static constexpr uint16_t THUMB_H = 32;

    for( uint16_t py = 0; py < PANEL_H; py++ )
    {
        uint16_t ty = py * THUMB_H / PANEL_H;

        for( uint16_t px = 0; px < PANEL_W; px++ )
        {
            uint16_t tx    = px * THUMB_W / PANEL_W;
            uint16_t idx   = ( ty * THUMB_W + tx ) * 2;
            uint16_t color = (uint16_t)thumb_rgb565[idx] | ( (uint16_t)thumb_rgb565[idx + 1] << 8 );
            s_panel->drawPixel( px, py, color );
        }
    }
}

void matrix_clear()
{
    if( s_panel ) s_panel->clearScreen();
}
