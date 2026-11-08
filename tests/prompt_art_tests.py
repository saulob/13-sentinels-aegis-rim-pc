#!/usr/bin/env python3
"""Looks at the art of the Xbox patches: original entries are decoded before and after the committed patch and every
PlayStation variant is checked to be gone and the rest to be intact.

    prompt_art_tests.py [build/test-prompt-art] [--archives ROOT]

build/test-prompt-art holds the entries tsr-game-profile-tests exported from the USA packages. ROOT (or the environment
variable TSR_PROMPT_ARCHIVES) holds the archives of each build, unpacked from its own package, one folder per package:
usa100, usa102, eu100, jp100, jp103, jp104, asia100 (an update's folder holds only its own archives). Each patch whose
archives are all there is applied to them in memory: every target must be the original (CRC32 before), give the recorded
result, change nothing outside its texture, and leave no PlayStation symbol in what the build reads.

Needs Python with numpy, Pillow and scipy (tools\\setup.ps1 -InstallPython). Exits 2 when there is nothing to check.
"""
import os
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.signal import fftconvolve

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'scripts' / 'prompts'))
import cpk  # noqa: E402
import draw_manual  # noqa: E402
import draw_xbox  # noqa: E402
import ftex  # noqa: E402
import make_manual_templates  # noqa: E402

sys.path.insert(0, str(HERE.parent / 'scripts' / 'exit_game'))
from title_layout import PARTS, POSITIONS, UVS, Layout  # noqa: E402

ATLAS = 'HD_Interface_arranged000.gnf'
PROMPTS = HERE.parent / 'launcher' / 'prompts'
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
# The shapes that stay, per layout: the directional pad, the sticks and the shoulder plates are not PlayStation symbols.
KEPT = {'100': [197, 218, 220, 222, 193, 195], '102': [206, 250, 256, 263, 202, 204],
        'jp100': [199, 222, 224, 226, 195, 197], 'jp103': [199, 222, 224, 226, 195, 197],
        'jp104': [208, 254, 260, 267, 204, 206], 'cn': [197, 220, 222, 224, 193, 195],
        'tw': [197, 218, 220, 222, 193, 195]}


def read_patch(path):
    """The targets of a DCPX3 patch: dicts with archive, name, size, before, after and runs [(offset, 16 bytes)]."""
    blob = Path(path).read_bytes()
    check(blob[:8] == b'DCPX3\0\0\0', f'{Path(path).name} is not a DCPX3 patch')
    pos, targets = 12, []
    for _ in range(struct.unpack_from('<I', blob, 8)[0]):
        names = []
        for _ in range(2):
            n = struct.unpack_from('<H', blob, pos)[0]
            names.append(blob[pos + 2:pos + 2 + n].decode())
            pos += 2 + n
        size, before, after, count = struct.unpack_from('<IIII', blob, pos)
        pos += 16
        runs = [(struct.unpack_from('<I', blob, pos + i * 20)[0], blob[pos + i * 20 + 4:pos + i * 20 + 20]) for i in range(count)]
        pos += count * 20
        targets.append(dict(archive=names[0], name=names[1], size=size, before=before, after=after, runs=runs))
    check(pos == len(blob), f'{Path(path).name} has {len(blob) - pos} trailing bytes')
    return targets


def apply_target(target, original):
    data = bytearray(original)
    for offset, run in target['runs']:
        data[offset:offset + 16] = run
    return bytes(data)


def committed_patch_applied(version, original):
    """The original entry with the committed .dcpx applied: what the launcher writes into the archive."""
    entry = 'GUI/Interface.ftx' if version == '100' else 'gui/interface.ftx'
    crc = zlib.crc32(original)
    for target in read_patch(PROMPTS / PATCHES[version]):
        if target['name'].lower() == entry.lower() and target['size'] == len(original) and target['before'] == crc:
            data = apply_target(target, original)
            check(zlib.crc32(data) == target['after'], f'{version}: the patch result does not match its recorded CRC32')
            return data
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


