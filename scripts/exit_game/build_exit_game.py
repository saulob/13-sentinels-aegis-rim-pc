#!/usr/bin/env python3
"""Builds assets/exit_game/title-labels.bin: the Exit Game row of the title menu for the five text languages.

The title already has a third menu row (the unused EXTRA row of the three-row parts *_P00/*_P01). For each language
this makes the row's label (EXIT GAME, QUITTER LE JEU, SPIEL BEENDEN, ESCI DAL GIOCO, SALIR DEL JUEGO) from the title's
own letters, draws it into space of the text atlas no sprite samples, and records the layout edits that point the third
row at it, including the frames of its selection write-on; where a language's three-row parts still show English
placeholder rows they are pointed back at the language's own rows. The game applies the result in memory
(src/exit_game.c), checking every byte it rewrites first.

    build_exit_game.py <folder with ROBO_US/FR/GE/IT/SP.CPK> [--patch2 <1.02 folder>] [--out FILE] [--preview DIR] [--cache DIR]
"""
import argparse
import io
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import bc7  # noqa: E402
import ftex  # noqa: E402
from cpk import Cpk  # noqa: E402
import labels as lb  # noqa: E402
import title_layout as tl  # noqa: E402

TEXT_ATLAS = '4K_title_arranged043.gnf'
TEXT_TEXTURE = 4
PX_PER_UNIT = 5.0
THREE_ROW = ['NEW_GAME_P00', 'LOAD_GAME_P00', 'EXTRA_GAME_P00', 'NEW_GAME_P01', 'LOAD_GAME_P01', 'EXTRA_GAME_P01']
MAGIC = b'TSRXGL01'

# key, archive suffix, label, text of the source rows by UV quad
LANGUAGES = [
    ('english', 'US', 'EXIT', {211: (lb.BOLD, 'EXTRA'), 212: (lb.BOLD, 'GAME'), 213: (lb.BOLD, 'NEW'), 214: (lb.BOLD, 'LOAD'),
                               215: (lb.BOLD, 'PRESS ANY BUTTON'), 216: (lb.PLATE_THIN, 'PRESS ANY BUTTON'),
                               199: (lb.THIN, 'GAME'), 200: (lb.THIN, 'NEW'), 201: (lb.THIN, 'LOAD'), 202: (lb.THIN, 'EXTRA')}),
    ('french', 'FR', 'QUITTER LE JEU', {212: (lb.BOLD, 'NOUVELLE PARTIE'), 213: (lb.BOLD, 'CHARGER PARTIE'),
                                        214: (lb.BOLD, 'APPUIE SUR UNE TOUCHE'), 215: (lb.PLATE_THIN, 'APPUIE SUR UNE TOUCHE'),
                                        199: (lb.THIN, 'NOUVELLE PARTIE'), 200: (lb.THIN, 'CHARGER PARTIE')}),
    ('german', 'GE', 'SPIEL BEENDEN', {212: (lb.BOLD, 'NEUES SPIEL'), 213: (lb.BOLD, 'SPIEL LADEN'),
                                       214: (lb.BOLD, 'BELIEBIGE TASTE DRÜCKEN'), 215: (lb.PLATE_THIN, 'BELIEBIGE TASTE DRÜCKEN'),
                                       199: (lb.THIN, 'SPIEL LADEN'), 200: (lb.THIN, 'NEUES SPIEL')}),
    ('italian', 'IT', 'ESCI DAL GIOCO', {212: (lb.BOLD, 'NUOVA PARTITA'), 213: (lb.BOLD, 'CARICA PARTITA'),
                                         214: (lb.BOLD, 'PREMI UN TASTO'), 215: (lb.PLATE_THIN, 'PREMI UN TASTO'),
                                         199: (lb.THIN, 'NUOVA PARTITA'), 200: (lb.THIN, 'CARICA PARTITA')}),
    ('spanish', 'SP', 'SALIR DEL JUEGO', {212: (lb.BOLD, 'PARTIDA NUEVA'), 213: (lb.BOLD, 'CARGAR PARTIDA'),
                                          214: (lb.BOLD, 'PULSA CUALQUIER BOTÓN'), 215: (lb.PLATE_THIN, 'PULSA CUALQUIER BOTÓN'),
                                          199: (lb.THIN, 'PARTIDA NUEVA'), 200: (lb.THIN, 'CARGAR PARTIDA')}),
]


def entry(cpk, name):
    return cpk.read(next(e for e in cpk.entries if e['path'].lower() == name))


