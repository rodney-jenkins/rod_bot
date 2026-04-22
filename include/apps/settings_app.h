#pragma once

// apps/settings_app.h — System settings and preferences.
// Allows user to adjust brightness and other system parameters.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include "core/app.h"


/*--- CLASSES -----------------------------------------------------------------------------------*/

class SettingsApp : public IApp
{
public:
    void   onEnter()  override;
    void   onExit()   override;
    AppCmd update()   override;
    void   draw()     override;

private:
    enum SettingType
    {
        BRIGHTNESS,
        VOLUME,
        NUM_SETTINGS
    };

    SettingType _selected_setting = BRIGHTNESS;
    uint8_t     _brightness = 192;      // 0-255, default 75%
    uint8_t     _volume     = 192;      // 0-255, default 75%
    
    bool        _dirty = true;

    void load_settings();
    void save_settings();
    void apply_brightness();
    void apply_volume();
};
