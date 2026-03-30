#pragma once

// apps/splash_app.h — Boot splash screen.
// Displays the product logo/name for a fixed duration, then replaces itself
// with the main menu automatically.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include "core/app.h"


/*--- CLASSES -----------------------------------------------------------------------------------*/

class SplashApp : public IApp
{
public:
    void   onEnter()  override;
    AppCmd update()   override;
    void   draw()     override;

private:
    uint32_t _enter_ms = 0;
    static constexpr uint32_t SPLASH_DURATION_MS = 8000;
};
