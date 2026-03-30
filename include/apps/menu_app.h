#pragma once

// apps/menu_app.h — Main menu.
// Presents a scrolling list of available applications.
// Add new apps by adding an entry to the _items array in menu_app.cpp.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>

#include "core/app.h"
#include "drivers/matrix_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

#define UI_MENU_PIC_X         77
#define UI_MENU_PIC_Y         12
#define UI_MENU_PIC_WIDTH     48
#define UI_MENU_PIC_HEIGHT    42
#define UI_MENU_ROWS          2
#define UI_MENU_COLS          3
#define UI_MENU_SELECT_X      6
#define UI_MENU_SELECT_Y      13
#define UI_MENU_SELECT_X_OFST 25
#define UI_MENU_SELECT_Y_OFST 20
#define UI_MENU_LABEL_X       2
#define UI_MENU_LABEL_Y       63


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
    void   draw_pic( MatrixPanel_I2S_DMA * matrix );

private:
    int16_t  _selected  = 0;
    bool     _dirty     = true;  // redraw needed
    uint16_t _pic       = 0;     // what picture to draw in free space

    static const MenuItem _items[];
    static const int      _item_count;
};
