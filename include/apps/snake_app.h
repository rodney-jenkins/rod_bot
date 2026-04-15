#pragma once

// apps/snake_app.h — Classic Snake game.
//
// Controls (PLAYING):
//   TURN_CW  / BTN_C  — turn clockwise
//   TURN_CCW / BTN_B  — turn counter-clockwise
//   BTN_A             — return to menu
//
// Controls (DEAD / game-over screen):
//   BTN_D  — restart
//   BTN_A or BTN_B — return to menu

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>
#include "core/app.h"
#include "config.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

static constexpr uint8_t  SNAKE_CELL    = 4;                        // pixels per cell
static constexpr uint8_t  SNAKE_COLS    = PANEL_W / SNAKE_CELL;     // 32
static constexpr uint8_t  SNAKE_ROWS    = PANEL_H / SNAKE_CELL;     // 16
static constexpr uint16_t SNAKE_MAX_LEN = SNAKE_COLS * SNAKE_ROWS;  // 512

static constexpr uint32_t SNAKE_TICK_BASE_MS = 150;  // starting step interval (ms)
static constexpr uint32_t SNAKE_TICK_MIN_MS  =  60;  // fastest step interval (ms)

// Colors (RGB565)
static constexpr uint16_t SNAKE_COL_HEAD = 0x07E0;  // bright green
static constexpr uint16_t SNAKE_COL_BODY = 0x0320;  // dark green
static constexpr uint16_t SNAKE_COL_FOOD = 0xF800;  // red
static constexpr uint16_t SNAKE_COL_BG   = 0x0000;  // black
static constexpr uint16_t SNAKE_COL_TEXT = 0xFFFF;  // white
static constexpr uint16_t SNAKE_COL_BOX  = 0x2104;  // dark grey


/*--- ENUMS -------------------------------------------------------------------------------------*/

enum class SnakeDir   : uint8_t { UP, DOWN, LEFT, RIGHT };
enum class SnakeState : uint8_t { PLAYING, DEAD };


/*--- CLASSES -----------------------------------------------------------------------------------*/

class SnakeApp : public IApp
{
public:
    void   onEnter()  override;
    AppCmd update()   override;
    void   draw()     override;

private:
    struct Point { int8_t x; int8_t y; };

    void  _reset();
    void  _place_food();
    void  _tick();
    void  _draw_cell( int8_t x, int8_t y, uint16_t color );
    void  _draw_all();
    void  _draw_game_over();

    // Circular buffer: active range [_tail_idx .. _head_idx] (inclusive, wrapping)
    Point    _body[SNAKE_MAX_LEN];
    uint16_t _head_idx  = 0;
    uint16_t _tail_idx  = 0;
    uint16_t _len       = 0;

    Point      _food       = { 0, 0 };
    Point      _prev_tail  = { 0, 0 };  // tail position before last tick (used to erase it)
    bool       _grew       = false;     // snake grew last tick — do not erase tail
    bool       _food_moved = false;     // food was eaten — draw new food position

    SnakeDir   _dir      = SnakeDir::RIGHT;
    SnakeDir   _next_dir = SnakeDir::RIGHT;
    SnakeState _state    = SnakeState::DEAD;
    uint16_t   _score    = 0;

    uint32_t   _last_tick_ms = 0;
    bool       _full_redraw  = true;
    bool       _moved        = false;   // set by _tick(), cleared by draw()
};
