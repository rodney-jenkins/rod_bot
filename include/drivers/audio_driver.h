#pragma once

// drivers/audio_driver.h — I2S audio output with PSRAM ring buffer.
//
// Audio is fully decoupled from the video loop:
//   audio_ring_push()      — called by the read-ahead task to feed raw PCM
//   audio_samples_played() — called by the video loop as the master clock
//
// The internal drain task on Core 0 empties the ring buffer to I2S DMA
// independently of anything the CPU is doing.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <stdint.h>
#include <stddef.h>


/*--- FUNCTIONS ---------------------------------------------------------------------------------*/

// Play a beep to test speaker
void audio_beep();

// Initialise I2S and allocate the PSRAM ring buffer.
// Safe to call again between files to change sample rate.
void audio_init( uint32_t sample_rate, uint8_t channels );

// Start the I2S drain task on Core 0.  Call once after audio_init().
void audio_start();

// Stop the drain task and silence I2S output.
void audio_stop();

// Push PCM into the ring buffer.  Blocks until space is available.
// Mono source is automatically duplicated to stereo for I2S.
// `samples` = samples per channel.
void audio_ring_push( const int16_t *pcm, uint16_t samples, uint8_t src_channels );

// Total stereo sample-pairs sent to I2S DMA since audio_start().
// Thread-safe atomic read — this is the master playback clock for video sync.
uint64_t audio_samples_played();

// audio_pause() silences I2S and freezes the playback clock.
void audio_pause();

// audio_resume() restarts I2S from exactly where it left off.
void audio_resume();

// Reset the audio playback clock to sample_offset and flush the ring buffer.
// Must only be called while audio is paused (audio_pause() has been called),
// so the drain task is sleeping and not blocked on the stream buffer.
void audio_set_playback_position( uint64_t sample_offset );