class Language:
    def __init__(self, key, suffix, label, sources, folder, patch2, cache):
        self.key, self.label, self.sources = key, label, sources
        cached = Path(cache) / f'{key}.npz' if cache else None
        if cached and cached.exists():
            z = np.load(cached)
            self.ftx, mbs, self.rgba = z['ftx'].tobytes(), z['mbs'].tobytes(), z['rgba']
        else:
            cpk = Cpk(Path(folder) / f'ROBO_{suffix}.CPK')
            self.ftx, mbs = entry(cpk, 'gui/title.ftx'), entry(cpk, 'gui/title.mbs')
            if patch2:
                other = entry(Cpk(Path(patch2) / f'ROBO_PATCH2_{suffix}.CPK'), 'gui/title.mbs')
                assert other[0x100:] == mbs[0x100:], f'{key}: the 1.02 title layout differs beyond its header'
        self.layout = tl.Layout(mbs)
        self.tex = next(t for t in ftex.textures(self.ftx) if t['name'] == TEXT_ATLAS)
        if not (cached and cached.exists()):
            self.rgba = np.array(ftex.decode(self.ftx, self.tex))
            if cached:
                cached.parent.mkdir(parents=True, exist_ok=True)
                np.savez(cached, ftx=np.frombuffer(self.ftx, np.uint8), mbs=np.frombuffer(mbs, np.uint8), rgba=self.rgba)
        self.W, self.H = self.tex['width'], self.tex['height']
        self.segments = {}

    def uv_rect(self, uv):
        u0, v0, u1, v1 = self.layout.bounds(tl.UVS, uv)
        return round(u0 * self.W), round(v0 * self.H), round(u1 * self.W), round(v1 * self.H)

    def crop(self, uv):
        x0, y0, x1, y1 = self.uv_rect(uv)
        return self.rgba[y0:y1, x0:x1]

    def text(self, uv):
        return self.sources.get(uv, (None, None))[1]


def textured(lay, e):
    return [s for s in lay.element_sprites(e) if s['kind'] == tl.TEXTURED]


def layer_elements(lay, part, k):
    return sorted(set(lay.layer_elements(lay.part_layers(part)[k])))


def y_centre(lay, s):
    b = lay.bounds(tl.POSITIONS, s['pos'])
    return (b[1] + b[3]) / 2


def geometry(lay):
    """The two-row parts as templates: row pitch, colours, and per row and source UV the main, shadow and dim draws and
    the plate. The main draw shares its position with the row's dim draw; the shadow sits next to it."""
    new00, load00 = lay.part_index('NEW_GAME_00'), lay.part_index('LOAD_GAME_00')
    states = [textured(lay, layer_elements(lay, p, 0)[0]) for p in (new00, load00)]
    ys = sorted({round(y_centre(lay, s)) for s in states[0]})
    origin, pitch = ys[0], ys[-1] - ys[0]
    row = lambda s: int(round((y_centre(lay, s) - origin) / pitch))
    rows = {0: {}, 1: {}}
    for r in (0, 1):
        mine = [s for s in states[r] if row(s) == r]
        dims = [s for s in states[1 - r] if row(s) == r]
        for uv in sorted({s['uv'] for s in mine}):
            dim = next(s for s in dims if s['uv'] == uv)
            main = next(s for s in mine if s['uv'] == uv and s['pos'] == dim['pos'])
            shadow = next(s for s in mine if s['uv'] == uv and s['pos'] != dim['pos'])
            rows[r][uv] = dict(main=main, shadow=shadow, dim=dim)
    plates = [textured(lay, layer_elements(lay, p, 1)[0]) for p in (new00, load00)]
    colours = {k: {v[k]['colour'] for r in rows.values() for v in r.values()} for k in ('main', 'shadow', 'dim')}
    assert all(len(c) == 1 for c in colours.values()), colours
    return dict(origin=origin, pitch=pitch, row=row, rows=rows, plates=plates, colour={k: c.pop() for k, c in colours.items()})


def build_pools(langs):
    pools = {lb.BOLD: lb.Pool(lb.BOLD), lb.THIN: lb.Pool(lb.THIN)}
    own, retry = {}, []

    def take(lang, uv, kind, seg):
        lang.segments[uv] = seg
        style = lb.BOLD if kind == lb.BOLD else lb.THIN
        pools[style].add(seg)
        for g in seg['glyphs']:
            own.setdefault((lang.key, style, g.ch), g)

    for lang in langs:
        for uv, (kind, text) in lang.sources.items():
            seg = lb.segment(lang.crop(uv), lb.THIN if kind == lb.THIN else lb.BOLD, text, f'{lang.key} uv{uv}')
            if seg is None:
                retry.append((lang, uv, kind, text))
            else:
                take(lang, uv, kind, seg)
    for lang, uv, kind, text in retry:
        style = lb.BOLD if kind == lb.BOLD else lb.THIN
        widths = {c: g.width - 4 for c, g in pools[style].glyphs.items()}
        seg = lb.segment(lang.crop(uv), lb.THIN if kind == lb.THIN else lb.BOLD, text, f'{lang.key} uv{uv}', widths)
        if seg is None:
            print(f'  {lang.key} uv{uv} "{text}": touching letters, not a source')
            continue
        print(f'  {lang.key} uv{uv} "{text}": touching letters left out, {"".join(g.ch for g in seg["glyphs"])} kept')
        take(lang, uv, kind, seg)
    for style, pool in pools.items():
        worst = pool.fit()
        print(f'  {style}: {len(pool.glyphs)} letters, word space {pool.space:.1f} px, worst pair misfit {worst:.1f} px')
    return pools, own


