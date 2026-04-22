// apps/radio_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include "config.h"

#include "core/app_manager.h"
#include "core/ui.h"
#include "drivers/audio_driver.h"
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"
#include "apps/radio_app.h"

#include <Fonts/TomThumb.h>


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;

static TaskHandle_t radioTaskHandle = nullptr;
static bool s_running = false;
static std::string s_current_song_path;


/*--- LOCALS ------------------------------------------------------------------------------------*/

static void radio_task( void *param );
static bool is_audio_file( const char *filename );


/*--- HELPERS -----------------------------------------------------------------------------------*/

// Check if a file is a supported audio format
static bool is_audio_file( const char *filename )
{
    const char *ext = strrchr( filename, '.' );
    if( !ext ) return false;

    // Compare case-insensitive
    return strcasecmp( ext, ".mp3" ) == 0 || 
           strcasecmp( ext, ".wav" ) == 0 ||
           strcasecmp( ext, ".m4a" ) == 0 ||
           strcasecmp( ext, ".aac" ) == 0;
}


/*--- Public API --------------------------------------------------------------------------------*/

void radio_start( const char *song_path )
{
    if( s_running )
    {
        return;
    }

    s_running = true;
    s_current_song_path = song_path;

    char *pathCopy = strdup( song_path );

    xTaskCreatePinnedToCore
    (
        radio_task,
        "RadioTask",
        16384,            // stack size (larger for file I/O)
        pathCopy,
        1,
        &radioTaskHandle,
        1                 // run on core 1
    );
}

void radio_stop()
{
    if( !s_running )
    {
        return;
    }

    s_running = false;

    if( radioTaskHandle )
    {
        vTaskDelete( radioTaskHandle );
        radioTaskHandle = nullptr;
    }
}

bool radio_is_running()
{
    return s_running;
}


/*--- TASKS -------------------------------------------------------------------------------------*/

static void radio_task( void *param )
{
    char *song_path = (char *)param;

    Serial.printf( "[radio] Task started, playing: %s\n", song_path );

    // Open the file from SD card
    File audioFile = SD.open( song_path, FILE_READ );
    
    if( !audioFile || !audioFile.isFile() )
    {
        Serial.printf( "[radio] Failed to open file: %s\n", song_path );
        free( song_path );
        s_running = false;
        vTaskDelete( nullptr );
        return;
    }

    Serial.printf( "[radio] File opened, size: %d bytes\n", audioFile.size() );

    // Buffer for audio chunks
    uint8_t audio_buffer[4096];
    int bytes_read = 0;

    // Read and stream audio data
    while( s_running && (bytes_read = audioFile.read( audio_buffer, sizeof(audio_buffer) )) > 0 )
    {
        // For now, just feed raw data to audio ring buffer
        // NOTE: This assumes the file is already in the correct PCM format
        // For MP3/M4A decoding, you'd need to add a decoder here
        
        // Push data to ring buffer (assuming stereo 16-bit PCM)
        if( bytes_read % 4 == 0 )  // Must be multiple of 4 bytes
        {
            uint16_t samples = bytes_read / 4;  // 4 bytes per stereo sample
            audio_ring_push( (const int16_t *)audio_buffer, samples, 2 );
        }

        // Yield to other tasks
        vTaskDelay( 1 / portTICK_PERIOD_MS );
    }

    Serial.println( "[radio] Playback ended" );

    audioFile.close();
    free( song_path );

    s_running = false;
    vTaskDelete( nullptr );
}


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void RadioApp::onEnter()
{
    Serial.print( "[radio] Opened radio app\r\n" );

    matrix_clear();
    
    // Initialize and start audio system with ring buffer
    audio_init( 44100, 2 );      // 44100 Hz, stereo
    audio_start();               // Start the audio drain task on Core 0

    // Allocate album art buffer
    if( !_art_buf )
    {
        _art_buf = (uint16_t *)malloc( ART_SIZE * ART_SIZE * sizeof(uint16_t) );
    }
    _art_loaded = false;

    _state = STATE_LOADING;
    _dirty = true;

    // Start scanning playlists from SD card
    scan_playlists();
}

