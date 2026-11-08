#!/usr/bin/env python3
"""Builds launcher/prompts/xbox-prompts-1.00.dcpx (used by 1.00 and 1.01) and xbox-prompts-1.02.dcpx from the original
archive entries.

    build_prompts.py ORIG_DIR OUT_DIR --font Poppins-Bold.ttf --work WORK_DIR

ORIG_DIR holds, per language L in US FR GE IT SP, the original entries cut out of the package's archives with cpk.py:
    ORIG_DIR/L/GUI/Interface.ftx and Interface.mbs, ORIG_DIR/L/Manual/ManualNN.ftx   (ROBO_L.CPK of the base game)
    ORIG_DIR/pL/gui/interface.ftx and interface.mbs                                   (ROBO_PATCH2_L.CPK of 1.02)
The patch lists every changed entry per archive; the launcher refuses an archive that does not hold the original.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import draw_manual
import draw_xbox
import ftex
import make_dcpx
import make_manual_templates
import patch_ftx

LANGUAGES = ['US', 'FR', 'GE', 'IT', 'SP']
ATLAS = 'HD_Interface_arranged000.gnf'


def patched_pack(blob, texture, rgba, changed):
    out, _ = patch_ftx.patch(blob, texture, rgba, changed)
    return out


def atlas_targets(blob, mbs, version, font, work, archive, entry):
    tex = next(t for t in ftex.textures(blob) if t['name'] == ATLAS)
    rgba = np.array(ftex.decode(blob, tex).convert('RGBA'))
    edited = rgba.copy()
    changed = draw_xbox.paint(edited, mbs, version, font) | (edited != rgba).any(-1)
    return patched_pack(blob, tex, edited, changed)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('orig')
    ap.add_argument('out')
    ap.add_argument('--font', required=True)
    ap.add_argument('--work', required=True)
    args = ap.parse_args()
    orig, out, work = Path(args.orig), Path(args.out), Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)

    page06 = ftex.decode((orig / 'US/Manual/Manual05.ftx').read_bytes(), ftex.textures((orig / 'US/Manual/Manual05.ftx').read_bytes())[0])
    page07 = ftex.decode((orig / 'US/Manual/Manual06.ftx').read_bytes(), ftex.textures((orig / 'US/Manual/Manual06.ftx').read_bytes())[0])
    page06.convert('RGB').save(work / 'page06.png')
    page07.convert('RGB').save(work / 'page07.png')
    sys.argv = ['make_manual_templates', str(work / 'page06.png'), str(work / 'page07.png'), str(work / 'templates')]
    make_manual_templates.main()
    templates = draw_manual.load_templates(work / 'templates')

    manual = []
    for lang in LANGUAGES:
        archive = f'ROBO_{lang}.CPK'
        for path in sorted((orig / lang / 'Manual').glob('Manual*.ftx')):
            blob = path.read_bytes()
            tex = ftex.textures(blob)[0]
            rgb = np.array(ftex.decode(blob, tex).convert('RGB'))
            edited = rgb.copy()
            done = draw_manual.paint(edited, templates, args.font)
            if not done:
                continue
            changed = (edited != rgb).any(-1)
            rgba = np.dstack([edited, np.full(edited.shape[:2], 255, np.uint8)])
            manual.append((archive, f'Manual/{path.name}', blob, patched_pack(blob, tex, rgba, changed)))
            print(f'{lang} {path.name}: {len(done)} icons')

    base = []
    for lang in LANGUAGES:
        blob = (orig / lang / 'GUI/Interface.ftx').read_bytes()
        base.append((f'ROBO_{lang}.CPK', 'GUI/Interface.ftx', blob,
                     atlas_targets(blob, orig / lang / 'GUI/Interface.mbs', '100', args.font, work, None, None)))
    update = []
    for lang in LANGUAGES:
        blob = (orig / f'p{lang}' / 'gui/interface.ftx').read_bytes()
        update.append((f'ROBO_PATCH2_{lang}.CPK', 'gui/interface.ftx', blob,
                       atlas_targets(blob, orig / f'p{lang}' / 'gui/interface.mbs', '102', args.font, work, None, None)))

    print(make_dcpx.write_patch(out / 'xbox-prompts-1.00.dcpx', base + manual), 'bytes: 1.00 and 1.01')
    print(make_dcpx.write_patch(out / 'xbox-prompts-1.02.dcpx', update + manual), 'bytes: 1.02')


if __name__ == '__main__':
    main()