def pool_for(pools, own, lang, style, text):
    """The style's pool with the language's own letters first, and J from U as no atlas has a J."""
    pool = lb.Pool(style)
    pool.sides, pool.median_side, pool.space = dict(pools[style].sides), pools[style].median_side, pools[style].space
    pool.glyphs = dict(pools[style].glyphs)
    for c in set(text) - {' '}:
        if (lang.key, style, c) in own:
            pool.glyphs[c] = own[(lang.key, style, c)]
    if 'J' in text and 'J' not in pool.glyphs:
        pool.glyphs['J'] = lb.derive_j(pool.glyphs['U'])
        pool.sides['J'] = pool.side('U')
    missing = set(text) - {' '} - set(pool.glyphs)
    assert not missing, (lang.key, style, missing)
    return pool


def compose(lang, pools, own, geo):
    """The label as a bold plate and a thin row, shaped like the language's second row."""
    row2 = geo['rows'][1]
    word_uv = next(uv for uv in row2 if lang.text(uv) != 'GAME')
    plate = next(s for s in geo['plates'][1] if lang.text(s['uv']) != 'GAME')
    bold_t, thin_t = lang.crop(plate['uv']), lang.crop(word_uv)
    bold = lb.bold_row(bold_t, lang.segments[plate['uv']]['cap'], *lb.set_text(pool_for(pools, own, lang, lb.BOLD, lang.label), lang.label),
                       *lb.margins(bold_t, lb.BOLD))
    thin = lb.thin_row(thin_t, lang.segments[word_uv]['cap'], *lb.set_text(pool_for(pools, own, lang, lb.THIN, lang.label), lang.label),
                       *lb.margins(thin_t, lb.THIN))
    return bold, thin, word_uv, plate


def join_with_game(word, game, word_rect, game_rect):
    """English rows are WORD GAME: the word's sprite ends where the second row's word ends, GAME stays where it is."""
    x0 = word_rect[2] - word.shape[1] / PX_PER_UNIT
    gx = int(round((game_rect[0] - x0) * PX_PER_UNIT))
    out = np.zeros((word.shape[0], max(gx + game.shape[1], word.shape[1]), 4), np.uint8)
    out[:, :word.shape[1]] = word
    region = out[:, gx:gx + game.shape[1]]
    np.copyto(region, np.maximum(region, game))
    return out, x0


def used_texels(lang):
    used = np.zeros((lang.H, lang.W), bool)
    lay = lang.layout
    for i in range(lay.count(tl.SPRITES)):
        s = lay.sprite(i)
        if s['kind'] == tl.TEXTURED and s['tex'] == TEXT_TEXTURE:
            x0, y0, x1, y1 = lang.uv_rect(s['uv'])
            used[max(0, y0 - 8):y1 + 8, max(0, x0 - 8):x1 + 8] = True
    return used