def check_symbols(atlas, uv, layout, key):
    """Each sprite of the layout's table shows the symbol it is named for in the original atlas."""
    rect = lambda i: draw_xbox.rect_of(uv[i])
    sprites = draw_xbox.SPRITES[layout]
    for symbol, index in sprites['disc_white'].items():
        check(symbol_of(glyph_mask(atlas, rect(index), False)) == symbol, f'{key}: disc sprite {index} is not the {symbol} symbol')
    for symbol, index in sprites['disc_colour'].items():
        check(symbol_of(glyph_mask(atlas, rect(index), True)) == symbol, f'{key}: coloured disc sprite {index} is not the {symbol} symbol')
    for symbol, index in sprites['flat'].items():
        box = draw_xbox.ink_box(atlas, rect(index))
        check(symbol_of(ink_mask(atlas, box)) == symbol, f'{key}: flat sprite {index} is not the {symbol} symbol')
    for box, symbol, mode in draw_xbox.EXTRA[draw_xbox.ATLAS_OF[layout]]:
        ink = draw_xbox.ink_box(atlas, box)
        if mode == 'flat':
            check(symbol_of(ink_mask(atlas, ink)) == symbol, f'{key}: extra shape at {box} is not the {symbol} symbol')


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
        check_symbols(atlas, uvs[version], version, version)

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


def check_atlas_images(before, after, uv, layout, key):
    sprites = draw_xbox.SPRITES[layout]
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
    for index in sprites.get('cleared', []):
        x0, y0, x1, y1 = rect(index)
        check(before[y0:y1, x0:x1, 3].max() > 128 and after[y0:y1, x0:x1, 3].max() < 16, f'{key}: sprite {index} is not cleared')
    for box, symbol, mode in draw_xbox.EXTRA[draw_xbox.ATLAS_OF[layout]]:
        check(differs(before, after, box), f'{key}: extra {symbol} {mode} at {box} is unchanged')
    for box in draw_xbox.BLOCK_WORDS:
        check(differs(before, after, box), f'{key}: label at {box} is unchanged')
    for index in KEPT[layout]:
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


def check_atlas(root, version, key):
    before = decode(root / 'original' / ENTRIES[version], ATLAS)
    after = decode_bytes(committed_patch_applied(version, (root / 'original' / ENTRIES[version]).read_bytes()))
    check_atlas_images(before, after, draw_xbox.quads(root / 'original' / ENTRIES[version].replace('.ftx', '.mbs')), version, key)


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


def check_button_part_images(layout, before, after, key):
    """Every atlas sprite the face-button parts of the game's own layout draw, as the game draws it (BUTTON_BATSU turns
    a "+" by 45 degrees into the Cross): repainted, no longer the PlayStation symbol, and a turned letter upright."""
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


def check_button_parts(root, version, key):
    layout = Layout((root / 'original' / ENTRIES[version].replace('.ftx', '.mbs')).read_bytes())
    blob = (root / 'original' / ENTRIES[version]).read_bytes()
    check_button_part_images(layout, decode_bytes(blob), decode_bytes(committed_patch_applied(version, blob)), key)


def icons_on(page, templates, aligned=False):
    return [c for c in draw_manual.candidates(page)
            if draw_manual.classify(c[0], page.mean(-1)[c[1][0], c[1][1]], templates, aligned)]


def check_manual_page(name, before, after, templates, key, aligned=False):
    # An icon is still PlayStation when it matches and its pixels are the page's own: the new pills match too.
    left = [c for c in icons_on(after, templates, aligned) if np.array_equal(after[c[1][0], c[1][1]], before[c[1][0], c[1][1]])]
    was = icons_on(before, templates, aligned)
    check(len(was) >= 1, f'{key}: {name} has no PlayStation icon to replace')
    check(not left, f'{key}: {name} still shows {len(left)} PlayStation icon(s)')
    return len(was)


def check_manual(root, templates, key):
    pages = sorted(p for p in (root / 'original').glob('Manual_Manual*.ftx'))
    check(len(pages) >= 10, f'{key}: only {len(pages)} manual pages patched')
    total = 0
    for page in pages:
        before = np.array(ftex.decode(page.read_bytes(), ftex.textures(page.read_bytes())[0]).convert('RGB'))
        data = (root / 'patched' / page.name).read_bytes()
        after = np.array(ftex.decode(data, ftex.textures(data)[0]).convert('RGB'))
        total += check_manual_page(page.name, before, after, templates, key)
    print(f'prompt art {key}: {len(pages)} manual pages, {total} icons replaced, none left')


