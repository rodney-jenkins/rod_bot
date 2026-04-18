#pragma once

// apps/frame_app.h — Picture frame random photo viewer.
// Presents a scrolling list of available applications.
// Add new apps by adding an entry to the _items array in menu_app.cpp.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>
#include "config.h"

#include "core/wifi.h"
#include "drivers/matrix_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

#define FETCH_INTERVAL   ( 10000 ) // 10 seconds
#define BIG_ENDIAN       ( FALSE )
#define IMAGE_SIZE       ( PANEL_W * PANEL_H * 2 ) // RGB565 = 2 bytes per pixel


/*--- CLASSES -----------------------------------------------------------------------------------*/

class FrameApp : public IApp
{
public:
    void   onEnter()  override;
    void   onResume() override;
    AppCmd update()   override;
    void   draw()     override;

    AppCmd input_handler();
    void fetch_and_display_image();
};
