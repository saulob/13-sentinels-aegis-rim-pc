#!/usr/bin/env python3
"""Builds the Xbox prompt patches (launcher/prompts/xbox-prompts-*.dcpx) of one region from the original archive entries.

    build_prompts.py ORIG_DIR OUT_DIR --font Poppins-Bold.ttf --work WORK_DIR [--region usa|eu|jp|asia] [--english DIR]

ORIG_DIR holds the entries cut out of the region's package archives with cpk.py, one folder per archive:
    usa, eu   L/GUI/Interface.ftx and .mbs, L/Manual/ManualNN.ftx for L in US FR GE IT SP     (ROBO_L.CPK of the base game)
              usa also pL/gui/interface.ftx and .mbs                                         (ROBO_PATCH2_L.CPK of 1.02)
    jp        base/GUI/Interface.ftx and .mbs, base/Manual/   (ROBO.CPK of 1.00)
              p103/Manual/                                    (ROBO_PATCH.CPK of 1.03)
              p103-2/GUI/Interface.mbs                        (ROBO_PATCH2.CPK of 1.03)
              p104/GUI/Interface.ftx and .mbs, p104/Manual/   (ROBO_PATCH.CPK of 1.04)
    asia      CN/GUI/..., CN/Manual/ and TW/GUI/..., TW/Manual/  (ROBO_CN.CPK and ROBO_TW.CPK)
The manual icons are matched against the English pages 06 and 07: --english names a folder holding Manual05.ftx and
Manual06.ftx of ROBO_US.CPK (default ORIG_DIR/US/Manual). Each patch lists, per archive, the entries the build reads that
carry button art; the launcher refuses an archive that does not hold the original.
"""
import argparse
import sys
from pathlib import Path

import numpy as np

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


def atlas_target(archive, entry, ftx, mbs, layout, font):
    blob = Path(ftx).read_bytes()
    tex = next(t for t in ftex.textures(blob) if t['name'] == ATLAS)
    rgba = np.array(ftex.decode(blob, tex).convert('RGBA'))
    edited = rgba.copy()
    changed = draw_xbox.paint(edited, mbs, layout, font) | (edited != rgba).any(-1)
    return archive, entry, blob, patched_pack(blob, tex, edited, changed)


def manual_targets(archive, folder, templates, font, aligned=False, skip=(), label=''):
    out = []
    for path in sorted(Path(folder).glob('Manual*.ftx')):
        if path.name.lower() in skip:
            continue
        blob = path.read_bytes()
        tex = ftex.textures(blob)[0]
        rgb = np.array(ftex.decode(blob, tex).convert('RGB'))
        edited = rgb.copy()
        done = draw_manual.paint(edited, templates, font, aligned)
        if not done:
            continue
        changed = (edited != rgb).any(-1)
        rgba = np.dstack([edited, np.full(edited.shape[:2], 255, np.uint8)])
        out.append((archive, f'Manual/{path.name}', blob, patched_pack(blob, tex, rgba, changed)))
        print(f'{label or archive} {path.name}: {len(done)} icons')
    return out


def page_names(folder):
    return {p.name.lower() for p in Path(folder).glob('Manual*.ftx')}


def load_templates(english, work):
    pages = []
    for number, name in ((6, 'Manual05.ftx'), (7, 'Manual06.ftx')):
        blob = (Path(english) / name).read_bytes()
        page = work / f'page{number:02d}.png'
        ftex.decode(blob, ftex.textures(blob)[0]).convert('RGB').save(page)
        pages.append(str(page))
    sys.argv = ['make_manual_templates', *pages, str(work / 'templates')]
    make_manual_templates.main()
    return draw_manual.load_templates(work / 'templates')


def build_usa(orig, templates, font):
    manual = []
    for lang in LANGUAGES:
        manual += manual_targets(f'ROBO_{lang}.CPK', orig / lang / 'Manual', templates, font, label=lang)
    base = [atlas_target(f'ROBO_{lang}.CPK', 'GUI/Interface.ftx', orig / lang / 'GUI/Interface.ftx',
                         orig / lang / 'GUI/Interface.mbs', '100', font) for lang in LANGUAGES]
    update = [atlas_target(f'ROBO_PATCH2_{lang}.CPK', 'gui/interface.ftx', orig / f'p{lang}' / 'gui/interface.ftx',
                           orig / f'p{lang}' / 'gui/interface.mbs', '102', font) for lang in LANGUAGES]
    return {'xbox-prompts-1.00.dcpx': base + manual, 'xbox-prompts-1.02.dcpx': update + manual}


