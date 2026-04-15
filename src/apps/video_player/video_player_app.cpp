// apps/video_player/video_player_app.cpp

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <esp32-hal-psram.h>
#include <SD_MMC.h>
#include <Fonts/TomThumb.h>
#include "config.h"
#include "rod_format.h"
#include "drivers/audio_driver.h"
#include "drivers/matrix_driver.h"
#include "drivers/input_driver.h"

#include "core/app_manager.h"
#include "apps/video_player/video_player_app.h"
#include "apps/video_player/file_browser.h"


/*--- GLOBALS -----------------------------------------------------------------------------------*/

extern AppManager g_app_manager;


/*--- MACROS ------------------------------------------------------------------------------------*/

static inline File &file_ref( void *p )
{
    return *reinterpret_cast<File *>( p );
}


/*--- APP OVERRIDES -----------------------------------------------------------------------------*/

void VideoPlayerApp::onEnter()
{
    Serial.print( "[vp] Entered\r\n" );
    _state     = PlayerState::BROWSING;
    _user_quit = false;

}

AppCmd VideoPlayerApp::update()
{
    if( ( _state == PlayerState::BROWSING ) 
     && ( _file == nullptr                )
     && ( _path.empty()                   ) )
    {
        Serial.print( "[vp] Opening FB\r\n" );
        auto *browser = new FileBrowser
        (
            [this](const std::string &path) { _path = path; },
            [this]()                        { _user_quit = true; }
        );
        g_app_manager.setPending( browser );

        return AppCmd::PUSH;
    }

    if( ( _state == PlayerState::DONE )
     || ( _user_quit                  ) )
    {
        return AppCmd::POP;
    }

    if( _state == PlayerState::PLAYING || _state == PlayerState::PAUSED )
    {
        while( input_has_event() )
        {
            InputEvent ev = input_next_event();
            switch( ev )
            {
                case( InputEvent::BTN_A ):   // Back to file browser
                    stop_playback();
                    _path.clear();
                    _state = PlayerState::BROWSING;
                    return AppCmd::NONE;

                case( InputEvent::BTN_B ):   // Rewind
                    _hud_drawn = false;
                    if( _state == PlayerState::PAUSED && _frame_offsets )
                    {
                        uint32_t target = ( _current_frame >= _seek_step_frames ) ? ( _current_frame - _seek_step_frames ) : 0;
                        seek_to_frame( target );
                    }
                    break;

                case( InputEvent::BTN_C ):   // Fast forward
                    _hud_drawn = false;
                    if( _state == PlayerState::PAUSED && _frame_offsets )
                    {
                        uint32_t target = _current_frame + _seek_step_frames;
                        seek_to_frame( target );  // clamps to _frame_count - 1 internally
                    }
                    break;

                case( InputEvent::BTN_D ):   // Pause
                    _hud_drawn = false;
                    if( _state == PlayerState::PLAYING )
                        pause_playback();
                    else
                        resume_playback();
                    break;

                default:
                    break;
            }
        }

        if( _state == PlayerState::PLAYING && !playback_tick() )
        {
            // Playback finished naturally — mark DONE so the next update()
            // call returns POP.  onExit() handles the actual resource teardown,
            // keeping stop_playback() to a single call site.
            _state = PlayerState::DONE;
        }
    }
    return AppCmd::NONE;
}

