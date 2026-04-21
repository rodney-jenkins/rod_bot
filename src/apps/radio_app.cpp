// apps/splash_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "core/wifi.h"
#include "drivers/input_driver.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

// Access the global app manager (defined in main.cpp).
extern AppManager g_app_manager;

static TaskHandle_t radioTaskHandle = nullptr;
static bool s_running = false;


/*--- LOCALS ------------------------------------------------------------------------------------*/

static void radio_task( void *param );


/*--- Public API --------------------------------------------------------------------------------*/

void radio_start( const char *playlistId )
{
    if( s_running )
    {
        return;
    }

    s_running = true;

    // Copy playlist ID into heap (safe for task)
    char *idCopy = strdup( playlistId );

    xTaskCreatePinnedToCore
    (
        radio_task,
        "RadioTask",
        8192,             // stack size (increase if needed)
        idCopy,
        1,
        &radioTaskHandle,
        1                 // run on core 1 (usually better for WiFi/audio)
    );
}

void radio_stop()
{
    if( !s_running )
    {
        return;
    }

    s_running = false;

    if( radioTaskHandle )
    {
        vTaskDelete( radioTaskHandle );
        radioTaskHandle = nullptr;
    }
}

bool radio_is_running()
{
    return s_running;
}

/*--- Helper functions --------------------------------------------------------------------------*/

void getPlaylists()
{
    HTTPClient http;

    http.begin( SRVR_PLAYLIST_CMD );
  
    int httpResponseCode = http.GET();
    if( httpResponseCode == 200 )
    {
        String payload = http.getString();
        Serial.println( "Available playlists:" );
        Serial.println( payload );
    }
    http.end();
}

void startPlaylist( const char *playlistId )
{
    HTTPClient http;
    String url = SRVR_PLAY_PLAYLIST_CMD + String( playlistId );
    http.begin( url );
  
    int httpResponseCode = http.POST( nullptr, 0 );

    if( code == 200 )
    {
        Serial.println( "[radio] Playlist started" );
    }
    else
    {
        Serial.printf( "[radio] Failed to start playlist: %d\n", code );
    }

    http.end();
}

void streamMusic( const char *playlistId )
{
    HTTPClient http;
    String url = SRVR_STREAM_PLAYLIST_CMD;
    url += String( playlistId );
    http.begin( url );
  
    int httpResponseCode = http.GET();
    if( httpResponseCode == 200 )
    {
        WiFiClient *stream = http.getStreamPtr();
    
        // Buffer for audio data
        uint8_t buffer[512];
        int bytesRead = 0;
    
        while( http.connected() && stream->available() )
        {
            int available = stream->available();
            int toRead = min( 512, available );
            bytesRead = stream->readBytes( buffer, toRead );
      
            if( bytesRead > 0 )
            {
                // Write to I2S for audio playback
                I2S.write( buffer, bytesRead );
            }
        }
    
        Serial.println( "Stream ended (all tracks in rotation completed)" );
    } 
    else 
    {
        Serial.println("Failed to start stream: " + String(httpResponseCode));
    }

    http.end();
}


/*--- TASKS -------------------------------------------------------------------------------------*/

static void radio_task(void *param)
{
    char *playlistId = (char *)param;

    Serial.println( "[radio] Task started" );

    // Step 1: Start playlist
    startPlaylist( playlistId );

    // Step 2: Open stream
    HTTPClient http;
    String url = SRVR_STREAM_PLAYLIST_CMD + String( playlistId );

    http.begin( url );
    int code = http.GET();

    if( code != 200 )
    {
        Serial.printf( "[radio] Stream failed: %d\n", code );
        http.end();
        free( playlistId );
        running = false;
        vTaskDelete( nullptr );
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    uint8_t buffer[1024];

    // Step 3: Stream loop
    while( running && http.connected() )
    {
        int available = stream->available();

        if( available > 0 )
        {
            int toRead = min( (int)sizeof(buffer), available );
            int bytesRead = stream->readBytes( buffer, toRead );

            if( bytesRead > 0 )
            {
                // Push to I2S
                size_t written;
                i2s_write( I2S_NUM_0, buffer, bytesRead, &written, portMAX_DELAY );
            }
        }
        else
        {
            // Avoid tight spin when no data
            vTaskDelay( 5 / portTICK_PERIOD_MS );
        }
    }

    Serial.println( "[radio] Stream ended" );

    http.end();
    free( playlistId );

    running = false;
    vTaskDelete( nullptr );
}

/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void RadioApp::onEnter()
{
    Serial.print( "[radio] Open radio\r\n" );

    matrix_clear();
    _dirty = true;

    // Start radio once when entering app
    radio_start( "3XvZuHnRyeys1UU9bh7Qyq" );
}

AppCmd RadioApp::update() 
{
    return AppCmd::NONE;
}

void RadioApp::onExit()
{
    // Stop radio when leaving app
    radio_stop();
}

void RadioApp::draw() 
{   
    if( !_dirty )
        {
        return;
        }
    _dirty = false;

    MatrixPanel_I2S_DMA *p = matrix_panel();

    draw_rect( p, COLOR_UI_MAIN, 0, 0, PANEL_W, PANEL_H );
}