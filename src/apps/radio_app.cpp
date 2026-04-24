// apps/radio_app.cpp
 
/*--- INCLUDES ----------------------------------------------------------------------------------*/
 
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <SD_MMC.h>
#include <vector>
#include <driver/i2s.h>
#include "drivers/input_driver.h"
#include "drivers/matrix_driver.h"
#include "core/app_manager.h"
#include "core/ui.h"
#include "config.h"
#include "apps/radio_app.h"
 
 
/*--- CONSTANTS ---------------------------------------------------------------------------------*/
 
enum AppState {
    MUSIC_SELECTING_PLAYLIST = 0,
    MUSIC_PLAYING = 1
};
 
enum MenuSubstate {
    MENU_SCANNING = 0,      // loading playlist list
    MENU_BROWSING = 1,      // showing menu, waiting for selection
    MENU_STARTING_PLAY = 2  // preparing to play (gathering songs)
};
 
 
#define I2S_PORT        I2S_NUM_0
#define AUDIO_BUF_SIZE  4096
#define SONG_QUEUE_LEN  4
#define MAX_PATH_LEN    256
#define THUMB_W         50
#define THUMB_H         50
#define THUMB_SIZE      (THUMB_W * THUMB_H * 2)  // 50x50 RGB565 = 5000 bytes
#define META_STR_LEN    64
 
// Readahead ring buffer: must be a power of two.
// 64 KB in PSRAM gives ~370 ms headroom at 44.1 kHz / 16-bit / stereo.
#define RING_BUF_SIZE   (64 * 1024)
#define RING_BUF_MASK   (RING_BUF_SIZE - 1)
 
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4];        // "RIFF"
    uint32_t fileSize;
    char     wave[4];        // "WAVE"
    char     fmt[4];         // "fmt "
    uint32_t fmtSize;
    uint16_t audioFormat;    // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)
 
struct SongMetadata {
    String    artist;
    String    album;
    String    song;
    uint16_t* thumbnail;  // RGB565 pixel data, allocated in PSRAM
    bool      hasThumb;
 
    SongMetadata() : thumbnail(nullptr), hasThumb(false) {}
    ~SongMetadata() { freeThumbnail(); }
 
    bool allocThumbnail() {
        if (!thumbnail) {
            thumbnail = (uint16_t*)ps_malloc(THUMB_SIZE);
        }
        return thumbnail != nullptr;
    }
    void freeThumbnail() {
        if (thumbnail) { free(thumbnail); thumbnail = nullptr; }
        hasThumb = false;
    }
};
 
// Queue item: POD struct safe for FreeRTOS memcpy-based queues.
// Thumbnail pointer ownership transfers through the queue.
struct SongQueueItem {
    char      path[MAX_PATH_LEN];
    char      artist[META_STR_LEN];
    char      album[META_STR_LEN];
    char      song[META_STR_LEN];
    uint16_t* thumbnail;   // PSRAM-allocated, or NULL
    bool      hasThumb;
};
 
// "Now playing" snapshot, readable from any task under metaMutex
struct NowPlaying {
    char      artist[META_STR_LEN];
    char      album[META_STR_LEN];
    char      song[META_STR_LEN];
    uint16_t* thumbnail;
    bool      hasThumb;
    bool      active;      // true while a track is playing
};
 
// Single-producer (readerTask) / single-consumer (audioTask) lock-free
// ring buffer backed by PSRAM.
struct RingBuf {
    uint8_t*         data;         // PSRAM allocation
    volatile size_t  writeHead;    // written by producer
    volatile size_t  readHead;     // written by consumer
    volatile bool    eof;          // producer sets when file is exhausted
 
