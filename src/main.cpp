// main.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_random.h>
#include <HTTPClient.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/wifi.h"
#include "drivers/audio_driver.h"
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"
#include "apps/splash_app.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

// Global app manager — referenced by app implementations via extern.
AppManager g_app_manager;


/*--- SETUP -------------------------------------------------------------------------------------*/

void setup()
{
    // Init UART
    Serial.begin( 115200 );
    delay( 300 );

    Serial.print( "[boot] RodBox starting...\r\n" );

    // Initialize SD Card
    SD_MMC.setPins( SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN );
    {
        static constexpr int  SD_RETRIES    = 5;
        static constexpr int  SD_RETRY_MS   = 200;
        bool mounted = false;
        for( int attempt = 1; attempt <= SD_RETRIES; attempt++ )
        {
            if( SD_MMC.begin( "/sdcard", true, true ) )
            {
                Serial.printf( "[boot] SD mounted (attempt %d).\r\n", attempt );
                mounted = true;
                break;
            }
            Serial.printf( "[boot] SD mount failed (attempt %d/%d).\r\n", attempt, SD_RETRIES );
            SD_MMC.end();
            delay( SD_RETRY_MS );
        }
        if( !mounted )
        {
            Serial.print( "[boot] SD unavailable — file browser will show no files.\r\n" );
        }
    }

    // Initialize input
    input_init();
    Serial.print( "[boot] Input ready.\r\n" );

    // Initialize LED matrix
    matrix_init();
    Serial.print( "[boot] Matrix ready.\r\n" );

    g_app_manager.push( new SplashApp() );
    Serial.print( "[boot] Entering main loop.\r\n" );

    randomSeed( esp_random() );  // seed with true RNG

    WiFi.connect( SSID, PASSWORD );
}


/*--- LOOP --------------------------------------------------------------------------------------*/

void loop() 
{
    input_update();          // poll/debounce inputs
    g_app_manager.tick();    // update + draw the top app, handle transitions

    if( g_app_manager.empty() ) 
        {
        // All apps exited — nothing to do.
        Serial.print( "[main] Empty app stack" );
        delay( 1000 );
        }
}