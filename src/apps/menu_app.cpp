// apps/menu_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"
#include "drivers/matrix_driver.h"
#include "drivers/input_driver.h"

#include "core/app_manager.h"
#include "core/draw.h"
#include "apps/menu_app.h"
#include "apps/video_player/video_player_app.h"
#include "apps/snake_app.h"
// #include "apps/settings_app.h"    ← add future apps here

#include "ui/menu.h"
#include "ui/fes0.h"
#include "ui/bucket0.h"
#include "ui/menu_select.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

const MenuItem MenuApp::_items[] =
{
    { "Video Player", []() -> IApp * { return new VideoPlayerApp(); } },
    { "Photos",       []() -> IApp * { return new VideoPlayerApp(); } },
    { "Snake",        []() -> IApp * { return new SnakeApp();       } },
    { "Chat",         []() -> IApp * { return new VideoPlayerApp(); } },
};
const int MenuApp::_item_count = sizeof( _items ) / sizeof( _items[0] );


/*--- HELPERS -----------------------------------------------------------------------------------*/

static void draw_menu( MatrixPanel_I2S_DMA * matrix )
{
    draw_png( matrix, UI_MENU, 0, 0, UI_MENU_WIDTH, UI_MENU_HEIGHT );
}

void MenuApp::draw_pic( MatrixPanel_I2S_DMA * matrix )
{
    switch( _pic )
    {
        case( 0 ): // FES0
            draw_png( matrix, UI_FES0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
            break;
        
        case( 1 ): // BUCKET0
            draw_png( matrix, UI_BUCKET0, UI_MENU_PIC_X, UI_MENU_PIC_Y, UI_MENU_PIC_WIDTH, UI_MENU_PIC_HEIGHT );
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
                break;

            case( InputEvent::BTN_B ):
                Serial.print( "[menu] Button B Press\r\n" );
                _selected = max((int)_selected - 1, 0);
                _dirty = true;
                break;

            case( InputEvent::BTN_A ):
            case( InputEvent::TURN_CW ):
            case( InputEvent::TURN_CCW ):
            default:
                break;
        }
    }

    return AppCmd::NONE;
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
    uint16_t x_org = UI_MENU_SELECT_X + ( _selected % 3 ) * UI_MENU_SELECT_X_OFST;
    uint16_t y_org = UI_MENU_SELECT_Y + ( _selected / 3 ) * UI_MENU_SELECT_Y_OFST;
    draw_png( p, UI_MENU_SELECT, x_org, y_org, UI_MENU_SELECT_WIDTH, UI_MENU_SELECT_HEIGHT );

    p->setFont( &TomThumb );
    p->setTextSize( 1 );
    p->setTextColor( COLOR_TEXT );
    p->setCursor( UI_MENU_LABEL_X, UI_MENU_LABEL_Y );
    p->print( _items[_selected].label );
}
