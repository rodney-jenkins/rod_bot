// apps/menu_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <HTTPClient.h>
#include "config.h"
#include "rod_format.h"

#include "core/app_manager.h"
#include "core/wifi.h"
#include "drivers/matrix_driver.h"
#include "drivers/input_driver.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;


/*--- CONSTANTS ---------------------------------------------------------------------------------*/


/*--- GLOBALS -----------------------------------------------------------------------------------*/

static unsigned long s_last_fetch = 0;


/*--- HELPERS -----------------------------------------------------------------------------------*/

AppCmd MenuApp::input_handler()
{
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();
        switch( ev )
        {
            case( InputEvent::BTN_A ):
                Serial.print( "[frame] Button A Press\r\n" );
                return AppCmd::POP;

            case( InputEvent::BTN_B ):
            case( InputEvent::BTN_C ):
            case( InputEvent::BTN_D ):
            case( InputEvent::TURN_CW ):
            case( InputEvent::TURN_CCW ):
            default:
                return AppCmd::NONE;
        }
    }
    return AppCmd::NONE;
}

void fetch_and_display_image()
{
    HTTPClient http;
    http.begin( SRVR_IMAGE_CMD );

    int response = http.GET();

    if( response == 200 )
    {
        // Read the RGB565 binary data
        int bytesRead = 0;
        WiFiClient *stream = http.getStreamPtr();

        while( http.connected() && bytesRead < IMAGE_SIZE )
        {
            int available = stream->available();
            if( available > 0 )
            {
                int toRead = min( available, IMAGE_SIZE - bytesRead );
                int read = stream->readBytes( &imageBuffer[bytesRead], toRead );
                bytesRead += read;
            }
        }
        
        if (bytesRead == IMAGE_SIZE)
        {
            // Render using your matrix function
            matrix_render_frame(
                imageBuffer,
                PANEL_W,
                PANEL_H,
                PIXFMT_RGB565
            );

            Serial.println("Image displayed successfully!");
        }
        else
        {
            Serial.println("Incomplete image received!");
        }
    }

    http.end();
}


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void MenuApp::onEnter()
{
    matrix_clear();
}

void MenuApp::onResume()
{
    matrix_clear();
}

AppCmd MenuApp::update() 
{
    AppCmd cmd = input_handler();

    return cmd;
}

void MenuApp::draw()
{   
    unsigned long now = millis();

    if( now - s_last_fetch >= FETCH_INTERVAL )
    {
        s_last_fetch = now;
        fetch_and_display_image();
    }
}
