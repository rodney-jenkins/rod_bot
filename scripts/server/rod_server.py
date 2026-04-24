from flask import Flask, request, jsonify, send_file
from werkzeug.utils import secure_filename
from PIL import Image
import os
import threading
import random
import io
import time
import subprocess
import json
import urllib.request
from pathlib import Path

import spotipy
from spotipy.oauth2 import SpotifyOAuth


app = Flask(__name__)

# Configuration
UPLOAD_FOLDER = 'uploads'
ALLOWED_EXTENSIONS = {'jpg', 'jpeg', 'png', 'gif', 'bmp'}
MAX_FILE_SIZE = 10 * 1024 * 1024  # 10MB
IMAGE_WIDTH = 128
IMAGE_HEIGHT = 64
IMAGE_UPDATE_INTERVAL = 60 # 120  # 2 minutes in seconds

# Music Configuration
MUSIC_CONFIG_FILE = 'music_config.json'
CACHE_FOLDER = 'music_cache'

# Create necessary folders
for folder in [UPLOAD_FOLDER, CACHE_FOLDER]:
    if not os.path.exists(folder):
        os.makedirs(folder)

app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = MAX_FILE_SIZE

# Global state for the current image
current_image_data = None
current_image_lock = threading.Lock()
image_update_thread = None

# Global state for music
current_playlist = None
current_playlist_index = 0
current_track_info = None
music_state_lock = threading.Lock()
spotify_client = None

# Radio station management
class RadioStation:
    """Represents a radio station streaming a single playlist"""
    def __init__(self, playlist_id, tracks):
        self.playlist_id = playlist_id
        self.tracks = tracks
        self.current_index = 0
        self.current_track = tracks[0] if tracks else None
        self.current_ffmpeg_process = None
        self.is_streaming = False
        self.lock = threading.Lock()
        self.thread = None
        self.stop_event = threading.Event()
    
    def stop(self):
        """Stop the station"""
        self.stop_event.set()
        self.kill_ffmpeg()
    
    def kill_ffmpeg(self):
        """Terminate current FFmpeg process"""
        with self.lock:
            if self.current_ffmpeg_process:
                try:
                    if self.current_ffmpeg_process.poll() is None:
                        self.current_ffmpeg_process.terminate()
                        try:
                            self.current_ffmpeg_process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            self.current_ffmpeg_process.kill()
                            self.current_ffmpeg_process.wait()
                except Exception as e:
                    print(f"Error terminating FFmpeg: {str(e)}")
                finally:
                    self.current_ffmpeg_process = None
    
    def station_loop(self):
        """Background thread that continuously streams playlist"""
        while not self.stop_event.is_set():
            try:
                with self.lock:
                    if not self.tracks:
                        break
                    
                    self.current_track = self.tracks[self.current_index]
                    audio_url = self.current_track.get('preview_url')
                
                if not audio_url:
                    # Skip tracks without preview
                    with self.lock:
                        self.current_index = (self.current_index + 1) % len(self.tracks)
                    continue
                
                # Stream this track
                cmd = [
                    'ffmpeg',
                    '-i', audio_url,
                    '-ab', '128k',
                    '-f', 'mp3',
                    '-'
                ]
                
                process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                
                with self.lock:
                    self.current_ffmpeg_process = process
                
                # Wait for process to complete (song finishes)
                process.wait()
                
                # Move to next track
                with self.lock:
                    self.current_index = (self.current_index + 1) % len(self.tracks)
                    self.current_ffmpeg_process = None
                
            except Exception as e:
                print(f"Error in station loop for {self.playlist_id}: {str(e)}")
                # Move to next track on error
                with self.lock:
                    self.current_index = (self.current_index + 1) % len(self.tracks)
    
    def start(self):
        """Start the radio station"""
        self.stop_event.clear()
        self.thread = threading.Thread(target=self.station_loop, daemon=True)
        self.thread.start()
        print(f"Radio station {self.playlist_id} started")
    
    def get_stream_process(self):
        """Get current FFmpeg process for streaming"""
        with self.lock:
            return self.current_ffmpeg_process
    
    def get_current_track_info(self):
        """Get current track info"""
        with self.lock:
            return {
                'playlist_id': self.playlist_id,
                'current_index': self.current_index,
                'track': self.current_track
            }

active_stations = {}  # {playlist_id: RadioStation}
active_stations_lock = threading.Lock()

def allowed_file(filename):
    """Check if file extension is allowed"""
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