    size_t available() const {
        return (writeHead - readHead) & RING_BUF_MASK;
    }
    size_t freeSpace() const {
        return RING_BUF_SIZE - 1 - available();
    }
    // Write up to `len` bytes; returns bytes written
    size_t write(const uint8_t* src, size_t len) {
        size_t canWrite = freeSpace();
        if (len > canWrite) len = canWrite;
        size_t wh = writeHead & RING_BUF_MASK;
        size_t first = RING_BUF_SIZE - wh;
        if (len <= first) {
            memcpy(data + wh, src, len);
        } else {
            memcpy(data + wh, src, first);
            memcpy(data, src + first, len - first);
        }
        writeHead += len;
        return len;
    }
    // Read up to `len` bytes into dst; returns bytes read
    size_t read(uint8_t* dst, size_t len) {
        size_t canRead = available();
        if (len > canRead) len = canRead;
        size_t rh = readHead & RING_BUF_MASK;
        size_t first = RING_BUF_SIZE - rh;
        if (len <= first) {
            memcpy(dst, data + rh, len);
        } else {
            memcpy(dst, data + rh, first);
            memcpy(dst + first, data + rh + first - RING_BUF_SIZE, len - first);
        }
        readHead += len;
        return len;
    }
};
 
struct MusicAppContext {
    AppState        state;              // main state
    MenuSubstate    menuState;          // substate when in SELECTING_PLAYLIST
 
    // Menu state
    std::vector<String>              playlists;
    std::vector<std::vector<String>> playlistSongs;
    int                              cursor;
    int                              songIndex;
    uint32_t                         retryTimer;
    String                           chosenPlaylist;
 
    // Dirty flag — written from audioTask (Core 1), read from main loop (Core 1 tick).
    // volatile ensures the compiler never caches the value in a register across
    // the task boundary.
    volatile bool dirty;
 
    // Lifecycle
    bool    isRunning;
 
    MusicAppContext() : state(MUSIC_SELECTING_PLAYLIST), menuState(MENU_SCANNING),
                        cursor(0), songIndex(0), retryTimer(0),
                        dirty(true), isRunning(false) {}
};
 
static MusicAppContext g_musicApp;
 
 
/*--- GLOBALS -----------------------------------------------------------------------------------*/
 
extern AppManager g_app_manager;
 
static QueueHandle_t      songQueue      = NULL;
static TaskHandle_t       audioHandle    = NULL;
static TaskHandle_t       readerHandle   = NULL;
static volatile bool      stopRequested  = false;
static volatile bool      backRequested  = false;
static SemaphoreHandle_t  metaMutex      = NULL;
static NowPlaying         nowPlaying     = {};
static RingBuf            ringBuf        = {};
static SemaphoreHandle_t  ringReadySem   = NULL;
static SemaphoreHandle_t  ringDoneSem    = NULL;
static char               readerPathBuf[MAX_PATH_LEN] = {};
static uint32_t           readerDataOffset = 0;
static uint32_t           readerDataSize   = 0;
 
 
/*--- HELPERS -----------------------------------------------------------------------------------*/
 
// Return a list of subdirectory paths under `path`
static std::vector<String> listSubDirs(const char* path) {
    std::vector<String> dirs;
    File root = SD_MMC.open(path);
    if (!root || !root.isDirectory()) return dirs;
 
    File entry;
    while ((entry = root.openNextFile())) {
        if (entry.isDirectory()) {
            dirs.push_back(String(entry.path()));
        }
        entry.close();
    }
    root.close();
    return dirs;
}
 
// Return the path of the first .wav file found in `dirPath`
static String findWavFile(const char* dirPath) {
    File dir = SD_MMC.open(dirPath);
    if (!dir || !dir.isDirectory()) return "";
 
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            name.toLowerCase();
            if (name.endsWith(".wav")) {
                String p = String(entry.path());
                entry.close();
                dir.close();
                return p;
            }
        }
        entry.close();
    }
    dir.close();
    return "";
}
 
