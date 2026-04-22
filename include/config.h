#pragma once

// config.h - single source of truth for hardware wiring and sizing.
// Edit this file when you rewire the board; nothing else should change.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <stdint.h>


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

// HUB75 LED Matrx
static constexpr int PANEL_W = 128;
static constexpr int PANEL_H = 64;

// HUB75 panel timing.
// Quick tuning knobs when comparing flicker vs stability:
// - Try HZ_10M, then HZ_20M if the image is stable.
// - Try CLKPHASE false first, then true if colors or latching look off.
//
// Suggested test presets:
//   Preset A: HZ_10M + false
//   Preset B: HZ_20M + false
//   Preset C: HZ_20M + true
static constexpr auto HUB75_I2S_SPEED = HUB75_I2S_CFG::HZ_10M;
static constexpr bool HUB75_CLK_PHASE = false;

// HUB75 pin assignments
static constexpr int HUB75_R1  = 17;
static constexpr int HUB75_G1  = 4;
static constexpr int HUB75_B1  = 5;
static constexpr int HUB75_R2  = 6;
static constexpr int HUB75_G2  = 7;
static constexpr int HUB75_B2  = 3;
static constexpr int HUB75_A   = 9;
static constexpr int HUB75_B   = 10;
static constexpr int HUB75_C   = 11;
static constexpr int HUB75_D   = 12;
static constexpr int HUB75_E   = 13;
static constexpr int HUB75_LAT = 15;
static constexpr int HUB75_OE  = 16;
static constexpr int HUB75_CLK = 8;

// SD Card (SDMMC 4-bit)
static constexpr int SD_CMD_PIN = 2;
static constexpr int SD_CLK_PIN = 41;
static constexpr int SD_D0_PIN  = 1;
static constexpr int SD_D1_PIN  = 48;
static constexpr int SD_D2_PIN  = 38;
static constexpr int SD_D3_PIN  = 40;

// I2S Audio
static constexpr int I2S_MCK_PIN  = 18;  // MCK
static constexpr int I2S_BCK_PIN  = 21;  // BCLK
static constexpr int I2S_WS_PIN   = 47;  // LRC
static constexpr int I2S_DATA_PIN = 19;  // DIN

// Serial from input controller
static constexpr int INPUT_RX_PIN = 14;
static constexpr int INPUT_TX_PIN = -1;

// Audio Buffer
static constexpr size_t MAX_AUDIO_SAMPLES = 4096; // per channel, per frame
