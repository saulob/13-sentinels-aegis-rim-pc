#!/usr/bin/env python3
"""Looks at the art of the Xbox patches: the entries tsr-game-profile-tests exported before and after the patch (real
archives of the packages) are decoded and every PlayStation variant is checked to be gone and the rest to be intact.

    prompt_art_tests.py build/test-prompt-art

Needs Python with numpy, Pillow and scipy (tools\\setup.ps1 -InstallPython). Exits 2 when there is nothing to check.
"""
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.signal import fftconvolve

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'scripts' / 'prompts'))
import draw_manual  # noqa: E402
import draw_xbox  # noqa: E402
import ftex  # noqa: E402

sys.path.insert(0, str(HERE.parent / 'scripts' / 'exit_game'))
from title_layout import PARTS, POSITIONS, UVS, Layout  # noqa: E402

ATLAS = 'HD_Interface_arranged000.gnf'
failures = 0
checks = 0


def check(condition, message):
    global failures, checks
    checks += 1
    if not condition:
        failures += 1
        print('FAIL', message)


def decode(path, name=None):
    blob = Path(path).read_bytes()
    textures = ftex.textures(blob)
    texture = next(t for t in textures if name is None or t['name'] == name)
    return np.array(ftex.decode(blob, texture).convert('RGBA'))


def differs(before, after, rect):
    x0, y0, x1, y1 = rect
    return bool((before[y0:y1, x0:x1] != after[y0:y1, x0:x1]).any())


def visibly_differs(before, after, rect, alpha_min=32):
    """Differs where either version has a visible pixel: re-encoded blocks change the colour of transparent ones."""
    x0, y0, x1, y1 = rect
    a, b = before[y0:y1, x0:x1], after[y0:y1, x0:x1]
    visible = (a[..., 3] >= alpha_min) | (b[..., 3] >= alpha_min)
    return bool(((a != b).any(-1) & visible).any())


def flat_matches(atlas, templates, threshold=0.82):
    alpha = atlas[..., 3].astype(np.float32) / 255.0
    hits = []
    for name, t in templates.items():
        t0 = t - t.mean()
        n = t.size
        ones = np.ones_like(t)
        s1 = fftconvolve(alpha, ones[::-1, ::-1], mode='valid')
        s2 = fftconvolve(alpha * alpha, ones[::-1, ::-1], mode='valid')
        var = np.maximum(s2 / n - (s1 / n) ** 2, 1e-6)
        cc = fftconvolve(alpha, t0[::-1, ::-1], mode='valid') / (n * np.sqrt(var) * (t.std() + 1e-6))
        cc = np.where(var > 0.01, cc, 0)
        if (cc > threshold).any():
            hits.append(name)
    return hits


PATCHES = {'100': 'xbox-prompts-1.00.dcpx', '102': 'xbox-prompts-1.02.dcpx'}
ENTRIES = {'100': 'GUI_Interface.ftx', '102': 'gui_interface.ftx'}
SEMANTIC_LETTER = {'cross': 'A', 'circle': 'B', 'square': 'X', 'tri': 'Y'}


def committed_patch_applied(version, original):
    """The original entry with the committed .dcpx applied: what the launcher writes into the archive."""
    blob = (HERE.parent / 'launcher' / 'prompts' / PATCHES[version]).read_bytes()
    entry = 'GUI/Interface.ftx' if version == '100' else 'gui/interface.ftx'
    crc = zlib.crc32(original)
    pos = 12
    for _ in range(struct.unpack_from('<I', blob, 8)[0]):
        names = []
        for _ in range(2):
            n = struct.unpack_from('<H', blob, pos)[0]
            names.append(blob[pos + 2:pos + 2 + n].decode())
            pos += 2 + n
        size, before, after, runs = struct.unpack_from('<IIII', blob, pos)
        pos += 16
        if names[1].lower() == entry.lower() and size == len(original) and before == crc:
            data = bytearray(original)
            for i in range(runs):
                offset = struct.unpack_from('<I', blob, pos + i * 20)[0]
                data[offset:offset + 16] = blob[pos + i * 20 + 4:pos + i * 20 + 20]
            check(zlib.crc32(data) == after, f'{version}: the patch result does not match its recorded CRC32')
            return bytes(data)
        pos += runs * 20
    return None


