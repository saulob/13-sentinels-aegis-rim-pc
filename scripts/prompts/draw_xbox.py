#!/usr/bin/env python3
"""Paints Xbox glyphs over the PlayStation ones in the button atlas (HD_Interface_arranged000, 1024x1024), in place:
every sprite keeps its rectangle. Sprite rectangles come from the version's own Interface.mbs.

    draw_xbox.py atlas.png Interface.mbs 100|102 out.png --font Poppins-Bold.ttf

Writes the edited atlas, the mask of changed pixels (out.changed.npy) and a before/after sheet (out.compare.png).
Poppins Bold is under the SIL Open Font License (fonts.google.com/specimen/Poppins).
"""
import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage

XBOX = {'A': (98, 192, 78), 'B': (228, 68, 60), 'X': (70, 142, 236), 'Y': (242, 200, 56)}
WHITE = (255, 255, 255)
LETTER = {'cross': 'A', 'circle': 'B', 'square': 'X', 'tri': 'Y'}

# Sprite indices in Interface.mbs (the UV quads), by role and symbol, per version of the game.
SPRITES = {
    '100': dict(
        disc_white={'tri': 279, 'circle': 280, 'square': 281, 'cross': 282},
        disc_colour={'tri': 294, 'circle': 296, 'square': 298, 'cross': 300},
        flat={'tri': 295, 'circle': 297, 'square': 299, 'cross': 301},
        outline={'tri': 139, 'circle': 136, 'square': 146},
        faint={'tri': 140, 'circle': 137, 'cross': 141, 'square': 142},
        glow={'circle': 135, 'tri': 138, 'square': 145},
        digits={'B': 311, 'T': 312},
        ovals={'MENU': 283, 'VIEW': 285}, words={'MENU': 284, 'VIEW': 286},
        logos=[287, 309], pad=304),
    '102': dict(
        disc_white={'tri': 289, 'circle': 290, 'square': 291, 'cross': 292},
        disc_colour={'tri': 304, 'circle': 309, 'square': 313, 'cross': 317},
        flat={'tri': 305, 'circle': 310, 'square': 314, 'cross': 318},
        outline={'tri': 140, 'circle': 137, 'square': 152},
        faint={'tri': 141, 'circle': 138, 'cross': 144, 'square': 145},
        glow={'circle': 136, 'tri': 139, 'square': 151},
        digits={'B': 336, 'T': 341},
        ovals={'MENU': 293, 'VIEW': 295}, words={'MENU': 294, 'VIEW': 296},
        logos=[297, 332], pad=323),
}
# Flat shapes no sprite entry names on its own (found by matching the flat shapes against the whole atlas).
EXTRA = {
    '100': [((900, 130, 960, 190), 'circle', 'outline'), ((962, 192, 1018, 248), 'square', 'outline'),
            ((762, 250, 834, 312), 'tri', 'flat'), ((836, 248, 896, 311), 'cross', 'flat')],
    '102': [((410, 836, 458, 884), 'square', 'flat')],
}
# The OPTIONS and TOUCHPAD words of the lower-left label block, the same place in both atlases.
BLOCK_WORDS = {(206, 346, 384, 390): 'MENU', (206, 399, 402, 443): 'VIEW'}


def quads(mbs):
    data = Path(mbs).read_bytes()
    count = (data.find(b'UU5A') - 0x120) // 48
    out = {}
    for i in range(count):
        f = struct.unpack_from('<12f', data, 0x120 + i * 48)
        xs, ys = f[2:10:2], f[3:10:2]
        if 0 <= min(xs) and max(xs) <= 1 and 0 <= min(ys) and max(ys) <= 1 and max(xs) > min(xs):
            out[i] = (min(xs), min(ys), max(xs), max(ys))
    return out


def rect_of(quad, size=1024):
    return (int(quad[0] * size), int(quad[1] * size), int(round(quad[2] * size)), int(round(quad[3] * size)))


