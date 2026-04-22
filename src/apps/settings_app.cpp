// apps/settings_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "drivers/matrix_driver.h"
#include "drivers/input_driver.h"
#include "drivers/audio_driver.h"
#include "apps/settings_app.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;

// NVS namespace for rod settings
static constexpr const char *NVS_NAMESPACE = "rod_settings";


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void SettingsApp::onEnter()
{
    Serial.print( "[settings] Opened settings app\r\n" );

    matrix_clear();
    _dirty = true;

    load_settings();
}

void SettingsApp::onExit()
{
    save_settings();
}

AppCmd SettingsApp::update()
{
    // Normal settings menu navigation
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();

        switch( ev )
        {
            case InputEvent::BTN_A:
                // Exit settings
                return AppCmd::POP;

            case InputEvent::TURN_CW:
            {
                // Increase selected setting
                if( _selected_setting == BRIGHTNESS )
                {
                    _brightness = (_brightness <= 230) ? _brightness + 25 : 255;
                    apply_brightness();
                }
                else if( _selected_setting == VOLUME )
                {
                    _volume = (_volume <= 230) ? _volume + 25 : 255;
                    apply_volume();
                }
                _dirty = true;
                break;
            }

            case InputEvent::TURN_CCW:
            {
                // Decrease selected setting
                if( _selected_setting == BRIGHTNESS )
                {
                    _brightness = (_brightness >= 25) ? _brightness - 25 : 0;
                    apply_brightness();
                }
                else if( _selected_setting == VOLUME )
                {
                    _volume = (_volume >= 25) ? _volume - 25 : 0;
                    apply_volume();
                }
                _dirty = true;
                break;
            }

            case InputEvent::BTN_B:
            {
                // Move to previous setting
                int sel = (int)_selected_setting - 1;
                if( sel < 0 ) sel = (int)NUM_SETTINGS - 1;
                _selected_setting = (SettingType)sel;
                _dirty = true;
                break;
            }

            case InputEvent::BTN_C:
            {
                // Move to next setting
                int sel = (int)_selected_setting + 1;
                if( sel >= (int)NUM_SETTINGS ) sel = 0;
                _selected_setting = (SettingType)sel;
                _dirty = true;
                break;
            }

            case InputEvent::BTN_D:
            default:
                break;
        }
    }

    return AppCmd::NONE;
}

void SettingsApp::draw()
{
    if( !_dirty )
    {
        return;
    }

    _dirty = false;

    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();

    p->setFont( &TomThumb );
    p->setTextColor( COLOR_TEXT );

    // Title
    p->setCursor( 5, 8 );
    p->print( "Settings" );

    // Draw settings based on selection
    int y = 20;

    // Brightness
    uint16_t color = (_selected_setting == BRIGHTNESS) ? COLOR_UI_NOTICE : COLOR_TEXT;
    p->setTextColor( color );
    p->setCursor( 5, y );
    p->print( "Brightness" );
    
    uint8_t percent = (_brightness * 100) / 255;
    char buf[16];
    snprintf( buf, sizeof(buf), "%u%%", percent );
    p->setCursor( 80, y );
    p->print( buf );

    // Volume
    y += 12;
    color = (_selected_setting == VOLUME) ? COLOR_UI_NOTICE : COLOR_TEXT;
    p->setTextColor( color );
    p->setCursor( 5, y );
    p->print( "Volume" );

    uint8_t vol_pct = (_volume * 100) / 255;
    snprintf( buf, sizeof(buf), "%u%%", vol_pct );
    p->setCursor( 80, y );
    p->print( buf );

    // Controls hint at bottom
    p->setTextColor( COLOR_TEXT );
    p->setCursor( 5, 56 );
    p->print( "B/C: Select" );
    p->setCursor( 5, 62 );
    p->print( "Enc: Adjust" );
}


/*--- SETTINGS MANAGEMENT -----------------------------------------------------------------------*/

void SettingsApp::load_settings()
{
    esp_err_t err = nvs_flash_init();
    if( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND )
    {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    }

    nvs_handle_t nvs_handle;
    err = nvs_open( NVS_NAMESPACE, NVS_READONLY, &nvs_handle );

    if( err == ESP_OK )
    {
        // Load brightness
        err = nvs_get_u8( nvs_handle, "brightness", &_brightness );
        if( err != ESP_OK )
        {
            Serial.println( "[settings] No brightness setting found, using default" );
            _brightness = 192;
        }
        else
        {
            Serial.printf( "[settings] Loaded brightness: %u\n", _brightness );
        }

        // Load volume
        err = nvs_get_u8( nvs_handle, "volume", &_volume );
        if( err != ESP_OK )
        {
            Serial.println( "[settings] No volume setting found, using default" );
            _volume = 192;
        }
        else
        {
            Serial.printf( "[settings] Loaded volume: %u\n", _volume );
        }

        nvs_close( nvs_handle );
    }
    else if( err == ESP_ERR_NVS_NOT_FOUND )
    {
        Serial.println( "[settings] NVS namespace not found, using defaults" );
        _brightness = 192;
    }
    else
    {
        Serial.printf( "[settings] Error opening NVS: %s\n", esp_err_to_name(err) );
        _brightness = 192;
    }

    apply_brightness();
    apply_volume();
}

void SettingsApp::save_settings()
{
    esp_err_t err;
    nvs_handle_t nvs_handle;

    err = nvs_open( NVS_NAMESPACE, NVS_READWRITE, &nvs_handle );
    if( err == ESP_OK )
    {
        // Save brightness
        err = nvs_set_u8( nvs_handle, "brightness", _brightness );
        if( err == ESP_OK )
        {
            Serial.printf( "[settings] Saved brightness: %u\n", _brightness );
        }

        // Save volume
        err = nvs_set_u8( nvs_handle, "volume", _volume );
        if( err == ESP_OK )
        {
            Serial.printf( "[settings] Saved volume: %u\n", _volume );
        }

        // Commit all changes
        err = nvs_commit( nvs_handle );
        if( err != ESP_OK )
        {
            Serial.printf( "[settings] Error committing NVS: %s\n", esp_err_to_name(err) );
        }

        nvs_close( nvs_handle );
    }

    if( err != ESP_OK )
    {
        Serial.printf( "[settings] Error saving settings: %s\n", esp_err_to_name(err) );
    }
}

void SettingsApp::apply_brightness()
{
    MatrixPanel_I2S_DMA *p = matrix_panel();
    if( p )
    {
        p->setBrightness8( _brightness );
        Serial.printf( "[settings] Applied brightness: %u\n", _brightness );
    }
}

void SettingsApp::apply_volume()
{
    audio_set_volume( _volume );
    Serial.printf( "[settings] Applied volume: %u\n", _volume );
}