void VideoPlayerApp::draw()
{
    // Frames are written directly to the matrix inside playback_tick().
    // When paused, overlay a HUD at the bottom: pause icon, time, progress bar.
    if( _state != PlayerState::PAUSED || _hud_drawn ) return;
    _hud_drawn = true;

    MatrixPanel_I2S_DMA *p = matrix_panel();

    const uint16_t COL_BG    = p->color565(  10,  10,  10 );
    const uint16_t COL_TRACK = p->color565(  55,  55,  55 );
    const uint16_t COL_FILL  = p->color565( 255, 255, 255 );
    const uint16_t COL_TEXT  = p->color565( 255, 220, 100 );

    // Dark background strip (bottom 11 rows)
    p->fillRect( 0, 53, PANEL_W, 11, COL_BG );

    // Pause icon
    p->fillRect( 2, 55, 2, 6, COL_FILL );
    p->fillRect( 5, 55, 2, 6, COL_FILL );

    // Time text ( "M:SS / M:SS"  or  "H:MM:SS / H:MM:SS" )
    auto fmt_time = []( char *buf, size_t len, uint32_t total_s )
    {
        uint32_t h = total_s / 3600;
        uint32_t m = ( total_s % 3600 ) / 60;
        uint32_t s = total_s % 60;
        if( h > 0 )
            snprintf( buf, len, "%u:%02u:%02u", h, m, s );
        else
            snprintf( buf, len, "%u:%02u", m, s );
    };

    uint32_t fps_num = ( _fps_num > 0 ) ? _fps_num : 24;
    uint32_t fps_den = ( _fps_den > 0 ) ? _fps_den : 1;
    uint32_t elapsed_s = (uint32_t)( (uint64_t)_current_frame * fps_den / fps_num );
    uint32_t total_s   = (uint32_t)( (uint64_t)_frame_count   * fps_den / fps_num );

    char cur_buf[12], tot_buf[12], time_str[28];
    fmt_time( cur_buf,  sizeof(cur_buf),  elapsed_s );
    fmt_time( tot_buf,  sizeof(tot_buf),  total_s   );
    snprintf( time_str, sizeof(time_str), "%s / %s", cur_buf, tot_buf );

    p->setFont( &TomThumb );
    p->setTextSize( 1 );
    p->setTextColor( COL_TEXT );
    p->setCursor( 10, 60 );
    p->print( time_str );
    p->setFont( nullptr );

    // Progress bar ( last 3 rows: y 61‥63 )
    int filled = ( _frame_count > 0 )
        ? (int)( (uint64_t)_current_frame * PANEL_W / _frame_count )
        : 0;
    if( filled > PANEL_W ) filled = PANEL_W;

    p->fillRect( 0,      61, PANEL_W, 3, COL_TRACK );
    if( filled > 0 )
        p->fillRect( 0,  61, filled,  3, COL_FILL  );
}

void VideoPlayerApp::onResume()
{
    if( _user_quit || _path.empty() )
    {
        _state = PlayerState::DONE;
        return;
    }
    start_playback( _path );
}

void VideoPlayerApp::onExit()
{
    stop_playback();
    matrix_clear();
}


// Read-ahead task (Core 0)
//
// Reads each .rod frame chunk sequentially from the SD card:
//   1. Frame header
//   2. Audio PCM  → pushed straight into the audio ring buffer
//                   (audio can stay 0.5–1 s ahead; ring buffer absorbs SD stalls)
//   3. Video pixels → deposited into a free pool slot and pushed to ready_queue
//                     (video stays ~VIDEO_POOL_DEPTH frames ahead)
//
// The two buffers drain at different depths, so audio never starves even if
// the video loop falls behind.