// Parse metadata.txt in a song directory
// Format:  artist="..."
//          album="..."
//          thumbnail=[<binary RGB565 data>]
static bool parseMetadata(const char* songDir, SongMetadata& meta) {
    meta.artist   = "";
    meta.album    = "";
    meta.hasThumb = false;
 
    String metaPath = String(songDir) + "/metadata.txt";
    File file = SD_MMC.open(metaPath.c_str());
    if (!file) return false;
 
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
 
        if (line.startsWith("artist=")) {
            String val = line.substring(7);
            val.trim();
            if (val.startsWith("\"") && val.endsWith("\""))
                val = val.substring(1, val.length() - 1);
            meta.artist = val;
        } else if (line.startsWith("album=")) {
            String val = line.substring(6);
            val.trim();
            if (val.startsWith("\"") && val.endsWith("\""))
                val = val.substring(1, val.length() - 1);
            meta.album = val;
        } else if (line.startsWith("song=")) {
            String val = line.substring(5);
            val.trim();
            if (val.startsWith("\"") && val.endsWith("\""))
                val = val.substring(1, val.length() - 1);
            meta.song = val;
        } else if (line.startsWith("thumbnail=[")) {
            if (meta.allocThumbnail()) {
                size_t bytesRead = file.read((uint8_t*)meta.thumbnail, THUMB_SIZE);
                if (bytesRead == THUMB_SIZE) {
                    meta.hasThumb = true;
                }
            }
            break;
        }
    }
 
    file.close();
 
    Serial.printf("[meta] artist=\"%s\" album=\"%s\" song=\"%s\" thumb=%s\n",
                  meta.artist.c_str(), meta.album.c_str(), meta.song.c_str(),
                  meta.hasThumb ? "yes" : "no");
    return true;
}
 
// Fisher-Yates shuffle using hardware RNG
static void shuffleVector(std::vector<String>& v) {
    for (int i = (int)v.size() - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        std::swap(v[i], v[j]);
    }
}
 
 
/*--- SETUP I2S ---------------------------------------------------------------------------------*/
 
static void installI2S(uint32_t sampleRate, uint16_t bitsPerSample, uint16_t numChannels) {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = sampleRate;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)bitsPerSample;
    cfg.channel_format       = (numChannels == 1) ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                  : I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 1024;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
 
    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_BCK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
    pins.data_out_num = I2S_DATA_PIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
 
    i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_PORT, &pins);
    i2s_zero_dma_buffer(I2S_PORT);
}
 
 
/*--- NOW PLAYING -------------------------------------------------------------------------------*/
 
// Read current now-playing info. Returns true if a track is currently active.
// `thumbOut` receives the raw pointer (still owned by audioTask — do NOT free it).
bool musicPlayerGetNowPlaying(char* artistOut, size_t artistLen,
                              char* albumOut,  size_t albumLen,
                              char* songOut,   size_t songLen,
                              const uint16_t** thumbOut, bool* hasThumb) {
    if (!metaMutex) return false;
    xSemaphoreTake(metaMutex, portMAX_DELAY);
    bool active = nowPlaying.active;
    if (active) {
        if (artistOut) strlcpy(artistOut, nowPlaying.artist, artistLen);
        if (albumOut)  strlcpy(albumOut,  nowPlaying.album,  albumLen);
        if (songOut)   strlcpy(songOut,   nowPlaying.song,   songLen);
        if (thumbOut)  *thumbOut  = nowPlaying.thumbnail;
        if (hasThumb)  *hasThumb = nowPlaying.hasThumb;
    }
    xSemaphoreGive(metaMutex);
    return active;
}
 
 
/*--- DRAW --------------------------------------------------------------------------------------*/
 
// Draw the playlist selection menu on the matrix.
// Mirrors the style used by MenuApp: bordered buttons with a highlight on the cursor row.
static void drawPlaylistMenu( MatrixPanel_I2S_DMA* matrix,
                              const std::vector<String>& playlists,
                              int cursor )
{
    matrix->clearScreen();
    matrix->setFont( nullptr );
    matrix->setTextSize( 1 );
 
    // Layout constants — tune to your panel size
    const int FIRST_X  = 2;
    const int FIRST_Y  = 2;
    const int BTN_W    = 58;
    const int BTN_H    = 9;
    const int Y_STRIDE = BTN_H + 2;
    const int MAX_ROWS = 5;  // max visible entries
 
    int count = min( (int)playlists.size(), MAX_ROWS );
 
    for ( int i = 0; i < count; i++ )
    {
        int x = FIRST_X;
        int y = FIRST_Y + i * Y_STRIDE;
 
        // Strip leading path component for display
        const char* raw  = playlists[i].c_str();
        const char* name = strrchr( raw, '/' );
        name = name ? name + 1 : raw;
 
        // Background fill + border
        draw_rect( matrix, COLOR_UI_MAIN, x, y, BTN_W, BTN_H );
        draw_rect_unfilled( matrix, COLOR_UI_ACCENT, x - 1, y - 1, BTN_W + 2, BTN_H + 2 );
 
        // Highlight selected row with a bright outline
        if ( i == cursor )
            draw_rect_unfilled( matrix, COLOR_TEXT, x - 1, y - 1, BTN_W + 2, BTN_H + 2 );
 
        matrix->setTextColor( COLOR_TEXT );
        matrix->setCursor( x + 2, y + 1 );
        matrix->print( name );
    }
}
 
