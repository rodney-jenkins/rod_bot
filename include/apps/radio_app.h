#pragma once

// apps/radio_app.h — SD card local music player app.
// Browse playlists (folders) and songs (files) on SD card /music folder.

/*--- INCLUDES ----------------------------------------------------------------------------------*/

#include "core/app.h"
#include <vector>
#include <string>


/*--- CLASSES -----------------------------------------------------------------------------------*/

class RadioApp : public IApp
{
public:
    void   onEnter()  override;
    void   onExit()   override;
    AppCmd update()   override;
    void   draw()     override;

private:
    enum State
    {
        STATE_LOADING,       // Scanning SD card
        STATE_PLAYLIST_SEL,  // Selecting a playlist (folder)
        STATE_PLAYING,       // Currently playing
        STATE_ERROR          // Error state
    };

    State _state = STATE_LOADING;
    bool  _dirty = true;

    // Playlist (folder in /music)
    struct Playlist
    {
        std::string path;    // Full path to folder
        std::string name;    // Folder name
    };
    std::vector<Playlist> _playlists;
    int _selected_playlist = 0;

    // Current playlist songs (shuffled)
    std::vector<std::string> _current_songs;  // Full paths to audio files
    int _current_song_index = 0;
    std::string _current_song_name;
    std::string _error_msg;
    bool _is_playing = false;

    // Song metadata
    struct SongMeta
    {
        std::string artist;
        std::string album;
        std::string art_filename;   // basename of .art file
    };
    SongMeta _current_meta;

    // Album art buffer (50x50 RGB565 = 5000 bytes)
    static constexpr int ART_SIZE = 50;
    uint16_t *_art_buf = nullptr;
    bool      _art_loaded = false;

    void scan_playlists();
    void load_and_play_playlist(const std::string &playlist_path);
    void play_next_song();
    void load_song_meta( const std::string &song_path );
    void extract_song_name( const std::string &path, std::string &out_name );
};


/*--- PROTOTYPES ----------------------------------------------------------------------------------*/

void radio_start( const char *song_path );

void radio_stop();

bool radio_is_running();