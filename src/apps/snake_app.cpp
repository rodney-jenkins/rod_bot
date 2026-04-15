// apps/snake_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"
#include "apps/snake_app.h"
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"

#include <Fonts/TomThumb.h>


/*--- HELPERS -----------------------------------------------------------------------------------*/

static SnakeDir turn_cw( SnakeDir d )
{
    switch( d )
    {
        case SnakeDir::UP:    return SnakeDir::RIGHT;
        case SnakeDir::RIGHT: return SnakeDir::DOWN;
        case SnakeDir::DOWN:  return SnakeDir::LEFT;
        case SnakeDir::LEFT:  return SnakeDir::UP;
    }
    return d;
}

static SnakeDir turn_ccw( SnakeDir d )
{
    switch( d )
    {
        case SnakeDir::UP:    return SnakeDir::LEFT;
        case SnakeDir::LEFT:  return SnakeDir::DOWN;
        case SnakeDir::DOWN:  return SnakeDir::RIGHT;
        case SnakeDir::RIGHT: return SnakeDir::UP;
    }
    return d;
}


/*--- PRIVATE METHODS ---------------------------------------------------------------------------*/

void SnakeApp::_draw_cell( int8_t x, int8_t y, uint16_t color )
{
    matrix_panel()->fillRect( x * SNAKE_CELL, y * SNAKE_CELL, SNAKE_CELL, SNAKE_CELL, color );
}

void SnakeApp::_place_food()
{
    Point candidate;
    bool  occupied;
    do
    {
        candidate.x = (int8_t)random( 0, SNAKE_COLS );
        candidate.y = (int8_t)random( 0, SNAKE_ROWS );
        occupied = false;
        for( uint16_t i = 0; i < _len; i++ )
        {
            uint16_t idx = ( _tail_idx + i ) % SNAKE_MAX_LEN;
            if( _body[idx].x == candidate.x && _body[idx].y == candidate.y )
            {
                occupied = true;
                break;
            }
        }
    } while( occupied );

    _food = candidate;
}

void SnakeApp::_reset()
{
    _len      = 3;
    _head_idx = 2;
    _tail_idx = 0;

    _body[0] = { (int8_t)( SNAKE_COLS / 2 - 2 ), (int8_t)( SNAKE_ROWS / 2 ) };
    _body[1] = { (int8_t)( SNAKE_COLS / 2 - 1 ), (int8_t)( SNAKE_ROWS / 2 ) };
    _body[2] = { (int8_t)( SNAKE_COLS / 2     ), (int8_t)( SNAKE_ROWS / 2 ) };

    _dir         = SnakeDir::RIGHT;
    _next_dir    = SnakeDir::RIGHT;
    _score       = 0;
    _grew        = false;
    _food_moved  = false;
    _moved       = false;
    _full_redraw = true;

    _place_food();

    _state        = SnakeState::PLAYING;
    _last_tick_ms = millis();
}

void SnakeApp::_tick()
{
    _dir = _next_dir;

    // Compute new head position.
    Point head = _body[_head_idx];
    switch( _dir )
    {
        case SnakeDir::UP:    head.y--; break;
        case SnakeDir::DOWN:  head.y++; break;
        case SnakeDir::LEFT:  head.x--; break;
        case SnakeDir::RIGHT: head.x++; break;
    }

    // Wall collision.
    if( head.x < 0 || head.x >= (int8_t)SNAKE_COLS ||
        head.y < 0 || head.y >= (int8_t)SNAKE_ROWS )
    {
        _state       = SnakeState::DEAD;
        _full_redraw = true;
        return;
    }

    // Self collision — check all segments except the tail (it will vacate this tick).
    for( uint16_t i = 0; i < _len - 1; i++ )
    {
        uint16_t idx = ( _tail_idx + i ) % SNAKE_MAX_LEN;
        if( _body[idx].x == head.x && _body[idx].y == head.y )
        {
            _state       = SnakeState::DEAD;
            _full_redraw = true;
            return;
        }
    }

    // Remember tail for incremental erase.
    _prev_tail  = _body[_tail_idx];
    _grew       = false;
    _food_moved = false;

    if( head.x == _food.x && head.y == _food.y )
    {
        // Snake eats food — grow, don't advance tail.
        _len++;
        _score++;
        _grew       = true;
        _food_moved = true;
    }
    else
    {
        _tail_idx = ( _tail_idx + 1 ) % SNAKE_MAX_LEN;
    }

    _head_idx        = ( _head_idx + 1 ) % SNAKE_MAX_LEN;
    _body[_head_idx] = head;

    if( _food_moved )
    {
        _place_food();
    }

    _moved = true;
}

