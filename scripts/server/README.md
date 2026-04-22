# Flask Server for Raspberry Pi

A lightweight Flask server designed to run on a Raspberry Pi and serve an ESP32-S3 client.

## Features

- Simple GET endpoint at `/hello` that returns "Hello World"
- Listens on all interfaces (0.0.0.0) for connections from ESP32-S3
- Runs on port 5000
- Upload images from ESP32-S3 and serve random 128x64 RGB565 images
- **Internet Radio Architecture**: Stream music from Spotify playlists with independent radio stations
  - Multiple "stations" (one per playlist) can run simultaneously
  - All clients listening to the same station hear the same track
  - Automatic continuous playback (loops through playlist)
  - No skipping allowed (like real radio)
- Comprehensive track metadata (album, artist, duration, release date)
- Album artwork in RGB565 format (50x50 pixels)
- **Robust resource management** – Clean FFmpeg process handling per station

## Requirements

- Python 3.7+
- Raspberry Pi (or any Linux-based system)

## Setup

1. Install dependencies:
```bash
pip install -r requirements.txt
```

2. (Optional) Set up Spotify music streaming:
   - Create a Spotify Developer account at [https://developer.spotify.com](https://developer.spotify.com)
   - Create an app to get your Client ID and Client Secret
   - Copy `music_config.json.example` to `music_config.json`
   - Fill in your Spotify credentials and playlist IDs
   - Install FFmpeg:
     - **Windows:** `choco install ffmpeg` or download from [ffmpeg.org](https://ffmpeg.org/download.html)
     - **Raspberry Pi:** `sudo apt-get install ffmpeg`

3. Run the server:
```bash
python app.py
```

The server will start and listen on `http://0.0.0.0:5000`

## API Endpoints

### GET /hello
Returns "Hello World"

**Example:**
```bash
curl http://localhost:5000/hello
```

**Response:**
```
Hello World
```

### POST /upload
Upload an image file to the server. The server will save it to the `uploads/` folder.

**Request:**
- Method: POST
- Content-Type: multipart/form-data
- File field: `file`
- Allowed formats: jpg, jpeg, png, gif, bmp
- Max file size: 10MB

**Example with curl:**
```bash
curl -X POST -F "file=@image.jpg" http://localhost:5000/upload
```

**Response (Success - 200):**
```json
{
  "message": "File uploaded successfully",
  "filename": "1234567890_image.jpg",
  "path": "uploads/1234567890_image.jpg"
}
```

**Response (Error - 400):**
```json
{
  "error": "File type not allowed. Allowed types: jpg, jpeg, png, gif, bmp"
}
```

### GET /image
Retrieve the current random image in RGB565 format (128x64 pixels, 16KB binary data).

**Features:**
- Returns a random image from uploaded files
- Image updates every 2 minutes and is shared across all clients
- Format: RGB565 (little-endian, 2 bytes per pixel)
- Resolution: 128x64 pixels
- Total size: 16,384 bytes (128 × 64 × 2)

**Example with curl:**
```bash
curl http://localhost:5000/image -o image.rgb565
```

**Response:**
- Content-Type: application/octet-stream
- Body: Raw RGB565 binary data (16KB)

### GET /playlists
Get list of available playlists configured in the server.

**Example:**
```bash
curl http://localhost:5000/playlists
```

**Response:**
```json
{
  "playlists": [
    {"name": "Chill Vibes", "id": "PLAYLIST_ID_1"},
    {"name": "Lo-Fi Hip Hop", "id": "PLAYLIST_ID_2"}
  ],
  "total": 2
}
```

### GET /playlist/<playlist_id>/tracks
Get all tracks from a specific playlist.

**Example:**
```bash
curl http://localhost:5000/playlist/PLAYLIST_ID_1/tracks
```

**Response:**
```json
{
  "playlist_id": "PLAYLIST_ID_1",
  "track_count": 25,
  "tracks": [
    {
      "name": "Track Name",
      "artist": "Artist Name",
      "preview_url": "https://...",
      "duration_ms": 180000
    }
  ]
}
```

### POST /play/playlist/<playlist_id>
Start a radio station for a specific playlist. All clients will listen to the same stream.

**Example:**
```bash
curl -X POST http://localhost:5000/play/playlist/PLAYLIST_ID_1
```

**Response:**
```json
{
  "message": "Radio station started",
  "playlist_id": "PLAYLIST_ID_1",
  "track_count": 25,
  "current_track": {
    "name": "Track Name",
    "artist": "Artist Name",
    ...
  }
}
```

**Note:** Multiple stations can run simultaneously. Each playlist can only have one active station.

### GET /current-track
Get info about the currently playing track on a specific radio station.

**Query Parameters:**
- `playlist_id` (required): The station's playlist ID

**Example:**
```bash
curl http://localhost:5000/current-track?playlist_id=PLAYLIST_ID_1
```

**Response:**
```json
{
  "playlist_id": "PLAYLIST_ID_1",
  "current_index": 5,
  "track": {
    "name": "Track Name",
    "artist": "Artist Name",
    "album": "Album Name",
    "preview_url": "https://...",
    "duration_ms": 180000,
    "image_url": "https://...",
    "spotify_id": "track_id",
    "release_date": "2023-01-15"
  }
}
```

### GET /track-metadata
Get comprehensive metadata for the currently playing track on a specific radio station.

**Query Parameters:**
- `playlist_id` (required): The station's playlist ID

**Example:**
```bash
curl http://localhost:5000/track-metadata?playlist_id=PLAYLIST_ID_1
```

**Response:**
```json
{
  "track_name": "Track Name",
  "artist": "Artist Name",
  "album": "Album Name",
  "duration_ms": 180000,
  "release_date": "2023-01-15",
  "spotify_id": "track_id",
  "image_url": "https://...",
  "playlist_position": 0
}
```

### GET /album-art
Get the album art of the currently playing track on a specific radio station as RGB565 format (50x50 pixels).

**Query Parameters:**
- `playlist_id` (required): The station's playlist ID

**Features:**
- Resolution: 50x50 pixels
- Format: RGB565 (little-endian, 2 bytes per pixel)
- Size: 5,000 bytes (50 × 50 × 2)

**Example:**
```bash
curl http://localhost:5000/album-art -o album_art.rgb565
```

**Response:**
- Content-Type: application/octet-stream
- Body: Raw RGB565 binary data (5KB)

### POST /next-track
Skip to the next track in the current playlist.

**Example:**
```bash
curl -X POST http://localhost:5000/next-track
```

### GET /stream
Stream the currently playing track from a radio station as MP3.

**Query Parameters:**
- `playlist_id` (required): The station's playlist ID

**Example:**
```bash
curl "http://localhost:5000/stream?playlist_id=PLAYLIST_ID_1" -o stream.mp3
```

**Response:**
- Content-Type: audio/mpeg
- Body: MP3 audio stream

**Station Behavior:**
- Automatically loops through the playlist continuously
- All clients listening to the same station hear the same track
- New clients joining mid-song will start from the current position in the FFmpeg stream
- No skipping allowed (like real radio)

### POST /next-track
(Removed - not supported in radio mode)

**Note:** Radio stations automatically advance to the next track when the current one finishes. No manual skipping is available.

### POST /stop-stream
(Removed - not applicable in radio mode)

**Note:** Clients simply disconnect to stop receiving the stream. The radio station continues playing for other listeners.

## ESP32-S3 Client Example

### GET Request Example
To connect from your ESP32-S3 and retrieve "Hello World":

```cpp
#include <HTTPClient.h>

void setup() {
  Serial.begin(115200);
  WiFi.connect("SSID", "PASSWORD");
}

void loop() {
  HTTPClient http;
  http.begin("http://<RPI_IP>:5000/hello");
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(response);
  }
  http.end();
  
  delay(1000);
}
```

### File Upload Example
To upload an image from the ESP32-S3's SD card to the server:

```cpp
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>

#define SD_CS 5  // Chip select pin for SD card

void setup() {
  Serial.begin(115200);
  WiFi.connect("SSID", "PASSWORD");
  
  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
    return;
  }
}

void uploadImageFromSD(const char *filename) {
  // Open file from SD card
  File file = SD.open(filename, FILE_READ);
  
  if (!file) {
    Serial.println("Failed to open file from SD card");
    return;
  }
  
  HTTPClient http;
  http.begin("http://<RPI_IP>:5000/upload");
  
  // Create multipart form data
  String boundary = "----FormBoundary";
  String body = boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"\r\n";
  body += "Content-Type: image/jpeg\r\n\r\n";
  
  // Send headers
  http.write((uint8_t*)body.c_str(), body.length());
  
  // Send file in chunks
  uint8_t buffer[512];
  while (file.available()) {
    int bytesRead = file.read(buffer, sizeof(buffer));
    http.write(buffer, bytesRead);
  }
  
  // Send footer
  String footer = "\r\n" + boundary + "--\r\n";
  http.write((uint8_t*)footer.c_str(), footer.length());
  
  int httpResponseCode = http.POST((uint8_t*)"", 0);
  
  if (httpResponseCode == 200) {
    Serial.println("Image uploaded successfully!");
  } else {
    Serial.println("Upload failed: " + String(httpResponseCode));
  }
  
  http.end();
  file.close();
}

void loop() {
  // Upload a specific image from SD card
  uploadImageFromSD("/DCIM/image001.jpg");
  delay(5000);  // Upload every 5 seconds
}
```

### Image Display Example
To retrieve and display the RGB565 image on a 128x64 display:

```cpp
#include <HTTPClient.h>
#include <TFT_eSPI.h>  // Display library for 128x64 RGB565

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define IMAGE_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)  // RGB565 = 2 bytes per pixel

TFT_eSPI tft = TFT_eSPI();
uint8_t imageBuffer[IMAGE_SIZE];

void setup() {
  Serial.begin(115200);
  WiFi.connect("SSID", "PASSWORD");
  
  // Initialize display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}

void fetchAndDisplayImage() {
  HTTPClient http;
  http.begin("http://<RPI_IP>:5000/image");
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    // Read the RGB565 binary data
    int bytesRead = 0;
    while (http.getStream().available() && bytesRead < IMAGE_SIZE) {
      int available = http.getStream().available();
      int toRead = min(512, IMAGE_SIZE - bytesRead);
      http.getStream().readBytes(&imageBuffer[bytesRead], toRead);
      bytesRead += toRead;
    }
    
    // Push RGB565 data directly to display
    tft.pushImage(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t*)imageBuffer);
    
    Serial.println("Image displayed successfully!");
  } else {
    Serial.println("Failed to fetch image: " + String(httpResponseCode));
  }
  
  http.end();
}

void loop() {
  // Fetch and display image every 2 minutes (same as server update interval)
  fetchAndDisplayImage();
  delay(120000);  // 2 minutes
}
```

### Music Streaming Example
To stream audio from the server's Spotify playlists and play it on the ESP32-S3:

```cpp
#include <HTTPClient.h>
#include <I2S.h>

// I2S pins for audio output (adjust based on your setup)
#define I2S_DOUT 8
#define I2S_BCLK 9
#define I2S_LRC 10

void setup() {
  Serial.begin(115200);
  WiFi.connect("SSID", "PASSWORD");
  
  // Initialize I2S for audio playback
  I2S.setPins(I2S_LRC, I2S_BCLK, I2S_DOUT);
  if (!I2S.begin(EXTERNAL_I2S, 16000, 16)) {
    Serial.println("Failed to initialize I2S!");
    while (1);
  }
}

void getPlaylists() {
  HTTPClient http;
  http.begin("http://<RPI_IP>:5000/playlists");
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("Available playlists:");
    Serial.println(payload);
  }
  http.end();
}

void startPlaylist(const char *playlistId) {
  HTTPClient http;
  String url = "http://<RPI_IP>:5000/play/playlist/" + String(playlistId);
  http.begin(url);
  
  int httpResponseCode = http.POST(nullptr, 0);
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("Radio station started:");
    Serial.println(payload);
  }
  http.end();
}

void streamMusic(const char *playlistId) {
  HTTPClient http;
  String url = "http://<RPI_IP>:5000/stream?playlist_id=";
  url += String(playlistId);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    WiFiClient *stream = http.getStreamPtr();
    
    // Buffer for audio data
    uint8_t buffer[512];
    int bytesRead = 0;
    
    while (http.connected() && stream->available()) {
      int available = stream->available();
      int toRead = min(512, available);
      bytesRead = stream->readBytes(buffer, toRead);
      
      if (bytesRead > 0) {
        // Write to I2S for audio playback
        I2S.write(buffer, bytesRead);
      }
    }
    
    Serial.println("Stream ended (all tracks in rotation completed)");
  } else {
    Serial.println("Failed to start stream: " + String(httpResponseCode));
  }
  http.end();
}

void getAndDisplayMetadata(const char *playlistId) {
  HTTPClient http;
  String url = "http://<RPI_IP>:5000/track-metadata?playlist_id=";
  url += String(playlistId);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("Track Metadata:");
    Serial.println(payload);
    
    // Parse JSON and display on screen (requires ArduinoJson library)
    // Example: extract track name, artist, album, duration
  }
  http.end();
}

void getAndDisplayAlbumArt(const char *playlistId, uint16_t *displayBuffer) {
  HTTPClient http;
  String url = "http://<RPI_IP>:5000/album-art?playlist_id=";
  url += String(playlistId);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    WiFiClient *stream = http.getStreamPtr();
    
    // RGB565 album art is 50x50 = 5000 bytes
    uint8_t buffer[5000];
    int bytesRead = 0;
    
    while (http.connected() && stream->available()) {
      int available = stream->available();
      int toRead = min(512, min(available, 5000 - bytesRead));
      bytesRead += stream->readBytes(&buffer[bytesRead], toRead);
    }
    
    if (bytesRead == 5000) {
      // Successfully received album art
      // Copy to display buffer or draw on display
      memcpy(displayBuffer, (uint16_t*)buffer, 5000);
      Serial.println("Album art received and ready to display");
    }
  }
  http.end();
}

void loop() {
  const char *playlistId = "PLAYLIST_ID_1";  // Replace with your actual playlist ID
  
  // Get available playlists (optional)
  getPlaylists();
  delay(1000);
  
  // Start the radio station (only needed once)
  startPlaylist(playlistId);
  delay(1000);
  
  // Periodically update and display metadata
  getAndDisplayMetadata(playlistId);
  delay(500);
  
  // Load album art into buffer
  uint16_t albumArtBuffer[50 * 50];
  getAndDisplayAlbumArt(playlistId, albumArtBuffer);
  delay(500);
  
  // Stream the music
  // In radio mode, this runs continuously, looping through all tracks
  // The function will only return when the client disconnects
  streamMusic(playlistId);
  
  // Station will automatically restart if disconnected and reconnected
  delay(5000);
}
```

**Radio Mode Behavior:**
- `/stream` continuously loops through the playlist
- All clients listening to a station hear the same track
- Metadata updates every iteration (optional—you can adjust timing)
- No manual track skipping—songs progress automatically
- Simply disconnect to stop listening (the station continues for others)
