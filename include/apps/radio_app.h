#pragma once

// apps/splash_app.h — Boot splash screen.
// Displays the product logo/name for a fixed duration, then replaces itself
// with the main menu automatically.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include "core/app.h"


/*--- CLASSES -----------------------------------------------------------------------------------*/

class RadioApp : public IApp
{
public:
    void   onEnter()  override;
    void   onExit()   override;
    AppCmd update()   override;
    void   draw()     override;

private:
    bool   _dirty     = true;  // redraw needed
};


/*--- PROTOTYPES ----------------------------------------------------------------------------------*/

void radio_start( const char *playlistId );

void radio_stop();

bool radio_is_running();