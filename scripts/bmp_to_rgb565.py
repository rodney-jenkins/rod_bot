from PIL import Image
import sys
import os

def rgb888_to_rgb565(r, g, b):
    """Convert 24-bit RGB to 16-bit RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_bmp_to_rgb565(input_file, output_file, array_name="image"):
    img = Image.open(input_file).convert("RGB")
    width, height = img.size
    pixels = list(img.getdata())

    with open(output_file, "w") as f:
        f.write(f"// Converted from {os.path.basename(input_file)}\n")
        f.write(f"// Size: {width}x{height}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint16_t {array_name}[{width * height}] PROGMEM = {{\n")

        for i, (r, g, b) in enumerate(pixels):
            color = rgb888_to_rgb565(r, g, b)
            f.write(f"0x{color:04X}")
            if i < len(pixels) - 1:
                f.write(", ")
            # line break every 12 pixels for readability
            if (i + 1) % 12 == 0:
                f.write("\n")

        f.write("\n};\n\n")
        f.write(f"#define {array_name.upper()}_WIDTH {width}\n")
        f.write(f"#define {array_name.upper()}_HEIGHT {height}\n")

    print(f"Saved to {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python bmp_to_rgb565.py input.bmp output.h [array_name]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    array_name = sys.argv[3] if len(sys.argv) > 3 else "image"

    convert_bmp_to_rgb565(input_file, output_file, array_name)

'''
for (int y = 0; y < MYICON_HEIGHT; y++) {
    for (int x = 0; x < MYICON_WIDTH; x++) {
        uint16_t color = myIcon[y * MYICON_WIDTH + x];
        if (color != MYICON_TRANSPARENT_COLOR) {
            matrix.drawPixel(x + X_OFFSET, y + Y_OFFSET, color);
        }
    }
}



uint16_t TRANSPARENT_COLOR = matrix.color565(255, 0, 255); // magenta

for (int y = 0; y < IMAGE_HEIGHT; y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
        uint16_t color = myImage[y * IMAGE_WIDTH + x];
        if (color != TRANSPARENT_COLOR) {
            matrix.drawPixel(x + X_OFFSET, y + Y_OFFSET, color);
        }
    }
}
'''