def make_templates(page06, page07, work):
    Image.fromarray(page06).save(work / 'page06.png')
    Image.fromarray(page07).save(work / 'page07.png')
    sys.argv = ['make_manual_templates', str(work / 'page06.png'), str(work / 'page07.png'), str(work / 'tpl')]
    make_manual_templates.main()
    return draw_manual.load_templates(work / 'tpl')


def page_rgb(blob):
    return np.array(ftex.decode(blob, ftex.textures(blob)[0]).convert('RGB'))


# Per patch: the package folders its archives come from (first match wins) and what the build reads, as the archives it
# mounts (the update first) and the layout table, once per language. `aligned`: pills are also matched a pixel wider.
LANGUAGES = ['US', 'FR', 'GE', 'IT', 'SP']
ARCHIVE_BUILDS = {
    'xbox-prompts-1.00.dcpx': (['usa100'], [([f'ROBO_{lang}.CPK'], '100') for lang in LANGUAGES], False),
    'xbox-prompts-1.02.dcpx': (['usa102', 'usa100'], [([f'ROBO_PATCH2_{lang}.CPK', f'ROBO_{lang}.CPK'], '102') for lang in LANGUAGES], False),
    'xbox-prompts-eu-1.00.dcpx': (['eu100'], [([f'ROBO_{lang}.CPK'], '100') for lang in LANGUAGES], False),
    'xbox-prompts-jp-1.00.dcpx': (['jp100'], [(['ROBO.CPK'], 'jp100')], True),
    'xbox-prompts-jp-1.03.dcpx': (['jp103', 'jp100'], [(['ROBO_PATCH2.CPK', 'ROBO_PATCH.CPK', 'ROBO.CPK'], 'jp103')], True),
    'xbox-prompts-jp-1.04.dcpx': (['jp104', 'jp100'], [(['ROBO_PATCH.CPK', 'ROBO.CPK'], 'jp104')], True),
    'xbox-prompts-asia-1.00.dcpx': (['asia100'], [(['ROBO_CN.CPK'], 'cn'), (['ROBO_TW.CPK', 'ROBO_CN.CPK'], 'tw')], True),
}


class Archives:
    """The archives of the packages under a folder, read on demand."""

    def __init__(self, root):
        self.root, self.open, self.cache = Path(root), {}, {}

    def path(self, folders, name):
        return next((self.root / f / name for f in folders if (self.root / f / name).is_file()), None)

    def entry(self, path, name):
        """The entry's bytes, or None when the archive does not hold it."""
        key = (str(path), name.lower())
        if key not in self.cache:
            if path not in self.open:
                self.open[path] = cpk.Cpk(path)
            found = [e for e in self.open[path].entries if e['path'].lower() == name.lower()]
            self.cache[key] = self.open[path].read(found[0]) if found else None
        return self.cache[key]

    def names(self, path):
        if path not in self.open:
            self.open[path] = cpk.Cpk(path)
        return [e['path'] for e in self.open[path].entries]


def english_templates(archives, work):
    path = archives.path(['usa100', 'eu100'], 'ROBO_US.CPK')
    if path is None:
        return None
    pages = [page_rgb(archives.entry(path, f'Manual/Manual{n}.ftx')) for n in ('05', '06')]
    return make_templates(pages[0], pages[1], work)


