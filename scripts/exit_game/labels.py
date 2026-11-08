"""Exit Game labels drawn from the title's own letters. Every title row is baked art: a bold row is a white plate with the
letters cut out of its alpha, a thin row is white strokes. The letters are cut from the rows the five language atlases
already have (one face and size in all of them) and set with sidebearings fitted to the spacing of those rows."""
import numpy as np
from scipy import ndimage

BOLD, THIN, PLATE_THIN = 'bold', 'thin', 'plate_thin'


def plate_box(alpha):
    plate = alpha > 0.5
    rows, cols = np.nonzero(plate.mean(1) > 0.9)[0], np.nonzero(plate.mean(0) > 0.9)[0]
    return rows.min(), rows.max() + 1, cols.min(), cols.max() + 1


def ink_of(rgba, kind):
    """Letter coverage 0..1: the strokes of a thin row, the holes of a plate."""
    a = rgba[:, :, 3].astype(np.float32) / 255
    if kind == THIN:
        return a
    y0, y1, x0, x1 = plate_box(a)
    ink = np.zeros_like(a)
    ink[y0 + 1:y1 - 1, x0 + 1:x1 - 1] = 1 - a[y0 + 1:y1 - 1, x0 + 1:x1 - 1]
    return ink


class Glyph:
    def __init__(self, ch, cover, top, source):
        self.ch, self.cover, self.top, self.source = ch, cover, top, source  # top: first row, relative to the cap line

    @property
    def width(self):
        return self.cover.shape[1]


def segment(rgba, kind, text, source, widths=None):
    """The letters of a row whose text is known, left to right, and the ink gaps between neighbours. Touching letters
    form one component: with `widths` (known ink widths per letter) such a component is recognised and skipped, else
    the row is not used (None)."""
    ink = ink_of(rgba, kind)
    lab, _ = ndimage.label(ink > 0.25)
    comps = sorted((sl[1].start, sl[1].stop, sl[0].start, sl[0].stop, k + 1)
                   for k, sl in enumerate(ndimage.find_objects(lab)) if sl is not None)
    tall = [c for c in comps if c[3] - c[2] > 0.4 * ink.shape[0]]
    marks = [c for c in comps if c not in tall]
    letters = [ch for ch in text if ch != ' ']
    groups = [[ch] for ch in letters]
    if len(tall) != len(letters):
        if not widths:
            return None
        groups, i = [], 0
        for c in tall:
            group, span = [letters[i]], widths.get(letters[i], c[1] - c[0])
            i += 1
            while i < len(letters) and c[1] - c[0] > span * 1.3 and letters[i] in widths:
                group.append(letters[i])
                span += widths[letters[i]]
                i += 1
            groups.append(group)
        if i != len(letters):
            return None
    cap = int(np.median([c[2] for c in tall]))
    glyphs, boxes = [], []
    for (x0, x1, y0, y1, k), group in zip(tall, groups):
        if len(group) > 1:
            glyphs.append(None)
            boxes.append(None)
            continue
        ch = group[0]
        ids = [k] + [m[4] for m in marks if m[0] < x1 and m[1] > x0]
        mask = ndimage.binary_dilation(np.isin(lab, ids), iterations=2)
        ys, xs = np.nonzero(mask)
        gx0, gx1, gy0, gy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        glyphs.append(Glyph(ch, np.where(mask, ink, 0)[gy0:gy1, gx0:gx1], gy0 - cap, source))
        boxes.append((x0, x1))
    if len(groups) != len(letters):  # letters only: the spacing of a row with touching letters is not measured
        return dict(glyphs=[g for g in glyphs if g], pairs=[], spaces=[], cap=cap, ink=ink, boxes=boxes)
    words, i = [], 0
    for w in text.split(' '):
        words.append(list(range(i, i + len(w))))
        i += len(w)
    pairs, spaces = [], []
    for w, word in enumerate(words):
        for a, b in zip(word, word[1:]):
            pairs.append((letters[a], letters[b], boxes[b][0] - boxes[a][1]))
        if w + 1 < len(words):
            nxt = words[w + 1][0]
            spaces.append((letters[word[-1]], letters[nxt], boxes[nxt][0] - boxes[word[-1]][1]))
    return dict(glyphs=glyphs, pairs=pairs, spaces=spaces, cap=cap, ink=ink, boxes=boxes)


