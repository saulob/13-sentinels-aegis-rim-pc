#!/usr/bin/env python3
"""Cuts the reference icons draw_manual.py matches against out of the English manual pages 06 and 07 (page PNGs dumped
from Manual05.ftx and Manual06.ftx): the four face buttons, the pills L1, L2, R1, R2 and the oval R3.

    make_manual_templates.py page06.png page07.png templates/
"""
import sys
from pathlib import Path

from PIL import Image

# (png, kind_name, x, y, w, h): positions on the 1920x1080 English pages.
CROPS = [
    (7, 'circle_cross', 1007, 449, 21, 21), (7, 'circle_circle', 1007, 506, 21, 21), (7, 'circle_tri', 1007, 562, 21, 21),
    (7, 'circle_square', 1007, 619, 21, 21),
    (7, 'pill_L1', 1007, 329, 48, 21), (7, 'pill_L2', 158, 502, 48, 21), (7, 'pill_R1', 1007, 676, 48, 21),
    (7, 'pill_R2', 158, 545, 48, 21), (6, 'pill_R3', 1047, 761, 39, 20),
]


def main():
    pages = {6: Image.open(sys.argv[1]).convert('RGB'), 7: Image.open(sys.argv[2]).convert('RGB')}
    out = Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    for page, name, x, y, w, h in CROPS:
        pages[page].crop((x, y, x + w, y + h)).save(out / f'{name}.png')
    print(f'{len(CROPS)} templates -> {out}')


if __name__ == '__main__':
    main()