def convert_to_rgb565(image_path):
    """
    Convert an image to RGB565 format (128x64).
    Returns binary RGB565 data (2 bytes per pixel).
    """
    try:
        # Open and resize image
        img = Image.open(image_path)
        img = img.resize((IMAGE_WIDTH, IMAGE_HEIGHT), Image.Resampling.LANCZOS)
        img = img.convert('RGB')
        
        # Convert to RGB565 format
        rgb565_data = io.BytesIO()
        pixels = img.load()
        
        for y in range(IMAGE_HEIGHT):
            for x in range(IMAGE_WIDTH):
                r, g, b = pixels[x, y]
                
                # Convert 8-bit RGB to 5-6-5 format
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                
                # Combine into 16-bit value (RGB565)
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Write as little-endian 16-bit value
                rgb565_data.write(rgb565.to_bytes(2, byteorder='little'))
        
        rgb565_data.seek(0)
        return rgb565_data.getvalue()
    except Exception as e:
        print(f"Error converting image: {str(e)}")
        return None

def get_random_image():
    """Get a random image from the uploads folder and convert to RGB565"""
    try:
        files = os.listdir(UPLOAD_FOLDER)
        if not files:
            return None
        
        # Filter for image files
        image_files = [f for f in files if allowed_file(f)]
        if not image_files:
            return None
        
        # Select random image
        random_file = random.choice(image_files)
        image_path = os.path.join(UPLOAD_FOLDER, random_file)
        
        # Convert to RGB565
        rgb565_data = convert_to_rgb565(image_path)
        return rgb565_data
    except Exception as e:
        print(f"Error getting random image: {str(e)}")
        return None

def image_update_loop():
    """Background thread that updates the current image every 2 minutes"""
    global current_image_data
    
    while True:
        try:
            image_data = get_random_image()
            if image_data:
                with current_image_lock:
                    current_image_data = image_data
        except Exception as e:
            print(f"Error in image update loop: {str(e)}")
        
        time.sleep(IMAGE_UPDATE_INTERVAL)

def start_image_update_thread():
    """Start the background thread for image updates"""
    global image_update_thread
    
    image_update_thread = threading.Thread(target=image_update_loop, daemon=True)
    image_update_thread.start()
    print("Image update thread started")

# Music and Spotify Functions

def init_spotify():
    global spotify_client
    try:
        if os.path.exists(MUSIC_CONFIG_FILE):
            with open(MUSIC_CONFIG_FILE, 'r') as f:
                config = json.load(f)
                client_id = config.get('spotify_client_id')
                client_secret = config.get('spotify_client_secret')
                redirect_uri = config.get('spotify_redirect_uri')

                if client_id and client_secret and redirect_uri:
                    auth_manager = SpotifyOAuth(
                        client_id=client_id,
                        client_secret=client_secret,
                        redirect_uri=redirect_uri,
                        scope="playlist-read-private playlist-read-collaborative"
                    )

                    spotify_client = spotipy.Spotify(auth_manager=auth_manager)
                    print("Spotify OAuth client initialized successfully")
                    return True
    except Exception as e:
        print(f"Error initializing Spotify client: {str(e)}")

    return False

def load_playlists():
    """Load playlist configuration from music_config.json"""
    try:
        if os.path.exists(MUSIC_CONFIG_FILE):
            with open(MUSIC_CONFIG_FILE, 'r') as f:
                config = json.load(f)
                return config.get('playlists', [])
    except Exception as e:
        print(f"Error loading playlists: {str(e)}")
    
    return []

def get_playlist_tracks(playlist_id):
    """Get all tracks from a Spotify playlist"""
    try:
        if not spotify_client:
            return []
        
        tracks = []
        results = spotify_client.playlist_tracks(playlist_id)
        
        while True:
            for item in results['items']:
                track = item['track']
                if track and track.get('preview_url'):  # Only include tracks with preview
                    # Get album info and image
                    album = track.get('album', {})
                    images = album.get('images', [])
                    image_url = images[0]['url'] if images else None
                    
                    tracks.append({
                        'name': track['name'],
                        'artist': ', '.join([artist['name'] for artist in track['artists']]),
                        'album': album.get('name', 'Unknown'),
                        'preview_url': track['preview_url'],
                        'duration_ms': track['duration_ms'],
                        'image_url': image_url,
                        'spotify_id': track['id'],
                        'release_date': album.get('release_date', '')
                    })
            
            # Check if there are more pages
            if results['next']:
                results = spotify_client.next(results)
            else:
                break
        
        return tracks
    except Exception as e:
        print(f"Error getting playlist tracks: {str(e)}")
        return []

def stream_audio_with_ffmpeg(audio_url, bitrate='128k'):
    """Stream audio using FFmpeg with specified bitrate"""
    try:
        # Use FFmpeg to re-encode audio on the fly
        cmd = [
            'ffmpeg',
            '-i', audio_url,
            '-ab', bitrate,
            '-f', 'mp3',
            '-'
        ]
        
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        return process
    except Exception as e:
        print(f"Error starting FFmpeg: {str(e)}")
        return None

def kill_ffmpeg_process():
    """Safely kill the current FFmpeg process if running"""
    # This function is deprecated - use RadioStation.kill_ffmpeg() instead
    pass