void RadioApp::onExit()
{
    if( radio_is_running() )
    {
        radio_stop();
    }
    
    // Properly shut down the audio system
    audio_stop();

    // Free album art buffer
    if( _art_buf )
    {
        free( _art_buf );
        _art_buf = nullptr;
    }
    _art_loaded = false;
}

AppCmd RadioApp::update()
{
    while( input_has_event() )
    {
        InputEvent ev = input_next_event();

        switch( _state )
        {
            case STATE_LOADING:
                // Can only go back while loading
                if( ev == InputEvent::BTN_A )
                {
                    return AppCmd::POP;
                }
                break;

            case STATE_PLAYLIST_SEL:
                if( ev == InputEvent::BTN_A )
                {
                    return AppCmd::POP;  // Go back
                }
                else if( ev == InputEvent::BTN_B )
                {
                    _selected_playlist = max( (int)_selected_playlist - 1, 0 );
                    _dirty = true;
                }
                else if( ev == InputEvent::BTN_C )
                {
                    _selected_playlist = min( (int)_selected_playlist + 1, (int)_playlists.size() - 1 );
                    _dirty = true;
                }
                else if( ev == InputEvent::BTN_D )
                {
                    // Select playlist and start playing (shuffled)
                    load_and_play_playlist( _playlists[_selected_playlist].path );
                    _state = STATE_PLAYING;
                    _is_playing = true;
                    _dirty = true;
                }
                break;

            case STATE_PLAYING:
                if( ev == InputEvent::BTN_A )
                {
                    // Stop and return to playlist selection
                    radio_stop();
                    _current_songs.clear();
                    _current_song_index = 0;
                    _state = STATE_PLAYLIST_SEL;
                    _dirty = true;
                }
                else if( ev == InputEvent::BTN_D )
                {
                    // Skip to next song
                    play_next_song();
                    _dirty = true;
                }
                break;

            case STATE_ERROR:
                if( ev == InputEvent::BTN_A )
                {
                    _state = STATE_PLAYLIST_SEL;
                    _dirty = true;
                }
                break;

            default:
                break;
        }
    }

    return AppCmd::NONE;
}

void RadioApp::draw()
{
    if( !_dirty )
    {
        return;
    }
    _dirty = false;

    MatrixPanel_I2S_DMA *p = matrix_panel();
    p->clearScreen();

    p->setFont( &TomThumb );
    p->setTextColor( COLOR_TEXT );

    switch( _state )
    {
        case STATE_LOADING:
            p->setCursor( 5, 30 );
            p->print( "Scanning..." );
            break;

        case STATE_PLAYLIST_SEL:
            // Draw title
            p->setCursor( 5, 8 );
            p->print( "Playlists:" );

            // Draw playlists
            if( _playlists.empty() )
            {
                p->setCursor( 5, 30 );
                p->print( "No playlists" );
            }
            else
            {
                for( size_t i = 0; i < _playlists.size() && i < 7; i++ )
                {
                    int y = 18 + (i * 8);
                    if( i == _selected_playlist )
                    {
                        draw_rect( p, COLOR_UI_MAIN, 0, y - 2, PANEL_W, 8 );
                        p->setTextColor( COLOR_UI_SECONDARY );
                    }
                    else
                    {
                        p->setTextColor( COLOR_TEXT );
                    }

                    p->setCursor( 5, y + 5 );
                    p->print( _playlists[i].name.c_str() );
                }
            }
            break;

        case STATE_PLAYING:
        {
            // Draw album art (50x50) on the left
            if( _art_loaded && _art_buf )
            {
                draw_png( p, _art_buf, 0, 0, ART_SIZE, ART_SIZE );
            }
            else
            {
                // Placeholder when no art
                draw_rect( p, COLOR_UI_SECONDARY, 0, 0, ART_SIZE, ART_SIZE );
                draw_rect_unfilled( p, COLOR_UI_MAIN, 0, 0, ART_SIZE, ART_SIZE );
                p->setCursor( 12, 28 );
                p->setTextColor( COLOR_TEXT );
                p->print( "No Art" );
            }

            // Metadata to the right of art
            int tx = ART_SIZE + 3;

            // Song name
            p->setTextColor( COLOR_UI_NOTICE );
            p->setCursor( tx, 8 );
            p->print( _current_song_name.c_str() );

            // Artist
            if( !_current_meta.artist.empty() )
            {
                p->setTextColor( COLOR_TEXT );
                p->setCursor( tx, 20 );
                p->print( _current_meta.artist.c_str() );
            }

            // Album
            if( !_current_meta.album.empty() )
            {
                p->setTextColor( COLOR_UI_ACCENT );
                p->setCursor( tx, 30 );
                p->print( _current_meta.album.c_str() );
            }

            // Controls hint
            p->setTextColor( COLOR_TEXT );
            p->setCursor( 2, 60 );
            p->print( "A:Back D:Next" );
            break;
        }

        case STATE_ERROR:
            p->setCursor( 5, 20 );
            p->print( "Error:" );
            p->setCursor( 5, 30 );
            p->print( _error_msg.c_str() );
            p->setCursor( 5, 55 );
            p->print( "Press A" );
            break;

        default:
            break;
    }
}

