// core/app_manager.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>

#include "core/app_manager.h"


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Clean up any apps left on the stack on shutdown.
AppManager::~AppManager()
{
    while( !_stack.empty() )
        {
        _stack.back()->onExit();
        delete _stack.back();
        _stack.pop_back();
        }
}


// Push new app to top of stack
void AppManager::push( IApp *app )
{
    // Serial.print( "[app_man] Pushing app\r\n" );
    if( top() )    top()->onPause();

    _stack.push_back( app );
    app->onEnter();
}


// Pop top app from stack
void AppManager::pop()
{
    if( _stack.empty() )    return;

    top()->onExit();
    delete top();

    _stack.pop_back();
    if( top() )
        top()->onResume();
}


// Replace app with another
void AppManager::replace( IApp *app )
{
    if( !_stack.empty() )
        {
        top()->onExit();
        delete top();
        _stack.pop_back();
        }

    _stack.push_back( app );
    app->onEnter();
}


// handle app tick
void AppManager::tick()
{
    if( _stack.empty() )    return;

    AppCmd cmd = top()->update();
    top()->draw();

    switch( cmd )
        {
        case( AppCmd::PUSH ):
            Serial.print( "[app_man] AppCmd::Push\r\n" );
            if( _pending )
            {
                // Serial.print( "[app_man] App pending\r\n" );
                push( _pending );
                _pending = nullptr; 
            }
            break;

        case( AppCmd::POP ):
            Serial.print( "[app_man] AppCmd::Pop\r\n" );
            pop();
            break;

        case( AppCmd::REPLACE ):
            Serial.print( "[app_man] AppCmd::Replace\r\n" );
            if( _pending )
            {
                replace( _pending );
                _pending = nullptr;
            }
            break;

        case( AppCmd::QUIT ):
            Serial.print( "[app_man] AppCmd::Quit\r\n" );
            while( !empty() ) pop();
            break;

        case( AppCmd::NONE ):
        default:
            break;
        }
}
