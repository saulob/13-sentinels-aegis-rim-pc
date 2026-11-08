/* Exit Game: the embedded label data, the texture and layout patches on synthetic data (original, already patched,
 * other data, damaged data), the hooks' behaviour (third row only while the art is in place, confirm on that row only,
 * one request), and the sites in the real boot images of USA 1.00, 1.01 and 1.02 (build\image or game\work, only
 * read). The patches on the real title files are checked by tests/exit_game_layout_tests.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "boot_image.h"
#include "exit_game.h"

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

/* A one-language asset for a 32 x 32 atlas: reference blocks (0,0)-(1,1), the label at blocks (4,4)-(5,5), and two
 * layout edits. */
static void make_asset(Buffer *b, const uint8_t *pixels) {
    b->size = 0;
    put(b, "TSRXGL01", 8);
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
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k) put32(b, 0x110 + 0x10 * k);
    put32(b, LAYOUT_SIZE);
    put32(b, 2);
    const uint8_t e1[8] = {1, 4, 0, 0, 0, 0, 0, 0}, e2[8] = {10, 2, 0, 0, 8, 0, 0, 0};
    put(b, e1, 8); put(b, kOld[0], 4); put(b, kNew[0], 4);
    put(b, e2, 8); put(b, kOld[1], 2); put(b, kNew[1], 2);
    put32(b, exit_game_crc32(b->data, b->size));
}

static void fill_pixels(uint8_t *pixels) {
    for (int i = 0; i < TEX * TEX; ++i) pixels[i] = (uint8_t)(i * 7 + 3);
}

static void make_layout(uint8_t *layout) {
    memset(layout, 0, LAYOUT_SIZE);
    for (int k = 0; k < EXIT_GAME_LAYOUT_SECTIONS; ++k) {
        const uint64_t pointer = (uint64_t)(uintptr_t)(layout + 0x110 + 0x10 * k);
        memcpy(layout + 0xb0 + 8 * k, &pointer, 8);
    }
    memcpy(layout + 0x120, kOld[0], 4);
    memcpy(layout + 0x1b0 + 8, kOld[1], 2);
}

static void test_embedded_asset(void) {
    size_t size = 0;
    const uint8_t *data = exit_game_embedded_asset(&size);
    ExitGameAsset asset;
    CHECK(exit_game_asset_parse(data, size, &asset));
    static const char *const keys[] = {"english", "french", "german", "italian", "spanish"};
    CHECK(asset.count == 5);
    for (uint32_t i = 0; i < asset.count && i < 5; ++i) {
        const ExitGameLabels *l = &asset.languages[i];
        CHECK(!strcmp(l->key, keys[i]));
        CHECK(l->region_count == 2 && l->edit_count > 0);
        CHECK(l->height == 2048 && (l->width == 2048 || l->width == 4096) && l->data_size == l->width * l->height);
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
    CHECK(exit_game_layout_patch(l, layout) == EXIT_GAME_OTHER);
    memcpy(layout + 0xb0, &saved, 8);
    layout[0x1b0 + 9] ^= 1;
    CHECK(exit_game_layout_patch(l, layout) == EXIT_GAME_MISMATCH);
    CHECK(!memcmp(layout + 0x120, kOld[0], 4));
    layout[0x1b0 + 9] ^= 1;
    CHECK(exit_game_layout_patch(l, layout) == EXIT_GAME_PATCHED);
    CHECK(!memcmp(layout + 0x120, kNew[0], 4) && !memcmp(layout + 0x1b8, kNew[1], 2));
    CHECK(exit_game_layout_patch(l, layout) == EXIT_GAME_ALREADY);
    CHECK(exit_game_layout_patch(l, NULL) == EXIT_GAME_OTHER);
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

/* The image and a stub area within a jump of it, as the loader lays them out. */
static void check_install(const BootImage *image, const char *build, const ExitGameSites *sites) {
    const size_t stub_size = exit_game_stub_bytes(), image_span = (image->size + 0xffff) & ~(size_t)0xffff;
    uint8_t *memory = VirtualAlloc(NULL, image_span + stub_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
    VirtualFree(memory, 0, MEM_RELEASE);
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
    };
    int verified = 0;
    for (int i = 0; i < 3; ++i) {
        BootImage image = {0};
        if (!boot_image_load_build(kBuilds[i].folder, kBuilds[i].work, &image)) {
            printf("skipped: no %s boot image (build\\image or game\\work)\n", kBuilds[i].build);
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
        CHECK(exit_game_locate(image.bytes, image.size, kBuilds[(i + 1) % 3].build, NULL, &reason) == EXIT_GAME_SITE_MISMATCH && reason);
        CHECK(exit_game_locate(image.bytes, image.size, "CUSA10602-1.00", NULL, NULL) == EXIT_GAME_UNKNOWN_BUILD);
        CHECK(exit_game_locate(image.bytes, image.size, NULL, NULL, NULL) == EXIT_GAME_UNKNOWN_BUILD);
        check_menu_queries(&image, &sites, kBuilds[i].decide, kBuilds[i].cancel, kBuilds[i].action_at);

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
    }
    if (verified == 3) printf("real boot images: USA 1.00, 1.01 and 1.02 Exit Game sites verified and installed\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_embedded_asset();
    test_tiled_offsets();
    test_texture_patch();
    test_layout_patch();
    test_hooks();
    test_real_sites();
    printf("exit game: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
