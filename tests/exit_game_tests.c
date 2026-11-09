/* Exit Game: the embedded label data, the texture and layout patches on synthetic data (original, already patched,
 * other data, damaged data, an atlas with two layouts), the hooks' behaviour (third row only while the art is in place,
 * confirm on that row only, one request), and the sites in the real boot images of USA 1.00 to 1.02, Japanese 1.00,
 * 1.03 and 1.04 and Asian 1.00 (build\image, game\work or TSR_TEST_IMAGES, only read). The patches on the real title
 * files are checked by tests/exit_game_layout_tests.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_image.h"
#include "exit_game.h"
#include "test_pages.h"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

typedef struct {
    uint8_t data[4096];
    size_t size;
} Buffer;

static void put(Buffer *b, const void *bytes, size_t n) {
    memcpy(b->data + b->size, bytes, n);
    b->size += n;
}

static void put32(Buffer *b, uint32_t v) {
    put(b, &v, 4);
}

static void put16(Buffer *b, uint16_t v) {
    put(b, &v, 2);
}

enum { TEX = 32, LAYOUT_SIZE = 0x200 };
static const uint8_t kOld[2][4] = {{1, 2, 3, 4}, {5, 6}};
static const uint8_t kNew[2][4] = {{9, 9, 9, 9}, {7, 7}};

static void block_bytes(uint8_t out[16], uint8_t seed) {
    for (int i = 0; i < 16; ++i) out[i] = (uint8_t)(seed + i);
}

static uint32_t region_crc(const uint8_t *pixels, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    uint8_t rows[64 * 16];
    size_t n = 0;
    for (uint32_t y = by; y < by + bh; ++y)
        for (uint32_t x = bx; x < bx + bw; ++x, n += 16) memcpy(rows + n, pixels + exit_game_tiled_offset(x, y, TEX), 16);
    return exit_game_crc32(rows, n);
}

/* Layout k of the synthetic assets: its sections start at 0x110 + 8k, 0x10 apart. */
static uint32_t section_at(int layout, int section) {
    return 0x110 + 8 * (uint32_t)layout + 0x10 * (uint32_t)section;
}

/* A one-language asset for a 32 x 32 atlas: reference blocks (0,0)-(1,1), the label at blocks (4,4)-(5,5), and per
 * layout (1 or 2, the second with other section offsets) two edits. */
static void make_asset_layouts(Buffer *b, const uint8_t *pixels, int layouts) {
    b->size = 0;
    put(b, "TSRXGL02", 8);
    put32(b, 1);
    put(b, "english\0\0\0\0\0", 12);
    put32(b, TEX); put32(b, TEX); put32(b, TEX); put32(b, TEX * TEX);
    put32(b, 0); put32(b, 0); put32(b, 2); put32(b, 2);
    put32(b, region_crc(pixels, 0, 0, 2, 2));
    put32(b, 1);
    put32(b, 4); put32(b, 4); put32(b, 2); put32(b, 2);
    put32(b, region_crc(pixels, 4, 4, 2, 2));
    put32(b, 2); put32(b, 2);
    uint8_t block[16];
    block_bytes(block, 0xa0); put(b, block, 16);
    block_bytes(block, 0xb0); put(b, block, 16);
    put16(b, 0); put16(b, 3); put16(b, 1); put16(b, 1);
    put32(b, (uint32_t)layouts);
    for (int v = 0; v < layouts; ++v) {
        put(b, layouts > 1 ? (v ? "update\0\0\0\0\0\0" : "base\0\0\0\0\0\0\0\0") : "\0\0\0\0\0\0\0\0\0\0\0\0", 12);
        for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k) put32(b, section_at(v, k));
        put32(b, LAYOUT_SIZE);
        put32(b, 2);
        const uint8_t e1[8] = {1, 4, 0, 0, 0, 0, 0, 0}, e2[8] = {10, 2, 0, 0, 8, 0, 0, 0};
        put(b, e1, 8); put(b, kOld[0], 4); put(b, kNew[0], 4);
        put(b, e2, 8); put(b, kOld[1], 2); put(b, kNew[1], 2);
    }
    put32(b, exit_game_crc32(b->data, b->size));
}

