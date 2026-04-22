from flask import Flask, request, jsonify, send_file
from werkzeug.utils import secure_filename
from PIL import Image
import os
import threading
import random
import io
import time
import json
import urllib.request
from pathlib import Path


app = Flask(__name__)

# Configuration
UPLOAD_FOLDER = 'uploads'
ALLOWED_EXTENSIONS = {'jpg', 'jpeg', 'png', 'gif', 'bmp'}
MAX_FILE_SIZE = 10 * 1024 * 1024  # 10MB
IMAGE_WIDTH = 128
IMAGE_HEIGHT = 64
IMAGE_UPDATE_INTERVAL = 60 # 120  # 2 minutes in seconds

# Create necessary folders
for folder in [UPLOAD_FOLDER ]:
    if not os.path.exists(folder):
        os.makedirs(folder)

app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = MAX_FILE_SIZE

# Global state for the current image
current_image_data = None
current_image_lock = threading.Lock()
image_update_thread = None


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

# API

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


# MAIN

if __name__ == '__main__':
    
    # Start the background image update thread
    start_image_update_thread()
    
    try:
        # Run on 0.0.0.0 to allow external connections from ESP32-S3
        app.run(host='0.0.0.0', port=5000, debug=True)
    finally:
        # Clean up all active radio stations on shutdown
        print( "DONE" )
