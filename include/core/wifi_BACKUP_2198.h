#pragma once

// wifi.h - wifi information

/*--- INCLUDES ----------------------------------------------------------------------------------*/

/*--- CONSTANTS ---------------------------------------------------------------------------------*/

<<<<<<< Updated upstream
// Internet
static const char *SSID = "Slow_Internet";
static const char *PASSWORD = "YouGotMail911";
static const char *SRVR_IMAGE_CMD = "http://192.168.86.21:5000/image";

=======
>>>>>>> Stashed changes
// http://127.0.0.1:5000
// http://192.168.86.21:5000


// Internet
const char *SSID = "Slow_Internet";
const char *PASSWORD = "YouGotMail911";

// Commands
const char *SRVR_IMAGE_CMD           = "http://192.168.86.21:5000/image"
const char *SRVR_PLAYLISTS_CMD       = "http://192.168.86.21:5000/playlists"
const char *SRVR_PLAY_PLAYLIST_CMD   = "http://192.168.86.21:5000/play/playlist/"
const char *SRVR_STREAM_PLAYLIST_CMD = "http://192.168.86.21:5000/stream?playlist_id="


/*--- PROTOTYPES --------------------------------------------------------------------------------*/