static void make_asset(Buffer *b, const uint8_t *pixels) {
    make_asset_layouts(b, pixels, 1);
}

static void fill_pixels(uint8_t *pixels) {
    for (int i = 0; i < TEX * TEX; ++i) pixels[i] = (uint8_t)(i * 7 + 3);
}

/* A loaded layout shaped as synthetic layout `v`: its first edit at 0x120 + 8v, its second at 0x1b8 + 8v. */
static void make_layout_as(uint8_t *layout, int v) {
    memset(layout, 0, LAYOUT_SIZE);
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k) {
        const uint64_t pointer = (uint64_t)(uintptr_t)(layout + section_at(v, k));
        memcpy(layout + 0xb0 + 8 * k, &pointer, 8);
    }
    memcpy(layout + section_at(v, 1), kOld[0], 4);
    memcpy(layout + section_at(v, 10) + 8, kOld[1], 2);
}

static void make_layout(uint8_t *layout) {
    make_layout_as(layout, 0);
}

static void test_embedded_asset(void) {
    size_t size = 0;
    const uint8_t *data = exit_game_embedded_asset(&size);
    ExitGameAsset asset;
    CHECK(exit_game_asset_parse(data, size, &asset));
    static const char *const keys[] = {"english", "french", "german", "italian", "spanish", "japanese", "schinese", "tchinese"};
    enum { LANGUAGES = sizeof(keys) / sizeof(*keys) };
    CHECK(asset.count == LANGUAGES);
    for (uint32_t i = 0; i < asset.count && i < LANGUAGES; ++i) {
        const ExitGameLabels *l = &asset.languages[i];
        CHECK(!strcmp(l->key, keys[i]));
        CHECK(l->region_count == 2);
        CHECK(l->height == 2048 && (l->width == 2048 || l->width == 4096) && l->data_size == l->width * l->height);
        const uint32_t layouts = strcmp(l->key, "japanese") ? 1 : 2;
        CHECK(l->layout_count == layouts);
        for (uint32_t v = 0; v < l->layout_count; ++v) CHECK(l->layouts[v].edit_count > 0);
        if (layouts == 1) CHECK(!l->layouts[0].tag[0]);
        else CHECK(!strcmp(l->layouts[0].tag, "base") && !strcmp(l->layouts[1].tag, "update") &&
                   memcmp(l->layouts[0].sections, l->layouts[1].sections, sizeof(l->layouts[0].sections)));
        for (uint32_t j = 0; j < i; ++j) {
            const ExitGameLabels *o = &asset.languages[j];
            CHECK(o->width != l->width || o->reference_crc != l->reference_crc ||
                  memcmp(o->reference, l->reference, sizeof(l->reference)));
        }
    }
    uint8_t *copy = malloc(size);
    memcpy(copy, data, size);
    copy[size / 2] ^= 1;
    CHECK(!exit_game_asset_parse(copy, size, &asset));
    copy[size / 2] ^= 1;
    CHECK(exit_game_asset_parse(copy, size, &asset));
    CHECK(!exit_game_asset_parse(copy, size - 4, &asset));
    CHECK(!exit_game_asset_parse(NULL, 0, &asset));
    free(copy);
}

static void test_tiled_offsets(void) {
    CHECK(exit_game_tiled_offset(0, 0, 2048) == 0);
    CHECK(exit_game_tiled_offset(9, 3, 2048) == 1200);
    CHECK(exit_game_tiled_offset(13, 10, 2048) == 66960);
    CHECK(exit_game_tiled_offset(7, 7, 4096) == 63 * 16);
}

