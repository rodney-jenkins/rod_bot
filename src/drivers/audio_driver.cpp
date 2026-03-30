// drivers/audio_driver.cpp — I2S audio output with PSRAM ring buffer.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <atomic>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>
#include <math.h>
#include <string.h>
#include "config.h"

#include "drivers/audio_driver.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

static constexpr i2s_port_t PORT = I2S_NUM_0;


/*--- GLOBALS -----------------------------------------------------------------------------------*/

static StreamBufferHandle_t       s_ring         = nullptr;
static uint32_t                   s_sample_rate  = 44100;
static uint8_t                    s_src_channels = 1;
static std::atomic<uint64_t>      s_samples_played{0};
static TaskHandle_t               s_task_handle  = nullptr;
static volatile bool              s_running      = false;
static volatile bool              s_paused       = false;

// I2S write buffer in DRAM (not PSRAM — DMA must access it directly).
// Sized for ~512 stereo sample-pairs per I2S write call.
static DRAM_ATTR uint8_t s_i2s_buf[ 512 * 2 * sizeof(int16_t) ];

// Mono-to-stereo expansion buffer (DRAM, used in audio_ring_push for mono sources).
// Must hold MAX_AUDIO_SAMPLES stereo pairs so a full-size mono frame never overflows.
static DRAM_ATTR int16_t s_stereo_expand[ MAX_AUDIO_SAMPLES * 2 ];

/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Audio Drain Task (Core 0)

static void audio_drain_task(void *) 
{
    while( s_running )
    {
        if( s_paused )
        {
            // While paused: sleep without draining.  The ring stays full,
            // back-pressuring the read-ahead task into a blocking wait.
            // s_samples_played doesn't advance, so the video sync clock freezes.
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
            continue;
        }

        // Block up to 10ms waiting for data; keeps the task from busy-spinning
        // when the ring buffer is empty at the start of playback.
        size_t got = xStreamBufferReceive
        (
            s_ring,
            s_i2s_buf,
            sizeof( s_i2s_buf ),
            pdMS_TO_TICKS( 10 )
        );

        if( got > 0 )
        {
            // Align down to a stereo sample-frame boundary (4 bytes = L+R × int16).
            // xStreamBufferReceive can return any byte count ≥ 1 (trigger level = 1).
            // Passing a non-multiple-of-4 to i2s_write corrupts the L/R interleave
            // alignment in the DMA ring for every subsequent write, causing persistent
            // crackling.  The ≤3 orphaned bytes are an inaudible sub-sample loss.
            got &= ~(size_t)( 2 * sizeof(int16_t) - 1 );   // i.e. got &= ~3u
            if( got == 0 ) continue;

            size_t written = 0;
            i2s_write( PORT, s_i2s_buf, got, &written, portMAX_DELAY );

            // Count stereo sample-pairs actually sent to hardware.
            s_samples_played.fetch_add
            (
                written / ( 2 * sizeof(int16_t) ),
                std::memory_order_relaxed
            );
        }
    }
    // Clear the handle before self-deletion so audio_stop()'s poll sees nullptr
    // and does not attempt to vTaskDelete an already-deleted handle.
    Serial.printf( "[aud] drain task stack HWM: %u bytes free\r\n",
                   uxTaskGetStackHighWaterMark( nullptr ) * sizeof(StackType_t) );
    s_task_handle = nullptr;
    vTaskDelete( nullptr );
}

// Public API

void audio_beep()
{
    #define SAMPLE_RATE 44100
    #define BEEP_FREQ   1000     // 1 kHz tone
    #define DURATION_MS 200      // 200 ms beep

    audio_init( SAMPLE_RATE, 1 );

    const int total_samples = (SAMPLE_RATE * DURATION_MS) / 1000;
    int16_t stereo_buffer[512];  // L + R

    for (int i = 0; i < total_samples; i += 256)
    {
        for (int j = 0; j < 256; j++)
        {
            int idx = i + j;
            if (idx >= total_samples) break;

            float t = (float)idx / SAMPLE_RATE;
            float sample = sinf(2 * M_PI * BEEP_FREQ * t);

            int16_t val = (int16_t)(sample * 3000); // volume
            stereo_buffer[2*j] = val;
            stereo_buffer[2*j + 1] = val;
        }

        size_t bytes_written;
        i2s_write(I2S_NUM_0, stereo_buffer, sizeof(stereo_buffer), &bytes_written, portMAX_DELAY);
    }
}