def build_eu(orig, templates, font):
    manual = []
    for lang in LANGUAGES:
        manual += manual_targets(f'ROBO_{lang}.CPK', orig / lang / 'Manual', templates, font, label=lang)
    base = [atlas_target(f'ROBO_{lang}.CPK', 'GUI/Interface.ftx', orig / lang / 'GUI/Interface.ftx',
                         orig / lang / 'GUI/Interface.mbs', '100', font) for lang in LANGUAGES]
    return {'xbox-prompts-eu-1.00.dcpx': base + manual}


def build_jp(orig, templates, font):
    # The updates are mounted over ROBO.CPK: a page an update carries hides the base one.
    base_ftx = orig / 'base/GUI/Interface.ftx'
    atlas100 = atlas_target('ROBO.CPK', 'GUI/Interface.ftx', base_ftx, orig / 'base/GUI/Interface.mbs', 'jp100', font)
    atlas103 = atlas_target('ROBO.CPK', 'GUI/Interface.ftx', base_ftx, orig / 'p103-2/GUI/Interface.mbs', 'jp103', font)
    assert atlas103[3] == atlas100[3], 'ROBO.CPK is shared by the JPN builds: its atlas must read the same in each'
    atlas104 = atlas_target('ROBO_PATCH.CPK', 'GUI/Interface.ftx', orig / 'p104/GUI/Interface.ftx',
                            orig / 'p104/GUI/Interface.mbs', 'jp104', font)
    pages = {target[1]: target for target in manual_targets('ROBO.CPK', orig / 'base/Manual', templates, font, True, label='base')}
    hidden103, hidden104 = page_names(orig / 'p103/Manual'), page_names(orig / 'p104/Manual')
    base_pages = lambda hidden: [t for name, t in pages.items() if name.split('/')[-1].lower() not in hidden]
    p103 = manual_targets('ROBO_PATCH.CPK', orig / 'p103/Manual', templates, font, True, label='1.03')
    p104 = manual_targets('ROBO_PATCH.CPK', orig / 'p104/Manual', templates, font, True, label='1.04')
    return {'xbox-prompts-jp-1.00.dcpx': [atlas100] + list(pages.values()),
            'xbox-prompts-jp-1.03.dcpx': [atlas103] + base_pages(hidden103) + p103,
            'xbox-prompts-jp-1.04.dcpx': [atlas104] + base_pages(hidden104) + p104}


def build_asia(orig, templates, font):
    atlases = [atlas_target(f'ROBO_{key}.CPK', 'GUI/Interface.ftx', orig / key / 'GUI/Interface.ftx',
                            orig / key / 'GUI/Interface.mbs', key.lower(), font) for key in ('CN', 'TW')]
    manual = []
    for key in ('CN', 'TW'):
        manual += manual_targets(f'ROBO_{key}.CPK', orig / key / 'Manual', templates, font, True, label=key)
    return {'xbox-prompts-asia-1.00.dcpx': atlases + manual}


REGIONS = {'usa': build_usa, 'eu': build_eu, 'jp': build_jp, 'asia': build_asia}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('orig')
    ap.add_argument('out')
    ap.add_argument('--font', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--region', choices=sorted(REGIONS), default='usa')
    ap.add_argument('--english', help='folder with Manual05.ftx and Manual06.ftx of ROBO_US.CPK (default ORIG_DIR/US/Manual)')
    args = ap.parse_args()
    orig, out, work = Path(args.orig), Path(args.out), Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    templates = load_templates(Path(args.english) if args.english else orig / 'US/Manual', work)
    for name, targets in REGIONS[args.region](orig, templates, args.font).items():
        keys = [(archive.lower(), entry.lower()) for archive, entry, _, _ in targets]
        assert len(keys) == len(set(keys)), f'{name}: an entry is listed twice for an archive'
        print(make_dcpx.write_patch(out / name, targets), 'bytes:', name)


if __name__ == '__main__':
    main()