static void test_texture_patch(void) {
    uint8_t pixels[TEX * TEX], original[TEX * TEX];
    fill_pixels(pixels);
    memcpy(original, pixels, sizeof(pixels));
    Buffer b;
    make_asset(&b, pixels);
    ExitGameAsset asset;
    CHECK(exit_game_asset_parse(b.data, b.size, &asset));
    const ExitGameLabels *l = &asset.languages[0];
    CHECK(exit_game_texture_patch(l, TEX, TEX * 2, TEX, TEX * TEX, pixels) == EXIT_GAME_OTHER);
    CHECK(exit_game_texture_patch(l, TEX, TEX, TEX, TEX * TEX - 1, pixels) == EXIT_GAME_OTHER);
    pixels[exit_game_tiled_offset(1, 1, TEX)] ^= 1;
    CHECK(exit_game_texture_patch(l, TEX, TEX, TEX, TEX * TEX, pixels) == EXIT_GAME_OTHER);
    CHECK(!memcmp(pixels + exit_game_tiled_offset(4, 4, TEX), original + exit_game_tiled_offset(4, 4, TEX), 16));
    pixels[exit_game_tiled_offset(1, 1, TEX)] ^= 1;
    pixels[exit_game_tiled_offset(5, 5, TEX) + 3] ^= 1;
    CHECK(exit_game_texture_patch(l, TEX, TEX, TEX, TEX * TEX, pixels) == EXIT_GAME_MISMATCH);
    pixels[exit_game_tiled_offset(5, 5, TEX) + 3] ^= 1;
    CHECK(!memcmp(pixels, original, sizeof(pixels)));
    CHECK(exit_game_texture_patch(l, TEX, TEX, TEX, TEX * TEX, pixels) == EXIT_GAME_PATCHED);
    uint8_t a[16], c[16];
    block_bytes(a, 0xa0);
    block_bytes(c, 0xb0);
    CHECK(!memcmp(pixels + exit_game_tiled_offset(4, 4, TEX), a, 16) && !memcmp(pixels + exit_game_tiled_offset(5, 4, TEX), a, 16));
    CHECK(!memcmp(pixels + exit_game_tiled_offset(4, 5, TEX), a, 16) && !memcmp(pixels + exit_game_tiled_offset(5, 5, TEX), c, 16));
    int outside_same = 1;
    for (uint32_t y = 0; y < TEX / 4; ++y)
        for (uint32_t x = 0; x < TEX / 4; ++x)
            if ((x < 4 || x > 5 || y < 4 || y > 5) &&
                memcmp(pixels + exit_game_tiled_offset(x, y, TEX), original + exit_game_tiled_offset(x, y, TEX), 16))
                outside_same = 0;
    CHECK(outside_same);
    CHECK(exit_game_texture_patch(l, TEX, TEX, TEX, TEX * TEX, pixels) == EXIT_GAME_ALREADY);
}

static void test_layout_patch(void) {
    uint8_t pixels[TEX * TEX];
    fill_pixels(pixels);
    Buffer b;
    make_asset(&b, pixels);
    ExitGameAsset asset;
    CHECK(exit_game_asset_parse(b.data, b.size, &asset));
    const ExitGameLabels *l = &asset.languages[0];
    static uint8_t layout[LAYOUT_SIZE];
    make_layout(layout);
    uint64_t moved = (uint64_t)(uintptr_t)(layout + 0x110 + 8);
    uint64_t saved;
    memcpy(&saved, layout + 0xb0, 8);
    memcpy(layout + 0xb0, &moved, 8);
    int which = 7;
    CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_OTHER && which == -1);
    memcpy(layout + 0xb0, &saved, 8);
    layout[0x1b0 + 9] ^= 1;
    CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_MISMATCH && which == 0);
    CHECK(!memcmp(layout + 0x120, kOld[0], 4));
    layout[0x1b0 + 9] ^= 1;
    CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_PATCHED && which == 0);
    CHECK(!memcmp(layout + 0x120, kNew[0], 4) && !memcmp(layout + 0x1b8, kNew[1], 2));
    CHECK(exit_game_layout_patch(l, layout, NULL) == EXIT_GAME_ALREADY);
    CHECK(exit_game_layout_patch(l, NULL, &which) == EXIT_GAME_OTHER);
}

/* One atlas, two layouts: each loaded layout gets the edits whose sections and original records it has, never the
 * other layout's. */
