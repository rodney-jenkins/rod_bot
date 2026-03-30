// apps/video_player/file_browser.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <SD_MMC.h>
#include <Fonts/TomThumb.h>
#include "apps/menu_app.h"
#include "config.h"
#include "drivers/audio_driver.h"
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"

#include "core/app_manager.h"
#include "core/draw.h"
#include "apps/video_player/file_browser.h"

#include "ui/videos.h"
#include "ui/video_item.h"
#include "ui/selector.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;

FileBrowser::FileBrowser
(
    std::function<void(const std::string &)> on_select,
    std::function<void()>                    on_cancel
): _on_select( on_select ), _on_cancel( on_cancel ) {}


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/\

void FileBrowser::scan_sd()
{
    _files.clear();
    File root = SD_MMC.open( "/" );

    if( !root )    return;

    File entry;
    while( ( entry = root.openNextFile() ) )
        {
        if( !entry.isDirectory() )
            {
            std::string name = entry.name();
            if( ( name.size() >= 4 ) 
             && ( name.substr(name.size() - 4) == ".rod" ) )
                {
                _files.push_back( "/" + name );
                }
            }
        entry.close();
        }
    root.close();
}

const char *FileBrowser::basename( const std::string &path ) const
{
    size_t pos = path.rfind( '/' );
    return ( pos == std::string::npos ) ? path.c_str() : path.c_str() + pos + 1;
}

void FileBrowser::load_preview()
{
    _preview_valid = false;
    _preview_path  = _files[_selected];

    File f = SD_MMC.open( _preview_path.c_str(), FILE_READ );
    if( !f ) return;

    bool ok = ( f.read( (uint8_t *)&_preview_hdr, sizeof(_preview_hdr) ) == sizeof(_preview_hdr) )
           && ( _preview_hdr.magic == ROD_MAGIC );
    f.close();
    _preview_valid = ok;
}

/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void FileBrowser::onEnter()
{
    Serial.print( "[fb] Entered\r\n" );

    scan_sd();
    _selected = 0;
    _scroll   = 0;
    _dirty    = true;
    // matrix_clear();
}

AppCmd FileBrowser::update()
{
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();
        switch( ev )
        {
            case( InputEvent::BTN_A ):
                Serial.print( "[fb] Button A Press\r\n" );
                return AppCmd::POP;

            case( InputEvent::BTN_B ):
                Serial.print( "[fb] Button B Press\r\n" );
                _selected = max( (int)_selected - 1, 0 );
                if( _selected < _scroll )
                {
                    _scroll = _selected;
                }
                _dirty = true;
                break;

            case( InputEvent::BTN_C ):
                Serial.print( "[fb] Button C Press\r\n" );
                _selected = min( (int)_selected + 1, (int)_files.size() - 1 );
                if( _selected >= _scroll + VISIBLE_ROWS )
                {
                    _scroll = _selected - VISIBLE_ROWS + 1;
                }
                _dirty = true;
                break;

            case( InputEvent::BTN_D ):
                Serial.print( "[fb] Button D Press\r\n" );
                if( !_files.empty() && _on_select )
                {
                    _on_select( _files[_selected] );
                }
                return AppCmd::POP;

            case( InputEvent::TURN_CW ):
            case( InputEvent::TURN_CCW ):
            default:
                break;
        }
    }
    return AppCmd::NONE;
}

