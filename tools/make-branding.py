#!/usr/bin/env python3
"""Draws the port's own application icon and launcher header art with the standard library only.

Developer tool: the generated files are committed, so building the port does not run it. The art is original: a
segmented "13" and an abstract skyline. No game asset is read.

  python tools/make-branding.py
"""
import math
import pathlib
import random
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / 'packaging' / 'icons'
HEADER = ROOT / 'assets' / 'launcher' / 'header-art.png'
ACCENT = (0x4F, 0xC3, 0xE0)
DEEP = (0x0B, 0x16, 0x24)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
HEADER_SIZE = (3840, 432)
# Segments of a seven-segment "1" and "3" in a 12 x 20 cell: x0, y0, x1, y1.
DIGIT_ONE = ((8, 0, 12, 20),)
DIGIT_THREE = ((0, 0, 12, 4), (0, 8, 12, 12), (0, 16, 12, 20), (8, 0, 12, 20))


def png(width, height, rgba):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b''.join(b'\0' + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def inside_rounded(x, y, size, radius):
    dx = max(radius - x, x - (size - 1 - radius), 0)
    dy = max(radius - y, y - (size - 1 - radius), 0)
    return math.hypot(dx, dy) <= radius


def digit_covers(segments, cell_x, cell_y):
    return any(x0 <= cell_x < x1 and y0 <= cell_y < y1 for x0, y0, x1, y1 in segments)


def icon(size):
    scale = 4  # supersampled, so small sizes stay smooth
    full = size * scale
    radius = full * 0.18
    cell = full * 0.62 / 20
    left = (full - cell * 27) / 2
    top = (full - cell * 20) / 2
    pixels = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            covered = lit = 0
            for sy in range(scale):
                for sx in range(scale):
                    fx, fy = x * scale + sx, y * scale + sy
                    if not inside_rounded(fx, fy, full, radius):
                        continue
                    covered += 1
                    cx, cy = (fx - left) / cell, (fy - top) / cell
                    lit += digit_covers(DIGIT_ONE, cx, cy) or digit_covers(DIGIT_THREE, cx - 15, cy)
            if covered:
                mix = lit / covered
                at = (y * size + x) * 4
                for channel in range(3):
                    pixels[at + channel] = round(DEEP[channel] + (ACCENT[channel] - DEEP[channel]) * mix)
                pixels[at + 3] = round(255 * covered / scale ** 2)
    return png(size, size, pixels)


def write_ico(path, images):
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b'', b''
    for size, image in images:
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
        data += image
    path.write_bytes(header + entries + data)


def header_art():
    width, height = HEADER_SIZE
    random.seed(13)
    alpha = bytearray(width * height)

    def rect(x0, y0, x1, y1, value):
        for y in range(max(y0, 0), min(y1, height)):
            row = y * width
            for x in range(max(x0, 0), min(x1, width)):
                alpha[row + x] = max(alpha[row + x], value)

    # Buildings thin out to the left, where the title stands.
    x = width // 2
    while x < width:
        share = (x - width // 2) / (width // 2)
        w = random.randint(50, 150)
        h = int(height * (0.2 + 0.6 * random.random()) * (0.4 + 0.6 * share))
        rect(x, height - h, x + w, height - h + 4, 150)
        rect(x, height - h, x + 4, height, 90)
        rect(x + w - 4, height - h, x + w, height, 90)
        for wy in range(height - h + 24, height, 34):
            for wx in range(x + 16, x + w - 16, 26):
                if random.random() < 0.45:
                    rect(wx, wy, wx + 12, wy + 12, 120)
        x += w + random.randint(6, 40)
    pixels = bytearray(width * height * 4)
    for at, value in enumerate(alpha):
        if value:
            pixels[at * 4:at * 4 + 4] = bytes((*ACCENT, value))
    return png(width, height, pixels)


def main():
    ICONS.mkdir(parents=True, exist_ok=True)
    images = [(size, icon(size)) for size in ICO_SIZES]
    write_ico(ICONS / 'app-icon.ico', images)
    (ICONS / 'app-icon-256.png').write_bytes(images[-1][1])
    HEADER.write_bytes(header_art())
    print('wrote', ICONS / 'app-icon.ico', HEADER)


if __name__ == '__main__':
    main()
