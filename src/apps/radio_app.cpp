// apps/splash_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <SD_MMC.h>
#include <vector>
#include <driver/i2s.h>
#include "config.h"
#include "apps/radio_app.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

enum AppState {
    MUSIC_SELECTING_PLAYLIST = 0,
    MUSIC_PLAYING = 1
};

enum MenuSubstate {
    MENU_SCANNING = 0,      // loading playlist list
    MENU_BROWSING = 1,      // showing menu, waiting for selection
    MENU_STARTING_PLAY = 2  // preparing to play (gathering songs)
};

enum ButtonPress {
    BTN_NONE = 0,
    BTN_UP = 1,
    BTN_DOWN = 2,
    BTN_SELECT = 3
};


#define I2S_PORT        I2S_NUM_0
#define AUDIO_BUF_SIZE  4096
#define SONG_QUEUE_LEN  4
#define MAX_PATH_LEN    256
#define THUMB_W         50
#define THUMB_H         50
#define THUMB_SIZE      (THUMB_W * THUMB_H * 2)  // 50x50 RGB565 = 5000 bytes
#define META_STR_LEN    64

// Readahead ring buffer: must be a power of two.
// 64 KB in PSRAM gives ~370 ms headroom at 44.1 kHz / 16-bit / stereo.
#define RING_BUF_SIZE   (64 * 1024)
#define RING_BUF_MASK   (RING_BUF_SIZE - 1)


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

    http.begin( SRVR_PLAYLISTS_CMD );
  
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

    if( httpResponseCode == 200 )
    {
        Serial.println( "[radio] Playlist started" );
    }
    else
    {
        Serial.printf( "[radio] Failed to start playlist: %d\n", httpResponseCode );
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
        s_running = false;
        vTaskDelete( nullptr );
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    uint8_t buffer[1024];

    // Step 3: Stream loop
    while( s_running && http.connected() )
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

    s_running = false;
    vTaskDelete( nullptr );
}

/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void RadioApp::onEnter()
{
    Serial.print( "[radio] Open radio\r\n" );

    matrix_clear();
    _dirty = true;
    
    // Start radio once when entering app
    audio_init( 44100, 2 );
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