def own_mask(a, rect, thresh=8, slack=4):
    """The pixels of the sprite in `rect`: the visible components inside it, not the soft edge of a neighbour."""
    x0, y0, x1, y1 = rect
    wx0, wy0 = max(0, x0 - 8), max(0, y0 - 8)
    wx1, wy1 = min(a.shape[1], x1 + 8), min(a.shape[0], y1 + 8)
    lab, n = ndimage.label(a[wy0:wy1, wx0:wx1, 3] > thresh)
    mask = np.zeros(a.shape[:2], bool)
    for k, s in enumerate(ndimage.find_objects(lab), 1):
        bx0, by0, bx1, by1 = wx0 + s[1].start, wy0 + s[0].start, wx0 + s[1].stop, wy0 + s[0].stop
        if bx0 >= x0 - slack and by0 >= y0 - slack and bx1 <= x1 + slack and by1 <= y1 + slack:
            mask[wy0:wy1, wx0:wx1] |= lab == k
    keep = np.zeros_like(mask)
    keep[y0:y1, x0:x1] = mask[y0:y1, x0:x1]
    return keep


def ink_box(a, rect, thresh=8):
    ys, xs = np.nonzero(own_mask(a, rect, thresh))
    if not len(xs):
        return None
    return (int(xs.min()), int(ys.min()), int(xs.max() + 1), int(ys.max() + 1))