class Pool:
    """Letters and spacing of one style from every source row. Glyph boxes are the dilated ink boxes, so a glyph's
    width includes 2 px each side; spacing is fitted between the ink boxes and set the same way."""

    def __init__(self, style):
        self.style, self.glyphs, self.pairs, self.spaces = style, {}, [], []

    def add(self, seg, letters_wanted=None):
        for g in seg['glyphs']:
            if letters_wanted is None or g.ch in letters_wanted:
                self.glyphs.setdefault(g.ch, g)
        self.pairs += seg['pairs']
        self.spaces += seg['spaces']

    def fit(self):
        """Sidebearings: gap(a, b) = right(a) + left(b), least squares over every observed pair."""
        letters = sorted({c for a, b, _ in self.pairs + self.spaces for c in (a, b)})
        col = {c: i for i, c in enumerate(letters)}
        rows, rhs = [], []
        for a, b, gap in self.pairs:
            r = np.zeros(2 * len(letters) + 1)
            r[2 * col[a] + 1] += 1
            r[2 * col[b]] += 1
            rows.append(r)
            rhs.append(gap)
        for a, b, gap in self.spaces:
            r = np.zeros(2 * len(letters) + 1)
            r[2 * col[a] + 1] += 1
            r[2 * col[b]] += 1
            r[-1] = 1
            rows.append(r)
            rhs.append(gap)
        for c in letters:  # a weak pull towards equal sides fixes the split that pair sums leave free
            r = np.zeros(2 * len(letters) + 1)
            r[2 * col[c]], r[2 * col[c] + 1] = 0.2, -0.2
            rows.append(r)
            rhs.append(0)
        sol = np.linalg.lstsq(np.array(rows), np.array(rhs, dtype=np.float64), rcond=None)[0]
        self.sides = {c: (sol[2 * i], sol[2 * i + 1]) for c, i in col.items()}
        self.median_side = float(np.median(sol[:-1]))
        self.space = float(sol[-1])
        return max(abs(g - self.gap(a, b)) for a, b, g in self.pairs)

    def side(self, c):
        return self.sides.get(c, (self.median_side, self.median_side))

    def gap(self, a, b):
        return self.side(a)[1] + self.side(b)[0]


def derive_j(u):
    """J from U: the right stem and the bowl, the left stem cut away above the bowl."""
    cover = u.cover.copy()
    h, w = cover.shape
    mid = cover[h // 3]
    left_stem = np.nonzero(mid[: w // 2] > 0.5)[0]
    cut_x = int(left_stem.max()) + 3
    bowl_top = h - int(round(0.62 * w))
    cover[:bowl_top, :cut_x] = 0
    return Glyph('J', cover, u.top, u.source + ', J from U')


def set_text(pool, text):
    """Coverage of a line set from the pool: (cover, cap row in cover, ink left, ink right)."""
    placed, x, prev = [], 0.0, None
    for c in text:
        if c == ' ':
            x += pool.space
            continue
        g = pool.glyphs[c]
        if prev is not None:
            x += pool.gap(prev, c) - 4  # boxes are dilated by 2 px each side
        placed.append((g, int(round(x))))
        x += g.width
        prev = c
    top = min(g.top for g, _ in placed)
    bottom = max(g.top + g.cover.shape[0] for g, _ in placed)
    cover = np.zeros((bottom - top, placed[-1][1] + placed[-1][0].width), np.float32)
    for g, gx in placed:
        h, w = g.cover.shape
        region = cover[g.top - top:g.top - top + h, gx:gx + w]
        np.maximum(region, g.cover, out=region)
    cols = np.nonzero(cover.max(0) > 0.25)[0]
    return cover, -top, int(cols.min()), int(cols.max()) + 1


def to_rgba(alpha):
    rgba = np.zeros(alpha.shape + (4,), np.uint8)
    rgba[:, :, :3] = 255
    rgba[:, :, 3] = np.clip(np.round(alpha * 255), 0, 255).astype(np.uint8)
    return rgba


def thin_row(template, template_cap, cover, cap_row, ink_left, ink_right, left_pad, right_pad):
    """A thin row the template's height: `left_pad`/`right_pad` px between the sprite edge and the ink."""
    h = template.shape[0]
    width = left_pad + (ink_right - ink_left) + right_pad
    alpha = np.zeros((h, width), np.float32)
    y = template_cap - cap_row
    src = cover[:, ink_left:ink_right]
    alpha[y:y + src.shape[0], left_pad:left_pad + src.shape[1]] = src
    return to_rgba(alpha)


def bold_row(template, template_cap, cover, cap_row, ink_left, ink_right, left_margin, right_margin):
    """A plate shaped like the template's (height, edge columns, padding outside the plate) with the text cut out."""
    a = template[:, :, 3].astype(np.float32) / 255
    y0, y1, x0, x1 = plate_box(a)
    pad_left, pad_right = x0, a.shape[1] - x1
    inner = left_margin + (ink_right - ink_left) + right_margin
    width = pad_left + inner + pad_right
    out = np.zeros((a.shape[0], width), np.float32)
    plain = x0 + max(3, (np.nonzero(ink_of(template, BOLD).max(0) > 0.25)[0].min() - x0) // 2)
    out[:, pad_left:pad_left + inner] = a[:, plain][:, None]
    out[:, :pad_left + 2] = a[:, :pad_left + 2]
    out[:, width - pad_right - 2:] = a[:, x1 - 2:]
    text = np.zeros_like(out)
    y = template_cap - cap_row
    src = cover[:, ink_left:ink_right]
    text[y:y + src.shape[0], pad_left + left_margin:pad_left + left_margin + src.shape[1]] = src
    return to_rgba(out * (1 - text))


def margins(rgba, kind):
    """(left, right) px between the row's edge (the plate's for bold, the sprite's for thin) and its ink."""
    ink = ink_of(rgba, kind)
    cols = np.nonzero(ink.max(0) > 0.25)[0]
    if kind == THIN:
        return int(cols.min()), int(rgba.shape[1] - cols.max() - 1)
    y0, y1, x0, x1 = plate_box(rgba[:, :, 3].astype(np.float32) / 255)
    return int(cols.min() - x0), int(x1 - cols.max() - 1)
