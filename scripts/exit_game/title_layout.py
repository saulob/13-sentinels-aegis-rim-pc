"""The title layout (GUI/title.mbs, FMBS v6 to v8) as the Exit Game tools need it: its record tables, the elements a part
draws, and byte edits recorded against the original data."""
import struct

SECTION_RECORD = {0: 24, 1: 48, 2: 48, 4: 20, 6: 28, 7: 36, 8: 32, 9: 48, 10: 24}
COLOURS, UVS, POSITIONS, SPRITES, ELEMENTS, TRANSFORMS, KEYFRAMES, PARTS, LAYERS = 0, 1, 2, 4, 6, 7, 8, 9, 10
TEXTURED = 1


class Layout:
    def __init__(self, data):
        assert data[:4] == b'FMBS' and data[0x12] in (6, 7, 8), 'not an FMBS v6/v7/v8 layout'
        self.original = bytes(data)
        self.d = bytearray(data)
        self.offsets = [struct.unpack_from('<Q', data, 0xb0 + 8 * i)[0] for i in range(11)]

    def section_size(self, s):
        later = sorted(o for o in self.offsets + [len(self.d)] if o > self.offsets[s])
        return later[0] - self.offsets[s]

    def count(self, s):
        return self.section_size(s) // SECTION_RECORD[s]

    def at(self, s, i, field=0):
        return self.offsets[s] + SECTION_RECORD[s] * i + field

    def sprite(self, i):
        o = self.at(SPRITES, i)
        info, kind, tex = struct.unpack_from('<I', self.d, o)[0], self.d[o + 5], self.d[o + 7]
        uv, colour, pos = struct.unpack_from('<HHI', self.d, o + 12)
        return dict(index=i, info=info, kind=kind, tex=tex, uv=uv, colour=colour, pos=pos)

    def element(self, i):
        first, count = struct.unpack_from('<I', self.d, self.at(ELEMENTS, i, 16))[0], struct.unpack_from('<H', self.d, self.at(ELEMENTS, i, 22))[0]
        return first, count

    def quad(self, s, i):
        """Centre and corners of a UV (s=1) or position (s=2) quad: six (x, y) points."""
        v = struct.unpack_from('<12f', self.d, self.at(s, i))
        return [(v[2 * k], v[2 * k + 1]) for k in range(6)]

    def bounds(self, s, i):
        pts = self.quad(s, i)[1:5]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        return min(xs), min(ys), max(xs), max(ys)

    def part(self, i):
        o = self.at(PARTS, i)
        name = bytes(self.d[o + 16:o + 40]).split(b'\0')[0].decode()
        return name, struct.unpack_from('<H', self.d, o + 40)[0], self.d[o + 42]

    def part_index(self, name):
        return next(i for i in range(self.count(PARTS)) if self.part(i)[0] == name)

    def layer_elements(self, layer):
        start, count = struct.unpack_from('<HH', self.d, self.at(LAYERS, layer))
        return [struct.unpack_from('<I', self.d, self.at(KEYFRAMES, k))[0] for k in range(start, start + count)]

    def part_layers(self, part):
        _, first, count = self.part(part)
        return list(range(first, first + count))

    def element_sprites(self, e):
        first, count = self.element(e)
        return [self.sprite(s) for s in range(first, first + count)]

    def users(self, parts):
        """sprite index -> set of parts that draw it, over the given parts."""
        out = {}
        for p in parts:
            for layer in self.part_layers(p):
                for e in set(self.layer_elements(layer)):
                    first, count = self.element(e)
                    for s in range(first, first + count):
                        out.setdefault(s, set()).add(p)
        return out

    def write(self, s, i, field, data):
        o = self.at(s, i, field)
        for k, b in enumerate(data):
            self.d[o + k] = b
        assert o + len(data) <= self.offsets[s] + self.section_size(s)

    def set_quad(self, s, i, points):
        self.write(s, i, 0, struct.pack('<12f', *[c for p in points for c in p]))

    def set_sprite(self, i, **fields):
        cur = self.sprite(i)
        cur.update(fields)
        o = SECTION_RECORD[SPRITES] * i
        rec = bytearray(self.d[self.offsets[SPRITES] + o:self.offsets[SPRITES] + o + 20])
        struct.pack_into('<I', rec, 0, cur['info'])
        rec[5], rec[7] = cur['kind'], cur['tex']
        struct.pack_into('<HHI', rec, 12, cur['uv'], cur['colour'], cur['pos'])
        self.write(SPRITES, i, 0, bytes(rec))

    def byte_edits(self):
        """[(section, offset in section, original bytes, new bytes)], adjacent changed bytes merged per record."""
        out = []
        for s in SECTION_RECORD:
            base, size, rec = self.offsets[s], self.section_size(s), SECTION_RECORD[s]
            for r in range(size // rec):
                o = base + r * rec
                old, new = self.original[o:o + rec], bytes(self.d[o:o + rec])
                if old != new:
                    first = next(k for k in range(rec) if old[k] != new[k])
                    last = max(k for k in range(rec) if old[k] != new[k])
                    out.append((s, r * rec + first, old[first:last + 1], new[first:last + 1]))
        return out


def rect_points(x0, y0, x1, y1):
    """A quad as the game stores it: centre, then the corners from the top-left going down, right, up."""
    return [((x0 + x1) / 2, (y0 + y1) / 2), (x0, y0), (x0, y1), (x1, y1), (x1, y0), (x0, y0)]
