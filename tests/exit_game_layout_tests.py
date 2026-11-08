#!/usr/bin/env python3
"""Exit Game on the real title files: assets/exit_game/title-labels.bin applied as the game applies it (every replaced
byte checked first) to each language's GUI/title.mbs and GUI/title.ftx, from the 1.00 base archives and the 1.02
update. Checks that the three-row parts draw exactly three rows at the menu's own pitch with Exit Game third, that the
selected and confirmed states follow the third row, that nothing the two-row menu draws changes, that each language's
data fits its own files only, and that a changed byte stops the patch. Every keyframe of the three-row parts is checked:
text draws sample only the language's own rows or the new label, and the third row's write-on frames light only the
label's letters under its plate.

    exit_game_layout_tests.py <1.00 folder> [<1.02 folder>]      exit code 2 when the folders lack the archives
"""
import io
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
sys.path.insert(0, str(ROOT / 'scripts' / 'exit_game'))
import ftex  # noqa: E402
from cpk import Cpk  # noqa: E402
import title_layout as tl  # noqa: E402

SUFFIX = {'english': 'US', 'french': 'FR', 'german': 'GE', 'italian': 'IT', 'spanish': 'SP'}
THREE_ROW = ['NEW_GAME_P00', 'LOAD_GAME_P00', 'EXTRA_GAME_P00', 'NEW_GAME_P01', 'LOAD_GAME_P01', 'EXTRA_GAME_P01']
checks = failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print(f'FAIL: {what}')


def parse_asset(data):
    assert data[:8] == b'TSRXGL01' and zlib.crc32(data[:-4]) == struct.unpack_from('<I', data, len(data) - 4)[0]
    pos, langs = 12, []
    for _ in range(struct.unpack_from('<I', data, 8)[0]):
        lang = dict(key=data[pos:pos + 12].rstrip(b'\0').decode())
        pos += 12
        lang['width'], lang['height'], lang['pitch'], lang['size'] = struct.unpack_from('<4I', data, pos)
        pos += 16
        *lang['reference'], lang['reference_crc'] = struct.unpack_from('<5I', data, pos)
        pos += 20
        regions, count = [], struct.unpack_from('<I', data, pos)[0]
        pos += 4
        for _ in range(count):
            bx, by, bw, bh, crc, pool_n, run_n = struct.unpack_from('<7I', data, pos)
            pos += 28
            pool = [data[pos + 16 * i:pos + 16 * i + 16] for i in range(pool_n)]
            pos += 16 * pool_n
            runs = [struct.unpack_from('<HH', data, pos + 4 * i) for i in range(run_n)]
            pos += 4 * run_n
            blocks = [pool[i] for i, n in runs for _ in range(n)]
            regions.append(dict(rect=(bx, by, bw, bh), crc=crc, blocks=blocks))
        lang['regions'] = regions
        lang['sections'] = struct.unpack_from('<11I', data, pos)
        lang['layout_size'] = struct.unpack_from('<I', data, pos + 44)[0]
        pos += 48
        edits, count = [], struct.unpack_from('<I', data, pos)[0]
        pos += 4
        for _ in range(count):
            sec, n, _, off = struct.unpack_from('<BBHI', data, pos)
            edits.append((sec, off, data[pos + 8:pos + 8 + n], data[pos + 8 + n:pos + 8 + 2 * n]))
            pos += 8 + 2 * n
        lang['edits'] = edits
        langs.append(lang)
    assert pos == len(data) - 4
    return langs


def patch_layout(lang, data):
    """As exit_game_layout_patch: 'other', 'mismatch', or the patched bytes."""
    lay = tl.Layout(data)
    if list(lay.offsets) != list(lang['sections']) or len(data) != lang['layout_size']:
        return 'other'
    out = bytearray(data)
    for sec, off, old, new in lang['edits']:
        o = lang['sections'][sec] + off
        if out[o:o + len(old)] != old:
            return 'mismatch'
    for sec, off, old, new in lang['edits']:
        o = lang['sections'][sec] + off
        out[o:o + len(new)] = new
    return bytes(out)


