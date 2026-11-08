#include "exit_game_assets.h"

#include <string.h>

#define LAYOUT_SECTION_TABLE 0xb0
#define BLOCK_BYTES 16

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t size) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

uint32_t exit_game_crc32(const uint8_t *data, size_t size) {
    return crc_update(0xffffffffu, data, size) ^ 0xffffffffu;
}

size_t exit_game_tiled_offset(uint32_t bx, uint32_t by, uint32_t pitch) {
    const uint32_t tiles_per_row = pitch / 4 / 8, x = bx & 7, y = by & 7;
    const uint32_t micro = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
    return ((size_t)(by / 8) * tiles_per_row + bx / 8) * 64 * BLOCK_BYTES + (size_t)micro * BLOCK_BYTES;
}

typedef struct {
    const uint8_t *at, *end;
} Reader;

static int take(Reader *r, size_t n, const uint8_t **out) {
    if ((size_t)(r->end - r->at) < n) return 0;
    if (out) *out = r->at;
    r->at += n;
    return 1;
}

static int u32(Reader *r, uint32_t *v) {
    const uint8_t *p;
    if (!take(r, 4, &p)) return 0;
    *v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return 1;
}

static uint16_t u16_at(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t u32_at(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t section_size(const ExitGameLabels *l, uint32_t section) {
    uint32_t end = l->layout_size;
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k)
        if (l->sections[k] > l->sections[section] && l->sections[k] < end) end = l->sections[k];
    return end - l->sections[section];
}

static int parse_region(Reader *r, const ExitGameLabels *l, ExitGameRegion *g) {
    if (!u32(r, &g->bx) || !u32(r, &g->by) || !u32(r, &g->bw) || !u32(r, &g->bh) || !u32(r, &g->original_crc) ||
        !u32(r, &g->pool_count) || !u32(r, &g->run_count))
        return 0;
    if (!g->bw || !g->bh || g->bx + g->bw > l->width / 4 || g->by + g->bh > l->height / 4 || !g->pool_count) return 0;
    if (!take(r, (size_t)g->pool_count * BLOCK_BYTES, &g->pool) || !take(r, (size_t)g->run_count * 4, &g->runs)) return 0;
    uint64_t blocks = 0;
    for (uint32_t i = 0; i < g->run_count; ++i) {
        if (u16_at(g->runs + 4 * i) >= g->pool_count) return 0;
        blocks += u16_at(g->runs + 4 * i + 2);
    }
    return blocks == (uint64_t)g->bw * g->bh;
}

static int parse_language(Reader *r, ExitGameLabels *l) {
    const uint8_t *key;
    if (!take(r, 12, &key)) return 0;
    memcpy(l->key, key, 12);
    l->key[12] = 0;
    if (!u32(r, &l->width) || !u32(r, &l->height) || !u32(r, &l->pitch) || !u32(r, &l->data_size)) return 0;
    if (l->width % 32 || l->height % 32 || l->pitch < l->width || (uint64_t)l->pitch * l->height != l->data_size) return 0;
    for (int i = 0; i < 4; ++i)
        if (!u32(r, &l->reference[i])) return 0;
    if (!u32(r, &l->reference_crc) || !u32(r, &l->region_count) || !l->region_count || l->region_count > EXIT_GAME_MAX_REGIONS) return 0;
    if (l->reference[0] + l->reference[2] > l->width / 4 || l->reference[1] + l->reference[3] > l->height / 4) return 0;
    for (uint32_t i = 0; i < l->region_count; ++i)
        if (!parse_region(r, l, &l->regions[i])) return 0;
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k)
        if (!u32(r, &l->sections[k])) return 0;
    if (!u32(r, &l->layout_size)) return 0;
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k)
        if (l->sections[k] < LAYOUT_SECTION_TABLE + 8 * EXIT_GAME_LAYOUT_SECTIONS || l->sections[k] >= l->layout_size) return 0;
    if (!u32(r, &l->edit_count) || !l->edit_count) return 0;
    l->edits = r->at;
    for (uint32_t i = 0; i < l->edit_count; ++i) {
        const uint8_t *e;
        if (!take(r, 8, &e)) return 0;
        const uint32_t section = e[0], length = e[1], offset = u32_at(e + 4);
        if (section >= EXIT_GAME_LAYOUT_SECTIONS || !length || offset + length > section_size(l, section)) return 0;
        if (!take(r, 2 * length, NULL)) return 0;
    }
    return 1;
}

