// core/ui.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include "config.h"
#include "drivers/matrix_driver.h"

#include "core/ui.h"


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

void draw_rect( MatrixPanel_I2S_DMA *matrix, uint16_t color, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height )
{
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            matrix->drawPixel( x + x_ofst, y + y_ofst, color );
        }
    }
}

void draw_rect_unfilled( MatrixPanel_I2S_DMA *matrix, uint16_t color, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height )
{
    for( int y = 0; y < height; y++ ) 
    {
        matrix->drawPixel( x_ofst, y + y_ofst, color );
        matrix->drawPixel( x_ofst + width - 1, y + y_ofst, color );
    }
    for( int x = 0; x < width; x++ )
    {
        matrix->drawPixel( x + x_ofst, y_ofst, color );
        matrix->drawPixel( x + x_ofst, y_ofst + height - 1, color );
    }
}

// Draw a PNG. Do not draw transparent pixels (magenta).
void draw_png( MatrixPanel_I2S_DMA *matrix, const uint16_t *img, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height )
{
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            uint16_t color = img[ y * width + x ];
            if( color != COLOR_TRANSPARENT )
            {
                matrix->drawPixel( x + x_ofst, y + y_ofst, color );
            }
        }
    }
}