static void test_layout_variants(void) {
    uint8_t pixels[TEX * TEX];
    fill_pixels(pixels);
    Buffer b;
    make_asset_layouts(&b, pixels, 2);
    ExitGameAsset asset;
    CHECK(exit_game_asset_parse(b.data, b.size, &asset));
    const ExitGameLabels *l = &asset.languages[0];
    CHECK(l->layout_count == 2 && !strcmp(l->layouts[0].tag, "base") && !strcmp(l->layouts[1].tag, "update"));
    static uint8_t layout[LAYOUT_SIZE], original[LAYOUT_SIZE];
    for (int v = 0; v < 2; ++v) {
        make_layout_as(layout, v);
        memcpy(original, layout, LAYOUT_SIZE);
        int which = -1;
        CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_PATCHED && which == v);
        CHECK(!memcmp(layout + section_at(v, 1), kNew[0], 4) && !memcmp(layout + section_at(v, 10) + 8, kNew[1], 2));
        size_t changed = 0;
        for (int k = 0; k < LAYOUT_SIZE; ++k) changed += layout[k] != original[k];
        CHECK(changed == 6);
        CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_ALREADY && which == v);
        make_layout_as(layout, v);
        layout[section_at(v, 10) + 9] ^= 1;
        memcpy(original, layout, LAYOUT_SIZE);
        CHECK(exit_game_layout_patch(l, layout, &which) == EXIT_GAME_MISMATCH && which == v);
        CHECK(!memcmp(layout, original, LAYOUT_SIZE));
    }
    /* The second layout's sections holding the first layout's records (and the reverse) is not a match. */
    make_layout_as(layout, 1);
    memset(layout + section_at(1, 1), 0, 4);
    memset(layout + section_at(1, 10) + 8, 0, 2);
    memcpy(layout + section_at(0, 1), kOld[0], 4);
    memcpy(layout + section_at(0, 10) + 8, kOld[1], 2);
    memcpy(original, layout, LAYOUT_SIZE);
    CHECK(exit_game_layout_patch(l, layout, NULL) == EXIT_GAME_MISMATCH);
    CHECK(!memcmp(layout, original, LAYOUT_SIZE));
}

static int closes;
static int count_close(void) {
    ++closes;
    return 1;
}

typedef struct {
    uint8_t header[0x100];
    uint8_t pixels[TEX * TEX];
} FakeGnf;

static void make_gnf(FakeGnf *g) {
    memset(g->header, 0, sizeof(g->header));
    memcpy(g->header, "GNF ", 4);
    const uint32_t words[5] = {0, 41u << 20, (TEX - 1) | (TEX - 1) << 14, 13u << 20, (uint32_t)(TEX - 1) << 13};
    memcpy(g->header + 16, words, sizeof(words));
    fill_pixels(g->pixels);
}

static void run_texture_hook(FakeGnf *g, uint32_t size) {
    uint64_t frame[16] = {0};
    memcpy((uint8_t *)&frame[8] - 0x38, &size, 4);
    GuestRegs regs = {0};
    regs.rbx = (uint64_t)(uintptr_t)g->header;
    regs.rax = sizeof(g->header) - 8;
    regs.rbp = (uint64_t)(uintptr_t)&frame[8];
    exit_game_texture_hook(&regs);
}

static void run_layout_hook(uint8_t *layout, const ExitGameSites *sites) {
    uint8_t scene[0x200] = {0}, resource[0x200] = {0};
    const uint64_t r = (uint64_t)(uintptr_t)resource, l = (uint64_t)(uintptr_t)layout;
    memcpy(scene + sites->layout_resource, &r, 8);
    memcpy(resource + sites->parsed_layout, &l, 8);
    GuestRegs regs = {0};
    regs.r13 = (uint64_t)(uintptr_t)scene;
    exit_game_layout_hook(&regs);
}

static void confirm(int32_t row) {
    int32_t *cursor = &row;
    uint64_t frame[16] = {0};
    const uint64_t address = (uint64_t)(uintptr_t)cursor;
    memcpy((uint8_t *)&frame[8] - 0x38, &address, 8);
    GuestRegs regs = {0};
    regs.rbp = (uint64_t)(uintptr_t)&frame[8];
    exit_game_confirm_hook(&regs);
}

