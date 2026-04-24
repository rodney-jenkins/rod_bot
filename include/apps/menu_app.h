#pragma once

// apps/menu_app.h — Main menu.
// Presents a scrolling list of available applications.
// Add new apps by adding an entry to the _items array in menu_app.cpp.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>

#include "core/app.h"
#include "drivers/matrix_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

#define UI_MENU_NUM_BUTTONS       5

#define UI_MENU_BUTTON_WIDTH      6
#define UI_MENU_BUTTON_HEIGHT     6
#define UI_MENU_FIRST_BUTTON_X    2
#define UI_MENU_FIRST_BUTTON_Y    2
#define UI_MENU_BUTTON_X_OFFSET   9

#define UI_MENU_PIC_X            75
#define UI_MENU_PIC_Y             8
#define UI_MENU_PIC_WIDTH        50
#define UI_MENU_PIC_HEIGHT       50


/*--- CLASSES -----------------------------------------------------------------------------------*/

struct MenuItem 
{
    const char *label;
    IApp *(*factory)();   // function that creates the app instance
};


class MenuApp : public IApp
{
public:
    void   onEnter()  override;
    void   onResume() override;
    AppCmd update()   override;
    void   draw()     override;

    AppCmd input_handler();
    void   draw_menu( MatrixPanel_I2S_DMA * matrix );
    void   draw_pic( MatrixPanel_I2S_DMA * matrix );

private:
    int16_t  _selected  = 0;
    bool     _dirty     = true;  // redraw needed
    uint16_t _pic       = 0;     // what picture to draw in free space

    static const MenuItem _items[];
    static const int      _item_count;
};