void SnakeApp::_draw_all()
{
    MatrixPanel_I2S_DMA *mx = matrix_panel();
    mx->fillScreen( SNAKE_COL_BG );

    // Body segments (tail → second-to-last).
    for( uint16_t i = 0; i < _len - 1; i++ )
    {
        uint16_t idx = ( _tail_idx + i ) % SNAKE_MAX_LEN;
        _draw_cell( _body[idx].x, _body[idx].y, SNAKE_COL_BODY );
    }

    // Head.
    _draw_cell( _body[_head_idx].x, _body[_head_idx].y, SNAKE_COL_HEAD );

    // Food.
    _draw_cell( _food.x, _food.y, SNAKE_COL_FOOD );
}

void SnakeApp::_draw_game_over()
{
    MatrixPanel_I2S_DMA *mx = matrix_panel();

    // Overlay box.
    mx->fillRect( 28, 18, 72, 32, SNAKE_COL_BOX );
    mx->drawRect( 28, 18, 72, 32, SNAKE_COL_TEXT );

    mx->setFont( &TomThumb );
    mx->setTextColor( SNAKE_COL_TEXT );
    mx->setTextWrap( false );

    mx->setCursor( 46, 26 );
    mx->print( "GAME OVER" );

    mx->setCursor( 44, 34 );
    mx->print( "Score: " );
    mx->print( _score );

    mx->setCursor( 35, 42 );
    mx->print( "D:Retry  A:Menu" );
}


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void SnakeApp::onEnter()
{
    _reset();
}

AppCmd SnakeApp::update()
{
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();

        if( _state == SnakeState::DEAD )
        {
            if( ev == InputEvent::BTN_D )
            {
                _reset();
            }
            else if( ev == InputEvent::BTN_A || ev == InputEvent::BTN_B )
            {
                return AppCmd::POP;
            }
            continue;
        }

        // PLAYING — steer the snake.
        switch( ev )
        {
            case InputEvent::TURN_CW:
            case InputEvent::BTN_C:
                _next_dir = turn_cw( _dir );
                break;

            case InputEvent::TURN_CCW:
            case InputEvent::BTN_B:
                _next_dir = turn_ccw( _dir );
                break;

            case InputEvent::BTN_A:
                return AppCmd::POP;

            default:
                break;
        }
    }

    if( _state == SnakeState::PLAYING )
    {
        uint32_t tick_ms = max( SNAKE_TICK_MIN_MS, static_cast<uint32_t>( SNAKE_TICK_BASE_MS - ( _score / 5 ) * 10UL ) );
        if( millis() - _last_tick_ms >= tick_ms )
        {
            _last_tick_ms = millis();
            _tick();
        }
    }

    return AppCmd::NONE;
}

void SnakeApp::draw()
{
    if( _state == SnakeState::DEAD )
    {
        if( _full_redraw )
        {
            _draw_all();
            _draw_game_over();
            _full_redraw = false;
        }
        return;
    }

    // PLAYING — full redraw on entry or after reset.
    if( _full_redraw )
    {
        _draw_all();
        _full_redraw = false;
        _moved       = false;
        return;
    }

    // Incremental update: only redraw the cells that changed.
    if( !_moved )
        return;
    _moved = false;

    // Erase the vacated tail cell (unless the snake grew this tick).
    if( !_grew )
    {
        _draw_cell( _prev_tail.x, _prev_tail.y, SNAKE_COL_BG );
    }

    // Re-colour the previous head as a body segment.
    if( _len > 1 )
    {
        uint16_t prev_head = ( _head_idx - 1 + SNAKE_MAX_LEN ) % SNAKE_MAX_LEN;
        _draw_cell( _body[prev_head].x, _body[prev_head].y, SNAKE_COL_BODY );
    }

    // Draw the new head.
    _draw_cell( _body[_head_idx].x, _body[_head_idx].y, SNAKE_COL_HEAD );

    // Draw newly placed food (old food cell was overwritten by the head draw above).
    if( _food_moved )
    {
        _draw_cell( _food.x, _food.y, SNAKE_COL_FOOD );
    }
}
