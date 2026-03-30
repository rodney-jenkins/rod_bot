#pragma once

// apps/video_player/video_player_app.h
//
// Pushes FileBrowser to select a file, then plays it with audio-driven sync:
//   - A read-ahead task (Core 0) reads each .rod chunk sequentially:
//       audio PCM → audio ring buffer  (can stay seconds ahead)
//       video pixels → 4-slot PSRAM pool → ready queue  (stays ~4 frames ahead)
//   - The video loop (Core 1) renders each frame when
//       audio_samples_played() >= frame.audio_sample_offset
//
// No wall-clock math, no sleep, no drift.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
#include <string>

#include "core/app.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

enum class PlayerState
{ 
    BROWSING, 
    PLAYING,
    PAUSED,
    DONE
};

// A decoded video frame waiting in the ready queue.
struct VideoFrame
{
    uint32_t audio_sample_offset;  // render when audio clock reaches this value
    uint32_t frame_idx;
    uint8_t *pixels;               // points into the PSRAM pixel pool
};


/*--- CLASSES -----------------------------------------------------------------------------------*/

class VideoPlayerApp : public IApp
{
public:
    void   onEnter()  override;
    void   onResume() override;
    AppCmd update()   override;
    void   draw()     override;
    void   onExit()   override;

private:
    PlayerState _state     = PlayerState::BROWSING;
    std::string _path;
    bool        _user_quit = false;
    bool        _is_running = false;

    void start_playback(const std::string &path);
    void stop_playback();
    void pause_playback();
    void resume_playback();
    bool playback_tick();

    // File format
    void    *_file              = nullptr;
    uint32_t _frame_count       = 0;
    uint32_t _frame_video_bytes = 0;
    uint8_t  _pixel_format      = 1;
    uint16_t _panel_w           = 0;
    uint16_t _panel_h           = 0;
    uint8_t  _channels          = 1;

    // Video frame pool
    static constexpr int VIDEO_POOL_DEPTH = 8;
    VideoFrame   _frame_pool[VIDEO_POOL_DEPTH];
    uint8_t     *_pixel_pool  = nullptr;  // PSRAM: VIDEO_POOL_DEPTH × frame_video_bytes
    QueueHandle_t _free_queue  = nullptr; // pool slots available for the read-ahead task
    QueueHandle_t _ready_queue = nullptr; // decoded frames ready for the video loop

    // Readahead task
    TaskHandle_t       _readahead_handle = nullptr;
    std::atomic<bool>  _readahead_stop{false};  // written Core 1, read Core 0
    std::atomic<bool>  _readahead_done{false};  // written Core 0, read Core 1

    static void readahead_task( void *param );
};