static void test_hooks(void) {
    FakeGnf gnf;
    make_gnf(&gnf);
    Buffer b;
    make_asset(&b, gnf.pixels);
    exit_game_reset();
    CHECK(exit_game_use_asset(b.data, b.size));
    const ExitGameSites sites = {"test", 0, {0}, 0, 0, 0, 0, 0x110, 0x180};
    exit_game_use_sites(&sites);
    exit_game_set_handler(count_close);
    closes = 0;

    GuestRegs regs = {0};
    regs.rax = 0x1234;
    exit_game_flag_hook(&regs);
    CHECK(regs.rax == 0x1234 && !regs.skip);
    confirm(EXIT_GAME_ROW);
    CHECK(closes == 0 && !exit_game_requested());

    static uint8_t layout[LAYOUT_SIZE];
    make_layout(layout);
    run_layout_hook(layout, &sites);
    CHECK(!exit_game_ready());
    CHECK(!memcmp(layout + 0x120, kOld[0], 4));
    run_texture_hook(&gnf, TEX * TEX - 16);
    CHECK(!exit_game_ready());
    run_texture_hook(&gnf, TEX * TEX);
    CHECK(!exit_game_ready());
    run_layout_hook(layout, &sites);
    CHECK(exit_game_ready() == 1);
    CHECK(!memcmp(layout + 0x120, kNew[0], 4));

    exit_game_flag_hook(&regs);
    CHECK(regs.skip == 1 && regs.rax && ((const uint8_t *)(uintptr_t)regs.rax)[0x14] == 1);
    GuestRegs nav = {0};
    nav.rbx = EXIT_GAME_ROW;
    exit_game_navigation_hook(&nav);
    CHECK(nav.skip == 1);

    confirm(0);
    confirm(1);
    CHECK(closes == 0 && !exit_game_requested());
    confirm(EXIT_GAME_ROW);
    CHECK(closes == 1 && exit_game_requested());
    confirm(EXIT_GAME_ROW);
    CHECK(closes == 1);

    make_layout(layout);
    layout[0x120] ^= 0x40;
    run_layout_hook(layout, &sites);
    CHECK(!exit_game_ready());
    regs = (GuestRegs){0};
    regs.rax = 0x1234;
    exit_game_flag_hook(&regs);
    CHECK(regs.rax == 0x1234 && !regs.skip);
    exit_game_reset();
}

/* The hooks with an atlas that has two layouts: either layout, once loaded, extends the menu. */
static void test_variant_hooks(void) {
    const ExitGameSites sites = {"test", 0, {0}, 0, 0, 0, 0, 0x110, 0x180};
    static uint8_t layout[LAYOUT_SIZE];
    for (int v = 0; v < 2; ++v) {
        FakeGnf gnf;
        make_gnf(&gnf);
        Buffer b;
        make_asset_layouts(&b, gnf.pixels, 2);
        exit_game_reset();
        CHECK(exit_game_use_asset(b.data, b.size));
        exit_game_use_sites(&sites);
        make_layout_as(layout, v);
        run_texture_hook(&gnf, TEX * TEX);
        CHECK(!exit_game_ready());
        run_layout_hook(layout, &sites);
        CHECK(exit_game_ready() == 1);
        CHECK(!memcmp(layout + section_at(v, 1), kNew[0], 4));
        make_layout_as(layout, 1 - v);
        layout[section_at(1 - v, 1)] ^= 1;
        run_layout_hook(layout, &sites);
        CHECK(!exit_game_ready());
    }
    exit_game_reset();
}

/* The menu's own confirm and cancel queries ask the game's input for its "decide" (0x20) and "cancel" (0x21) actions,
 * which follow the game's Cross/Circle convention; the dispatch the confirm hook sits in follows a call of the decide
 * query. */
static void check_menu_queries(const BootImage *image, const ExitGameSites *sites, uint32_t decide, uint32_t cancel,
                               uint32_t action_at) {
    CHECK(image->bytes[decide + action_at - 1] == 0xbe && image->bytes[decide + action_at] == 0x20);
    CHECK(image->bytes[cancel + action_at - 1] == 0xbe && image->bytes[cancel + action_at] == 0x21);
    int decided = 0;
    for (uint32_t at = sites->confirm_dispatch - 0x100; at < sites->confirm_dispatch; ++at) {
        int32_t rel;
        memcpy(&rel, image->bytes + at + 1, 4);
        if (image->bytes[at] == 0xe8 && (int64_t)at + 5 + rel == decide) decided = 1;
    }
    CHECK(decided);
}