def check_archive_patch(archives, patch, templates):
    folders, views, aligned = ARCHIVE_BUILDS[patch]
    key = patch.replace('xbox-prompts-', '').replace('.dcpx', '')
    targets = read_patch(PROMPTS / patch)
    names = sorted({t['archive'] for t in targets} | {a for stack, _ in views for a in stack})
    paths = {name: archives.path(folders, name) for name in names}
    missing = [name for name, path in paths.items() if path is None]
    if missing:
        print(f'skipped {key}: no {", ".join(missing)} under {archives.root} ({", ".join(folders)})')
        return False
    # The patch applied to the original archives: each target once per archive, the original before, the recorded
    # result after, and runs only inside the texture it repaints.
    listed = [(t['archive'].lower(), t['name'].lower()) for t in targets]
    check(len(listed) == len(set(listed)), f'{key}: an entry is listed twice for an archive')
    results = {}
    for t in targets:
        where = f"{key}: {t['archive']} {t['name']}"
        original = archives.entry(paths[t['archive']], t['name'])
        check(original is not None, f'{where} is not in the archive')
        if original is None:
            continue
        check(len(original) == t['size'] and zlib.crc32(original) == t['before'], f'{where} is not the original file')
        result = apply_target(t, original)
        check(zlib.crc32(result) == t['after'], f'{where}: the result does not match its recorded CRC32')
        textures = ftex.textures(original)
        texture = next(x for x in textures if x['name'] == (ATLAS if 'interface' in t['name'].lower() else textures[0]['name']))
        inside = all(texture['data'] <= o and o + 16 <= texture['data'] + texture['size'] and o % 16 == 0 for o, _ in t['runs'])
        check(inside, f'{where}: the patch writes outside the texture it repaints')
        results[(t['archive'].lower(), t['name'].lower())] = (original, result)
    summary = {}
    for t in targets:
        summary.setdefault(t['archive'], []).append(t['name'].split('/')[-1])
    print(f'prompt art {key}: ' + '; '.join(f'{a}: {", ".join(n)}' for a, n in summary.items()))

    pages_checked = icons = 0
    for stack, layout in views:
        view = f'{key} {stack[0]}'

        def visible(entry):
            """(archive, bytes) of the entry the build reads: the first archive of the stack that holds it."""
            for a in stack:
                data = archives.entry(paths[a], entry)
                if data is not None:
                    return a, data
            return None, None

        atlas_archive, atlas_blob = visible('GUI/Interface.ftx')
        _, mbs = visible('GUI/Interface.mbs')
        patched = results.get((atlas_archive.lower(), 'gui/interface.ftx'))
        check(patched is not None, f'{view}: the atlas the build reads ({atlas_archive}) is not patched')
        if patched:
            uv = draw_xbox.quads(mbs)
            before, after = decode_bytes(patched[0]), decode_bytes(patched[1])
            check_symbols(before, uv, layout, view)
            check_atlas_images(before, after, uv, layout, view)
            check_button_part_images(Layout(mbs), before, after, view)
        if templates is None:
            print(f'prompt art {view}: manual not checked (no English pages: ROBO_US.CPK under usa100 or eu100)')
            continue
        pages = sorted({n.split('/')[-1] for a in stack for n in archives.names(paths[a])
                        if n.lower().startswith('manual/manual') and n.lower().endswith('.ftx')})
        for page in pages:
            owner, blob = visible(f'Manual/{page}')
            before = page_rgb(blob)
            found = icons_on(before, templates, aligned)
            patched_page = results.get((owner.lower(), f'manual/{page}'.lower()))
            if not found:
                check(patched_page is None, f'{view}: {owner} {page} shows no PlayStation icon yet is patched')
                continue
            check(patched_page is not None, f'{view}: {owner} {page} shows {len(found)} PlayStation icon(s) and is not patched')
            if patched_page:
                icons += check_manual_page(f'{owner} {page}', before, page_rgb(patched_page[1]), templates, view, aligned)
                pages_checked += 1
    if templates is not None:
        print(f'prompt art {key}: {pages_checked} manual pages read by the build, {icons} icons replaced, none left')
    return True


def main():
    args = sys.argv[1:]
    archive_root = os.environ.get('TSR_PROMPT_ARCHIVES')
    if '--archives' in args:
        i = args.index('--archives')
        archive_root = args[i + 1]
        del args[i:i + 2]
    folder = Path(args[0]) if args else None
    ran = False
    import tempfile
    if folder and (folder / 'templates').is_dir() and (folder / '100').is_dir():
        ran = True
        with tempfile.TemporaryDirectory() as work:
            pages = [page_rgb((folder / 'templates' / name).read_bytes()) for name in ('Manual_Manual05.ftx', 'Manual_Manual06.ftx')]
            templates = make_templates(pages[0], pages[1], Path(work))
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
    if archive_root and Path(archive_root).is_dir():
        archives = Archives(archive_root)
        with tempfile.TemporaryDirectory() as work:
            templates = english_templates(archives, Path(work))
        for patch in ARCHIVE_BUILDS:
            ran |= check_archive_patch(archives, patch, templates)
    if not ran:
        print('skipped: no exported entries (run the Packages group with TSR_TEST_PACKAGES set first) and no archives '
              '(--archives or TSR_PROMPT_ARCHIVES)')
        return 2
    print(f'prompt art: {checks} checks, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
