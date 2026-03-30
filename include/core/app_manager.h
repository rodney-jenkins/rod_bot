#pragma once

// core/app_manager.h — Stack-based application manager.
//
// Usage:
//   AppManager mgr;
//   mgr.push(new SplashApp());
//   // in loop():
//   mgr.tick();

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <vector>

#include "app.h"


/*--- CLASSES -----------------------------------------------------------------------------------*/

class AppManager 
{
public:
    AppManager() = default;
    ~AppManager();

    // Push an app onto the stack and call its onEnter().
    // Pauses the current top app first.
    void push(IApp *app);

    // Pop the top app (calls onExit + delete), then resumes the one below.
    void pop();

    // Replace the top app with a new one (onExit old, onEnter new).
    void replace(IApp *app);

    // Returns true if the stack is empty (application should halt).
    bool empty() const { return _stack.empty(); }

    // Set the next app to push/replace (called by an app before returning
    // AppCmd::PUSH or AppCmd::REPLACE from update()).
    void setPending(IApp *app) { _pending = app; }

    // Call once per loop() iteration.  Drives the top app; handles transitions.
    void tick();

private:
    std::vector<IApp *> _stack;
    IApp               *_pending = nullptr;

    IApp *top() const { return _stack.empty() ? nullptr : _stack.back(); }
};