def decode_bytes(blob):
    texture = next(t for t in ftex.textures(blob) if t['name'] == ATLAS)
    return np.array(ftex.decode(blob, texture).convert('RGBA'))


def occupied(mask, fx, fy, radius):
    h, w = mask.shape
    x, y = int(round(fx * (w - 1))), int(round(fy * (h - 1)))
    return bool(mask[max(0, y - radius):y + radius + 1, max(0, x - radius):x + radius + 1].any())


def symbol_of(mask):
    """cross, circle, square or tri from where a shape's strokes are: corners and edge middles of its bounding box."""
    ys, xs = np.nonzero(mask)
    box = mask[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    tl, tr, bl, br = (occupied(box, fx, fy, 1) for fx, fy in ((0.08, 0.08), (0.92, 0.08), (0.08, 0.92), (0.92, 0.92)))
    mids = [occupied(box, fx, fy, 2) for fx, fy in ((0.5, 0.04), (0.5, 0.96), (0.04, 0.5), (0.96, 0.5))]
    if tl and tr and bl and br:
        return 'square' if all(mids) else 'cross' if not any(mids) else None
    if not (tl or tr or bl or br):
        return 'circle' if all(mids) else None
    return 'tri' if (bl and br and not tl and not tr) else None


def glyph_mask(atlas, rect, coloured):
    """The symbol on a glossy disc: the bright strokes inside the face, saturated ones on a coloured disc."""
    x0, y0, x1, y1 = draw_xbox.ink_box(atlas, rect)
    sub = atlas[y0:y1, x0:x1, :3].astype(int)
    h, w = sub.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    radius = np.hypot((yy - (h - 1) / 2) / (h / 2), (xx - (w - 1) / 2) / (w / 2))
    bright = sub.max(-1) > 110
    return (radius < 0.7) & bright & ((sub.max(-1) - sub.min(-1) > 60) if coloured else True)


def ink_mask(atlas, box):
    x0, y0, x1, y1 = box
    return atlas[y0:y1, x0:x1, 3] > 128


def find_shape(template, atlas_alpha):
    """Where the template (alpha 0..1) sits in the atlas: (x, y, normalised squared error) of the best match."""
    t = template.astype(np.float32)
    a = atlas_alpha.astype(np.float32) / 255
    ones = np.ones_like(t)
    ssd = fftconvolve(a * a, ones[::-1, ::-1], mode='valid') - 2 * fftconvolve(a, t[::-1, ::-1], mode='valid') + (t * t).sum()
    y, x = divmod(int(np.argmin(ssd)), ssd.shape[1])
    return x, y, float(ssd[y, x]) / max(1.0, float((t * t).sum()))


def check_semantics(roots):
    """Cross gives A, circle B, square X, triangle Y: the letter table, the symbol each original sprite really shows, and
    every PlayStation shape of the 1.00 atlas repainted wherever it sits in the other atlas."""
    check(draw_xbox.LETTER == SEMANTIC_LETTER, f'letters by symbol are {draw_xbox.LETTER}')
    originals, patched, uvs = {}, {}, {}
    for version, root in roots.items():
        blob = (root / 'original' / ENTRIES[version]).read_bytes()
        originals[version] = decode_bytes(blob)
        result = committed_patch_applied(version, blob)
        check(result is not None, f'{version}: no target of the committed patch holds this original')
        if result is None:
            return
        patched[version] = decode_bytes(result)
        uvs[version] = draw_xbox.quads(root / 'original' / ENTRIES[version].replace('.ftx', '.mbs'))
    for version, atlas in originals.items():
        rect = lambda i: draw_xbox.rect_of(uvs[version][i])
        for symbol, index in draw_xbox.SPRITES[version]['disc_white'].items():
            check(symbol_of(glyph_mask(atlas, rect(index), False)) == symbol, f'{version}: disc sprite {index} is not the {symbol} symbol')
        for symbol, index in draw_xbox.SPRITES[version]['disc_colour'].items():
            check(symbol_of(glyph_mask(atlas, rect(index), True)) == symbol, f'{version}: coloured disc sprite {index} is not the {symbol} symbol')
        for symbol, index in draw_xbox.SPRITES[version]['flat'].items():
            box = draw_xbox.ink_box(atlas, rect(index))
            check(symbol_of(ink_mask(atlas, box)) == symbol, f'{version}: flat sprite {index} is not the {symbol} symbol')
        for box, symbol, mode in draw_xbox.EXTRA[version]:
            ink = draw_xbox.ink_box(atlas, box)
            if mode == 'flat':
                check(symbol_of(ink_mask(atlas, ink)) == symbol, f'{version}: extra shape at {box} is not the {symbol} symbol')

    # The 1.00 atlas is the reference: each of its painted shapes is searched for in the other atlas and must be repainted
    # there, and as the same letter (the repainted pixels correlate with the reference's).
    reference = originals['100']
    shapes = {}
    for role in ('flat', 'outline', 'faint', 'glow'):
        for symbol, index in draw_xbox.SPRITES['100'][role].items():
            shapes[f'{role} {symbol}'] = draw_xbox.rect_of(uvs['100'][index])
    for box, symbol, mode in draw_xbox.EXTRA['100']:
        shapes[f'extra {mode} {symbol}'] = box
    for version in roots:
        found = 0
        for name, rect in shapes.items():
            ink = draw_xbox.ink_box(reference, rect)
            x0, y0, x1, y1 = ink
            x, y, error = find_shape(reference[y0:y1, x0:x1, 3] / 255, originals[version][..., 3])
            if error > 0.02:
                continue
            found += 1
            where = (x, y, x + x1 - x0, y + y1 - y0)
            check(visibly_differs(originals[version], patched[version], where), f'{version}: {name} at {where} is still the PlayStation shape')
            twin = [b for b, s, m in draw_xbox.EXTRA[version] if name == f'extra {m} {s}' and b[0] <= x <= b[2] and b[1] <= y <= b[3]]
            if version == '100' or not twin or twin[0][2] - twin[0][0] != rect[2] - rect[0] or twin[0][3] - twin[0][1] != rect[3] - rect[1]:
                continue
            mine = patched[version][y:y + y1 - y0, x:x + x1 - x0, 3].astype(np.float32)
            theirs = patched['100'][y0:y1, x0:x1, 3].astype(np.float32)
            same = np.corrcoef(mine.ravel(), theirs.ravel())[0, 1] if mine.std() > 0 and theirs.std() > 0 else 0
            check(same > 0.9, f'{version}: {name} at {where} is not the letter the 1.00 atlas shows (correlation {same:.3f})')
        check(found >= len(shapes) - 1, f'{version}: only {found} of {len(shapes)} reference shapes were found in the atlas')
    print(f'prompt art semantics: cross A, circle B, square X, triangle Y; discs and flat shapes of '
          f'{", ".join(roots)} are the symbols they are named for')


def check_atlas(root, version, key):
    before = decode(root / 'original' / ('GUI_Interface.ftx' if version == '100' else 'gui_interface.ftx'), ATLAS)
    after = decode_bytes(committed_patch_applied(version, (root / 'original' / ENTRIES[version]).read_bytes()))
    mbs = root / 'original' / ('GUI_Interface.mbs' if version == '100' else 'gui_interface.mbs')
    uv = draw_xbox.quads(mbs)
    sprites = draw_xbox.SPRITES[version]
    rect = lambda i: draw_xbox.rect_of(uv[i])
    changed = 0
    for role in ('disc_white', 'disc_colour', 'flat', 'outline', 'faint', 'glow'):
        for symbol, index in sprites[role].items():
            ok = differs(before, after, rect(index))
            changed += ok
            check(ok, f'{key}: {role} {symbol} (sprite {index}) is unchanged')
    for label, index in sprites['digits'].items():
        check(differs(before, after, rect(index)), f'{key}: digit sprite {index} ({label}) is unchanged')
    for index in list(sprites['ovals'].values()) + list(sprites['words'].values()) + sprites['logos'] + [sprites['pad']]:
        check(differs(before, after, rect(index)), f'{key}: sprite {index} is unchanged')
    for box, symbol, mode in draw_xbox.EXTRA[version]:
        check(differs(before, after, box), f'{key}: extra {symbol} {mode} at {box} is unchanged')
    for box in draw_xbox.BLOCK_WORDS:
        check(differs(before, after, box), f'{key}: label at {box} is unchanged')

    # The shapes that stay: the directional pad, the sticks and the shoulder plates are not PlayStation symbols.
    kept = {'100': [292, 313, 315, 317, 288, 290], '102': [302, 346, 352, 359, 298, 300]}[version]
    for index in kept:
        check(not visibly_differs(before, after, rect(index)), f'{key}: sprite {index} (pad, stick or plate) was changed')

    # No flat PlayStation shape is left anywhere in the atlas.
    alpha = before[..., 3].astype(np.float32) / 255.0
    templates = {}
    for symbol, index in sprites['flat'].items():
        x0, y0, x1, y1 = draw_xbox.ink_box(before, rect(index))
        templates[symbol] = alpha[y0:y1, x0:x1]
    check(len(flat_matches(before, templates)) >= 3, f'{key}: the original atlas shows no flat PlayStation shapes')
    left = flat_matches(after, templates)
    check(not left, f'{key}: flat PlayStation shapes are left after patching: {left}')
    print(f'prompt art {key}: {changed} symbol sprites repainted, pad/stick/plates kept, no flat shape left')


BUTTON_PARTS = {'BUTTON_BATSU': 'cross', 'BUTTON_CIRCLE': 'circle', 'BUTTON_SQUARE': 'square', 'BUTTON_TRIANGLE': 'tri'}


def part_sprites(layout, part):
    """{uv index: (atlas rect, on-screen turn in degrees)} of the atlas sprites a part of the layout draws."""
    index = next(i for i in range(layout.count(PARTS)) if layout.part(i)[0] == part)
    out = {}
    for layer in layout.part_layers(index):
        for element in layout.layer_elements(layer):
            for s in layout.element_sprites(element):
                if s['kind'] == 1 and s['tex'] == 0:
                    q = layout.quad(POSITIONS, s['pos'])
                    turn = -np.degrees(np.arctan2(q[4][1] - q[1][1], q[4][0] - q[1][0]))
                    rect = tuple(int(round(v * 1024)) for v in layout.bounds(UVS, s['uv']))
                    out.setdefault(s['uv'], (rect, float(turn)))
    return out


def as_drawn(atlas, rect, turn):
    """The sprite's alpha as the game shows it: turned counter-clockwise by `turn` degrees."""
    x0, y0, x1, y1 = rect
    return np.array(Image.fromarray(atlas[y0:y1, x0:x1, 3]).rotate(turn, Image.BICUBIC, expand=True))


def letter_shape(alpha, turn=0.0):
    image = Image.fromarray(alpha).rotate(turn, Image.BICUBIC, expand=True)
    a = np.array(image).astype(np.float32)
    ys, xs = np.nonzero(a > 64)
    if not len(xs):
        return None
    crop = Image.fromarray(a[ys.min():ys.max() + 1, xs.min():xs.max() + 1].astype(np.uint8)).resize((32, 32), Image.LANCZOS)
    v = np.array(crop).astype(np.float32).ravel()
    return (v - v.mean()) / (v.std() + 1e-6)


def check_button_parts(root, version, key):
    """Every atlas sprite the face-button parts of the game's own layout draw, as the game draws it (BUTTON_BATSU turns
    a "+" by 45 degrees into the Cross): repainted, no longer the PlayStation symbol, and a turned letter upright."""
    layout = Layout((root / 'original' / ENTRIES[version].replace('.ftx', '.mbs')).read_bytes())
    blob = (root / 'original' / ENTRIES[version]).read_bytes()
    before = decode_bytes(blob)
    after = decode_bytes(committed_patch_applied(version, blob))
    turned_cross = 0
    for part, symbol in BUTTON_PARTS.items():
        sprites = part_sprites(layout, part)
        check(len(sprites) >= 3, f'{key}: {part} draws {len(sprites)} atlas sprites')
        upright = [after[r[1]:r[3], r[0]:r[2], 3] for r, t in sprites.values() if abs(t) < 1]
        for uv, (rect, turn) in sprites.items():
            name = f'{key}: {part} sprite uv{uv} at {rect} (turned {turn:.0f})'
            shown_before, shown_after = as_drawn(before, rect, turn), as_drawn(after, rect, turn)
            if symbol == 'cross' and abs(turn) > 1 and (shown_before > 128).any() and symbol_of(shown_before > 128) == 'cross':
                turned_cross += 1
            check(visibly_differs(before, after, rect), f'{name} is still the original PlayStation art')
            if (shown_after > 128).any():
                check(symbol_of(shown_after > 128) != symbol, f'{name} still shows the PlayStation {symbol}')
            ink = shown_before > 128
            if ink.any():
                kept = (np.abs(shown_before.astype(int) - shown_after.astype(int)) < 24) & ink
                check(kept.sum() < 0.5 * ink.sum(), f'{name} keeps {kept.sum()} of {ink.sum()} original ink pixels')
            if abs(turn) > 1 and upright:
                shape = letter_shape(shown_after)
                refs = [r for r in (letter_shape(u) for u in upright) if r is not None]
                if shape is not None and refs:
                    score = lambda s: max(float(np.dot(s, r)) / s.size for r in refs)
                    straight = score(shape)
                    tilted = max(score(letter_shape(shown_after, a)) for a in (-45, 45, -90, 90))
                    check(straight > tilted, f'{name} is not upright on screen ({straight:.2f} vs {tilted:.2f} turned)')
    check(turned_cross >= 1, f'{key}: BUTTON_BATSU draws no turned Cross in the original layout')
    print(f'prompt art {key}: face-button parts repainted as drawn on screen, {turned_cross} turned Cross sprites')


def check_manual(root, templates, key):
    pages = sorted(p for p in (root / 'original').glob('Manual_Manual*.ftx'))
    check(len(pages) >= 10, f'{key}: only {len(pages)} manual pages patched')
    total = 0
    for page in pages:
        before = np.array(ftex.decode(page.read_bytes(), ftex.textures(page.read_bytes())[0]).convert('RGB'))
        patched = root / 'patched' / page.name
        data = patched.read_bytes()
        after = np.array(ftex.decode(data, ftex.textures(data)[0]).convert('RGB'))
        # An icon is still PlayStation when it matches and its pixels are the page's own: the new pills match too.
        left = [c for c in draw_manual.candidates(after)
                if draw_manual.classify(c[0], after.mean(-1)[c[1][0], c[1][1]], templates)
                and np.array_equal(after[c[1][0], c[1][1]], before[c[1][0], c[1][1]])]
        was = [c for c in draw_manual.candidates(before)
               if draw_manual.classify(c[0], before.mean(-1)[c[1][0], c[1][1]], templates)]
        check(len(was) >= 1, f'{key}: {page.name} has no PlayStation icon to replace')
        check(not left, f'{key}: {page.name} still shows {len(left)} PlayStation icon(s)')
        total += len(was)
    print(f'prompt art {key}: {len(pages)} manual pages, {total} icons replaced, none left')


def main():
    folder = Path(sys.argv[1])
    if not (folder / 'templates').is_dir() or not (folder / '100').is_dir():
        print('skipped: no exported entries (run the Packages group with TSR_TEST_PACKAGES set first)')
        return 2
    import tempfile
    with tempfile.TemporaryDirectory() as work:
        crops = Path(work)
        pages = {}
        for number, name in ((6, 'Manual_Manual05.ftx'), (7, 'Manual_Manual06.ftx')):
            data = (folder / 'templates' / name).read_bytes()
            Image.fromarray(np.array(ftex.decode(data, ftex.textures(data)[0]).convert('RGB'))).save(crops / f'page{number:02d}.png')
        import make_manual_templates
        sys.argv = ['make_manual_templates', str(crops / 'page06.png'), str(crops / 'page07.png'), str(crops / 'tpl')]
        make_manual_templates.main()
        templates = draw_manual.load_templates(crops / 'tpl')
    roots = {}
    for version in ('100', '102'):
        root = folder / version
        if not root.is_dir():
            print(f'skipped: no exported entries for {version}')
            continue
        roots[version] = root
        check_atlas(root, version, version)
        check_button_parts(root, version, version)
        if version == '100':
            check_manual(root, templates, version)
    if set(roots) == {'100', '102'}:
        check_semantics(roots)
    print(f'prompt art: {checks} checks, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