// Draw the now-playing screen on the matrix.
static void drawNowPlaying( MatrixPanel_I2S_DMA* matrix,
                            const char* artist,
                            const char* song,
                            const char* album,
                            const uint16_t* thumb,
                            bool hasThumb )
{
    matrix->clearScreen();
    matrix->setFont( nullptr );
    matrix->setTextSize( 1 );
    matrix->setTextColor( COLOR_TEXT );
 
    if ( hasThumb && thumb )
    {
        // Blit 50×50 RGB565 thumbnail into the left portion of the panel
        for ( int row = 0; row < THUMB_H; row++ )
        {
            for ( int col = 0; col < THUMB_W; col++ )
            {
                matrix->drawPixel( col, row, thumb[ row * THUMB_W + col ] );
            }
        }
        // Place text to the right of the thumbnail
        const int TX = THUMB_W + 2;
        matrix->setCursor( TX, 2 );  matrix->print( song );
        matrix->setCursor( TX, 12 ); matrix->print( artist );
        matrix->setCursor( TX, 22 ); matrix->print( album );
    }
    else
    {
        // Full-width text layout when no thumbnail is available
        matrix->setCursor( 2, 2 );  matrix->print( song );
        matrix->setCursor( 2, 12 ); matrix->print( artist );
        matrix->setCursor( 2, 22 ); matrix->print( album );
    }
}
 
 
/*--- TASKS  ------------------------------------------------------------------------------------*/
 
