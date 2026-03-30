// drivers/input_driver.cpp — Quadrature encoder + debounced buttons.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"
#include "drivers/input_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/



/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Public API

void input_init()
{
    Serial1.begin( 115200, SERIAL_8N1, INPUT_RX_PIN, INPUT_TX_PIN );
    Serial1.setRxBufferSize( 256 );
}

void input_update()
{
    // Nothing needed — ISRs push events directly.
    // This hook exists so apps can add polling-based input here if needed.
}

InputEvent input_next_event()
{
    char c = Serial1.read();

    switch( c )
    {
        case( 'A' ):
            return InputEvent::BTN_A;
        
        case( 'B' ): 
            return InputEvent::BTN_B;

        case( 'C' ):
            return InputEvent::BTN_C;
        
        case( 'D' ): 
            return InputEvent::BTN_D;

        case( '+' ):
            return InputEvent::TURN_CW;
        
        case( '-' ): 
            return InputEvent::TURN_CCW;

        default:
            return InputEvent::NONE;
    }
}

bool input_has_event()
{
    return Serial1.available();
}
