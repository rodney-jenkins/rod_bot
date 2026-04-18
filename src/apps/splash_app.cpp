// apps/splash_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "drivers/matrix_driver.h"
#include "apps/menu_app.h"
#include "apps/splash_app.h"

#include "img/rod_box.h"
#include "img/fesucket.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

// Access the global app manager (defined in main.cpp).
extern AppManager g_app_manager;


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void SplashApp::onEnter()
{
    Serial.print( "[splash] Open splash\r\n" );
    _enter_ms = millis();
    // matrix_clear();
}

AppCmd SplashApp::update() 
{
    if( millis() - _enter_ms >= SPLASH_DURATION_MS ) 
        {
        // Replace ourselves with the menu — user can never "back" to the splash.
        g_app_manager.setPending( new MenuApp() );
        return AppCmd::REPLACE;
        }

    return AppCmd::NONE;
}

void SplashApp::draw() 
{   
    MatrixPanel_I2S_DMA *p = matrix_panel();

    uint32_t elapsed = millis() - _enter_ms;

    static bool first_draw_done = false;     // has the first splash been drawn?
    static bool second_draw_done = false;    // has the second splash been drawn?

    // Determine which half we're in
    bool second_half = elapsed >= ( SPLASH_DURATION_MS / 2 );

    // First splash
    if( !second_half && !first_draw_done )
    {
        int idx = 0;

        for( int y = 0; y < PANEL_H; y++ )
        {
            for( int x = 0; x < PANEL_W; x++ )
            {
                uint8_t r = UI_RODBOX[idx++];
                uint8_t g = UI_RODBOX[idx++];
                uint8_t b = UI_RODBOX[idx++];

                p->drawPixelRGB888( x, y, r, g, b );
            }
        }

        first_draw_done = true;
    }

    // Second splash
    if( second_half && !second_draw_done )
    {
        p->clearScreen();  // wipe first image

        // draw_png( p, UI_FESUCKET, 0, 0, UI_FESUCKET_WIDTH, UI_FESUCKET_HEIGHT );

        second_draw_done = true;
    }
}