// Reads raw PCM data from SD into the ring buffer.
// audioTask sets readerPathBuf and gives ringReadySem to trigger a fill.
// readerTask gives ringDoneSem when the file's data chunk is exhausted.
static void readerTask(void* /*param*/) {
    static uint8_t sdBuf[AUDIO_BUF_SIZE];
 
    while (!stopRequested) {
        if (xSemaphoreTake(ringReadySem, pdMS_TO_TICKS(200)) != pdTRUE)
            continue;
        if (stopRequested) break;
 
        File file = SD_MMC.open(readerPathBuf);
        if (!file) {
            Serial.printf("[reader] Open failed: %s\n", readerPathBuf);
            ringBuf.eof = true;
            xSemaphoreGive(ringDoneSem);
            continue;
        }
 
        file.seek(readerDataOffset);
 
        uint32_t remaining = readerDataSize;
        while (remaining > 0 && !stopRequested && !backRequested) {
            while (ringBuf.freeSpace() < AUDIO_BUF_SIZE &&
                   !stopRequested && !backRequested) {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            if (stopRequested || backRequested) break;
 
            size_t toRead = (remaining < AUDIO_BUF_SIZE) ? remaining : AUDIO_BUF_SIZE;
            size_t got    = file.read(sdBuf, toRead);
            if (got == 0) break;
            ringBuf.write(sdBuf, got);
            remaining -= got;
        }
 
        file.close();
        ringBuf.eof = true;
        xSemaphoreGive(ringDoneSem);
    }
 
    Serial.println("[reader] Task exiting");
    readerHandle = NULL;
    vTaskDelete(NULL);
}
 
// Receives songs from the queue, parses WAV headers, then drives I2S
// from the ring buffer filled by readerTask.
static void audioTask(void* /*param*/) {
    static uint8_t i2sBuf[AUDIO_BUF_SIZE];
    SongQueueItem item;
    bool i2sUp = false;
    uint32_t curRate = 0;
    uint16_t curBits = 0, curCh = 0;
 
    while (!stopRequested) {
        if (xQueueReceive(songQueue, &item, pdMS_TO_TICKS(200)) != pdTRUE)
            continue;
 
        // Publish "now playing"
        xSemaphoreTake(metaMutex, portMAX_DELAY);
        if (nowPlaying.thumbnail) free(nowPlaying.thumbnail);
        strlcpy(nowPlaying.artist, item.artist, META_STR_LEN);
        strlcpy(nowPlaying.album,  item.album,  META_STR_LEN);
        strlcpy(nowPlaying.song,   item.song,   META_STR_LEN);
        nowPlaying.thumbnail = item.thumbnail;
        nowPlaying.hasThumb  = item.hasThumb;
        nowPlaying.active    = true;
        item.thumbnail = nullptr;
        xSemaphoreGive(metaMutex);
 
        // Signal the main task that metadata changed so draw() repaints
        g_musicApp.dirty = true;
 
        Serial.printf("[audio] Playing: %s\n", item.path);
 
        File file = SD_MMC.open(item.path);
        if (!file) {
            Serial.printf("[audio] Open failed: %s\n", item.path);
            continue;
        }
 
        WavHeader hdr;
        if (file.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr) ||
            memcmp(hdr.riff, "RIFF", 4) != 0 ||
            memcmp(hdr.wave, "WAVE", 4) != 0) {
            Serial.println("[audio] Invalid WAV");
            file.close(); continue;
        }
        if (hdr.audioFormat != 1) {
            Serial.println("[audio] Only PCM WAV supported");
            file.close(); continue;
        }
 
        // Find "data" chunk
        file.seek(12 + 8 + hdr.fmtSize);
        char chunkId[4];
        uint32_t chunkSize = 0;
        bool dataFound = false;
        while (file.available() >= 8) {
            file.read((uint8_t*)chunkId, 4);
            file.read((uint8_t*)&chunkSize, 4);
            if (memcmp(chunkId, "data", 4) == 0) { dataFound = true; break; }
            file.seek(file.position() + chunkSize);
        }
        uint32_t dataOffset = file.position();
        file.close();
 
        if (!dataFound) {
            Serial.println("[audio] No data chunk");
            continue;
        }
 
        Serial.printf("[audio] %u Hz / %u-bit / %u ch\n",
                       hdr.sampleRate, hdr.bitsPerSample, hdr.numChannels);
 
        if (!i2sUp || curRate != hdr.sampleRate ||
            curBits != hdr.bitsPerSample || curCh != hdr.numChannels) {
            if (i2sUp) i2s_driver_uninstall(I2S_PORT);
            installI2S(hdr.sampleRate, hdr.bitsPerSample, hdr.numChannels);
            curRate = hdr.sampleRate;
            curBits = hdr.bitsPerSample;
            curCh   = hdr.numChannels;
            i2sUp   = true;
        }
 
        ringBuf.writeHead   = 0;
        ringBuf.readHead    = 0;
        ringBuf.eof         = false;
        readerDataOffset    = dataOffset;
        readerDataSize      = chunkSize;
        strlcpy(readerPathBuf, item.path, MAX_PATH_LEN);
        xSemaphoreGive(ringReadySem);
 
        size_t written;
        while (!stopRequested && !backRequested) {
            size_t avail = ringBuf.available();
            if (avail == 0) {
                if (ringBuf.eof) break;
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            size_t toWrite = (avail < AUDIO_BUF_SIZE) ? avail : AUDIO_BUF_SIZE;
            ringBuf.read(i2sBuf, toWrite);
            i2s_write(I2S_PORT, i2sBuf, toWrite, &written, pdMS_TO_TICKS(500));
        }
 
        xSemaphoreTake(ringDoneSem, portMAX_DELAY);
 
        if (!stopRequested && !backRequested) {
            memset(i2sBuf, 0, AUDIO_BUF_SIZE);
            for (int i = 0; i < 4; i++)
                i2s_write(I2S_PORT, i2sBuf, AUDIO_BUF_SIZE, &written, pdMS_TO_TICKS(100));
        }
 
        if (backRequested) {
            xSemaphoreTake(metaMutex, portMAX_DELAY);
            nowPlaying.active = false;
            xSemaphoreGive(metaMutex);
        }
 
        Serial.println("[audio] Track finished");
    }
 
    if (i2sUp) {
        i2s_zero_dma_buffer(I2S_PORT);
        i2s_driver_uninstall(I2S_PORT);
    }
    xSemaphoreTake(metaMutex, portMAX_DELAY);
    nowPlaying.active = false;
    xSemaphoreGive(metaMutex);
 
    Serial.println("[audio] Task exiting");
    audioHandle = NULL;
    vTaskDelete(NULL);
}
 
 
/*--- APP OVERRIDES -----------------------------------------------------------------------------*/
 
void RadioApp::onEnter()
{
    if (g_musicApp.isRunning) return;
 
    Serial.println("[musicApp] Entering");
 
    stopRequested = false;
    backRequested = false;
 
    // Allocate ring buffer in PSRAM
    if (!ringBuf.data) {
        ringBuf.data = (uint8_t*)ps_malloc(RING_BUF_SIZE);
        if (!ringBuf.data) {
            Serial.println("[musicApp] Ring buffer alloc failed");
            return;
        }
    }
    ringBuf.writeHead = 0;
    ringBuf.readHead  = 0;
    ringBuf.eof       = false;
 
    songQueue = xQueueCreate(SONG_QUEUE_LEN, sizeof(SongQueueItem));
    if (!songQueue) {
        Serial.println("[musicApp] Queue creation failed");
        return;
    }
 
    metaMutex    = xSemaphoreCreateMutex();
    ringReadySem = xSemaphoreCreateBinary();
    ringDoneSem  = xSemaphoreCreateBinary();
    memset(&nowPlaying, 0, sizeof(nowPlaying));
 
    xTaskCreatePinnedToCore(readerTask, "reader", 4096, NULL, 2, &readerHandle, 0);
    xTaskCreatePinnedToCore(audioTask,  "audio",  8192, NULL, 2, &audioHandle,  1);
 
    g_musicApp.state     = MUSIC_SELECTING_PLAYLIST;
    g_musicApp.menuState = MENU_SCANNING;
    g_musicApp.cursor    = 0;
    g_musicApp.songIndex = 0;
    g_musicApp.retryTimer = 0;
    g_musicApp.dirty     = true;
    g_musicApp.isRunning = true;
 
    matrix_clear();
    Serial.println("[musicApp] Started");
}
 
void RadioApp::onResume()
{
    Serial.println("[musicApp] Resumed");
    g_musicApp.dirty = true;
    matrix_clear();
}
 
AppCmd RadioApp::update()
{
    if (!g_musicApp.isRunning) return AppCmd::NONE;
 
    uint32_t now = millis();
 
    // Drain the input queue. We process one meaningful event per update()
    // call, consistent with how menu_app.cpp works.
    InputEvent btn = InputEvent::NONE;
    while( input_has_event() )
    {
        btn = input_next_event();
        break;  // take only the first event this tick
    }
 
    if (g_musicApp.state == MUSIC_SELECTING_PLAYLIST)
    {
        // ── Menu State ──────────────────────────────────────────────────────
        switch (g_musicApp.menuState)
        {
            case MENU_SCANNING:
            {
                // Throttled playlist scan — retry every second until folders appear
                if (now - g_musicApp.retryTimer >= 1000)
                {
                    g_musicApp.retryTimer = now;
                    g_musicApp.playlists  = listSubDirs("/music");
                    if (!g_musicApp.playlists.empty())
                    {
                        g_musicApp.cursor    = 0;
                        g_musicApp.menuState = MENU_BROWSING;
                        g_musicApp.dirty     = true;
                    }
                    else
                    {
                        Serial.println("[musicApp] No playlists found, retrying...");
                    }
                }
                break;
            }
 
            case MENU_BROWSING:
            {
                if (btn == InputEvent::BTN_A)
                {
                    // Back to main menu
                    Serial.println("[musicApp] BTN_A -> POP");
                    return AppCmd::POP;
                }
                else if (btn == InputEvent::BTN_B)
                {
                    // Scroll up
                    g_musicApp.cursor = max( g_musicApp.cursor - 1, 0 );
                    g_musicApp.dirty  = true;
                }
                else if (btn == InputEvent::BTN_C)
                {
                    // Scroll down
                    g_musicApp.cursor = min( g_musicApp.cursor + 1,
                                             (int)g_musicApp.playlists.size() - 1 );
                    g_musicApp.dirty  = true;
                }
                else if (btn == InputEvent::BTN_D)
                {
                    // Confirm selection — gather and shuffle songs
                    g_musicApp.chosenPlaylist = g_musicApp.playlists[g_musicApp.cursor];
                    const char* raw  = g_musicApp.chosenPlaylist.c_str();
                    const char* name = strrchr( raw, '/' );
                    name = name ? name + 1 : raw;
                    Serial.printf("[musicApp] Selected playlist: %s\n", name);
 
                    g_musicApp.playlistSongs.clear();
                    std::vector<String> songDirs;
                    for (const auto& dir : listSubDirs(g_musicApp.chosenPlaylist.c_str()))
                        songDirs.push_back(dir);
 
                    if (songDirs.empty())
                    {
                        Serial.println("[musicApp] Playlist is empty, rescanning");
                        g_musicApp.menuState = MENU_SCANNING;
                    }
                    else
                    {
                        shuffleVector(songDirs);
                        for (const auto& d : songDirs)
                            g_musicApp.playlistSongs.push_back({d});
 
                        Serial.printf("[musicApp] Shuffled %d songs\n",
                                      g_musicApp.playlistSongs.size());
 
                        backRequested        = false;
                        g_musicApp.songIndex = 0;
                        g_musicApp.state     = MUSIC_PLAYING;
                        g_musicApp.dirty     = true;
                    }
                }
                break;
            }
 
            case MENU_STARTING_PLAY:
                // Shouldn't normally be reached; transition to playing just in case
                g_musicApp.state = MUSIC_PLAYING;
                g_musicApp.dirty = true;
                break;
        }
    }
    else if (g_musicApp.state == MUSIC_PLAYING)
    {
        // ── Playback State ───────────────────────────────────────────────────
        // Any button press triggers a return to the playlist menu
        if (btn != InputEvent::NONE)
        {
            Serial.println("[musicApp] Button press -> returning to menu");
            backRequested = true;
        }
 
        if (!backRequested &&
            g_musicApp.songIndex < (int)g_musicApp.playlistSongs.size())
        {
            // Enqueue one song per update() call
            String songDir = g_musicApp.playlistSongs[g_musicApp.songIndex][0];
            g_musicApp.songIndex++;
 
            String wavPath = findWavFile(songDir.c_str());
            if (wavPath.length() > 0)
            {
                SongMetadata meta;
                parseMetadata(songDir.c_str(), meta);
 
                SongQueueItem item = {};
                strlcpy(item.path,   wavPath.c_str(),    MAX_PATH_LEN);
                strlcpy(item.artist, meta.artist.c_str(), META_STR_LEN);
                strlcpy(item.album,  meta.album.c_str(),  META_STR_LEN);
                strlcpy(item.song,   meta.song.c_str(),   META_STR_LEN);
                item.thumbnail = meta.thumbnail;
                item.hasThumb  = meta.hasThumb;
                meta.thumbnail = nullptr;  // transfer ownership to queue
 
                if (xQueueSend(songQueue, &item, 0) != pdTRUE)
                {
                    if (item.thumbnail) free(item.thumbnail);
                    g_musicApp.songIndex--;  // retry on next update
                }
            }
        }
        else if (backRequested || g_musicApp.songIndex >= (int)g_musicApp.playlistSongs.size())
        {
            if (backRequested)
            {
                // Drain queued-but-unplayed songs
                SongQueueItem discard;
                while (xQueueReceive(songQueue, &discard, 0) == pdTRUE)
                {
                    if (discard.thumbnail) free(discard.thumbnail);
                }
 
                // Wait for the audio task to finish the current track before
                // returning to the menu, so we don't race on shared resources.
                // Read nowPlaying.active under metaMutex — audioTask writes it
                // from Core 1 and we must not read it naked.
                bool audioIdle = false;
                if (metaMutex)
                {
                    xSemaphoreTake(metaMutex, portMAX_DELAY);
                    audioIdle = !nowPlaying.active;
                    xSemaphoreGive(metaMutex);
                }
                if (audioIdle)
                {
                    Serial.println("[musicApp] Audio idle -> returning to menu");
                    g_musicApp.state     = MUSIC_SELECTING_PLAYLIST;
                    g_musicApp.menuState = MENU_SCANNING;
                    backRequested        = false;
                    g_musicApp.dirty     = true;
                }
            }
            else
            {
                Serial.println("[musicApp] Playlist finished");
                g_musicApp.state     = MUSIC_SELECTING_PLAYLIST;
                g_musicApp.menuState = MENU_SCANNING;
                g_musicApp.dirty     = true;
            }
        }
    }
 
    return AppCmd::NONE;
}
 
void RadioApp::draw()
{
    if (!g_musicApp.dirty) return;
    g_musicApp.dirty = false;
 
    MatrixPanel_I2S_DMA* p = matrix_panel();
 
    if (g_musicApp.state == MUSIC_SELECTING_PLAYLIST)
    {
        if (g_musicApp.menuState == MENU_SCANNING || g_musicApp.playlists.empty())
        {
            // Show a simple "Scanning…" message while we wait for the SD
            p->clearScreen();
            p->setFont( nullptr );
            p->setTextSize( 1 );
            p->setTextColor( COLOR_TEXT );
            p->setCursor( 4, 4 );
            p->print( "Scanning..." );
        }
        else
        {
            drawPlaylistMenu( p, g_musicApp.playlists, g_musicApp.cursor );
        }
    }
    else  // MUSIC_PLAYING
    {
        char artist[META_STR_LEN] = {};
        char album[META_STR_LEN]  = {};
        char song[META_STR_LEN]   = {};
        bool hasThumb = false;
        const uint16_t* thumb = nullptr;
 
        bool active = musicPlayerGetNowPlaying( artist, sizeof(artist),
                                                album,  sizeof(album),
                                                song,   sizeof(song),
                                                &thumb, &hasThumb );
        if (active)
        {
            drawNowPlaying( p, artist, song, album, thumb, hasThumb );
        }
        else
        {
            // Track not yet started — show a brief loading screen
            p->clearScreen();
            p->setFont( nullptr );
            p->setTextSize( 1 );
            p->setTextColor( COLOR_TEXT );
            p->setCursor( 4, 4 );
            p->print( "Loading..." );
        }
    }
}
 
void RadioApp::onExit()
{
    if (!g_musicApp.isRunning) return;
 
    Serial.println("[musicApp] Exiting");
    stopRequested = true;
 
    // Wait up to 500 ms for each task to exit voluntarily, then force-delete.
    // A stalled SD read or a full ring buffer must never hang the main loop.
    for (int i = 0; i < 10 && (audioHandle || readerHandle); i++)
        vTaskDelay(pdMS_TO_TICKS(50));
 
    if (readerHandle)
    {
        Serial.println("[musicApp] readerTask did not exit — force deleting");
        vTaskDelete(readerHandle);
        readerHandle = NULL;
    }
    if (audioHandle)
    {
        Serial.println("[musicApp] audioTask did not exit — force deleting");
        vTaskDelete(audioHandle);
        audioHandle = NULL;
    }
 
    // Ring buffer semaphores may be in an inconsistent state after a force-delete;
    // zero the heads so no stale data is replayed if the app is re-entered.
    ringBuf.writeHead = 0;
    ringBuf.readHead  = 0;
    ringBuf.eof       = true;
 
    // Drain queue
    if (songQueue) {
        SongQueueItem discard;
        while (xQueueReceive(songQueue, &discard, 0) == pdTRUE) {
            if (discard.thumbnail) free(discard.thumbnail);
        }
        vQueueDelete(songQueue);
        songQueue = NULL;
    }
 
    if (nowPlaying.thumbnail) { free(nowPlaying.thumbnail); nowPlaying.thumbnail = nullptr; }
    memset(&nowPlaying, 0, sizeof(nowPlaying));
    if (metaMutex)    { vSemaphoreDelete(metaMutex);    metaMutex    = NULL; }
    if (ringReadySem) { vSemaphoreDelete(ringReadySem); ringReadySem = NULL; }
    if (ringDoneSem)  { vSemaphoreDelete(ringDoneSem);  ringDoneSem  = NULL; }
    if (ringBuf.data) { free(ringBuf.data); ringBuf.data = nullptr; }
 
    g_musicApp.isRunning = false;
    g_musicApp.playlists.clear();
    g_musicApp.playlistSongs.clear();
 
    Serial.println("[musicApp] Stopped");
}