/* The registers the hooks read hold what they assume: ebx is the menu's cursor row [r13+cursor] at the navigation and
 * decide reads, and [rbp-0x38] still holds the cursor's address when the dispatch runs after the decide branch. */
static void check_row_contract(const BootImage *image, const ExitGameSites *sites, uint32_t decide) {
    const uint8_t *b = image->bytes;
    const uint8_t *cursor = b + sites->flag_reads[2] - 7;
    const uint8_t row_load[] = {0x41, 0x8b, 0x9d, cursor[0], cursor[1], cursor[2], cursor[3]}; /* mov ebx,[r13+cursor] */
    const uint8_t cursor_store[] = {0x49, 0x8d, 0xbd, cursor[0], cursor[1], cursor[2], cursor[3], 0x48, 0x89, 0x7d, 0xc8};
    CHECK(cursor[0] == 0x00 || cursor[0] == 0x20);
    for (int read = 2; read <= 3; ++read) {
        const uint32_t at = sites->flag_reads[read] - 10;
        CHECK(!memcmp(b + at, row_load, sizeof(row_load)));
        CHECK(b[at + 7] == 0x49 && b[at + 8] == 0x89 && (b[at + 9] == 0xc6 || b[at + 9] == 0xc7));
    }
    uint32_t store = 0;
    for (uint32_t at = sites->flag_reads[1]; at < sites->flag_reads[2]; ++at)
        if (!memcmp(b + at, cursor_store, sizeof(cursor_store))) store = at;
    CHECK(store);
    int overwritten = 0;
    for (uint32_t at = store + sizeof(cursor_store); store && at + 3 < sites->confirm_dispatch; ++at)
        overwritten |= (b[at] == 0x48 || b[at] == 0x4c) && b[at + 1] == 0x89 && (b[at + 2] & 0xc7) == 0x45 && b[at + 3] == 0xc8;
    CHECK(!overwritten);
    int32_t rel;
    uint32_t decide_call = 0;
    for (uint32_t at = store; at < sites->flag_reads[3]; ++at) {
        memcpy(&rel, b + at + 1, 4);
        if (b[at] == 0xe8 && (int64_t)at + 5 + rel == decide) decide_call = at;
    }
    CHECK(decide_call && decide_call < sites->flag_reads[3] && sites->flag_reads[3] < sites->confirm_dispatch);
}

/* The image and a stub area within a jump of it, as the loader lays them out. */
static void check_install(const BootImage *image, const char *build, const ExitGameSites *sites) {
    const size_t stub_size = exit_game_stub_bytes(), image_span = (image->size + 0xffff) & ~(size_t)0xffff;
    uint8_t *memory = test_pages(image_span + stub_size, 0);
    if (!memory) {
        CHECK(!"no memory for the install check");
        return;
    }
    memcpy(memory, image->bytes, image->size);
    CHECK(!exit_game_install(memory, image->size, image->entry, "CUSA19610-9.99", memory + image_span, stub_size));
    CHECK(!memcmp(memory, image->bytes, image->size));
    CHECK(exit_game_install(memory, image->size, image->entry, build, memory + image_span, stub_size));
    int jumps = 0;
    for (int i = 0; i < EXIT_GAME_FLAG_READS; ++i) jumps += memory[sites->flag_reads[i]] == 0xe9;
    jumps += memory[sites->confirm_dispatch] == 0xe9;
    jumps += memory[sites->layout_bind] == 0xe9;
    jumps += memory[sites->texture_copy] == 0xe9;
    CHECK(jumps == EXIT_GAME_HOOK_COUNT);
    size_t changed = 0;
    for (uint64_t k = 0; k < image->size; ++k) changed += memory[k] != image->bytes[k];
    CHECK(changed <= EXIT_GAME_HOOK_COUNT * 7);
    test_pages_free(memory, image_span + stub_size);
    exit_game_reset();
}