void audio_init( uint32_t sample_rate, uint8_t channels )
{
    s_sample_rate  = sample_rate;
    s_src_channels = channels;
    s_samples_played.store( 0, std::memory_order_relaxed );

    // (Re)install I2S driver at the new sample rate.
    // Uninstall first so audio_init() is safe to call between videos.
    i2s_driver_uninstall( PORT );

    i2s_config_t cfg =
    {
        .mode                 = (i2s_mode_t)( I2S_MODE_MASTER | I2S_MODE_TX ),
        .sample_rate          = sample_rate,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,  // always stereo out
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = true,   // APLL for accurate pitch — no drift
        .tx_desc_auto_clear   = true,   // silence on underrun instead of repeat
    };

    i2s_pin_config_t pins =
    {
        .bck_io_num   = I2S_BCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_DATA_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    i2s_driver_install( PORT, &cfg, 0, nullptr );
    i2s_set_pin( PORT, &pins );
    i2s_zero_dma_buffer( PORT );

    // Ring buffer: 1 second of stereo int16.
    // Trigger level = 1 so the drain task wakes as soon as any bytes arrive.
    // xStreamBufferCreate uses pvPortMalloc which routes to PSRAM when
    // CONFIG_SPIRAM_USE_MALLOC is set (standard for PSRAM-enabled boards).
    size_t ring_bytes = (size_t)sample_rate * 2 * sizeof(int16_t);
    if( s_ring )
    {
        vStreamBufferDelete( s_ring );
    }
    s_ring = xStreamBufferCreate( ring_bytes, /*trigger=*/1 );
}

void audio_start()
{
    s_running = true;
    // 8192 bytes: the I2S driver path (xStreamBufferReceive + i2s_write) can
    // consume over 1 KB of stack inside ESP-IDF; 4096 is dangerously tight.
    xTaskCreatePinnedToCore
    (
        audio_drain_task,
        "aud_drain",
        8192,
        nullptr,
        6,
        &s_task_handle,
        0
    );
}

void audio_stop()
{
    s_running = false;
    s_paused  = false;
    // Give the task up to 100ms to exit, then forcibly delete it.
    for( int i = 0; i < 10 && s_task_handle != nullptr; i++ )
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if( s_task_handle != nullptr )
    {
        vTaskDelete( s_task_handle );
        s_task_handle = nullptr;
    }
    i2s_zero_dma_buffer( PORT );
    i2s_stop( PORT );  // halt BCK/WS/DATA — consistent with audio_pause(), prevents
                       // continuous I2S clocking from producing a noise floor at idle
}

void audio_ring_push( const int16_t *pcm, uint16_t samples, uint8_t src_channels )
{
    const uint8_t *send_buf;
    size_t         send_bytes;

    if( src_channels == 2 )
    {
        // Already interleaved stereo — push directly.
        send_buf   = (const uint8_t *)pcm;
        send_bytes = (size_t)samples * 2 * sizeof(int16_t);
    }
    else
    {
        // Mono — duplicate each sample to L+R in s_stereo_expand.
        uint16_t count = ( samples > MAX_AUDIO_SAMPLES ) ? (uint16_t)MAX_AUDIO_SAMPLES : samples;
        for( uint16_t i = 0; i < count; i++ )
        {
            s_stereo_expand[i * 2]     = pcm[i];
            s_stereo_expand[i * 2 + 1] = pcm[i];
        }
        send_buf   = (const uint8_t *)s_stereo_expand;
        send_bytes = (size_t)count * 2 * sizeof(int16_t);
    }

    // Push in chunks; blocks if the ring buffer is full (back-pressure).
    while( send_bytes > 0 )
    {
        size_t sent = xStreamBufferSend( s_ring, send_buf, send_bytes, portMAX_DELAY );
        send_buf   += sent;
        send_bytes -= sent;
    }
}

uint64_t audio_samples_played()
{
    return s_samples_played.load( std::memory_order_relaxed );
}

void audio_pause()
{
    s_paused = true;
    i2s_stop( PORT );  // silence I2S output immediately
}

void audio_resume()
{
    i2s_start( PORT );  // restart I2S before unblocking the drain task
    s_paused = false;
}

void audio_set_playback_position( uint64_t sample_offset )
{
    // Safe because the drain task is sleeping in the s_paused branch
    // and is therefore not blocked on xStreamBufferReceive.
    xStreamBufferReset( s_ring );
    s_samples_played.store( sample_offset, std::memory_order_relaxed );
}
