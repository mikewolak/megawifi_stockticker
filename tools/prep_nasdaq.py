#!/usr/bin/env python3
"""
prep_nasdaq.py — Quantize source PNG to 16-color Genesis-palette indexed PNG.

Pipeline:
  1. ImageMagick: quantize to 16 colors, output indexed PNG8
  2. Python: snap each palette entry to Genesis 3-bit-per-channel space
     (valid channel values: 0, 36, 73, 109, 146, 182, 219, 255)

Usage: python3 tools/prep_nasdaq.py <src.png> <dst.png>
"""
import sys
import subprocess
import tempfile
import os
from PIL import Image

GENESIS_STEPS = [0, 36, 73, 109, 146, 182, 219, 255]


def snap(v):
    return min(GENESIS_STEPS, key=lambda s: abs(s - v))


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} src.png dst.png")
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]

    # Step 1: ImageMagick quantize to 16-color indexed PNG
    with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as tf:
        tmp = tf.name
    try:
        subprocess.run(
            ['magick', src, '-resize', '128x64!', '-flatten', '+dither', '-colors', '14', f'PNG8:{tmp}'],
            check=True
        )

        # Step 2: Snap palette to Genesis 3-bit-per-channel values
        img = Image.open(tmp).convert('P')
    finally:
        os.unlink(tmp)

    import numpy as np
    pixels = np.array(img, dtype=np.uint8)

    pal = img.getpalette()  # 256 * 3 = 768 bytes
    n_entries = max(pixels.flatten()) + 1

    # Snap each used palette entry
    for i in range(n_entries):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        pal[i * 3]     = snap(r)
        pal[i * 3 + 1] = snap(g)
        pal[i * 3 + 2] = snap(b)

    # Shift indices 0..14 → 1..15 so index 0 (transparent on Genesis) is unused
    pixels = (pixels + 1).clip(1, 15).astype(np.uint8)

    out = Image.fromarray(pixels, mode='P')
    out.putpalette(pal)
    out.save(dst)

    print(f"Saved {dst}: {img.size[0]}x{img.size[1]}, {len(set(pixels.flatten().tolist()))} colors")


if __name__ == '__main__':
    main()
