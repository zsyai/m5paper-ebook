import struct
import sys
import os
from PIL import Image, ImageDraw, ImageFont
import fontTools.ttLib

def ttf_to_bin(ttf_path, bin_path, font_size=24, filter_chars=None):
    print(f"Loading font {ttf_path}...")
    font = fontTools.ttLib.TTFont(ttf_path)
    cmap = font.getBestCmap()
    assert cmap is not None, "No cmap found in font"
    unicodes = sorted(cmap.keys())
    
    # Filter for BMP characters (0x0000 - 0xFFFF)
    unicodes = [u for u in unicodes if u <= 0xFFFF]
    
    if filter_chars:
        unicodes = [u for u in unicodes if chr(u) in filter_chars]
        print(f"Filtered to {len(unicodes)} characters based on filter list.")
        
    print(f"Found {len(unicodes)} characters to render.")
    
    pil_font = ImageFont.truetype(ttf_path, font_size)
    
    char_infos = []
    bitmap_data = bytearray()
    
    current_offset = 0
    
    print("Rendering glyphs...")
    for u in unicodes:
        char = chr(u)
        width = int(pil_font.getlength(char))
        try:
            bbox = pil_font.getbbox(char)
        except Exception as e:
            print(f"Error getting bbox for U+{u:04X}: {e}")
            continue
            
        if not bbox:
            if width > 0:
                info = {
                    'unicode': u,
                    'width': width,
                    'bitmapW': 0,
                    'bitmapH': 0,
                    'x_offset': 0,
                    'y_offset': 0,
                    'bitmap_offset': current_offset,
                    'bitmap_size': 0
                }
                char_infos.append(info)
            continue
        
        x0, y0, x1, y1 = bbox
        w = x1 - x0
        h = y1 - y0
        advance = int(pil_font.getlength(char))
        
        if w != advance and u > 32: # Skip control chars and space
            print(f"DIFF_METRICS: Char={char} (U+{u:04X}), bbox={bbox}, w={w}, advance={advance}")
        
        if w == 0 or h == 0:
            continue
            
        img = Image.new('L', (w, h), 255)
        draw = ImageDraw.Draw(img)
        draw.text((-x0, -y0), char, font=pil_font, fill=0)
        
        pixels = list(img.getdata())
        packed_bytes = []
        for i in range(0, len(pixels), 4):
            p1 = pixels[i] >> 6
            p2 = (pixels[i+1] >> 6) if i+1 < len(pixels) else 3
            p3 = (pixels[i+2] >> 6) if i+2 < len(pixels) else 3
            p4 = (pixels[i+3] >> 6) if i+3 < len(pixels) else 3
            packed_bytes.append((p1 << 6) | (p2 << 4) | (p3 << 2) | p4)
        bitmap = bytes(packed_bytes)
        bitmap_size = len(bitmap)
        
        # width (advance)
        width = int(pil_font.getlength(char))
        
        info = {
            'unicode': u,
            'width': width,
            'bitmapW': w,
            'bitmapH': h,
            'x_offset': x0,
            'y_offset': y0,
            'bitmap_offset': current_offset,
            'bitmap_size': bitmap_size
        }
        char_infos.append(info)
        bitmap_data.extend(bitmap)
        current_offset += bitmap_size
        
    print(f"Writing to {bin_path}...")
    os.makedirs(os.path.dirname(bin_path), exist_ok=True)
    with open(bin_path, 'wb') as f:
        char_count = len(char_infos)
        f.write(struct.pack('<I', char_count))
        f.write(struct.pack('<B', font_size))
        f.write(struct.pack('<B', 3)) # version
        try:
            font_name_str = font['name'].getDebugName(1)
        except Exception:
            font_name_str = None
        if not font_name_str:
            font_name_str = os.path.splitext(os.path.basename(ttf_path))[0]
        f.write(font_name_str.encode('utf-8')[:64].ljust(64, b'\0'))
        f.write(b'Regular'.ljust(64, b'\0'))
        
        for info in char_infos:
            f.write(struct.pack('<H', info['unicode']))
            f.write(struct.pack('<H', info['width']))
            f.write(struct.pack('<B', info['bitmapW']))
            f.write(struct.pack('<B', info['bitmapH']))
            f.write(struct.pack('<b', info['x_offset']))
            f.write(struct.pack('<b', info['y_offset']))
            f.write(struct.pack('<I', info['bitmap_offset']))
            f.write(struct.pack('<I', info['bitmap_size']))
            
        f.write(bitmap_data)
    print("Done.")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python ttf2bin.py <ttf_path> <bin_path> [size] [text_file]")
        sys.exit(1)
    ttf = sys.argv[1]
    bin_f = sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 24
    text_file = sys.argv[4] if len(sys.argv) > 4 else None
    
    filter_chars = None
    if text_file:
        print(f"Loading filter characters from {text_file}...")
        with open(text_file, 'r', encoding='utf-8') as f:
            filter_chars = set(f.read())
            
    ttf_to_bin(ttf, bin_f, size, filter_chars)
