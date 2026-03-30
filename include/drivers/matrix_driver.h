#pragma once

// drivers/matrix_driver.h — HUB75 LED matrix abstraction.
// Thin wrapper around ESP32-HUB75-MatrixPanel-DMA.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "rod_format.h"


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Initialise panel.  Call once in setup().
void matrix_init();

// Access the underlying panel object for direct drawing (UI, splash, etc.).
MatrixPanel_I2S_DMA *matrix_panel();

// Decode and render a full .rod video frame onto the panel.
void matrix_render_frame( const uint8_t *video, uint16_t w, uint16_t h, PixelFormat fmt );

// Decode a 32×32 RGB565 .rod thumbnail and render it scaled to fill the panel.
void matrix_render_thumbnail( const uint8_t *thumb_rgb565 );

// Clear the panel to black.
void matrix_clear();