void FileBrowser::draw()
{
    if( !_dirty )
    {
        return;
    }
    _dirty = false;

    // Load the preview header if the selection changed since the last draw.
    if( !_files.empty() && ( _files[_selected] != _preview_path ) )
    {
        load_preview();
    }

    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();

    // Draw background
    draw_png( p, UI_VIDEOS, 0, 0, UI_VIDEOS_WIDTH, UI_VIDEOS_HEIGHT );

    // Draw file list
    if( _files.empty() )
    {
        p->setFont( nullptr );
        p->setTextColor( COLOR_TEXT );
        p->setCursor( 2, 32 );
        p->print( "No .rod files" );
        return;
    }

    // Draw buttons
    for( int i = 0; i < 3 && i < _files.size(); i++ )
        {
        draw_png( p, UI_VIDEO_ITEM, UI_BUTTON_X, UI_BUTTON_Y + i * UI_BUTTON_Y_OFST, UI_VIDEO_ITEM_WIDTH, UI_VIDEO_ITEM_HEIGHT );
        }

    // Fill button names
    for( int i = 0; i < VISIBLE_ROWS; i++ )
    {
        int  idx = _scroll + i;
        if( idx >= (int)_files.size() )
        {
            break;
        }
        int  y   = UI_BUTTON_Y + i * UI_BUTTON_Y_OFST;
        bool sel = ( idx == _selected );

        if( sel )
        {
            draw_png( p, UI_SELECTOR, UI_BUTTON_X - 1, y - 1, UI_SELECTOR_WIDTH, UI_SELECTOR_HEIGHT );
        }

        p->setFont( nullptr );
        p->setTextColor( COLOR_TEXT );
        p->setCursor( UI_BUTTON_X + 2, y + 2 );  // baseline = row_top + ascent(8)

        // 10 chars max: 10×6px = 60px = 60px max
        char name[11];
        strncpy( name, basename( _files[idx] ), 10 );
        name[10] = '\0';
        p->print( name );
    }

    // Arrows: TODO

    // Metadata
    {
        p->setFont( &TomThumb );
        p->setTextColor( COLOR_TEXT );
        p->setCursor( METADATA_X, METADATA_Y );
        
        // Write metadata
        if( !_preview_valid )
        {
            p->print( "No prev" );
            return;
        }

        uint32_t total_s = _preview_hdr.duration_ms / 1000;
        uint32_t mins    = total_s / 60;
        uint32_t secs    = total_s % 60;
        uint16_t fps = ( _preview_hdr.fps_den > 0 ) ? _preview_hdr.fps_num / _preview_hdr.fps_den : 0;

        char buf[ 11 ];
        snprintf( buf, sizeof(buf), "%u:%02u %ufps", mins, secs, fps );
        p->print( buf );
    }

    // Title
    {
        char title[30];
        if( _preview_hdr.title[0] != '\0' )
        {
            strncpy( title, _preview_hdr.title, 29 );
        }
        else
        {
            strncpy( title, basename( _preview_path ), 29 );
        }
        title[29] = '\0';
        p->setCursor( NAME_X, NAME_Y );
        p->print( title );
    }
    
    // Thumbnail
    const uint8_t *thumb = _preview_hdr.thumbnail;
    for( int ty = 0; ty < 32; ty++ )
    {
        for( int tx = 0; tx < 32; tx++ )
        {
            int      si    = ( ty * 32 + tx ) * 2;
            uint16_t color = (uint16_t)thumb[si] | ( (uint16_t)thumb[si + 1] << 8 );
            p->drawPixel( THUMBNAIL_X + tx, THUMBNAIL_Y + ty, color );
        }
    }


    /*
    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();
    // TomThumb: 3×5px glyphs, 4px x-advance, 6px y-advance.
    // Uses Adafruit GFX custom-font baseline system: setCursor y = baseline.
    // Ascent = 5px, so top-of-glyph = baseline - 5.
    p->setFont( &TomThumb );
    p->setTextSize( 1 );

    const int LIST_W = ( PANEL_W * 3 ) / 4;  // 96 — 3/4 of display for file list
    const int RX     = ( PANEL_W * 3 ) / 4;  // 96 — x-offset of right quarter

    // Vertical divider between the two halves.
    p->drawFastVLine( LIST_W - 1, 0, PANEL_H, p->color565( 50, 50, 50 ) );

    // ── LEFT PANEL: file list ───────────────────────────────────────────────
    // Header bar — 7px tall; TomThumb ascent=5 so baseline at row 6 puts
    // glyph tops at row 1, fitting neatly inside the bar.
    p->fillRect( 0, 0, LIST_W, 7, p->color565( 0, 120, 0 ) );
    p->setTextColor( p->color565( 255, 255, 255 ) );
    p->setCursor( 2, 6 );  // baseline = top(1) + ascent(5)
    p->print( "FILES" );

    if( _files.empty() )
    {
        p->setTextColor( p->color565( 180, 60, 60 ) );
        p->setCursor( 2, 19 );  // baseline = top(14) + 5
        p->print( "No .rod files" );
        return;
    }

    for( int i = 0; i < VISIBLE_ROWS; i++ )
    {
        int  idx = _scroll + i;
        if( idx >= (int)_files.size() ) break;
        int  y   = 8 + i * ROW_H;
        bool sel = ( idx == _selected );

        if( sel )
        {
            p->fillRect( 0, y - 1, LIST_W - 1, ROW_H, p->color565( 255, 170, 0 ) );
            p->setTextColor( p->color565( 0, 0, 0 ) );
        }
        else
        {
            p->setTextColor( p->color565( 180, 180, 180 ) );
        }
        p->setCursor( 4, y + 5 );  // baseline = row_top + ascent(5)
        // 22 chars max: 4px margin + 22×4px = 92px < 96px
        char name[23];
        strncpy( name, basename( _files[idx] ), 22 );
        name[22] = '\0';
        p->print( name );
    }

    // ── RIGHT PANEL: thumbnail + metadata ────────────────────────────────────
    if( !_preview_valid )
    {
        p->setTextColor( p->color565( 80, 80, 80 ) );
        p->setCursor( RX + 2, 37 );  // vertically centred; baseline = 32 + 5
        p->print( "No prev" );
        return;
    }

    // Thumbnail: render native 32×32 RGB565 at (RX, 0) — exact 1:1, no scaling.
    const uint8_t *thumb = _preview_hdr.thumbnail;
    for( int ty = 0; ty < 32; ty++ )
    {
        for( int tx = 0; tx < 32; tx++ )
        {
            int      si    = ( ty * 32 + tx ) * 2;
            uint16_t color = (uint16_t)thumb[si] | ( (uint16_t)thumb[si + 1] << 8 );
            p->drawPixel( RX + tx, ty, color );
        }
    }

    // Separator between thumbnail and info text.
    p->drawFastHLine( RX, 32, PANEL_W - RX, p->color565( 40, 40, 40 ) );

    // Info area: 5 lines at y ∈ {33,39,45,51,57} (top of glyph).
    // TomThumb baseline = top + 5, so setCursor y values are top + 5.
    // Right panel is 32px; 2px left margin leaves 30px = 7 chars max per line.
    p->setTextColor( p->color565( 200, 200, 200 ) );

    // Line 1 — Title (up to 7 chars, falls back to filename)
    {
        char title[8];
        if( _preview_hdr.title[0] != '\0' )
            strncpy( title, _preview_hdr.title, 7 );
        else
            strncpy( title, basename( _preview_path ), 7 );
        title[7] = '\0';
        p->setCursor( RX + 2, 38 );  // top=33, baseline=38
        p->print( title );
    }

    // Line 2 — Duration (M:SS)
    {
        uint32_t total_s = _preview_hdr.duration_ms / 1000;
        uint32_t mins    = total_s / 60;
        uint32_t secs    = total_s % 60;
        char buf[8];
        snprintf( buf, sizeof(buf), "%u:%02u", mins, secs );
        p->setCursor( RX + 2, 44 );  // top=39, baseline=44
        p->print( buf );
    }

    // Line 3 — Frame rate
    {
        uint16_t fps = ( _preview_hdr.fps_den > 0 )
                     ? _preview_hdr.fps_num / _preview_hdr.fps_den : 0;
        char buf[8];
        snprintf( buf, sizeof(buf), "%ufps", fps );
        p->setCursor( RX + 2, 50 );  // top=45, baseline=50
        p->print( buf );
    }

    // Line 4 — Resolution
    {
        char buf[8];
        snprintf( buf, sizeof(buf), "%ux%u",
                  (unsigned)_preview_hdr.panel_w,
                  (unsigned)_preview_hdr.panel_h );
        p->setCursor( RX + 2, 56 );  // top=51, baseline=56
        p->print( buf );
    }

    // Line 5 — Audio (sample rate kHz + Stereo/Mono)
    {
        uint16_t    khz = (uint16_t)( _preview_hdr.sample_rate / 1000 );
        const char *ch  = ( _preview_hdr.channels == 2 ) ? "St" : "Mo";
        char buf[8];
        snprintf( buf, sizeof(buf), "%uk%s", khz, ch );
        p->setCursor( RX + 2, 62 );  // top=57, baseline=62
        p->print( buf );
    }
    */
}