int exit_game_asset_parse(const uint8_t *data, size_t size, ExitGameAsset *out) {
    memset(out, 0, sizeof(*out));
    if (!data || size < 16 || memcmp(data, "TSRXGL01", 8)) return 0;
    if (exit_game_crc32(data, size - 4) != u32_at(data + size - 4)) return 0;
    Reader r = {data + 8, data + size - 4};
    if (!u32(&r, &out->count) || !out->count || out->count > EXIT_GAME_MAX_LANGUAGES) return 0;
    uint32_t parsed = 0;
    while (parsed < out->count && parse_language(&r, &out->languages[parsed])) ++parsed;
    if (parsed == out->count && r.at == r.end) return 1;
    memset(out, 0, sizeof(*out));
    return 0;
}

/* CRC32 of the blocks in row order, as the asset tool computes it. */
static uint32_t blocks_crc(const uint8_t *pixels, uint32_t pitch, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t y = by; y < by + bh; ++y)
        for (uint32_t x = bx; x < bx + bw; ++x) crc = crc_update(crc, pixels + exit_game_tiled_offset(x, y, pitch), BLOCK_BYTES);
    return crc ^ 0xffffffffu;
}

/* Calls `visit` with each block of the region and the pool block it gets, row by row; stops early on 0. */
static int each_block(const ExitGameRegion *g, uint32_t pitch, uint8_t *pixels,
                      int (*visit)(uint8_t *block, const uint8_t *wanted)) {
    uint32_t k = 0;
    for (uint32_t run = 0; run < g->run_count; ++run) {
        const uint8_t *wanted = g->pool + (size_t)u16_at(g->runs + 4 * run) * BLOCK_BYTES;
        for (uint32_t n = u16_at(g->runs + 4 * run + 2); n; --n, ++k) {
            uint8_t *block = pixels + exit_game_tiled_offset(g->bx + k % g->bw, g->by + k / g->bw, pitch);
            if (!visit(block, wanted)) return 0;
        }
    }
    return 1;
}

static int is_wanted(uint8_t *block, const uint8_t *wanted) {
    return !memcmp(block, wanted, BLOCK_BYTES);
}

static int write_wanted(uint8_t *block, const uint8_t *wanted) {
    memcpy(block, wanted, BLOCK_BYTES);
    return 1;
}

ExitGamePatchStatus exit_game_texture_patch(const ExitGameLabels *l, uint32_t width, uint32_t height, uint32_t pitch,
                                            uint32_t size, uint8_t *pixels) {
    if (!pixels || width != l->width || height != l->height || pitch != l->pitch || size != l->data_size) return EXIT_GAME_OTHER;
    if (blocks_crc(pixels, pitch, l->reference[0], l->reference[1], l->reference[2], l->reference[3]) != l->reference_crc)
        return EXIT_GAME_OTHER;
    int original = 0, done = 0;
    for (uint32_t i = 0; i < l->region_count; ++i) {
        const ExitGameRegion *g = &l->regions[i];
        if (blocks_crc(pixels, pitch, g->bx, g->by, g->bw, g->bh) == g->original_crc) ++original;
        else if (each_block(g, pitch, pixels, is_wanted)) ++done;
    }
    if (done == (int)l->region_count) return EXIT_GAME_ALREADY;
    if (original != (int)l->region_count) return EXIT_GAME_MISMATCH;
    for (uint32_t i = 0; i < l->region_count; ++i) each_block(&l->regions[i], pitch, pixels, write_wanted);
    return EXIT_GAME_PATCHED;
}

ExitGamePatchStatus exit_game_layout_patch(const ExitGameLabels *l, uint8_t *layout) {
    if (!layout) return EXIT_GAME_OTHER;
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k) {
        uint64_t pointer;
        memcpy(&pointer, layout + LAYOUT_SECTION_TABLE + 8 * k, sizeof(pointer));
        if (pointer != (uint64_t)(uintptr_t)(layout + l->sections[k])) return EXIT_GAME_OTHER;
    }
    uint32_t original = 0, done = 0;
    const uint8_t *e = l->edits;
    for (uint32_t i = 0; i < l->edit_count; ++i) {
        const uint32_t length = e[1];
        const uint8_t *at = layout + l->sections[e[0]] + u32_at(e + 4);
        original += !memcmp(at, e + 8, length);
        done += !memcmp(at, e + 8 + length, length);
        e += 8 + 2 * length;
    }
    if (done == l->edit_count) return EXIT_GAME_ALREADY;
    if (original != l->edit_count) return EXIT_GAME_MISMATCH;
    e = l->edits;
    for (uint32_t i = 0; i < l->edit_count; ++i) {
        const uint32_t length = e[1];
        memcpy(layout + l->sections[e[0]] + u32_at(e + 4), e + 8 + length, length);
        e += 8 + 2 * length;
    }
    return EXIT_GAME_PATCHED;
}
