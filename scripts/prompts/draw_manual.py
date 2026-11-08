#!/usr/bin/env python3
"""Paints Xbox icons over the PlayStation ones in a page of the game's built-in manual (Manual/ManualNN.ftx, 1920x1080):
the black circles of the face buttons and the black pills of L1, L2, R1, R2 and R3.

    draw_manual.py templates/ page.png out.png --font Poppins-Bold.ttf

`templates/` holds reference crops of the icons from the English pages (made by make_manual_templates.py); a candidate is
only replaced when it matches one of them closely, so lettering and other artwork are left alone. Writes the edited page,
the mask of changed pixels (out.changed.npy) and prints what was replaced.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

XBOX = {'A': (98, 192, 78), 'B': (228, 68, 60), 'X': (70, 142, 236), 'Y': (242, 200, 56)}
FACE = {'cross': 'A', 'circle': 'B', 'square': 'X', 'tri': 'Y'}
PILL_TEXT = {'L1': 'LB', 'R1': 'RB', 'L2': 'LT', 'R2': 'RT', 'R3': 'RS', 'L3': 'LS'}
CIRCLE_SIZE = (21, 21)
PILL_SIZE = (48, 21)
MIN_SCORE = 0.75
MIN_MARGIN = 0.08


def normalised(gray, size):
    img = Image.fromarray(gray.astype(np.uint8)).resize(size, Image.LANCZOS)
    arr = np.asarray(img, dtype=np.float32)
    return (arr - arr.mean()) / (arr.std() + 1e-6)


def correlate(a, b):
    return float((a * b).mean())


def candidates(img):
    gray = img.mean(-1)
    dark = ndimage.binary_closing(gray < 90, iterations=1)
    filled = ndimage.binary_fill_holes(dark)
    lab, _ = ndimage.label(filled)
    for k, s in enumerate(ndimage.find_objects(lab), 1):
        h, w = s[0].stop - s[0].start, s[1].stop - s[1].start
        comp = lab[s] == k
        solid = float(comp.mean())
        if 17 <= h <= 24 and 17 <= w <= 24 and abs(w - h) <= 2 and solid > 0.7:
            yield 'circle', s, comp
        elif 36 <= w <= 52 and 17 <= h <= 24 and solid > 0.8:
            yield 'pill', s, comp


def load_templates(folder):
    out = {'circle': {}, 'pill': {}}
    for path in sorted(Path(folder).glob('*.png')):
        kind, name = path.stem.split('_', 1)
        size = CIRCLE_SIZE if kind == 'circle' else PILL_SIZE
        out[kind][name] = normalised(np.asarray(Image.open(path).convert('L')), size)
    return out


def classify(kind, gray_patch, templates, aligned=False):
    """The template the patch matches closely, or None. `aligned` also compares, unscaled, every window of the template's
    size inside a larger patch (a pill drawn a pixel wider than the template), with the same thresholds."""
    size = CIRCLE_SIZE if kind == 'circle' else PILL_SIZE
    probe = normalised(gray_patch, size)
    scores = {name: correlate(probe, t) for name, t in templates[kind].items()}
    if aligned:
        w, h = size
        for dy in range(gray_patch.shape[0] - h + 1):
            for dx in range(gray_patch.shape[1] - w + 1):
                window = gray_patch[dy:dy + h, dx:dx + w].astype(np.float32)
                window = (window - window.mean()) / (window.std() + 1e-6)
                for name, t in templates[kind].items():
                    scores[name] = max(scores[name], correlate(window, t))
    ranked = sorted(((score, name) for name, score in scores.items()), reverse=True)
    if not ranked or ranked[0][0] < MIN_SCORE:
        return None
    if len(ranked) > 1 and ranked[0][0] - ranked[1][0] < MIN_MARGIN:
        return None
    return ranked[0][1]


def background(img, box):
    x0, y0, x1, y1 = box
    ring = np.concatenate([img[y0 - 3:y0, x0 - 3:x1 + 3].reshape(-1, 3), img[y1:y1 + 3, x0 - 3:x1 + 3].reshape(-1, 3),
                           img[y0:y1, x0 - 3:x0].reshape(-1, 3), img[y0:y1, x1:x1 + 3].reshape(-1, 3)])
    return np.median(ring, axis=0).astype(np.uint8)


def draw_circle(img, box, letter, font):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    img[y0 - 2:y1 + 2, x0 - 2:x1 + 2] = background(img, (x0 - 2, y0 - 2, x1 + 2, y1 + 2))
    scale = 8
    layer = Image.new('RGBA', (w * scale, h * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((0, 0, w * scale - 1, h * scale - 1), fill=XBOX[letter] + (255,))
    f = ImageFont.truetype(font, int(h * 0.62 * scale * 1.38))
    d.text((w * scale / 2, h * scale / 2), letter, font=f, fill=(255, 255, 255, 255), anchor='mm')
    layer = layer.resize((w, h), Image.LANCZOS)
    base = Image.fromarray(img[y0:y1, x0:x1]).convert('RGBA')
    base.alpha_composite(layer)
    img[y0:y1, x0:x1] = np.array(base.convert('RGB'))


def draw_pill(img, box, comp, text, font):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    region = img[y0:y1, x0:x1]
    black = np.median(region[comp & (region.mean(-1) < 60)], axis=0).astype(np.uint8)
    inner = ndimage.binary_erosion(comp, iterations=2)
    region[inner] = black
    scale = 8
    layer = Image.new('RGBA', (w * scale, h * scale), (0, 0, 0, 0))
    f = ImageFont.truetype(font, int(h * 0.56 * scale * 1.38))
    ImageDraw.Draw(layer).text((w * scale / 2, h * scale / 2), ' '.join(text), font=f, fill=(255, 255, 255, 255), anchor='mm')
    layer = layer.resize((w, h), Image.LANCZOS)
    base = Image.fromarray(region).convert('RGBA')
    base.alpha_composite(layer)
    img[y0:y1, x0:x1] = np.array(base.convert('RGB'))


def paint(img, templates, font, aligned=False):
    original = img.copy()
    done = []
    gray = original.mean(-1)
    for kind, s, comp in list(candidates(original)):
        box = (s[1].start, s[0].start, s[1].stop, s[0].stop)
        patch = gray[s[0], s[1]]
        name = classify(kind, patch, templates, aligned)
        if name is None:
            continue
        if kind == 'circle':
            draw_circle(img, box, FACE[name], font)
        else:
            draw_pill(img, box, comp, PILL_TEXT[name], font)
        done.append((kind, name, box))
    return done


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('templates')
    ap.add_argument('page')
    ap.add_argument('out')
    ap.add_argument('--font', required=True)
    ap.add_argument('--aligned', action='store_true', help='also match pills a pixel wider than the templates')
    args = ap.parse_args()
    original = np.array(Image.open(args.page).convert('RGB'))
    if original.shape[:2] != (1080, 1920):
        raise SystemExit('expected a 1920x1080 page')
    edited = original.copy()
    done = paint(edited, load_templates(args.templates), args.font, args.aligned)
    changed = (edited != original).any(-1)
    out = Path(args.out)
    Image.fromarray(edited).save(out)
    np.save(out.with_suffix('.changed.npy'), changed)
    print(f'{out.name}: {len(done)} icons ' + ' '.join(f'{k}:{n}' for k, n, _ in done))


if __name__ == '__main__':
    main()