def place(lang, images):
    """Block-aligned top-left corners for the images, stacked in a rectangle no sprite samples."""
    S = np.pad(used_texels(lang).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    w = (max(i.shape[1] for i in images) + 3) // 4 * 4
    h = sum((i.shape[0] + 3) // 4 * 4 + 8 for i in images)
    for y in range(0, lang.H - h + 1, 4):
        for x in range(0, lang.W - w + 1, 4):
            if S[y + h, x + w] - S[y, x + w] - S[y + h, x] + S[y, x] == 0:
                out = []
                for i in images:
                    out.append((x, y))
                    y += (i.shape[0] + 3) // 4 * 4 + 8
                return out
    raise SystemExit(f'{lang.key}: no free space for the labels')


def tiled_blocks(lang, rect):
    bx0, by0, bw, bh = rect
    pb = lang.tex['pitch'] // 4
    data = lang.ftx[lang.tex['data']:lang.tex['data'] + lang.tex['size']]
    return b''.join(data[o:o + 16] for o in (ftex.tiled_offset(bx, by, pb) for by in range(by0, by0 + bh)
                                               for bx in range(bx0, bx0 + bw)))


def encode_region(lang, atlas, rect):
    x0, y0, x1, y1 = rect
    blocks_rect = (x0 // 4, y0 // 4, (x1 + 3) // 4 - x0 // 4, (y1 + 3) // 4 - y0 // 4)
    bx0, by0, bw, bh = blocks_rect
    px = atlas[by0 * 4:(by0 + bh) * 4, bx0 * 4:(bx0 + bw) * 4]
    enc = bc7.encode_blocks(px.reshape(bh, 4, bw, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4))
    return blocks_rect, [enc[k * 16:(k + 1) * 16] for k in range(bw * bh)], tiled_blocks(lang, blocks_rect)


def uv_points(lang, rect):
    x0, y0, x1, y1 = rect
    return tl.rect_points(x0 / lang.W, y0 / lang.H, x1 / lang.W, y1 / lang.H)


def holes(rgba):
    """A bold plate's letter holes (clear texels the plate encloses) and the clear texels around the plate."""
    clear = rgba[:, :, 3] < 230
    lab, _ = ndimage.label(clear)
    edge = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    outside = np.isin(lab, edge[edge > 0])
    return clear & ~outside, outside


def letters(hole):
    """The holes split at clear columns: (x0, x1, y0, y1) per letter, in texels, left to right."""
    cols = np.concatenate([[False], hole.any(0), [False]]).astype(np.int8)
    starts, ends = np.nonzero(np.diff(cols) == 1)[0], np.nonzero(np.diff(cols) == -1)[0]
    out = []
    for x0, x1 in zip(starts, ends):
        ys = np.nonzero(hole[:, x0:x1].any(1))[0]
        out.append((int(x0), int(x1), int(ys[0]), int(ys[-1]) + 1))
    return out


def colour_roles(lay):
    """The opaque white and opaque black colour records."""
    rec = lambda c: bytes(lay.d[lay.at(tl.COLOURS, c):lay.at(tl.COLOURS, c) + 24])
    white = {c for c in range(lay.count(tl.COLOURS)) if rec(c) == b'\xff' * 24}
    black = {c for c in range(lay.count(tl.COLOURS)) if rec(c) == b'\0\0\0\xff' * 6}
    assert white and black
    return white, black


def plate_frames(lay, part):
    """The element each keyframe of the part's plate layer draws."""
    return lay.layer_elements(lay.part_layers(part)[1])


def native_rhythm(lang, lay):
    """The write-on of the second row's plate (white strokes under a black plate with the letters cut out): per letter
    and frame, how many thirds of the letter's holes are lit."""
    frames = plate_frames(lay, lay.part_index('LOAD_GAME_P00'))
    white, black = colour_roles(lay)
    first = lay.element_sprites(frames[0])
    plates = sorted((s for s in first if s['kind'] == tl.TEXTURED), key=lambda s: lay.bounds(tl.POSITIONS, s['pos'])[0])
    out = []
    for plate in plates:
        hole, _ = holes(lang.crop(plate['uv']))
        px0, py0, px1, py1 = lay.bounds(tl.POSITIONS, plate['pos'])
        sx, sy = hole.shape[1] / (px1 - px0), hole.shape[0] / (py1 - py0)
        runs = letters(hole)
        seqs = [[] for _ in runs]
        for e in frames:
            sprites = lay.element_sprites(e)
            kinds = [s['kind'] == tl.TEXTURED for s in sprites]
            assert kinds == sorted(kinds) and {(s['uv'], s['pos']) for s in sprites if s['kind'] == tl.TEXTURED} == {(s['uv'], s['pos']) for s in plates}
            lit = np.zeros(hole.shape, bool)
            for s in sprites:
                if s['kind'] == tl.TEXTURED:
                    continue
                assert s['colour'] in white | black
                x0, y0, x1, y1 = lay.bounds(tl.POSITIONS, s['pos'])
                c0, c1 = max(0, round((x0 - px0) * sx)), max(0, round((x1 - px0) * sx))
                r0, r1 = max(0, round((y0 - py0) * sy)), max(0, round((y1 - py0) * sy))
                lit[r0:r1, c0:c1] = s['colour'] in white
            for seq, (x0, x1, y0, y1) in zip(seqs, runs):
                seq.append(round(3 * lit[y0:y1, x0:x1][hole[y0:y1, x0:x1]].mean()))
        out += seqs
    return out


def axis_aligned(lay, s, i):
    p = lay.quad(s, i)
    return p[1][0] == p[2][0] == p[5][0] and p[3][0] == p[4][0] and p[1][1] == p[4][1] == p[5][1] and p[2][1] == p[3][1]


def check_only_three_row(lay, quads):
    """Every sprite that samples one of the quads is drawn by the three-row parts alone."""
    three = {lay.part_index(n) for n in THREE_ROW}
    drawn_elsewhere = lay.users(set(range(lay.count(tl.PARTS))) - three)
    for i in range(lay.count(tl.SPRITES)):
        s = lay.sprite(i)
        for sec, q in quads:
            hit = (s['uv'] == q and s['kind'] == tl.TEXTURED) if sec == tl.UVS else s['pos'] == q
            assert not (hit and i in drawn_elsewhere), (sec, q, i)


def plan(lang, pools, own):
    lay = lang.layout
    geo = geometry(lay)
    rhythm = native_rhythm(lang, lay)
    bold, thin, word_uv, plate = compose(lang, pools, own, geo)
    row2 = geo['rows'][1][word_uv]
    main_r = lay.bounds(tl.POSITIONS, row2['main']['pos'])
    shadow_r = lay.bounds(tl.POSITIONS, row2['shadow']['pos'])
    plate_r = lay.bounds(tl.POSITIONS, plate['pos'])
    if lang.key == 'english':
        game_uv = next(uv for uv in geo['rows'][1] if lang.text(uv) == 'GAME')
        game_plate = next(s for s in geo['plates'][1] if lang.text(s['uv']) == 'GAME')
        bold, bx0 = join_with_game(bold, lang.crop(game_plate['uv']), plate_r, lay.bounds(tl.POSITIONS, game_plate['pos']))
        thin, tx0 = join_with_game(thin, lang.crop(game_uv), main_r, lay.bounds(tl.POSITIONS, geo['rows'][1][game_uv]['main']['pos']))
    else:
        bx0 = (plate_r[0] + plate_r[2]) / 2 - bold.shape[1] / PX_PER_UNIT / 2
        tx0 = (main_r[0] + main_r[2]) / 2 - thin.shape[1] / PX_PER_UNIT / 2
    p = geo['pitch']
    plate3 = (bx0, plate_r[1] + p, bx0 + bold.shape[1] / PX_PER_UNIT, plate_r[3] + p)
    main3 = (tx0, main_r[1] + p, tx0 + thin.shape[1] / PX_PER_UNIT, main_r[3] + p)
    dx, dy = shadow_r[0] - main_r[0], shadow_r[1] - main_r[1]
    shadow3 = (main3[0] + dx, main3[1] + dy, main3[2] + dx, main3[3] + dy)
    (bpos, tpos) = place(lang, [bold, thin])
    bold_rect = (bpos[0], bpos[1], bpos[0] + bold.shape[1], bpos[1] + bold.shape[0])
    thin_rect = (tpos[0], tpos[1], tpos[0] + thin.shape[1], tpos[1] + thin.shape[0])
    atlas = lang.rgba.copy()
    for (x0, y0, x1, y1), img in ((bold_rect, bold), (thin_rect, thin)):
        atlas[y0 // 4 * 4:(y1 + 3) // 4 * 4, x0 // 4 * 4:(x1 + 3) // 4 * 4] = 0
        atlas[y0:y1, x0:x1] = img
    regions = [encode_region(lang, atlas, r) for r in (bold_rect, thin_rect)]
    edit_layout(lang, geo, uv_points(lang, bold_rect), uv_points(lang, thin_rect), plate3, main3, shadow3, plate, bold, rhythm)
    ref = lang.uv_rect(plate['uv'])
    ref_blocks = (ref[0] // 4, ref[1] // 4, (ref[2] - ref[0]) // 4, (ref[3] - ref[1]) // 4)
    return dict(atlas=atlas, regions=regions, edits=lay.byte_edits(), reference=(ref_blocks, zlib.crc32(tiled_blocks(lang, ref_blocks))),
                bold=bold, thin=thin, rects=(plate3, main3, shadow3), bold_rect=bold_rect, thin_rect=thin_rect)


def edit_layout(lang, geo, bold_uv_pts, thin_uv_pts, plate3, main3, shadow3, plate2, bold, rhythm):
    lay = lang.layout
    three = [lay.part_index(n) for n in THREE_ROW]
    col = geo['colour']
    plate_elements = sorted(set(layer_elements(lay, three[2], 1)) | set(layer_elements(lay, three[5], 1)))
    strokes = sorted({s['pos'] for e in plate_elements for s in lay.element_sprites(e) if s['kind'] != tl.TEXTURED})
    texts = [s for p in three for e in layer_elements(lay, p, 0) for s in textured(lay, e)]
    if lang.key == 'english':
        row3 = [s for s in texts if geo['row'](s) == 2]
        (thin_uv,) = {s['uv'] for s in row3}
        (main_pos,) = {s['pos'] for s in row3 if s['colour'] != col['shadow']}
        (shadow_pos,) = {s['pos'] for s in row3 if s['colour'] == col['shadow']}
        plates = [s for e in plate_elements for s in textured(lay, e)]
        (bold_uv,), (plate_pos,), (plate_info,) = {s['uv'] for s in plates}, {s['pos'] for s in plates}, {s['info'] for s in plates}
        quads = [(tl.UVS, thin_uv), (tl.UVS, bold_uv), (tl.POSITIONS, main_pos), (tl.POSITIONS, shadow_pos), (tl.POSITIONS, plate_pos)]
    else:
        own_uv = {uv for r in geo['rows'].values() for uv in r}
        placeholders = [s for s in texts if s['uv'] not in own_uv]
        uvs, poss = sorted({s['uv'] for s in placeholders}), sorted({s['pos'] for s in placeholders})
        assert len(uvs) >= 2 and len(poss) >= 4, (lang.key, uvs, poss)
        thin_uv, bold_uv = uvs[:2]
        main_pos, shadow_pos, plate_pos, nowhere = poss[:4]
        info = {uv: next(s['info'] for s in placeholders if s['uv'] == uv) for uv in uvs}
        plate_info = info[bold_uv]
        quads = [(tl.UVS, thin_uv), (tl.UVS, bold_uv), (tl.POSITIONS, main_pos), (tl.POSITIONS, shadow_pos),
                 (tl.POSITIONS, plate_pos), (tl.POSITIONS, nowhere)]
    check_only_three_row(lay, quads + [(tl.POSITIONS, q) for q in strokes])
    assert not {q for s, q in quads if s == tl.POSITIONS} & set(strokes), lang.key
    assert all(axis_aligned(lay, s, i) for s, i in quads), lang.key
    lay.set_quad(tl.UVS, thin_uv, thin_uv_pts)
    lay.set_quad(tl.UVS, bold_uv, bold_uv_pts)
    lay.set_quad(tl.POSITIONS, main_pos, tl.rect_points(*main3))
    lay.set_quad(tl.POSITIONS, shadow_pos, tl.rect_points(*shadow3))
    lay.set_quad(tl.POSITIONS, plate_pos, tl.rect_points(*plate3))
    if lang.key != 'english':
        lay.set_quad(tl.POSITIONS, nowhere, [(0.0, 0.0)] * 6)
        third = dict(main=(thin_uv, main_pos, col['main'], info[thin_uv]), shadow=(thin_uv, shadow_pos, col['shadow'], info[thin_uv]),
                     dim=(thin_uv, main_pos, col['dim'], info[thin_uv]))
        rewrite_rows(lay, geo, three, third, nowhere)
    plate = dict(info=plate_info, kind=tl.TEXTURED, tex=TEXT_TEXTURE, uv=bold_uv, pos=plate_pos, colour=plate2['colour'])
    write_on(lay, plate_frames(lay, three[2]), plate_elements, strokes, bold, plate3, rhythm, plate)
    match_timing(lay, three[2], three[1])
    for e in plate_elements:
        lay.write(tl.ELEMENTS, e, 0, struct.pack('<4f', *plate3))


def rewrite_rows(lay, geo, three, third, nowhere):
    """The three-row text layers of a layout that still shows English placeholder rows: every state draws the
    language's rows 1 and 2 as its two-row parts do, and the third row from the new label."""
    draws = {}
    for r in (0, 1):
        (v,) = geo['rows'][r].values()
        draws[r] = {k: (v[k]['uv'], v[k]['pos'], v[k]['colour'], v[k]['info']) for k in ('main', 'shadow', 'dim')}
    draws[2] = third
    for k, p in enumerate(three):
        selected, confirmed = k % 3, k >= 3
        want = []
        for r in range(3):
            if r != selected:
                want.append(draws[r]['dim'])
            elif not confirmed:
                want += [draws[r]['shadow'], draws[r]['main']]
        want.sort(key=lambda w: (w[2] != geo['colour']['shadow'],))
        for e in layer_elements(lay, p, 0):
            slots = textured(lay, e)
            assert len(slots) >= len(want), (p, e, len(slots), len(want))
            for slot, w in zip(slots, want + [None] * (len(slots) - len(want))):
                uv, pos, colour, info = w if w else (draws[0]['main'][0], nowhere, geo['colour']['main'], draws[0]['main'][3])
                lay.set_sprite(slot['index'], info=info, kind=tl.TEXTURED, tex=TEXT_TEXTURE, uv=uv, pos=pos, colour=colour)


def match_timing(lay, target, reference):
    """The target's plate layer timed as the reference's: the layer's timing fields and each keyframe from its duration on."""
    (t,), (r,) = lay.part_layers(target)[1:2], lay.part_layers(reference)[1:2]
    ks, kc = struct.unpack_from('<HH', lay.d, lay.at(tl.LAYERS, t))
    rs, rc = struct.unpack_from('<HH', lay.d, lay.at(tl.LAYERS, r))
    assert kc == rc, (kc, rc)
    lay.write(tl.LAYERS, t, 4, bytes(lay.d[lay.at(tl.LAYERS, r, 4):lay.at(tl.LAYERS, r, 24)]))
    for k in range(kc):
        assert lay.d[lay.at(tl.KEYFRAMES, ks + k, 4):lay.at(tl.KEYFRAMES, ks + k, 6)] == lay.d[lay.at(tl.KEYFRAMES, rs + k, 4):lay.at(tl.KEYFRAMES, rs + k, 6)]
        lay.write(tl.KEYFRAMES, ks + k, 6, bytes(lay.d[lay.at(tl.KEYFRAMES, rs + k, 6):lay.at(tl.KEYFRAMES, rs + k, 32)]))


def write_on(lay, frames, elements, quads, bold, plate3, rhythm, plate):
    """The third row's plate frames redrawn for the label: the native strokes spell EXTRA, so each frame instead lights
    every letter of the label from the top by the thirds the second row's letter at the same place has lit, under the
    plate drawn last. Every lit rectangle stays inside one letter's holes."""
    hole, outside = holes(bold)
    runs = letters(hole)
    white = min(colour_roles(lay)[0])
    nowhere, *free = quads
    assert len(free) >= 3 * len(runs) and all(len(r) == len(frames) for r in rhythm), (len(free), len(runs))
    h, w = hole.shape
    x0, y0, x1, y1 = plate3
    units = lambda px, py: (x0 + px * (x1 - x0) / w, y0 + py * (y1 - y0) / h)
    lay.set_quad(tl.POSITIONS, nowhere, [(0.0, 0.0)] * 6)
    fill = {}
    for i, (rx0, rx1, ry0, ry1) in enumerate(runs):
        for level in (1, 2, 3):
            bottom = ry0 + math.ceil((ry1 - ry0) * level / 3)
            assert not outside[ry0:bottom, rx0:rx1].any(), (i, level)
            fill[i, level] = free[3 * i + level - 1]
            lay.set_quad(tl.POSITIONS, fill[i, level], tl.rect_points(*units(rx0, ry0), *units(rx1, bottom)))
    for e in elements:
        k = frames.index(e)
        *strokes, last = lay.element_sprites(e)
        lit = [fill[i, level] for i in range(len(runs))
               for level in [rhythm[(2 * i + 1) * len(rhythm) // (2 * len(runs))][k]] if level]
        assert len(lit) <= len(strokes) and all(s['kind'] != tl.TEXTURED for s in strokes), e
        for slot, q in zip(strokes, lit + [nowhere] * (len(strokes) - len(lit))):
            lay.set_sprite(slot['index'], pos=q, colour=white)
        lay.set_sprite(last['index'], **plate)


def write_asset(path, results):
    """TSRXGL01 (little endian): per language its key, the text atlas it fits, a CRC of reference blocks, the label
    regions (block rect, CRC of the blocks replaced, a pool of BC7 blocks and runs of pool indices in row order), the
    layout's section offsets and length, and its edits (section, length, offset in the section, original bytes, new bytes). A CRC32 of everything ends it."""
    out = bytearray(MAGIC + struct.pack('<I', len(results)))
    for lang, r in results:
        out += lang.key.encode().ljust(12, b'\0')
        t = lang.tex
        out += struct.pack('<4I', t['width'], t['height'], t['pitch'], t['size'])
        ref, crc = r['reference']
        out += struct.pack('<5I', *ref, crc)
        out += struct.pack('<I', len(r['regions']))
        for rect, blocks, orig in r['regions']:
            pool, index, runs = [], {}, []
            for b in blocks:
                i = index.setdefault(b, len(pool))
                if i == len(pool):
                    pool.append(b)
                if runs and runs[-1][0] == i and runs[-1][1] < 0xffff:
                    runs[-1][1] += 1
                else:
                    runs.append([i, 1])
            out += struct.pack('<7I', *rect, zlib.crc32(orig), len(pool), len(runs))
            out += b''.join(pool) + b''.join(struct.pack('<HH', i, n) for i, n in runs)
        out += struct.pack('<12I', *lang.layout.offsets, len(lang.layout.original))
        out += struct.pack('<I', len(r['edits']))
        for sec, off, old, new in r['edits']:
            out += struct.pack('<BBHI', sec, len(old), 0, off) + old + new
    out += struct.pack('<I', zlib.crc32(out))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(out)
    return len(out)


def decoded_atlas(lang, r):
    atlas = r['atlas'].copy()
    for (bx, by, bw, bh), blocks, _ in r['regions']:
        rows = [b''.join(blocks[y * bw:(y + 1) * bw]) for y in range(bh)]
        img = Image.open(io.BytesIO(ftex.dds_wrap(b''.join(rows), bw * 4, bh * 4, 41))).convert('RGBA')
        atlas[by * 4:(by + bh) * 4, bx * 4:(bx + bw) * 4] = np.array(img)
    return atlas


def render(lang, atlas, lay, part, colours, scale=2.5):
    """A part's first frame: its text and plate layers, textured sprites only, shadow in black."""
    img = Image.new('RGBA', (1300, 270), (40, 44, 78, 255))
    a = Image.fromarray(atlas)
    for layer in lay.part_layers(part)[:2]:
        e = lay.layer_elements(layer)[0]
        for s in lay.element_sprites(e):
            if s['kind'] != tl.TEXTURED or s['tex'] != TEXT_TEXTURE:
                continue
            x0, y0, x1, y1 = lay.bounds(tl.POSITIONS, s['pos'])
            if x1 - x0 < 1:
                continue
            u0, v0, u1, v1 = lay.bounds(tl.UVS, s['uv'])
            crop = a.crop((round(u0 * lang.W), round(v0 * lang.H), round(u1 * lang.W), round(v1 * lang.H)))
            crop = crop.resize((max(1, round((x1 - x0) * scale)), max(1, round((y1 - y0) * scale))), Image.LANCZOS)
            arr = np.array(crop)
            if s['colour'] == colours['shadow'] and layer == lay.part_layers(part)[0]:
                arr[:, :, :3] = 0
            elif s['colour'] == colours['dim']:
                arr[:, :, 3] = arr[:, :, 3] * 3 // 5
            img.alpha_composite(Image.fromarray(arr), (650 + round(x0 * scale), round((y0 - 135) * scale)))
    return img


def render_plate_frames(lang, atlas, lay, parts, scale=2.5):
    """Every keyframe of the parts' plate layers in draw order, solid sprites included, one row per part."""
    a = Image.fromarray(atlas)
    rows = [[lay.element_sprites(e) for e in plate_frames(lay, lay.part_index(p))] for p in parts]
    fw, fh = round(340 * scale), round(48 * scale)
    sheet = Image.new('RGBA', (fw * max(len(r) for r in rows), fh * len(rows)), (40, 44, 78, 255))
    for j, frames in enumerate(rows):
        for i, sprites in enumerate(frames):
            plate = next(s for s in sprites if s['kind'] == tl.TEXTURED)
            pb = lay.bounds(tl.POSITIONS, plate['pos'])
            ox, oy = -170, (pb[1] + pb[3]) / 2 - 24
            for s in sprites:
                x0, y0, x1, y1 = lay.bounds(tl.POSITIONS, s['pos'])
                w, h = round((x1 - x0) * scale), round((y1 - y0) * scale)
                if w < 1 or h < 1:
                    continue
                c = np.frombuffer(bytes(lay.d[lay.at(tl.COLOURS, s['colour']):lay.at(tl.COLOURS, s['colour']) + 4]), np.uint8) / 255
                if s['kind'] == tl.TEXTURED:
                    u0, v0, u1, v1 = lay.bounds(tl.UVS, s['uv'])
                    crop = a.crop((round(u0 * lang.W), round(v0 * lang.H), round(u1 * lang.W), round(v1 * lang.H))).resize((w, h), Image.LANCZOS)
                    px = (np.array(crop) * c).astype(np.uint8)
                else:
                    px = np.empty((h, w, 4), np.uint8)
                    px[:] = (c * 255).astype(np.uint8)
                sheet.alpha_composite(Image.fromarray(px), (i * fw + round((x0 - ox) * scale), j * fh + round((y0 - oy) * scale)))
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder')
    ap.add_argument('--patch2')
    ap.add_argument('--out', default=str(HERE.parents[1] / 'assets' / 'exit_game' / 'title-labels.bin'))
    ap.add_argument('--preview')
    ap.add_argument('--cache', help='folder for the decoded atlases between runs')
    a = ap.parse_args()
    langs = [Language(k, s, label, src, a.folder, a.patch2, a.cache) for k, s, label, src in LANGUAGES]
    print('letters:')
    pools, own = build_pools(langs)
    results = []
    for lang in langs:
        original = tl.Layout(lang.layout.original)
        r = plan(lang, pools, own)
        results.append((lang, r))
        print(f'{lang.key}: "{lang.label}" plate {r["bold"].shape[1]}x{r["bold"].shape[0]} at {r["bold_rect"][:2]}, '
              f'thin {r["thin"].shape[1]}x{r["thin"].shape[0]} at {r["thin_rect"][:2]}, {len(r["edits"])} layout records')
        if a.preview:
            out = Path(a.preview)
            out.mkdir(parents=True, exist_ok=True)
            atlas = decoded_atlas(lang, r)
            geo = geometry(original)
            names = [(original, 'NEW_GAME_00'), (original, 'LOAD_GAME_00')] + [(lang.layout, n) for n in THREE_ROW]
            sheet = Image.new('RGBA', (1300, 270 * len(names)), (0, 0, 0, 255))
            for k, (lay, name) in enumerate(names):
                sheet.alpha_composite(render(lang, atlas, lay, lay.part_index(name), geo['colour']), (0, 270 * k))
            sheet.convert('RGB').save(out / f'{lang.key}.png')
            frames = render_plate_frames(lang, atlas, lang.layout, ['LOAD_GAME_P00', 'EXTRA_GAME_P00', 'EXTRA_GAME_P01'])
            frames.convert('RGB').save(out / f'{lang.key}-animation.png')
            crops = [atlas[y0:y1, x0:x1] for x0, y0, x1, y1 in (r['bold_rect'], r['thin_rect'])]
            row2 = geo['rows'][1]
            crops += [lang.rgba[slice(*lang.uv_rect(s['uv'])[1::2]), slice(*lang.uv_rect(s['uv'])[0::2])] for s in geo['plates'][1]]
            crops += [lang.crop(uv) for uv in row2]
            labels = Image.new('RGBA', (max(c.shape[1] for c in crops) + 16, sum(c.shape[0] + 12 for c in crops)), (40, 44, 78, 255))
            y = 0
            for c in crops:
                labels.alpha_composite(Image.fromarray(c), (8, y))
                y += c.shape[0] + 12
            labels.convert('RGB').save(out / f'{lang.key}-labels.png')
    print(f'{a.out}: {write_asset(a.out, results)} bytes')


if __name__ == '__main__':
    main()
