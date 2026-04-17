// apps/video_player/file_browser.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <SD_MMC.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "drivers/audio_driver.h"
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"
#include "apps/menu_app.h"
#include "apps/video_player/file_browser.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;

FileBrowser::FileBrowser
(
    std::function<void( const std::string & )> on_select,
    std::function<void()> on_cancel,
    const std::string &start_path
)
: _on_select( on_select ), _on_cancel( on_cancel ), _cwd( start_path ) {}


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/\

void FileBrowser::scan_sd()
{
    _entries.clear();

    File root = SD_MMC.open( _cwd.c_str() );
    if( !root )
    {
        return;
    }

    // Add parent directory
    if( _cwd != "/" )
    {
        _entries.insert( _entries.begin(), { "..", true } );
    }

    File entry;
    while( ( entry = root.openNextFile() ) )
    {
        std::string name = entry.name();

        // Skip system/hidden entries
        if( name == "System Volume Information" || name == "$RECYCLE.BIN" || name == "._" || name[0] == '.')   // hidden files like .DS_Store
        {
            entry.close();
            continue;
        }

        std::string full_path = ( _cwd == "/" ) ? ( "/" + name ) : ( _cwd + "/" + name );

        if( entry.isDirectory() )
        {
            _entries.push_back( { full_path, true } );
        }
        else
        {
            if( name.size() >= 4 && name.substr( name.size() - 4 ) == ".rod" )
            {
                _entries.push_back( { full_path, false } );
            }
        }

        entry.close();
    }

    std::sort(
        _entries.begin(),
        _entries.end(),
        [](const Entry &a, const Entry &b)
        {
            if( a.path == ".." ) return true;
            if( b.path == ".." ) return false;

            if( a.is_dir != b.is_dir )
                return a.is_dir > b.is_dir;

            return a.path < b.path;
        }
    );

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
    _preview_path  = _entries[_selected].path;

    const Entry &e = _entries[_selected];
    if( e.is_dir )
    {
        // Find the first .rod file in the directory to use as the folder thumbnail
        File dir = SD_MMC.open( e.path.c_str() );
        if( !dir ) return;

        File child;
        while( ( child = dir.openNextFile() ) )
        {
            std::string name = child.name();
            if( !child.isDirectory() && name.size() >= 4 && name.substr( name.size() - 4 ) == ".rod" )
            {
                bool ok = ( child.read( (uint8_t *)&_preview_hdr, sizeof(_preview_hdr) ) == sizeof(_preview_hdr) )
                       && ( _preview_hdr.magic == ROD_MAGIC );
                child.close();
                dir.close();
                _preview_valid = ok;
                return;
            }
            child.close();
        }
        dir.close();
        return;
    }

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
                _selected = min( (int)_selected + 1, (int)_entries.size() - 1 );
                if( _selected >= _scroll + VISIBLE_ROWS )
                {
                    _scroll = _selected - VISIBLE_ROWS + 1;
                }
                _dirty = true;
                break;

            case( InputEvent::BTN_D ):
                Serial.print( "[fb] Button D Press\r\n" );
                {
                    if( _entries.empty() ) break;

                    const Entry &e = _entries[_selected];
                    if( e.is_dir )
                    {
                        if( e.path == ".." )
                        {
                            // Go up
                            size_t pos = _cwd.find_last_of( '/' );
                            _cwd = ( pos == 0 ) ? "/" : _cwd.substr( 0, pos );
                        }
                        else
                        {
                            _cwd = e.path;
                        }

                        scan_sd();
                        _selected = 0;
                        _scroll = 0;
                        _dirty = true;
                        break;
                    }
                    else
                    {
                        if( _on_select) 
                            _on_select( e.path );

                        return AppCmd::POP;
                    }
                }

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

    // Load preview for files, and for folders (using the first .rod file inside)
    if( !_entries.empty() )
    {
        const Entry &e = _entries[_selected];

        if( e.path != _preview_path )
        {
            if( e.is_dir && e.path == ".." )
            {
                _preview_path  = e.path;
                _preview_valid = false;
            }
            else
            {
                load_preview();
            }
        }
    }

    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();

    // Draw background --------------------------------------------------------
    // TODO: these should all be constants...
    // Title box
    draw_rect( p, COLOR_UI_ACCENT, 0, 56, PANEL_W, 1 );
    draw_rect( p, COLOR_UI_SECONDARY, 0, 57, PANEL_W, 7 );
    // List box
    draw_rect( p, COLOR_UI_ACCENT, 0, 0, 1, 57 );
    draw_rect( p, COLOR_UI_ACCENT, 0, 0, 92, 1 );
    // Thumbnail box
    draw_rect_unfilled( p, COLOR_UI_ACCENT, 92, 0, 36, 36 );
    draw_rect_unfilled( p, COLOR_UI_ACCENT, 93, 1, 34, 34 );
    // Meta box
    draw_rect_unfilled( p, COLOR_UI_ACCENT, 92, 34, 36, 22 );
    draw_rect_unfilled( p, COLOR_UI_ACCENT, 93, 35, 34, 20 );


    // Draw file list ---------------------------------------------------------

    if( _entries.empty() )
    {
        p->setFont( nullptr );
        p->setTextColor( COLOR_TEXT );
        p->setCursor( 5, 5 );
        p->print( "No .rod files" );
        p->setCursor( 20, 5 );
        p->print( ":(" );
        return;
    }

    bool can_scroll_up   = ( _scroll > 0 );
    bool can_scroll_down = ( _scroll + VISIBLE_ROWS < (int)_entries.size() );

    p->setFont( &TomThumb );
    p->setTextColor( COLOR_TEXT );

    // Draw buttons
    for( int i = 0; i < VISIBLE_ROWS && i < _entries.size(); i++ )
    {
        int idx = _scroll + i; // Compute actual index in _entries
        
        const Entry &e = _entries[idx];

        // Handle scroll indicators
        if( i == 0 && can_scroll_up )
        {
            draw_rect_unfilled( p, COLOR_UI_ACCENT, 0, i * 8, 93, 9 );
            draw_rect( p, COLOR_UI_NOTICE, 1, 1 + i * 8, 91, 7 );
            p->setCursor( 2, 2 + 5 );
            p->print( "^^^" );
            continue;
        }

        if( i == VISIBLE_ROWS - 1 && can_scroll_down )
        {
            draw_rect_unfilled( p, COLOR_UI_ACCENT, 0, i * 8, 93, 9 );
            draw_rect( p, COLOR_UI_NOTICE, 1, 1 + i * 8, 91, 7 );
            p->setCursor( 2, 2 + i * 8 + 5 );
            p->print( "vvv" );
            continue;
        }

        if( can_scroll_up )
            idx -= 1;  // shift down because row 0 is taken

        if( idx < 0 || idx >= (int)_entries.size() )
            continue;

        draw_rect_unfilled( p, COLOR_UI_ACCENT, 0, i * 8, 93, 9 );

        if( e.is_dir )
            draw_rect( p, COLOR_UI_TERTIARY, 1, 1 + i * 8, 91, 7 );
        else
            draw_rect( p, COLOR_UI_MAIN, 1, 1 + i * 8, 91, 7 );

        int x = 2;
        int y = 2 + i * 8;
        p->setCursor( x, y + 5 );

        char name[ 22 ];
        if( e.path == ".." )
        {
            strncpy( name, "back", sizeof(name) );
        }
        else
        {
            strncpy( name, basename( e.path ), 21 );
        }
        name[ 21 ] = '\0';
        p->print( name );
    }

    draw_rect_unfilled( p, COLOR_TEXT, 0, _selected * 8, 93, 9 );

    // Metadata
    {
        p->setFont( &TomThumb );
        p->setTextColor( COLOR_TEXT );
        p->setCursor( 95, 43 );
        
        if( _entries[ _selected ].is_dir )
        {
            p->print( "Folder" );
            if( !_preview_valid ) return;
        }
        else
        {
            if( !_preview_valid )
            {
                p->print( "No prev" );
                return;
            }

            uint32_t total_s = _preview_hdr.duration_ms / 1000;
            uint32_t hrs     = ( total_s / 3600 );
            uint32_t mins    = ( total_s % 3600 ) / 60;
            uint32_t secs    = total_s % 60;

            char buf[ 8 ];
            snprintf( buf, sizeof(buf), "%u:%02u:%02u", hrs, mins, secs );
            p->print( buf );
        }
    }

    // Title (files only)
    if( !_entries[ _selected ].is_dir )
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
        p->setCursor( 1, 63 );
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
            p->drawPixel( 94 + tx, 2 + ty, color );
        }
    }
}