#!/usr/bin/env python3
"""Convert .float (PF4 format) HDR image to PNG with exposure tonemapping."""
import struct, sys, math
from PIL import Image

def load_float(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        dims = f.readline().strip()
        endian = f.readline().strip()
        w, h = map(int, dims.split())
        data = f.read()
    pixels = struct.unpack(f'<{w*h*3}f', data)
    return w, h, pixels

def tonemap(x, exposure=1.5):
    if x < 0: return 0.0
    mapped = 1.0 - math.exp(-x * exposure)
    return mapped ** (1.0 / 2.2)

def convert(inpath, outpath):
    w, h, pixels = load_float(inpath)
    img = Image.new('RGB', (w, h))
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            r = min(255, max(0, int(tonemap(pixels[i+0]) * 255)))
            g = min(255, max(0, int(tonemap(pixels[i+1]) * 255)))
            b = min(255, max(0, int(tonemap(pixels[i+2]) * 255)))
            img.putpixel((x, h - 1 - y), (r, g, b))
    img.save(outpath)
    print(f"Saved {outpath} ({w}x{h})")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.float output.png")
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])