/*--- HELPER FUNCTIONS --------------------------------------------------------------------------*/

void RadioApp::scan_playlists()
{
    _playlists.clear();

    // Open /music directory on SD card
    File musicDir = SD.open( "/music" );
    
    if( !musicDir || !musicDir.isDirectory() )
    {
        Serial.println( "[radio] /music directory not found on SD card" );
        _state = STATE_ERROR;
        _error_msg = "No /music folder";
        _dirty = true;
        return;
    }

    // Scan for subdirectories (playlists)
    File entry = musicDir.openNextFile();
    while( entry )
    {
        if( entry.isDirectory() )
        {
            _playlists.push_back( {
                std::string( entry.path() ),
                std::string( entry.name() )
            });
            Serial.printf( "[radio] Found playlist: %s\n", entry.name() );
        }
        entry.close();
        entry = musicDir.openNextFile();
    }

    musicDir.close();

    if( _playlists.empty() )
    {
        _state = STATE_ERROR;
        _error_msg = "No playlists found";
    }
    else
    {
        _state = STATE_PLAYLIST_SEL;
        _selected_playlist = 0;
    }

    _dirty = true;
}

void RadioApp::load_and_play_playlist( const std::string &playlist_path )
{
    _current_songs.clear();
    _current_song_index = 0;

    // Open the playlist directory
    File playlistDir = SD.open( playlist_path.c_str() );
    
    if( !playlistDir || !playlistDir.isDirectory() )
    {
        Serial.printf( "[radio] Failed to open playlist: %s\n", playlist_path.c_str() );
        _state = STATE_ERROR;
        _error_msg = "Failed to open";
        _dirty = true;
        return;
    }

    // Scan for audio files
    File entry = playlistDir.openNextFile();
    while( entry )
    {
        if( !entry.isDirectory() && is_audio_file( entry.name() ) )
        {
            _current_songs.push_back( std::string( entry.path() ) );
            Serial.printf( "[radio] Found song: %s\n", entry.name() );
        }
        entry.close();
        entry = playlistDir.openNextFile();
    }

    playlistDir.close();

    if( _current_songs.empty() )
    {
        Serial.println( "[radio] No songs found in playlist" );
        _state = STATE_ERROR;
        _error_msg = "No songs found";
        _dirty = true;
        return;
    }

    // Shuffle the playlist
    for( size_t i = _current_songs.size() - 1; i > 0; i-- )
    {
        size_t j = random( 0, i + 1 );
        std::swap( _current_songs[i], _current_songs[j] );
    }

    Serial.printf( "[radio] Loaded and shuffled %d songs\n", _current_songs.size() );

    // Start playing the first song
    Serial.printf( "[radio] Starting first song: %s\n", _current_songs[0].c_str() );
    
    extract_song_name( _current_songs[0], _current_song_name );
    load_song_meta( _current_songs[0] );
    radio_start( _current_songs[0].c_str() );
}