def download_and_convert_album_art(image_url, width=50, height=50):
    """
    Download album art from URL and convert to RGB565 format.
    Returns binary RGB565 data (2 bytes per pixel).
    """
    try:
        if not image_url:
            return None
        
        # Download image
        image_data = io.BytesIO()
        with urllib.request.urlopen(image_url) as response:
            image_data.write(response.read())
        image_data.seek(0)
        
        # Open and resize image
        img = Image.open(image_data)
        img = img.resize((width, height), Image.Resampling.LANCZOS)
        img = img.convert('RGB')
        
        # Convert to RGB565 format
        rgb565_data = io.BytesIO()
        pixels = img.load()
        
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                
                # Convert 8-bit RGB to 5-6-5 format
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                
                # Combine into 16-bit value (RGB565)
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Write as little-endian 16-bit value
                rgb565_data.write(rgb565.to_bytes(2, byteorder='little'))
        
        rgb565_data.seek(0)
        return rgb565_data.getvalue()
    except Exception as e:
        print(f"Error converting album art: {str(e)}")
        return None

@app.route('/hello', methods=['GET'])
def hello():
    """GET endpoint that returns 'Hello World'"""
    return "Hello World"

@app.route('/upload', methods=['POST'])
def upload_image():
    """
    POST endpoint to upload an image file.
    Expects multipart/form-data with file field.
    """
    if 'file' not in request.files:
        return jsonify({'error': 'No file part in request'}), 400
    
    file = request.files['file']
    
    if file.filename == '':
        return jsonify({'error': 'No selected file'}), 400
    
    if not allowed_file(file.filename):
        return jsonify({'error': f'File type not allowed. Allowed types: {", ".join(ALLOWED_EXTENSIONS)}'}), 400
    
    try:
        # Secure the filename
        filename = secure_filename(file.filename)
        # Add timestamp to avoid collisions
        timestamp = int(time.time() * 1000)
        filename = f"{timestamp}_{filename}"
        
        # Save the file
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        file.save(filepath)
        
        return jsonify({
            'message': 'File uploaded successfully',
            'filename': filename,
            'path': filepath
        }), 200
    
    except Exception as e:
        return jsonify({'error': f'File upload failed: {str(e)}'}), 500

@app.route('/image', methods=['GET'])
def get_image():
    """
    GET endpoint to retrieve the current image in RGB565 format (128x64).
    Image updates every 2 minutes and is shared across all clients.
    """
    global current_image_data
    
    with current_image_lock:
        if current_image_data is None:
            # No image available yet
            return jsonify({'error': 'No images available. Please upload images first.'}), 404
        
        image_bytes = current_image_data
    
    # Return as binary data with appropriate headers
    return send_file(
        io.BytesIO(image_bytes),
        mimetype='application/octet-stream',
        as_attachment=True,
        download_name='image.rgb565'
    )

# Music Streaming Endpoints

