#!/usr/bin/env python3
"""Looks at the art of the Xbox patches: the entries tsr-game-profile-tests exported before and after the patch (real
archives of the packages) are decoded and every PlayStation variant is checked to be gone and the rest to be intact.

    prompt_art_tests.py build/test-prompt-art

Needs Python with numpy, Pillow and scipy (tools\\setup.ps1 -InstallPython). Exits 2 when there is nothing to check.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.signal import fftconvolve

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'scripts' / 'prompts'))
import draw_manual  # noqa: E402
import draw_xbox  # noqa: E402
import ftex  # noqa: E402

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


def check_atlas(root, version, key):
    before = decode(root / 'original' / ('GUI_Interface.ftx' if version == '100' else 'gui_interface.ftx'), ATLAS)
    after = decode(root / 'patched' / ('GUI_Interface.ftx' if version == '100' else 'gui_interface.ftx'), ATLAS)
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
    for version in ('100', '102'):
        root = folder / version
        if not root.is_dir():
            print(f'skipped: no exported entries for {version}')
            continue
        check_atlas(root, version, version)
        if version == '100':
            check_manual(root, templates, version)
    print(f'prompt art: {checks} checks, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