def letter_layer(size, text, font, height, fill, stroke=0, blur=0.0, alpha=1.0, shadow=0):
    scale = 4
    big = Image.new('RGBA', (size[0] * scale, size[1] * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    px = max(8, int(height * 1.38)) * scale
    f = ImageFont.truetype(font, px)
    box = d.textbbox((0, 0), text, font=f, anchor='ls', stroke_width=stroke * scale)
    room = size[0] * scale - (shadow + 2 * stroke + 2) * scale
    if box[2] - box[0] > room:
        f = ImageFont.truetype(font, max(8, int(px * room / (box[2] - box[0]))))
        box = d.textbbox((0, 0), text, font=f, anchor='ls', stroke_width=stroke * scale)
    x = (big.width - (box[2] - box[0])) / 2 - box[0] - shadow * scale / 2
    y = (big.height - (box[3] - box[1])) / 2 - box[1] - shadow * scale / 2
    if shadow:
        d.text((x + shadow * scale, y + shadow * scale), text, font=f, fill=(0, 0, 0, 255), anchor='ls',
               stroke_width=stroke * scale, stroke_fill=(0, 0, 0, 255))
    d.text((x, y), text, font=f, fill=tuple(fill) + (255,), anchor='ls', stroke_width=stroke * scale,
           stroke_fill=(0, 0, 0, 255))
    layer = big.resize(size, Image.LANCZOS)
    if blur:
        layer = layer.filter(ImageFilter.GaussianBlur(float(blur)))
    if alpha < 1.0:
        arr = np.array(layer)
        arr[..., 3] = (arr[..., 3] * alpha).astype(np.uint8)
        layer = Image.fromarray(arr)
    return layer


def paste(a, rect, layer, offset=(0, 0)):
    x0, y0, x1, y1 = rect
    base = Image.fromarray(a[y0:y1, x0:x1])
    base.alpha_composite(layer, offset)
    a[y0:y1, x0:x1] = np.array(base)


def erase_disc_symbol(a, box):
    """Takes the PlayStation symbol off a glossy disc: its bright pixels on the dark face get the face's colour at the
    same radius and angle, from the face pixels the symbol does not cover."""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1]
    h, w = sub.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    ny, nx = (yy - (h - 1) / 2) / (h / 2), (xx - (w - 1) / 2) / (w / 2)
    r = np.hypot(ny, nx)
    ang = np.arctan2(ny, nx)
    face = r < 0.86
    glyph = ndimage.binary_dilation(face & (sub[..., :3].max(-1) > 70), iterations=2) & face
    angles, radii = 48, 12
    ab = ((ang + np.pi) / (2 * np.pi) * angles).astype(int) % angles
    rb = np.minimum((r / 0.86 * radii).astype(int), radii - 1)
    clean = face & ~glyph
    for k in range(angles):
        for j in range(radii):
            target = glyph & (ab == k) & (rb == j)
            if not target.any():
                continue
            for spread in range(1, radii + 1):
                near = clean & (np.abs(rb - j) < spread) & (np.abs((ab - k + angles // 2) % angles - angles // 2) < spread)
                if near.sum() >= 4:
                    break
            sub[target] = np.median(sub[near], axis=0).round().astype(np.uint8)
    return glyph


def paint_disc(a, rect, letter, coloured, font, changed):
    box = ink_box(a, rect)
    x0, y0, x1, y1 = box
    glyph = erase_disc_symbol(a, box)
    size = (x1 - x0, y1 - y0)
    layer = letter_layer(size, letter, font, size[1] * 0.46, XBOX[letter] if coloured else WHITE)
    paste(a, box, layer)
    changed[y0:y1, x0:x1] |= glyph | (np.array(layer)[..., 3] > 0)


def clear_rect(a, rect, changed):
    own = own_mask(a, rect)
    changed |= own
    a[own] = 0


def paint_symbol(a, rect, letter, mode, font, changed):
    """Replaces a flat PlayStation shape (and its outline, faint and glow variants) by the Xbox letter, in the same
    style: the sprite is cleared and the letter drawn where the shape was."""
    box = ink_box(a, rect)
    x0, y0, x1, y1 = box
    height = y1 - y0
    pad = 6
    area = (max(rect[0], x0 - pad), max(rect[1], y0 - pad), min(rect[2], x1 + pad), min(rect[3], y1 + pad))
    clear_rect(a, rect, changed)
    size = (area[2] - area[0], area[3] - area[1])
    if mode == 'flat':
        layer = letter_layer(size, letter, font, height * 0.9, WHITE)
    elif mode == 'outline':
        layer = letter_layer(size, letter, font, height * 0.8, WHITE, stroke=2)
    elif mode == 'faint':
        layer = letter_layer(size, letter, font, height * 0.9, WHITE, blur=1.2, alpha=0.45)
    else:
        layer = letter_layer(size, letter, font, height * 0.85, WHITE, blur=max(2.0, height * 0.08), alpha=0.95)
    paste(a, area, layer)
    changed[area[1]:area[3], area[0]:area[2]] = True


def paint_label(a, rect, text, font, changed, shadow=True):
    box = ink_box(a, rect)
    x0, y0, x1, y1 = box
    clear_rect(a, rect, changed)
    size = (x1 - x0, y1 - y0)
    paste(a, box, letter_layer(size, text, font, size[1] * 0.85, WHITE, stroke=2, shadow=2 if shadow else 0))
    changed[y0:y1, x0:x1] = True


def inpaint(sub, mask, iterations=400):
    """Fills the masked pixels by diffusing in the colours around them."""
    arr = sub.astype(np.float32)
    keep = ~mask
    arr[mask] = arr[keep].mean(axis=0) if keep.any() else 0
    for _ in range(iterations):
        mean = (np.roll(arr, 1, 0) + np.roll(arr, -1, 0) + np.roll(arr, 1, 1) + np.roll(arr, -1, 1)) / 4
        arr[mask] = mean[mask]
    sub[mask] = arr[mask].round().astype(np.uint8)


def ellipse_radius(h, w):
    yy, xx = np.mgrid[0:h, 0:w]
    return np.hypot((yy - (h - 1) / 2) / (h / 2), (xx - (w - 1) / 2) / (w / 2))


def erase_inside(a, rect, changed, thresh=150, radius=0.985):
    """Removes bright artwork (text) from a dark oval sprite, the rim included; the caller draws the rim again."""
    x0, y0, x1, y1 = ink_box(a, rect)
    sub = a[y0:y1, x0:x1]
    inside = (ellipse_radius(*sub.shape[:2]) < radius) & (sub[..., 3] > 200)
    text = ndimage.binary_dilation(inside & (sub[..., :3].max(-1) > thresh), iterations=2) & inside
    inpaint(sub, text)
    changed[y0:y1, x0:x1] |= text


def draw_rim(a, rect, changed):
    x0, y0, x1, y1 = ink_box(a, rect)
    w, h = x1 - x0, y1 - y0
    scale = 4
    layer = Image.new('RGBA', (w * scale, h * scale), (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse((2 * scale, 2 * scale, (w - 3) * scale, (h - 3) * scale), outline=(235, 235, 235, 255),
                                  width=2 * scale)
    paste(a, (x0, y0, x1, y1), layer.resize((w, h), Image.LANCZOS))
    changed[y0:y1, x0:x1] = True


def erase_logo(a, rect, changed):
    """Takes the PlayStation logo off a glossy button: the bright pixels inside the face are filled from the face."""
    x0, y0, x1, y1 = ink_box(a, rect)
    sub = a[y0:y1, x0:x1]
    inside = ellipse_radius(*sub.shape[:2]) < 0.8
    logo = ndimage.binary_dilation(inside & (sub[..., :3].max(-1) > 70), iterations=2) & inside
    inpaint(sub, logo)
    changed[y0:y1, x0:x1] |= logo


def paint_oval_text(a, rect, text, font, changed):
    box = ink_box(a, rect)
    erase_inside(a, rect, changed)
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    tw, th = int(w * 0.7), int(h * 0.4)
    area = (x0 + (w - tw) // 2, y0 + (h - th) // 2, x0 + (w - tw) // 2 + tw, y0 + (h - th) // 2 + th)
    paste(a, area, letter_layer((tw, th), text, font, th * 0.9, WHITE, stroke=1))
    draw_rim(a, (x0, y0, x1, y1), changed)


def fit_into(a, rect, source, changed):
    x0, y0, x1, y1 = rect
    clear_rect(a, rect, changed)
    w, h = x1 - x0, y1 - y0
    s = min(w / source.shape[1], h / source.shape[0])
    tw, th = max(1, round(source.shape[1] * s)), max(1, round(source.shape[0] * s))
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    layer.paste(Image.fromarray(source).resize((tw, th), Image.LANCZOS), ((w - tw) // 2, (h - th) // 2))
    paste(a, rect, layer)
    changed[y0:y1, x0:x1] = True


def paint(a, mbs, version, font):
    sprites = SPRITES[version]
    uv = quads(mbs)
    rect = lambda i: rect_of(uv[i])
    changed = np.zeros(a.shape[:2], bool)
    for symbol, i in sprites['disc_white'].items():
        paint_disc(a, rect(i), LETTER[symbol], False, font, changed)
    for symbol, i in sprites['disc_colour'].items():
        paint_disc(a, rect(i), LETTER[symbol], True, font, changed)
    for mode in ('flat', 'outline', 'faint', 'glow'):
        for symbol, i in sprites[mode].items():
            paint_symbol(a, rect(i), LETTER[symbol], mode, font, changed)
    for box, symbol, mode in EXTRA[version]:
        paint_symbol(a, box, LETTER[symbol], mode, font, changed)
    for text, i in sprites['digits'].items():
        paint_label(a, rect(i), text, font, changed)
    for text, i in sprites['ovals'].items():
        paint_oval_text(a, rect(i), text, font, changed)
    for text, i in sprites['words'].items():
        paint_label(a, rect(i), text, font, changed)
    for box, text in BLOCK_WORDS.items():
        paint_label(a, box, text, font, changed)
    for i in sprites['logos']:
        erase_logo(a, rect(i), changed)
    view_oval = ink_box(a, rect(sprites['ovals']['VIEW']))
    x0, y0, x1, y1 = view_oval
    fit_into(a, rect(sprites['pad']), a[y0:y1, x0:x1].copy(), changed)
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('atlas')
    ap.add_argument('mbs')
    ap.add_argument('version', choices=sorted(SPRITES))
    ap.add_argument('out')
    ap.add_argument('--font', required=True)
    args = ap.parse_args()
    original = np.array(Image.open(args.atlas).convert('RGBA'))
    if original.shape[:2] != (1024, 1024):
        raise SystemExit('expected the 1024x1024 button atlas')
    edited = original.copy()
    changed = paint(edited, args.mbs, args.version, args.font)
    changed |= (edited != original).any(-1)
    out = Path(args.out)
    Image.fromarray(edited).save(out)
    np.save(out.with_suffix('.changed.npy'), changed)
    sheet = Image.new('RGB', (1024, 1024), (40, 40, 90))
    for i, image in enumerate((original, edited)):
        layer = Image.new('RGBA', (1024, 1024), (40, 40, 90, 255))
        layer.alpha_composite(Image.fromarray(image))
        sheet.paste(layer.convert('RGB').crop((0, 0, 1024, 512)), (0, i * 512))
    sheet.save(out.with_suffix('.compare.png'))
    print(f'{out}: {int(changed.sum())} pixels changed')


if __name__ == '__main__':
    main()