static void test_real_sites(void) {
    static const struct {
        const char *folder, *work, *build;
        uint32_t decide, cancel, action_at;
    } kBuilds[] = {
        {"CUSA19610-1.00", "image-1.00", "CUSA19610-1.00", 0x36e490, 0x36e4c0, 0x22},
        {"CUSA19610-1.01", "image-1.01", "CUSA19610-1.01", 0x36ee20, 0x36ee50, 0x22},
        {"CUSA19610-1.02", "image-1.02", "CUSA19610-1.02", 0x3293d0, 0x329400, 0x23},
        {"CUSA10602-1.00", "image-jp-1.00", "CUSA10602-1.00", 0x308d50, 0x308d80, 0x1f},
        {"CUSA10602-1.03", "image-jp-1.03", "CUSA10602-1.03", 0x36e510, 0x36e540, 0x22},
        {"CUSA10602-1.04", "image-jp-1.04", "CUSA10602-1.04", 0x328d70, 0x328da0, 0x23},
        {"CUSA18093-1.00", "image-asia-1.00", "CUSA18093-1.00", 0x3798a0, 0x3798d0, 0x1f},
    };
    enum { BUILDS = sizeof(kBuilds) / sizeof(*kBuilds) };
    int verified = 0;
    char done[160] = "";
    for (int i = 0; i < BUILDS; ++i) {
        BootImage image = {0};
        if (!boot_image_load_build(kBuilds[i].folder, kBuilds[i].work, &image)) {
            CHECK(!boot_image_required());
            printf("skipped: no %s boot image (build\\image, game\\work or TSR_TEST_IMAGES)\n", kBuilds[i].build);
            continue;
        }
        ExitGameSites sites;
        const char *reason = NULL;
        const ExitGameLocate found = exit_game_locate(image.bytes, image.size, kBuilds[i].build, &sites, &reason);
        CHECK(found == EXIT_GAME_FOUND);
        if (found != EXIT_GAME_FOUND) {
            printf("%s: %s differs\n", kBuilds[i].build, reason ? reason : "?");
            free(image.bytes);
            continue;
        }
        CHECK(exit_game_locate(image.bytes, image.size, kBuilds[(i + 1) % BUILDS].build, NULL, &reason) == EXIT_GAME_SITE_MISMATCH && reason);
        CHECK(exit_game_locate(image.bytes, image.size, "CUSA99999-1.00", NULL, NULL) == EXIT_GAME_UNKNOWN_BUILD);
        CHECK(exit_game_locate(image.bytes, image.size, NULL, NULL, NULL) == EXIT_GAME_UNKNOWN_BUILD);
        check_menu_queries(&image, &sites, kBuilds[i].decide, kBuilds[i].cancel, kBuilds[i].action_at);
        check_row_contract(&image, &sites, kBuilds[i].decide);

        const uint8_t saved = image.bytes[sites.confirm_dispatch + 2];
        image.bytes[sites.confirm_dispatch + 2] ^= 0x08;
        CHECK(exit_game_locate(image.bytes, image.size, kBuilds[i].build, NULL, &reason) == EXIT_GAME_SITE_MISMATCH &&
              reason && !strcmp(reason, "confirm dispatch"));
        image.bytes[sites.confirm_dispatch + 2] = saved;
        const uint8_t call = image.bytes[sites.flag_reads[4]];
        image.bytes[sites.flag_reads[4]] = 0x90;
        CHECK(exit_game_locate(image.bytes, image.size, kBuilds[i].build, NULL, &reason) == EXIT_GAME_SITE_MISMATCH &&
              reason && !strcmp(reason, "flag read"));
        image.bytes[sites.flag_reads[4]] = call;
        image.bytes[sites.texture_copy + 4] ^= 1;
        CHECK(exit_game_locate(image.bytes, image.size, kBuilds[i].build, NULL, &reason) == EXIT_GAME_SITE_MISMATCH &&
              reason && !strcmp(reason, "texture copy"));
        image.bytes[sites.texture_copy + 4] ^= 1;

        check_install(&image, kBuilds[i].build, &sites);
        free(image.bytes);
        ++verified;
        snprintf(done + strlen(done), sizeof(done) - strlen(done), "%s%s", *done ? ", " : "", kBuilds[i].build);
    }
    if (verified) printf("real boot images: Exit Game sites verified and installed on %d of %d builds (%s)\n", verified, BUILDS, done);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_embedded_asset();
    test_tiled_offsets();
    test_texture_patch();
    test_layout_patch();
    test_layout_variants();
    test_hooks();
    test_variant_hooks();
    test_real_sites();
    printf("exit game: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
