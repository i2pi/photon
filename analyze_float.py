#!/usr/bin/env python3
"""Analyze .float HDR image values."""
import struct, sys

def load_float(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        dims = f.readline().strip()
        endian = f.readline().strip()
        w, h = map(int, dims.split())
        data = f.read()
    pixels = struct.unpack(f'<{w*h*3}f', data)
    return w, h, pixels

w, h, pixels = load_float(sys.argv[1])
print(f"Image: {w}x{h}")

# Find max, avg, and distribution
vals = []
nonzero = 0
for i in range(0, len(pixels), 3):
    lum = 0.2126*pixels[i] + 0.7152*pixels[i+1] + 0.0722*pixels[i+2]
    vals.append(lum)
    if lum > 0.001:
        nonzero += 1

vals.sort()
print(f"Non-zero pixels: {nonzero}/{w*h} ({100*nonzero/(w*h):.1f}%)")
print(f"Max luminance: {vals[-1]:.4f}")
print(f"P99: {vals[int(len(vals)*0.99)]:.4f}")
print(f"P95: {vals[int(len(vals)*0.95)]:.4f}")
print(f"P50 (median): {vals[len(vals)//2]:.4f}")

# Sample the left side of image (where projection wall is)
# Left 25% of pixels
left_vals = []
for y in range(h):
    for x in range(w//4):
        i = (y * w + x) * 3
        lum = 0.2126*pixels[i] + 0.7152*pixels[i+1] + 0.0722*pixels[i+2]
        left_vals.append(lum)

left_vals.sort()
left_nz = sum(1 for v in left_vals if v > 0.001)
print(f"\nLeft 25% of image (projection wall area):")
print(f"  Non-zero: {left_nz}/{len(left_vals)} ({100*left_nz/len(left_vals):.1f}%)")
if left_nz > 0:
    print(f"  Max: {left_vals[-1]:.4f}")
    print(f"  P99: {left_vals[int(len(left_vals)*0.99)]:.6f}")
    print(f"  P95: {left_vals[int(len(left_vals)*0.95)]:.6f}")

# Check for any color (R != G != B) in brighter pixels
colored = 0
for i in range(0, len(pixels), 3):
    r, g, b = pixels[i], pixels[i+1], pixels[i+2]
    mx = max(r, g, b)
    if mx > 0.001:
        mn = min(r, g, b)
        sat = (mx - mn) / mx
        if sat > 0.1:
            colored += 1
print(f"\nPixels with color saturation > 10%: {colored}")