void RadioApp::play_next_song()
{
    if( _current_songs.empty() )
        return;

    radio_stop();

    _current_song_index = (_current_song_index + 1) % _current_songs.size();
    
    const std::string &song_path = _current_songs[_current_song_index];
    Serial.printf( "[radio] Skipping to: %s\n", song_path.c_str() );

    extract_song_name( song_path, _current_song_name );
    load_song_meta( song_path );
    radio_start( song_path.c_str() );
}

void RadioApp::extract_song_name( const std::string &path, std::string &out_name )
{
    size_t last_slash = path.rfind( '/' );
    std::string filename = ( last_slash != std::string::npos ) ?
        path.substr( last_slash + 1 ) : path;

    size_t dot_pos = filename.rfind( '.' );
    if( dot_pos != std::string::npos )
    {
        filename = filename.substr( 0, dot_pos );
    }

    out_name = filename;
}

void RadioApp::load_song_meta( const std::string &song_path )
{
    _current_meta = {};
    _art_loaded = false;

    // Build .meta path from song path (replace extension with .meta)
    std::string meta_path = song_path;
    size_t dot_pos = meta_path.rfind( '.' );
    if( dot_pos != std::string::npos )
    {
        meta_path = meta_path.substr( 0, dot_pos );
    }
    meta_path += ".meta";

    // Read .meta file
    File meta_file = SD.open( meta_path.c_str(), FILE_READ );
    if( !meta_file )
    {
        Serial.printf( "[radio] No meta file: %s\n", meta_path.c_str() );
        return;
    }

    while( meta_file.available() )
    {
        String line = meta_file.readStringUntil( '\n' );
        line.trim();

        int eq = line.indexOf( '=' );
        if( eq < 0 ) continue;

        String key = line.substring( 0, eq );
        String val = line.substring( eq + 1 );
        key.trim();
        val.trim();

        if( key == "artist" )
        {
            _current_meta.artist = val.c_str();
        }
        else if( key == "album" )
        {
            _current_meta.album = val.c_str();
        }
        else if( key == "art" )
        {
            _current_meta.art_filename = val.c_str();
        }
    }

    meta_file.close();
    Serial.printf( "[radio] Meta loaded — artist: %s, album: %s\n",
                   _current_meta.artist.c_str(), _current_meta.album.c_str() );

    // Load album art (.art file = raw 50x50 RGB565 binary)
    if( !_current_meta.art_filename.empty() && _art_buf )
    {
        // Build full path to .art file (same directory as song)
        std::string art_path = song_path;
        size_t slash = art_path.rfind( '/' );
        if( slash != std::string::npos )
        {
            art_path = art_path.substr( 0, slash + 1 );
        }
        art_path += _current_meta.art_filename;

        File art_file = SD.open( art_path.c_str(), FILE_READ );
        size_t expected = ART_SIZE * ART_SIZE * sizeof(uint16_t);

        if( art_file && art_file.size() >= expected )
        {
            size_t bytes_read = art_file.read( (uint8_t *)_art_buf, expected );
            _art_loaded = ( bytes_read == expected );
            Serial.printf( "[radio] Art loaded: %s (%zu bytes)\n", art_path.c_str(), bytes_read );
        }
        else
        {
            Serial.printf( "[radio] Art file not found or too small: %s\n", art_path.c_str() );
        }

        if( art_file ) art_file.close();
    }
}