def blocks(pixels, pitch, rect):
    bx, by, bw, bh = rect
    return [bytes(pixels[o:o + 16]) for o in (ftex.tiled_offset(x, y, pitch // 4) for y in range(by, by + bh) for x in range(bx, bx + bw))]


def patch_texture(lang, tex, pixels):
    if (tex['width'], tex['height'], tex['pitch'], tex['size']) != (lang['width'], lang['height'], lang['pitch'], lang['size']):
        return 'other'
    if zlib.crc32(b''.join(blocks(pixels, tex['pitch'], lang['reference']))) != lang['reference_crc']:
        return 'other'
    if any(zlib.crc32(b''.join(blocks(pixels, tex['pitch'], r['rect']))) != r['crc'] for r in lang['regions']):
        return 'mismatch'
    out = bytearray(pixels)
    for r in lang['regions']:
        bx, by, bw, bh = r['rect']
        for k, b in enumerate(r['blocks']):
            o = ftex.tiled_offset(bx + k % bw, by + k // bw, tex['pitch'] // 4)
            out[o:o + 16] = b
    return bytes(out)


def drawn(lay, part, layer_index):
    layer = lay.part_layers(part)[layer_index]
    out = []
    for e in sorted(set(lay.layer_elements(layer))):
        out.append([(s, lay.bounds(tl.POSITIONS, s['pos']), lay.bounds(tl.UVS, s['uv'])) for s in lay.element_sprites(e)
                    if s['kind'] == tl.TEXTURED and lay.bounds(tl.POSITIONS, s['pos'])[2] > lay.bounds(tl.POSITIONS, s['pos'])[0]])
    return out


def two_row_draws(lay):
    """Every quad and sprite the parts outside the three-row menu draw."""
    three = {lay.part_index(n) for n in THREE_ROW}
    users = lay.users(set(range(lay.count(tl.PARTS))) - three)
    out = {}
    for i in sorted(users):
        s = lay.sprite(i)
        out[i] = (s, lay.quad(tl.POSITIONS, s['pos']), lay.quad(tl.UVS, s['uv']) if s['kind'] == tl.TEXTURED else None)
    return out


def check_layout(key, original, patched, label_uv_rects):
    a, b = tl.Layout(original), tl.Layout(patched)
    check(two_row_draws(a) == two_row_draws(b), f'{key}: the two-row menu and the rest of the title are unchanged')
    new00 = a.part_index('NEW_GAME_00')
    ys = sorted({round((p[1] + p[3]) / 2, 1) for frame in drawn(a, new00, 0) for _, p, _ in frame})
    pitch = ys[-1] - ys[0]
    three = [b.part_index(n) for n in THREE_ROW]
    rows_seen = set()
    for k, part in enumerate(three):
        selected, confirmed = k % 3, k >= 3
        for frame in drawn(b, part, 0):
            rows = {}
            for s, p, _ in frame:
                row = round(((p[1] + p[3]) / 2 - ys[0]) / pitch)
                check(abs((p[1] + p[3]) / 2 - ys[0] - row * pitch) < 1.5, f'{key} {THREE_ROW[k]}: row {row} on the menu pitch')
                rows.setdefault(row, []).append(s)
            want = {r for r in range(3) if not (confirmed and r == selected)}
            check(set(rows) == want, f'{key} {THREE_ROW[k]}: rows drawn {sorted(rows)} want {sorted(want)}')
            for r, sprites in rows.items():
                n = 2 if (r == selected and not confirmed) else 1  # the selected row adds its shadow
                words = len({x['uv'] for x in sprites})
                check(len({x['pos'] for x in sprites}) == n * words, f'{key} {THREE_ROW[k]}: row {r} drawn {len(sprites)} times')
                rows_seen.add(r)
        plates = drawn(b, part, 1)
        for frame in plates:
            check(len(frame) >= 1, f'{key} {THREE_ROW[k]}: a plate in every frame')
            if frame:
                plate_row = round(((frame[0][1][1] + frame[0][1][3]) / 2 - ys[0]) / pitch)
                check(plate_row == selected, f'{key} {THREE_ROW[k]}: the plate is on row {selected} (is {plate_row})')
    third = [p for frame in drawn(b, three[2], 1) for _, p, _ in frame]
    third_uv = {u for frame in drawn(b, three[2], 1) for _, _, u in frame} | {u for frame in drawn(b, three[0], 0) for s, p, u in frame
                                                                               if round(((p[1] + p[3]) / 2 - ys[0]) / pitch) == 2}
    for u in third_uv:
        check(any(abs(u[0] - r[0]) < 1e-6 and abs(u[1] - r[1]) < 1e-6 for r in label_uv_rects), f'{key}: the third row samples the new label')
    check(rows_seen == {0, 1, 2}, f'{key}: rows 1 to 3 drawn')
    return pitch


def region_rgba(region):
    bx, by, bw, bh = region['rect']
    rows = b''.join(b''.join(region['blocks'][y * bw:(y + 1) * bw]) for y in range(bh))
    return np.array(Image.open(io.BytesIO(ftex.dds_wrap(rows, bw * 4, bh * 4, 41))).convert('RGBA'))


def sampled_texels(lay, s, tex):
    u0, v0, u1, v1 = lay.bounds(tl.UVS, s['uv'])
    return round(u0 * tex['width']), round(v0 * tex['height']), round(u1 * tex['width']), round(v1 * tex['height'])


def inside(rect, outer):
    return outer[0] <= rect[0] and outer[1] <= rect[1] and rect[2] <= outer[2] and rect[3] <= outer[3]


def overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def visible(lay, s):
    x0, y0, x1, y1 = lay.bounds(tl.POSITIONS, s['pos'])
    return x1 > x0 and y1 > y0


def check_animation(key, original, patched, lang, tex):
    """Every keyframe of the three-row parts: what each draw samples, and the third row's write-on frames."""
    a, b = tl.Layout(original), tl.Layout(patched)
    label_rects = [(r['rect'][0] * 4, r['rect'][1] * 4, (r['rect'][0] + r['rect'][2]) * 4, (r['rect'][1] + r['rect'][3]) * 4)
                   for r in lang['regions']]
    check(not overlaps(*label_rects), f'{key}: the label regions do not overlap')
    used = [sampled_texels(a, a.sprite(i), tex) for i in range(a.count(tl.SPRITES))
            if a.sprite(i)['kind'] == tl.TEXTURED and a.sprite(i)['tex'] == 4]
    check(not any(overlaps(r, u) for r in label_rects for u in used), f'{key}: the label regions lie in atlas space no sprite samples')
    check({sec for sec, *_ in lang['edits']} <= {tl.UVS, tl.POSITIONS, tl.SPRITES, tl.ELEMENTS, tl.KEYFRAMES, tl.LAYERS},
          f'{key}: transforms, colours and parts keep their original records')
    for ref, name in (('LOAD_GAME_P00', 'EXTRA_GAME_P00'), ('LOAD_GAME_P01', 'EXTRA_GAME_P01')):
        durations = lambda lay, n: [[struct.unpack_from('<H', lay.d, lay.at(tl.KEYFRAMES, k, 6))[0] for k in range(*(lambda s, c: (s, s + c))(
            *struct.unpack_from('<HH', lay.d, lay.at(tl.LAYERS, L))))] for L in lay.part_layers(lay.part_index(n))]
        check(durations(b, name) == durations(b, ref), f'{key} {name}: frame durations match {ref}')
        check(durations(a, ref) == durations(b, ref), f'{key} {ref}: its own timing is unchanged')
        for layer in (0, 1):
            tr = lambda lay, n: lay.d[lay.at(tl.LAYERS, lay.part_layers(lay.part_index(n))[layer], 4):lay.at(tl.LAYERS, lay.part_layers(lay.part_index(n))[layer], 24)]
            check(tr(b, name) == tr(b, ref), f'{key} {name} layer {layer}: the layer timing fields match {ref}')
            check(sum(durations(b, name)[layer]) == sum(durations(b, ref)[layer]), f'{key} {name} layer {layer}: total duration matches {ref}')
    two_row = {a.part_index('NEW_GAME_00'), a.part_index('LOAD_GAME_00')}
    own_rows = {sampled_texels(a, a.sprite(i), tex) for i in a.users(two_row)
                if a.sprite(i)['kind'] == tl.TEXTURED and a.sprite(i)['tex'] == 4}
    three = [b.part_index(n) for n in THREE_ROW]
    stale = changed = 0
    for p in three:
        for layer in b.part_layers(p):
            for e in set(b.layer_elements(layer)):
                for s in b.element_sprites(e):
                    if s['kind'] != tl.TEXTURED or not visible(b, s):
                        continue
                    if s['tex'] == 4:
                        t = sampled_texels(b, s, tex)
                        stale += not (t in own_rows or any(inside(t, r) for r in label_rects))
                    else:
                        o = a.sprite(s['index'])
                        changed += (o, a.quad(tl.POSITIONS, o['pos']), a.quad(tl.UVS, o['uv'])) != \
                                   (s, b.quad(tl.POSITIONS, s['pos']), b.quad(tl.UVS, s['uv']))
    check(stale == 0, f'{key}: every three-row frame samples the language\'s own rows or the new label ({stale} stale draws)')
    check(changed == 0, f'{key}: the three-row parts\' other artwork is drawn as before ({changed} changed)')
    for name in ('NEW_GAME_P00', 'LOAD_GAME_P00', 'NEW_GAME_P01', 'LOAD_GAME_P01'):
        pa, pb = a.part_index(name), b.part_index(name)
        frames = lambda lay, p: [[(s, lay.quad(tl.POSITIONS, s['pos']), lay.quad(tl.UVS, s['uv'])) for s in lay.element_sprites(e)]
                                 for e in lay.layer_elements(lay.part_layers(p)[1])]
        check(frames(a, pa) == frames(b, pb), f'{key} {name}: the first two rows keep their write-on frames')

    art = region_rgba(lang['regions'][0])
    lab, _ = ndimage.label(art[:, :, 3] < 250)
    edge = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    outside = np.isin(lab, edge[edge > 0])
    white = {c for c in range(b.count(tl.COLOURS)) if bytes(b.d[b.at(tl.COLOURS, c):b.at(tl.COLOURS, c) + 24]) == b'\xff' * 24}
    lit_frames = 0
    for p in (three[2], three[5]):
        for e in set(b.layer_elements(b.part_layers(p)[1])):
            *strokes, plate = b.element_sprites(e)
            check(plate['kind'] == tl.TEXTURED and inside(sampled_texels(b, plate, tex), label_rects[0]),
                  f'{key} {THREE_ROW[three.index(p)]} frame {e}: the label plate is drawn last')
            check(all(s['kind'] != tl.TEXTURED for s in strokes), f'{key} frame {e}: only the plate samples the atlas')
            px0, py0, px1, py1 = b.bounds(tl.POSITIONS, plate['pos'])
            u0, v0, u1, v1 = sampled_texels(b, plate, tex)
            ox, oy = u0 - label_rects[0][0], v0 - label_rects[0][1]
            sx, sy = (u1 - u0) / (px1 - px0), (v1 - v0) / (py1 - py0)
            lit = [s for s in strokes if visible(b, s)]
            lit_frames += bool(lit)
            for s in lit:
                x0, y0, x1, y1 = b.bounds(tl.POSITIONS, s['pos'])
                check(s['colour'] in white and px0 <= x0 and x1 <= px1 and py0 <= y0 and y1 <= py1,
                      f'{key} frame {e}: a white stroke inside the plate')
                c0, c1 = ox + round((x0 - px0) * sx), ox + round((x1 - px0) * sx)
                r0, r1 = oy + round((y0 - py0) * sy), oy + round((y1 - py0) * sy)
                check(not outside[r0:r1, c0:c1].any(), f'{key} frame {e}: a stroke shows only through the label\'s letters')
    check(lit_frames >= 10, f'{key}: the third row writes on over its frames ({lit_frames} lit)')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    base = Path(sys.argv[1])
    update = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    if not (base / 'ROBO_US.CPK').exists():
        print(f'no ROBO_US.CPK in {base}')
        return 2
    asset = parse_asset((ROOT / 'assets' / 'exit_game' / 'title-labels.bin').read_bytes())
    check([l['key'] for l in asset] == list(SUFFIX), 'the five text languages, in order')
    files = {}
    for lang in asset:
        cpk = Cpk(base / f'ROBO_{SUFFIX[lang["key"]]}.CPK')
        get = lambda c, n: c.read(next(e for e in c.entries if e['path'].lower() == n))
        mbs = [get(cpk, 'gui/title.mbs')]
        if update and (update / f'ROBO_PATCH2_{SUFFIX[lang["key"]]}.CPK').exists():
            mbs.append(get(Cpk(update / f'ROBO_PATCH2_{SUFFIX[lang["key"]]}.CPK'), 'gui/title.mbs'))
        files[lang['key']] = (mbs, get(cpk, 'gui/title.ftx'))
    for lang in asset:
        key = lang['key']
        mbs, ftx = files[key]
        tex = next(t for t in ftex.textures(ftx) if t['name'] == '4K_title_arranged043.gnf')
        pixels = ftx[tex['data']:tex['data'] + tex['size']]
        patched_tex = patch_texture(lang, tex, pixels)
        check(isinstance(patched_tex, bytes), f'{key}: the label art fits the {key} text atlas')
        if isinstance(patched_tex, bytes):
            check(patch_texture(lang, tex, patched_tex) == 'mismatch', f'{key}: a patched atlas is not patched twice')
            touched = set()
            for r in lang['regions']:
                bx, by, bw, bh = r['rect']
                touched |= {ftex.tiled_offset(x, y, tex['pitch'] // 4) for y in range(by, by + bh) for x in range(bx, bx + bw)}
            changed = {o for o in range(0, len(pixels), 16) if pixels[o:o + 16] != patched_tex[o:o + 16]}
            check(changed <= touched, f'{key}: only the label blocks change in the atlas')
            damaged = bytearray(pixels)
            r0 = lang['regions'][0]['rect']
            o = ftex.tiled_offset(r0[0] + 1, r0[1] + 1, tex['pitch'] // 4)
            damaged[o] ^= 1
            check(patch_texture(lang, tex, bytes(damaged)) == 'mismatch', f'{key}: changed atlas blocks stop the patch')
        rects = []
        for r in lang['regions']:
            bx, by = r['rect'][:2]
            rects.append((bx * 4 / tex['width'], by * 4 / tex['height']))
        for m in mbs:
            out = patch_layout(lang, m)
            check(isinstance(out, bytes), f'{key}: the layout edits fit the {key} title layout ({"1.02" if m[0x12] == 8 else "1.00/1.01"})')
            if isinstance(out, bytes):
                pitch = check_layout(key, m, out, rects)
                check_animation(key, m, out, lang, tex)
                check(out[:0x100] == m[:0x100], f'{key}: the layout header is unchanged')
                check(patch_layout(lang, out) == 'mismatch', f'{key}: a patched layout is not patched twice')
                damaged = bytearray(m)
                sec, off, old, _ = lang['edits'][len(lang['edits']) // 2]
                damaged[lang['sections'][sec] + off] ^= 1
                check(patch_layout(lang, bytes(damaged)) == 'mismatch', f'{key}: a changed layout record stops the patch')
        for other in asset:
            if other is lang:
                continue
            check(patch_layout(other, mbs[0]) == 'other', f'{other["key"]} data does not fit the {key} layout')
            check(patch_texture(other, tex, pixels) == 'other', f'{other["key"]} data does not fit the {key} atlas')
        print(f'exit game {key}: three rows at a {pitch:.0f}-unit pitch, {len(lang["edits"])} layout records, '
              f'{sum(len(r["blocks"]) for r in lang["regions"])} atlas blocks')
    print(f'exit game layout: {checks} checks, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
