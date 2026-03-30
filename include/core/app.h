#pragma once

// core/app.h — IApp: the interface every screen/application must implement.
//
// The AppManager owns a stack of IApp*. Each tick it calls the top app's
// update() and draw().  An app signals transitions by returning an AppCmd.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

// Commands an app can return from update() to control the stack.
enum class AppCmd : uint8_t
{
    NONE,       // stay on this app, nothing changes
    PUSH,       // push a new app (set via AppManager::setPending before returning)
    POP,        // pop self — returns to the previous app on the stack
    REPLACE,    // replace self with a new app (pop + push, no "back")
    QUIT,       // unwind the whole stack and halt
};


/*--- CLASSES -----------------------------------------------------------------------------------*/

class IApp
{
public:
    virtual ~IApp() = default;

    // Called once when the app is pushed onto the stack.
    virtual void onEnter() = 0;

    // Called once when another app is pushed on top (app is now paused).
    virtual void onPause() {}

    // Called once when the app on top is popped and this one resumes.
    virtual void onResume() {}

    // Called once just before the app is popped and destroyed.
    virtual void onExit() {}

    // Called every loop iteration while this app is on top.
    // Returns an AppCmd to drive transitions.
    virtual AppCmd update() = 0;

    // Called every loop iteration after update(), while this app is on top.
    virtual void draw() = 0;
};
