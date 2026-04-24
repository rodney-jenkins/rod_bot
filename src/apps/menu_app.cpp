// apps/menu_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "drivers/matrix_driver.h"
#include "drivers/input_driver.h"
#include "apps/frame_app.h"
#include "apps/menu_app.h"
#include "apps/radio_app.h"
#include "apps/video_player/video_player_app.h"
#include "apps/snake_app.h"

#include "img/fes0.h"
#include "img/buck0.h"
#include "img/rod0.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

const MenuItem MenuApp::_items[] =
{
    { "Movie",        []() -> IApp * { return new VideoPlayerApp(); } },
    { "Frame",        []() -> IApp * { return new FrameApp(); } },
    { "Music",        []() -> IApp * { return new RadioApp(); } },
    { "Snake",        []() -> IApp * { return new SnakeApp();       } },
};
const int MenuApp::_item_count = sizeof( _items ) / sizeof( _items[0] );


/*--- HELPERS -----------------------------------------------------------------------------------*/

AppCmd MenuApp::input_handler()
{
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();
        switch( ev )
        {
            case( InputEvent::BTN_D ):
            {
                // Launch the selected app.
                Serial.print( "[menu] Button D Press\r\n" );
                IApp *next = _items[_selected].factory();
                g_app_manager.setPending( next );
                return AppCmd::PUSH;
            }

            case( InputEvent::BTN_C ):
                Serial.print( "[menu] Button C Press\r\n" );
                _selected = min( (int)_selected + 1, _item_count - 1 );
                _dirty = true;
                return AppCmd::NONE;

            case( InputEvent::BTN_B ):
                Serial.print( "[menu] Button B Press\r\n" );
                _selected = max( (int)_selected - 1, 0 );
                _dirty = true;
                return AppCmd::NONE;

            case( InputEvent::BTN_A ):
            case( InputEvent::TURN_CW ):
            case( InputEvent::TURN_CCW ):
            default:
                return AppCmd::NONE;
        }
    }
    return AppCmd::NONE;
}

void MenuApp::draw_menu( MatrixPanel_I2S_DMA * matrix )
{
    int x, y;

    matrix->setFont( nullptr );
    matrix->setTextSize( 1 );
    matrix->setTextColor( COLOR_TEXT );

    for( int i = 0; i < UI_MENU_NUM_BUTTONS; i++ )
    {
        x = UI_MENU_FIRST_BUTTON_X + UI_MENU_BUTTON_X_OFFSET * i;
        y = UI_MENU_FIRST_BUTTON_Y;

        draw_rect_unfilled( matrix, COLOR_UI_ACCENT, x, y, UI_MENU_BUTTON_WIDTH, UI_MENU_BUTTON_HEIGHT );
    }
}

void MenuApp::draw_pic( MatrixPanel_I2S_DMA * matrix )
{
    switch( _pic )
    {
        case( 0 ): // FES0
            draw_png( matrix, UI_ROD0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
            // draw_png( matrix, UI_FES0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
            break;
        
        case( 1 ): // BUCK0
            draw_png( matrix, UI_ROD0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
            // draw_png( matrix, UI_BUCK0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
            break;
        
        default:
            break;
    }
}


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void MenuApp::onEnter()
{
    _selected = 0;
    _dirty    = true;
    _pic = random( 0, 2 );
    matrix_clear();
}

void MenuApp::onResume()
{
    // Returning from a child app — redraw.
    _dirty = true;
    _pic = random( 0, 2 );
    matrix_clear();
}

AppCmd MenuApp::update() 
{
    AppCmd cmd = input_handler();

    return cmd;
}

void MenuApp::draw()
{   
    if( !_dirty )
        {
        return;
        }
    _dirty = false;

    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();

    // Draw menu
    draw_menu( p );
    draw_pic( p );

    // Draw selected box and label
    uint16_t x = UI_MENU_FIRST_BUTTON_X + 1 + _selected * UI_MENU_BUTTON_X_OFFSET;
    uint16_t y = UI_MENU_FIRST_BUTTON_Y + 1;
    draw_rect( p, COLOR_UI_MAIN, x, y, UI_MENU_BUTTON_WIDTH - 2, UI_MENU_BUTTON_HEIGHT - 2 );
    
    p->setCursor( 2, 55 );
    p->print( _items[_selected].label );
}
