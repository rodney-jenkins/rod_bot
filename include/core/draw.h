#pragma once

// core/draw.h — For drawing to the matrix

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>
#include "drivers/matrix_driver.h"


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

void draw_png( MatrixPanel_I2S_DMA *matrix, const uint16_t *img, uint16_t x_ofst, uint16_t y_ofst, uint16_t width, uint16_t height );