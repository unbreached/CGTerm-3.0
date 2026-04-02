#!/usr/bin/env python3
"""
Convert a TTF/OTF font to a CGTerm BMP font spritesheet.

Usage:
  python3 tools/make_font.py <input.ttf|otf> <output.bmp> [--size N] [--cell WxH]

Output: 8-bit indexed BMP, 16x16 grid (256 chars), color 0=black, color 1=white.

Example:
  python3 tools/make_font.py assets/Maesto-Regular_demo.otf assets/maesto_large.bmp --size 30 --cell 24x32
"""

import sys
import argparse
from PIL import Image, ImageFont, ImageDraw

def main():
    parser = argparse.ArgumentParser(description='Convert TTF/OTF to CGTerm font BMP')
    parser.add_argument('input', help='Input TTF/OTF font file')
    parser.add_argument('output', help='Output BMP file')
    parser.add_argument('--size', type=int, default=16, help='Font render size (default: 16)')
    parser.add_argument('--cell', default=None, help='Cell size WxH (default: auto from font metrics)')
    parser.add_argument('--threshold', type=int, default=80, help='Binarization threshold 0-255 (default: 80)')
    args = parser.parse_args()

    font = ImageFont.truetype(args.input, args.size)

    # Determine cell size
    if args.cell:
        cell_w, cell_h = [int(x) for x in args.cell.split('x')]
    else:
        # Auto-detect from max glyph size
        test = Image.new('L', (200, 200), 0)
        draw = ImageDraw.Draw(test)
        max_w, max_h = 0, 0
        for code in range(32, 127):
            bbox = draw.textbbox((0, 0), chr(code), font=font)
            w = bbox[2] - bbox[0]
            h = bbox[3] - bbox[1]
            if w > max_w: max_w = w
            if h > max_h: max_h = h
        cell_w = max_w + 2
        cell_h = max_h + 2
        print(f"Auto cell size: {cell_w}x{cell_h}")

    GRID_W = 16
    GRID_H = 16

    # Create indexed image
    img = Image.new('P', (GRID_W * cell_w, GRID_H * cell_h), 0)
    pal = [0] * 768
    pal[3] = 255; pal[4] = 255; pal[5] = 255  # index 1 = white
    img.putpalette(pal)

    # Render to grayscale
    gray = Image.new('L', (GRID_W * cell_w, GRID_H * cell_h), 0)
    draw = ImageDraw.Draw(gray)

    rendered = 0
    for code in range(256):
        if code < 32 or code > 126:
            continue
        ch = chr(code)
        col = code % GRID_W
        row = code // GRID_W
        x = col * cell_w
        y = row * cell_h

        bbox = draw.textbbox((0, 0), ch, font=font)
        cw = bbox[2] - bbox[0]
        ch_h = bbox[3] - bbox[1]
        if cw == 0 or ch_h == 0:
            continue
        ox = (cell_w - cw) // 2 - bbox[0]
        oy = (cell_h - ch_h) // 2 - bbox[1]
        draw.text((x + ox, y + oy), ch, fill=255, font=font)
        rendered += 1

    # Threshold to 1-bit
    for py in range(gray.height):
        for px in range(gray.width):
            v = gray.getpixel((px, py))
            img.putpixel((px, py), 1 if v > args.threshold else 0)

    img.save(args.output)
    print(f"Saved {args.output}: {GRID_W * cell_w}x{GRID_H * cell_h}, cell={cell_w}x{cell_h}, {rendered} glyphs")
    print(f"Use in code: font_load_font(fname, {cell_w}, {cell_h}, {GRID_W}, {GRID_H})")

if __name__ == '__main__':
    main()