@app.route('/playlists', methods=['GET'])
def get_playlists():
    """Get list of available playlists"""
    try:
        playlists = load_playlists()
        return jsonify({
            'playlists': playlists,
            'total': len(playlists)
        }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to get playlists: {str(e)}'}), 500

@app.route('/playlist/<playlist_id>/tracks', methods=['GET'])
def get_playlist_info(playlist_id):
    """Get track list and info for a specific playlist"""
    try:
        tracks = get_playlist_tracks(playlist_id)
        if not tracks:
            return jsonify({'error': 'Playlist not found or has no tracks with previews'}), 404
        
        return jsonify({
            'playlist_id': playlist_id,
            'track_count': len(tracks),
            'tracks': tracks
        }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to get playlist info: {str(e)}'}), 500

@app.route('/play/playlist/<playlist_id>', methods=['POST'])
def play_playlist(playlist_id):
    """Start a radio station for a specific playlist"""
    try:
        with active_stations_lock:
            # Check if station already exists
            if playlist_id in active_stations:
                station = active_stations[playlist_id]
                return jsonify({
                    'message': 'Station already playing',
                    'playlist_id': playlist_id,
                    'current_track': station.get_current_track_info()['track']
                }), 200
        
        # Get playlist tracks
        tracks = get_playlist_tracks(playlist_id)
        if not tracks:
            return jsonify({'error': 'Playlist not found or has no tracks'}), 404
        
        # Create and start new station
        station = RadioStation(playlist_id, tracks)
        station.start()
        
        with active_stations_lock:
            active_stations[playlist_id] = station
        
        return jsonify({
            'message': 'Radio station started',
            'playlist_id': playlist_id,
            'track_count': len(tracks),
            'current_track': station.get_current_track_info()['track']
        }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to start station: {str(e)}'}), 500

@app.route('/current-track', methods=['GET'])
def get_current_track():
    """Get info about the currently playing track for a specific station"""
    try:
        playlist_id = request.args.get('playlist_id')
        
        if not playlist_id:
            return jsonify({'error': 'playlist_id query parameter required'}), 400
        
        with active_stations_lock:
            if playlist_id not in active_stations:
                return jsonify({'error': f'Station {playlist_id} is not currently playing'}), 404
            
            station = active_stations[playlist_id]
            return jsonify(station.get_current_track_info()), 200
    except Exception as e:
        return jsonify({'error': f'Failed to get current track: {str(e)}'}), 500

@app.route('/track-metadata', methods=['GET'])
def get_track_metadata():
    """
    Get comprehensive metadata for the currently playing track on a station.
    Includes: album, artist, duration, release date, etc.
    """
    try:
        playlist_id = request.args.get('playlist_id')
        
        if not playlist_id:
            return jsonify({'error': 'playlist_id query parameter required'}), 400
        
        with active_stations_lock:
            if playlist_id not in active_stations:
                return jsonify({'error': f'Station {playlist_id} is not currently playing'}), 404
            
            station = active_stations[playlist_id]
            track_info = station.get_current_track_info()
            track = track_info['track']
            
            return jsonify({
                'track_name': track.get('name'),
                'artist': track.get('artist'),
                'album': track.get('album'),
                'duration_ms': track.get('duration_ms'),
                'release_date': track.get('release_date'),
                'spotify_id': track.get('spotify_id'),
                'image_url': track.get('image_url'),
                'playlist_position': track_info.get('current_index', 0)
            }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to get metadata: {str(e)}'}), 500

@app.route('/album-art', methods=['GET'])
def get_album_art():
    """
    Get album art of the currently playing track on a station as RGB565 format (50x50).
    """
    try:
        playlist_id = request.args.get('playlist_id')
        
        if not playlist_id:
            return jsonify({'error': 'playlist_id query parameter required'}), 400
        
        with active_stations_lock:
            if playlist_id not in active_stations:
                return jsonify({'error': f'Station {playlist_id} is not currently playing'}), 404
            
            station = active_stations[playlist_id]
            track_info = station.get_current_track_info()
            image_url = track_info['track'].get('image_url')
        
        if not image_url:
            return jsonify({'error': 'No album art available'}), 404
        
        # Convert album art to RGB565
        rgb565_data = download_and_convert_album_art(image_url, 50, 50)
        
        if not rgb565_data:
            return jsonify({'error': 'Failed to convert album art'}), 500
        
        return send_file(
            io.BytesIO(rgb565_data),
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name='album_art.rgb565'
        )
    except Exception as e:
        return jsonify({'error': f'Failed to get album art: {str(e)}'}), 500

@app.route('/next-track', methods=['POST'])
def next_track():
    """Skip to next track in the current playlist"""
    try:
        global current_playlist, current_playlist_index, current_track_info
        
        # Stop current stream
        kill_ffmpeg_process()
        
        with music_state_lock:
            if not current_playlist or len(current_playlist) == 0:
                return jsonify({'error': 'No playlist is currently playing'}), 400
            
            current_playlist_index = (current_playlist_index + 1) % len(current_playlist)
            current_track_info['current_index'] = current_playlist_index
            current_track_info['track'] = current_playlist[current_playlist_index]
        
        return jsonify({
            'message': 'Skipped to next track',
            'current_track': current_track_info['track']
        }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to skip track: {str(e)}'}), 500

@app.route('/stream', methods=['GET'])
def stream_music():
    """Stream the current track from a radio station as MP3"""
    try:
        playlist_id = request.args.get('playlist_id')
        
        if not playlist_id:
            return jsonify({'error': 'playlist_id query parameter required'}), 400
        
        with active_stations_lock:
            if playlist_id not in active_stations:
                return jsonify({'error': f'Station {playlist_id} is not currently playing. Start it with /play/playlist/{playlist_id}'}), 404
            
            station = active_stations[playlist_id]
        
        # Get the current FFmpeg process from the station
        process = station.get_stream_process()
        
        if not process:
            return jsonify({'error': 'Station stream is not ready'}), 500
        
        return send_file(
            process.stdout,
            mimetype='audio/mpeg',
            as_attachment=True,
            download_name='stream.mp3'
        )
    except Exception as e:
        return jsonify({'error': f'Failed to stream audio: {str(e)}'}), 500

if __name__ == '__main__':
    # Initialize Spotify client
    init_spotify()
    
    # Start the background image update thread
    start_image_update_thread()
    
    try:
        # Run on 0.0.0.0 to allow external connections from ESP32-S3
        app.run(host='0.0.0.0', port=5000, debug=True)
    finally:
        # Clean up all active radio stations on shutdown
        with active_stations_lock:
            for station in active_stations.values():
                station.stop()
            active_stations.clear()
