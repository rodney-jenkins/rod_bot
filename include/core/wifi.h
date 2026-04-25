#pragma once

// wifi.h - wifi information

/*--- INCLUDES ----------------------------------------------------------------------------------*/

/*--- CONSTANTS ---------------------------------------------------------------------------------*/

// http://127.0.0.1:5000
// http://192.168.86.21:5000


// Internet
constexpr const char *SSID = "FesBuck House";
constexpr const char *PASSWORD = "FesBuck412";

// Commands
constexpr const char *SRVR_IMAGE_CMD           = "http://192.168.86.21:5000/image";
constexpr const char *SRVR_PLAYLISTS_CMD       = "http://192.168.86.21:5000/playlists";
constexpr const char *SRVR_PLAY_PLAYLIST_CMD   = "http://192.168.86.21:5000/play/playlist/";
constexpr const char *SRVR_STREAM_PLAYLIST_CMD = "http://192.168.86.21:5000/stream?playlist_id=";


/*--- PROTOTYPES --------------------------------------------------------------------------------*/
