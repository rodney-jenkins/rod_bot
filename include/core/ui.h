#pragma once

// core/draw.h — For drawing to the matrix

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>

#include "drivers/matrix_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

// UI palette (RGB565)
static constexpr uint16_t COLOR_BG          = 0x0000;     // black
static constexpr uint16_t COLOR_UI_MAIN     = 0x3368;     // forest green
static constexpr uint16_t COLOR_UI_ACCENT   = 0xBA80;     // burnt orange
static constexpr uint16_t COLOR_TEXT        = 0xFFDA;     // cream
static constexpr uint16_t COLOR_TRANSPARENT = 0xF81F;     // magenta


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

void draw_rect( MatrixPanel_I2S_DMA *matrix, uint16_t color, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height );
void draw_rect_unfilled( MatrixPanel_I2S_DMA *matrix, uint16_t color, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height );

void draw_png( MatrixPanel_I2S_DMA *matrix, const uint16_t *img, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height );
