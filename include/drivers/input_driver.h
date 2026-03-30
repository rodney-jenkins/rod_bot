#pragma once

// drivers/input_driver.h — Rotary encoder + push buttons.
// Call input_init() once in setup().
// Call input_update() once per loop() BEFORE AppManager::tick().

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

enum class InputEvent : uint8_t
{
    NONE,     // No input
    BTN_A,    // Button A press ( O )
    BTN_B,    // Button B press ( < )
    BTN_C,    // Button C press ( > )
    BTN_D,    // Button D press ( X )
    TURN_CW,  // Encoder CW turn
    TURN_CCW, // Encoder CCW turn
};


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Initialise GPIO and attach interrupts.
void input_init();

// Poll / debounce inputs.  Call once per loop().
void input_update();

// Returns and consumes the oldest event in the queue (NONE if empty).
InputEvent input_next_event();

// Returns true if there are pending events.
bool input_has_event();