void VideoPlayerApp::readahead_task( void *param )
{
    Serial.print( "[vp] readahead\r\n" );

    VideoPlayerApp *self = static_cast<VideoPlayerApp *>( param );
    File &f = file_ref( self->_file );

    // Heap buffer for one frame's worth of PCM (avoids large stack allocation).
    // Must hold MAX_AUDIO_SAMPLES samples × channels so stereo frames don't overflow.
    int16_t *pcm_tmp = (int16_t *)heap_caps_malloc
    (
        MAX_AUDIO_SAMPLES * self->_channels * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    for( uint32_t fi = self->_readahead_start_frame; fi < self->_frame_count && !self->_readahead_stop; fi++ )
    {
        // 1. Frame header
        FrameHeader fhdr;
        if( f.read( (uint8_t *)&fhdr, sizeof( fhdr ) ) != sizeof( fhdr ) )
        {
            break;
        }

        // 2. Audio - pushed to ring buffer
        if( fhdr.audio_samples > 0 && pcm_tmp )
        {
            // Guard against a malformed or low-fps frame whose audio_samples count
            // exceeds MAX_AUDIO_SAMPLES — that would overflow pcm_tmp on the heap.
            // Read only what fits; seek past any remaining bytes to keep file position
            // correct so the following video read lands at the right offset.
            uint16_t samples_to_push = fhdr.audio_samples;
            if( samples_to_push > MAX_AUDIO_SAMPLES )
            {
                samples_to_push = (uint16_t)MAX_AUDIO_SAMPLES;
                size_t skip = (size_t)( fhdr.audio_samples - samples_to_push )
                            * self->_channels * sizeof(int16_t);
                f.read( (uint8_t *)pcm_tmp,
                        (size_t)samples_to_push * self->_channels * sizeof(int16_t) );
                f.seek( f.position() + skip );
            }
            else
            {
                f.read( (uint8_t *)pcm_tmp,
                        (size_t)samples_to_push * self->_channels * sizeof(int16_t) );
            }
            audio_ring_push( pcm_tmp, samples_to_push, self->_channels );
            // blocks here if ring is full — keeps read-ahead from running too far ahead
        }

        if( self->_readahead_stop )
        {
            break;
        }

        // 3. Video - acquire a free pool slot (blocks if all 4 are in use)
        VideoFrame *vf = nullptr;
        while( !self->_readahead_stop )
        {
            if( xQueueReceive( self->_free_queue, &vf, pdMS_TO_TICKS(20) ) == pdTRUE )
            {
                break;
            }
        }
        if( self->_readahead_stop || vf == nullptr )
        {
            break;
        }

        if( f.read( vf->pixels, self->_frame_video_bytes ) != self->_frame_video_bytes )
        {
            xQueueSend( self->_free_queue, &vf, portMAX_DELAY );  // return slot
            break;
        }

        vf->audio_sample_offset = fhdr.audio_sample_offset;
        vf->frame_idx           = fi;

        // Push to ready queue — blocks if the video loop is 4 frames behind.
        xQueueSend(self->_ready_queue, &vf, portMAX_DELAY);
    }

    if( pcm_tmp )
    {
        heap_caps_free( pcm_tmp );
    }
    Serial.printf( "[vp] readahead stack HWM: %u bytes free\r\n",
                   uxTaskGetStackHighWaterMark( nullptr ) * sizeof(StackType_t) );
    self->_readahead_done = true;
    vTaskDelete( nullptr );
}

// Playback Control

void VideoPlayerApp::start_playback( const std::string &path )
{
    Serial.print( "[vp] Starting playback\r\n" );
    _is_running = true;

    File *fp = new File( SD_MMC.open( path.c_str(), FILE_READ ) );
    if( !*fp )
    {
        Serial.printf( "[player] Cannot open: %s\r\n", path.c_str() );
        delete fp;
        _state = PlayerState::DONE;
        return;
    }

    RodHeader hdr;
    if( ( fp->read( (uint8_t *)&hdr, sizeof(hdr) ) != sizeof( hdr ) )
     || ( hdr.magic != ROD_MAGIC                                    ) )
    {
        Serial.print( "[player] Invalid .rod file.\r\n" );
        fp->close();
        delete fp;
        _state = PlayerState::DONE;
        return;
    }

    _file              = fp;
    _frame_count       = hdr.frame_count;
    _panel_w           = hdr.panel_w;
    _panel_h           = hdr.panel_h;
    _pixel_format      = hdr.pixel_format;
    _channels          = hdr.channels;
    _frame_video_bytes = (uint32_t)hdr.panel_w * hdr.panel_h * rod_bytes_per_pixel( (PixelFormat)hdr.pixel_format );
    _fps_num           = hdr.fps_num;
    _fps_den           = hdr.fps_den;

    // Compute seek step: 5 seconds of frames at the file's native frame rate.
    _seek_step_frames  = ( hdr.fps_num > 0 && hdr.fps_den > 0 )
                       ? ( 5u * hdr.fps_num / hdr.fps_den )
                       : 120u;
    if( _seek_step_frames == 0 ) _seek_step_frames = 1;

    // Reset seek state for this new playback session.
    _current_frame         = 0;
    _readahead_start_frame = 0;
    _frame_offsets         = nullptr;

    // Load frame offset table if the file contains one.
    // File::seek() takes uint32_t, so skip the index when index_offset exceeds 4 GB.
    if( hdr.index_offset != 0 && hdr.frame_count > 0
     && hdr.index_offset <= (uint64_t)UINT32_MAX )
    {
        size_t table_bytes = (size_t)hdr.frame_count * sizeof(uint64_t);
        _frame_offsets = (uint64_t *)heap_caps_malloc
        (
            table_bytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if( _frame_offsets )
        {
            fp->seek( (uint32_t)hdr.index_offset );
            if( fp->read( (uint8_t *)_frame_offsets, table_bytes ) != table_bytes )
            {
                heap_caps_free( _frame_offsets );
                _frame_offsets = nullptr;
                Serial.print( "[player] Frame index read failed — seek disabled.\r\n" );
                fp->seek( sizeof(RodHeader) );  // restore file position to first frame
            }
            else
            {
                // Reposition file at the first frame.
                fp->seek( (uint32_t)_frame_offsets[0] );
                Serial.printf( "[player] Frame index loaded (%u entries).\r\n", hdr.frame_count );
            }
        }
        else
        {
            Serial.print( "[player] PSRAM alloc for frame index failed — seek disabled.\r\n" );
        }
    }

    // Allocate PSRAM pixel pool (VIDEO_POOL_DEPTH contiguous slots).
    _pixel_pool = (uint8_t *)heap_caps_malloc
    (
        (size_t)VIDEO_POOL_DEPTH * _frame_video_bytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if( !_pixel_pool )
    {
        Serial.println( "[player] PSRAM allocation failed." );
        if( _frame_offsets )
        {
            heap_caps_free( _frame_offsets );
            _frame_offsets = nullptr;
        }
        fp->close();
        delete fp;
        _file = nullptr;
        _state = PlayerState::DONE;
        return;
    }

    // Wire pool slots to pixel memory and pre-fill the free queue.
    _free_queue  = xQueueCreate( VIDEO_POOL_DEPTH, sizeof(VideoFrame *) );
    _ready_queue = xQueueCreate( VIDEO_POOL_DEPTH, sizeof(VideoFrame *) );
    for( int i = 0; i < VIDEO_POOL_DEPTH; i++ )
    {
        _frame_pool[i].pixels = _pixel_pool + (size_t)i * _frame_video_bytes;
        VideoFrame *vf = &_frame_pool[i];
        xQueueSend( _free_queue, &vf, 0 );
    }

    // Start audio (I2S + drain task).
    audio_init( hdr.sample_rate, hdr.channels );
    audio_start();

    // Start read-ahead task on Core 0 (alongside the audio drain task).
    // Stack must be large enough for the SD_MMC + FatFS + Arduino call chain,
    // which can consume 2–3 KB on its own; 8192 gives comfortable headroom.
    _readahead_stop = false;
    _readahead_done = false;
    xTaskCreatePinnedToCore
    ( 
        readahead_task,
        "rod_ra",
        8192,
        this,
        4,
        &_readahead_handle,
        0
    );

    _state = PlayerState::PLAYING;
    Serial.printf( "[player] Playing: %s  (%u frames)\r\n", path.c_str(), _frame_count );
}

void VideoPlayerApp::pause_playback()
{
    audio_pause();
    _state = PlayerState::PAUSED;
    Serial.print( "[player] Paused\r\n" );
}

void VideoPlayerApp::resume_playback()
{
    audio_resume();
    _state = PlayerState::PLAYING;
    Serial.print( "[player] Resumed\r\n" );
}

void VideoPlayerApp::stop_playback()
{
    if( !_is_running )
        {
        return;
        }
    _is_running = false;
    _readahead_stop = true;

    // Wait up to 200ms for the read-ahead task to exit voluntarily.
    for( int i = 0; i < 20 && !_readahead_done; i++ )
    {
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
    if( !_readahead_done && _readahead_handle )
    {
        vTaskDelete( _readahead_handle );
    }
    _readahead_handle = nullptr;

    audio_stop();

    // Drain queues so no stale pointers remain before we free PSRAM.
    VideoFrame *vf = nullptr;
    if( _ready_queue )
    {
        while( xQueueReceive( _ready_queue, &vf, 0 ) == pdTRUE ) {}
        vQueueDelete( _ready_queue );
        _ready_queue = nullptr;
    }
    if( _free_queue )
    {
        vQueueDelete( _free_queue );
        _free_queue = nullptr;
    }

    if( _frame_offsets )
    {
        heap_caps_free( _frame_offsets );
        _frame_offsets = nullptr;
    }

    if( _pixel_pool )
    {
        heap_caps_free( _pixel_pool );
        _pixel_pool = nullptr;
    }

    if( _file )
    {
        file_ref( _file ).close();
        delete reinterpret_cast<File *>( _file );
        _file = nullptr;
    }
}

void VideoPlayerApp::seek_to_frame( uint32_t target )
{
    if( !_frame_offsets || !_file ) return;
    if( target >= _frame_count ) target = _frame_count - 1;
    Serial.printf( "[player] Seeking to frame %u\r\n", target );

    // 1. Stop the readahead task.
    _readahead_stop = true;
    for( int i = 0; i < 40 && !_readahead_done; i++ )
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    if( !_readahead_done && _readahead_handle )
        vTaskDelete( _readahead_handle );
    _readahead_handle = nullptr;

    // 2. Flush the ready queue — return all video slots back to the free pool.
    {
        VideoFrame *vf = nullptr;
        while( xQueueReceive( _ready_queue, &vf, 0 ) == pdTRUE )
            xQueueSend( _free_queue, &vf, portMAX_DELAY );
    }

    // 3. Read the target frame header to get the audio clock offset,
    //    skip the audio data, then render a synchronous preview frame
    //    so the display updates immediately (no audio during scrubbing).
    File &f = file_ref( _file );
    f.seek( (uint32_t)_frame_offsets[target] );
    FrameHeader fhdr{};
    uint32_t audio_offset = 0;
    if( f.read( (uint8_t *)&fhdr, sizeof(fhdr) ) == sizeof(fhdr) )
    {
        audio_offset = fhdr.audio_sample_offset;
        f.seek( f.position() + (size_t)fhdr.audio_samples * _channels * sizeof(int16_t) );

        uint8_t *preview = (uint8_t *)heap_caps_malloc
        (
            _frame_video_bytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if( preview )
        {
            if( f.read( preview, _frame_video_bytes ) == _frame_video_bytes )
                matrix_render_frame( preview, _panel_w, _panel_h, (PixelFormat)_pixel_format );
            heap_caps_free( preview );
        }
    }

    // 4. Reset the audio clock to the target position (flushes ring buffer).
    audio_set_playback_position( audio_offset );

    // 5. Reposition the file at the target frame start for the readahead task.
    f.seek( (uint32_t)_frame_offsets[target] );

    // 6. Restart readahead from the target frame.
    _current_frame         = target;
    _readahead_start_frame = target;
    _readahead_stop        = false;
    _readahead_done        = false;
    xTaskCreatePinnedToCore
    (
        readahead_task,
        "rod_ra",
        8192,
        this,
        4,
        &_readahead_handle,
        0
    );
}

// Playback tick (Core 1, called from loop() via AppManager::tick())

bool VideoPlayerApp::playback_tick()
{
    if( !_ready_queue )
    {
        return false;
    }

    // Peek at the next frame without dequeuing it.
    VideoFrame *vf = nullptr;
    if( xQueuePeek( _ready_queue, &vf, 0 ) != pdTRUE )
    {
        // Queue is empty — either the read-ahead task is still filling it,
        // or we've reached the end of the file.
        if( _readahead_done && uxQueueMessagesWaiting(_ready_queue) == 0 )
        {
            return false;   // end of file, all frames consumed
        }
        vTaskDelay(1);      // yield and try again next tick
        return true;
    }

    // Not yet time to display this frame — yield.
    if( audio_samples_played() < vf->audio_sample_offset )
    {
        vTaskDelay( 1 );
        return true;
    }

    // Time to render
    xQueueReceive( _ready_queue, &vf, 0 );

    // Drop any additional frames that are also already overdue.
    VideoFrame *next = nullptr;
    while( ( xQueuePeek(_ready_queue, &next, 0 ) == pdTRUE )
        && ( audio_samples_played() >= next->audio_sample_offset                          ) )
    {
        xQueueReceive( _ready_queue, &next, 0 );
        Serial.printf( "[player] Frame %u dropped — catching up.\r\n", next->frame_idx );
        xQueueSend( _free_queue, &next, portMAX_DELAY );  // return dropped slot
    }

    // Render the frame and return its slot to the free pool.
    _current_frame = vf->frame_idx;
    matrix_render_frame( vf->pixels, _panel_w, _panel_h, (PixelFormat)_pixel_format );
    xQueueSend( _free_queue, &vf, portMAX_DELAY );

    // Done when the task has finished AND the ready queue is now empty.
    return !( _readahead_done && uxQueueMessagesWaiting(_ready_queue) == 0 );
}