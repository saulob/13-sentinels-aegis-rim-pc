#include "exit_game.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define GNF_DESCRIPTOR 16
#define BC7_FORMAT 41
#define THIN_1D_TILING 13
#define NAVIGATION_READ 2

static const ExitGameSites kBuilds[] = {
    {"CUSA19610-1.00", 0x37cf0, {0x3b0466, 0x3b0839, 0x3b0907, 0x3b096b, 0x3b0ac4, 0x3b0aed, 0x3b0b5d}, 0x3b09dc, 0x3b02ad,
     0x68b51c, 0x639a80, 0xf0, 0x160},
    {"CUSA19610-1.01", 0x37da0, {0x3b1952, 0x3b1d6d, 0x3b1e47, 0x3b1eab, 0x3b2004, 0x3b202d, 0x3b209d}, 0x3b1f1c, 0x3b179d,
     0x68d7ac, 0x63b1c0, 0xf0, 0x160},
    {"CUSA19610-1.02", 0x3b5a0, {0x36adb8, 0x36b246, 0x36b319, 0x36b37d, 0x36b4b4, 0x36b4dd, 0x36b54d}, 0x36b3e7, 0x36ac8d,
     0x5c6e48, 0x56fee0, 0x110, 0x180},
    {"CUSA10602-1.00", 0x374c0, {0x347c63, 0x347f94, 0x348067, 0x3480cb, 0x348214, 0x34823d, 0x3482ad}, 0x3481a6, 0x347b4d,
     0x61f27c, 0x5cd750, 0xf0, 0x160},
    {"CUSA10602-1.03", 0x37aa0, {0x3afb92, 0x3aff77, 0x3b0047, 0x3b00ab, 0x3b0204, 0x3b022d, 0x3b029d}, 0x3b011c, 0x3af9dd,
     0x68bc4c, 0x639400, 0xf0, 0x160},
    {"CUSA10602-1.04", 0x3b270, {0x369148, 0x36956d, 0x369649, 0x3696ad, 0x3697e4, 0x36980d, 0x36987d}, 0x369717, 0x36901d,
     0x5c50d8, 0x56e170, 0x110, 0x180},
    {"CUSA18093-1.00", 0x37ed0, {0x3ba013, 0x3ba476, 0x3ba547, 0x3ba5ab, 0x3ba6f4, 0x3ba71d, 0x3ba78d}, 0x3ba686, 0x3b9edd,
     0x691a2c, 0x63fb20, 0xf0, 0x160},
};

static const struct {
    const char *key, *name;
} kLanguageNames[] = {{"english", "English"}, {"french", "French"}, {"german", "German"}, {"italian", "Italian"},
                      {"spanish", "Spanish"}, {"japanese", "Japanese"}, {"schinese", "Simplified Chinese"},
                      {"tchinese", "Traditional Chinese"}};

static ExitGameAsset asset;
static ExitGameSites sites;
static ExitGameHandler handler;
static atomic_int texture_language, layout_language, layout_variant, ready, requested, logged_layout, logged_texture,
    last_row = -1;

/* The title's flag block as the hooked reads see it once Exit Game is in place: the third row shown. */
static uint8_t shown_flags[0x20] = {[0x14] = 1};

static const char *language_name(int index) {
    const char *key = index >= 0 && (uint32_t)index < asset.count ? asset.languages[index].key : "";
    for (size_t i = 0; i < sizeof(kLanguageNames) / sizeof(*kLanguageNames); ++i)
        if (!strcmp(key, kLanguageNames[i].key)) return kLanguageNames[i].name;
    return key;
}

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint64_t rd64(uint64_t address) {
    uint64_t v;
    memcpy(&v, (const void *)(uintptr_t)address, sizeof(v));
    return v;
}

void exit_game_set_handler(ExitGameHandler value) {
    handler = value;
}

void exit_game_reset(void) {
    atomic_store(&texture_language, 0);
    atomic_store(&layout_language, 0);
    atomic_store(&layout_variant, 0);
    atomic_store(&ready, 0);
    atomic_store(&requested, 0);
    atomic_store(&logged_layout, 0);
    atomic_store(&logged_texture, 0);
    atomic_store(&last_row, -1);
    handler = NULL;
}

int exit_game_ready(void) {
    return atomic_load(&ready);
}

int exit_game_requested(void) {
    return atomic_load(&requested);
}

int exit_game_use_asset(const uint8_t *data, size_t size) {
    return exit_game_asset_parse(data, size, &asset);
}

void exit_game_use_sites(const ExitGameSites *value) {
    sites = *value;
}

/* "English", or "Japanese, update layout" when the language's atlas has several layouts. */
static const char *layout_name(int index, int variant, char *out, size_t size) {
    const ExitGameLabels *l = &asset.languages[index];
    if (l->layout_count < 2 || variant < 0 || (uint32_t)variant >= l->layout_count) return language_name(index);
    snprintf(out, size, "%s, %s layout", language_name(index), l->layouts[variant].tag);
    return out;
}

static void update_ready(void) {
    const int texture = atomic_load(&texture_language), layout = atomic_load(&layout_language);
    const int now = texture && texture == layout ? texture : 0;
    char name[64];
    if (atomic_exchange(&ready, now) != now && now)
        printf("Exit Game: title menu extended to 3 rows (%s)\n", layout_name(now - 1, atomic_load(&layout_variant), name, sizeof(name)));
}

void GUEST_HOOK_ABI exit_game_flag_hook(GuestRegs *regs) {
    if (!atomic_load(&ready)) return;
    regs->rax = (uint64_t)(uintptr_t)shown_flags;
    regs->skip = 1;
}

/* After the cursor moved; rbx holds the new row. */
void GUEST_HOOK_ABI exit_game_navigation_hook(GuestRegs *regs) {
    exit_game_flag_hook(regs);
    if (!atomic_load(&ready)) return;
    const int row = (int)(uint32_t)regs->rbx;
    if (atomic_exchange(&last_row, row) != row && row == EXIT_GAME_ROW) printf("Exit Game: selected\n");
}

/* The menu's dispatch after the game's own confirm: rbp-0x38 holds the address of the confirmed row. */
void GUEST_HOOK_ABI exit_game_confirm_hook(GuestRegs *regs) {
    if (!atomic_load(&ready)) return;
    int32_t row;
    memcpy(&row, (const void *)(uintptr_t)rd64(regs->rbp - 0x38), sizeof(row));
    if (row != EXIT_GAME_ROW || atomic_exchange(&requested, 1)) return;
    printf("Exit Game: confirmed\n");
    if (!handler || !handler()) printf("Exit Game: no host window to close\n");
}

void GUEST_HOOK_ABI exit_game_layout_hook(GuestRegs *regs) {
    const uint64_t resource = rd64(regs->r13 + sites.layout_resource);
    uint8_t *layout = resource ? (uint8_t *)(uintptr_t)rd64(resource + sites.parsed_layout) : NULL;
    if (!layout) return;
    /* The layout's third row points at the label art, so only the language whose art is in place is patched. */
    const int art = atomic_load(&texture_language);
    ExitGamePatchStatus status = EXIT_GAME_OTHER;
    int variant = -1;
    if (art) status = exit_game_layout_patch(&asset.languages[art - 1], layout, &variant);
    const int language = status == EXIT_GAME_PATCHED || status == EXIT_GAME_ALREADY ? art : 0;
    atomic_store(&layout_variant, language ? variant : 0);
    atomic_store(&layout_language, language);
    if (!language && !atomic_exchange(&logged_layout, 1)) {
        char name[64];
        if (!art) printf("Exit Game: title art not recognised, menu left with 2 rows\n");
        else if (status == EXIT_GAME_MISMATCH)
            printf("Exit Game: unsupported title layout (%s: records differ), menu left with 2 rows\n",
                   layout_name(art - 1, variant, name, sizeof(name)));
        else printf("Exit Game: unsupported title layout (%s: unknown layout), menu left with 2 rows\n", language_name(art - 1));
    }
    update_ready();
}

/* A GNF texture's pixels just before the game copies or keeps them: rbx = the GNF, its pixels at rbx + rax + 8,
 * their size at rbp-0x38. */
void GUEST_HOOK_ABI exit_game_texture_hook(GuestRegs *regs) {
    const uint8_t *gnf = (const uint8_t *)(uintptr_t)regs->rbx;
    if (!gnf || memcmp(gnf, "GNF ", 4)) return;
    const uint8_t *t = gnf + GNF_DESCRIPTOR;
    const uint32_t word1 = rd32(t + 4), word2 = rd32(t + 8), word3 = rd32(t + 12), word4 = rd32(t + 16);
    if ((word1 >> 20 & 0x3f) != BC7_FORMAT || (word3 >> 20 & 0x1f) != THIN_1D_TILING) return;
    const uint32_t width = (word2 & 0x3fff) + 1, height = (word2 >> 14 & 0x3fff) + 1, pitch = (word4 >> 13 & 0x3fff) + 1;
    uint32_t size;
    memcpy(&size, (const void *)(uintptr_t)(regs->rbp - 0x38), sizeof(size));
    uint8_t *pixels = (uint8_t *)(uintptr_t)(regs->rbx + regs->rax + 8);
    for (uint32_t i = 0; i < asset.count; ++i) {
        const ExitGamePatchStatus status = exit_game_texture_patch(&asset.languages[i], width, height, pitch, size, pixels);
        if (status == EXIT_GAME_PATCHED || status == EXIT_GAME_ALREADY) {
            if (atomic_exchange(&texture_language, (int)i + 1) != (int)i + 1)
                printf("Exit Game: text installed %s\n", language_name((int)i));
            update_ready();
            return;
        }
        if (status == EXIT_GAME_MISMATCH && !atomic_exchange(&logged_texture, 1))
            printf("Exit Game: the %s title art differs from the known one, menu left with 2 rows\n", language_name((int)i));
    }
}

static const uint8_t kFlagRead[] = {0x80, 0x78, 0x14, 0x00};          /* cmp byte [rax+0x14],0 */
static const uint8_t kCountRead[] = {0x31, 0xc9, 0x80, 0x78, 0x14, 0x00}; /* xor ecx,ecx; cmp byte [rax+0x14],0 */
static const uint8_t kFlagLoad[] = {0x8a, 0x40, 0x14};                /* mov al,[rax+0x14] */
static const uint8_t kGetter[] = {0x48, 0x8b, 0x3d};                  /* mov rdi,[rip+...]; jmp (1.02) */
static const uint8_t kGetterLea[] = {0x48, 0x8d, 0x05};               /* lea rax,[rip+...]; mov rdi,[rax]; jmp */
static const uint8_t kGetterLoad[] = {0x48, 0x8b, 0x38, 0xe9};
static const uint8_t kDispatch[] = {0x48, 0x8b, 0x45, 0xc8, 0x8b, 0x00, 0x83, 0xf8, 0x02};
static const uint8_t kNavigation[] = {0x49, 0x89, 0xc7};              /* mov r15,rax (the row is in ebx) */
static const uint8_t kNavigationR14[] = {0x49, 0x89, 0xc6};           /* mov r14,rax (Japanese 1.00) */
static const uint8_t kTextureCopy[] = {0x4c, 0x01, 0xe0, 0x41, 0xf6, 0x46, 0x20, 0x08};
static const uint8_t kLayoutGetter[] = {0x48, 0x8b, 0x47, 0x08, 0x48, 0x85, 0xc0, 0x74, 0x08, 0x48, 0x8b, 0x80};

static int at(const uint8_t *image, uint64_t size, uint64_t offset, const uint8_t *bytes, size_t length) {
    return offset <= size && length <= size - offset && !memcmp(image + offset, bytes, length);
}

static int flag_read_matches(const uint8_t *image, uint64_t size, const ExitGameSites *s, uint32_t call) {
    if (!at(image, size, call, (const uint8_t[]){0xe8}, 1) || (uint64_t)call + 11 > size) return 0;
    int32_t rel;
    memcpy(&rel, image + call + 1, sizeof(rel));
    if ((int64_t)call + 5 + rel != (int64_t)s->flag_getter) return 0;
    return at(image, size, call + 5, kFlagRead, sizeof(kFlagRead)) || at(image, size, call + 5, kCountRead, sizeof(kCountRead)) ||
           at(image, size, call + 5, kFlagLoad, sizeof(kFlagLoad));
}

static const char *check_sites(const uint8_t *image, uint64_t size, const ExitGameSites *s) {
    const int getter = (at(image, size, s->flag_getter, kGetter, sizeof(kGetter)) && at(image, size, s->flag_getter + 7, (const uint8_t[]){0xe9}, 1)) ||
                       (at(image, size, s->flag_getter, kGetterLea, sizeof(kGetterLea)) &&
                        at(image, size, s->flag_getter + 7, kGetterLoad, sizeof(kGetterLoad)));
    if (!getter) return "flag getter";
    for (int i = 0; i < EXIT_GAME_FLAG_READS; ++i)
        if (!flag_read_matches(image, size, s, s->flag_reads[i])) return "flag read";
    const uint32_t navigation = s->flag_reads[NAVIGATION_READ] - sizeof(kNavigation);
    if (!at(image, size, navigation, kNavigation, sizeof(kNavigation)) &&
        !at(image, size, navigation, kNavigationR14, sizeof(kNavigationR14)))
        return "menu navigation";
    if (!at(image, size, s->confirm_dispatch, kDispatch, sizeof(kDispatch))) return "confirm dispatch";
    const uint8_t bind[] = {0x49, 0x8b, 0xb5, s->layout_resource & 0xff, s->layout_resource >> 8 & 0xff, 0, 0,
                            0x49, 0x8b, 0x95, (s->layout_resource + 8) & 0xff, (s->layout_resource + 8) >> 8 & 0xff, 0, 0};
    if (!at(image, size, s->layout_bind, bind, sizeof(bind))) return "layout bind";
    if (!at(image, size, s->texture_copy - 3, kTextureCopy, sizeof(kTextureCopy))) return "texture copy";
    const uint8_t field[] = {s->parsed_layout & 0xff, s->parsed_layout >> 8 & 0xff, 0, 0, 0xc3};
    if (!at(image, size, s->resource_layout_getter, kLayoutGetter, sizeof(kLayoutGetter)) ||
        !at(image, size, s->resource_layout_getter + sizeof(kLayoutGetter), field, sizeof(field)))
        return "layout resource";
    return NULL;
}

ExitGameLocate exit_game_locate(const uint8_t *image, uint64_t size, const char *build, ExitGameSites *out, const char **reason) {
    const ExitGameSites *known = NULL;
    for (size_t i = 0; build && i < sizeof(kBuilds) / sizeof(*kBuilds); ++i)
        if (!strcmp(build, kBuilds[i].build)) known = &kBuilds[i];
    if (reason) *reason = NULL;
    if (!known) return EXIT_GAME_UNKNOWN_BUILD;
    const char *bad = check_sites(image, size, known);
    if (bad) {
        if (reason) *reason = bad;
        return EXIT_GAME_SITE_MISMATCH;
    }
    if (out) *out = *known;
    return EXIT_GAME_FOUND;
}

size_t exit_game_stub_bytes(void) {
    return EXIT_GAME_HOOK_COUNT * guest_hook_stub_size();
}

int exit_game_install(uint8_t *image, uint64_t size, uint64_t entry, const char *build, uint8_t *stubs, size_t stubs_size) {
    const char *reason = NULL;
    switch (exit_game_locate(image, size, build, &sites, &reason)) {
    case EXIT_GAME_FOUND: break;
    case EXIT_GAME_UNKNOWN_BUILD:
        printf("Exit Game: not installed: not a verified game build\n");
        return 0;
    case EXIT_GAME_SITE_MISMATCH:
        printf("Exit Game: hook verification failed (%s of %s), not installed\n", reason, build);
        return 0;
    }
    size_t asset_size = 0;
    const uint8_t *data = exit_game_embedded_asset(&asset_size);
    if (!exit_game_asset_parse(data, asset_size, &asset)) {
        printf("Exit Game: not installed: the title label data is damaged\n");
        return 0;
    }
    GuestHook hooks[EXIT_GAME_HOOK_COUNT];
    for (int i = 0; i < EXIT_GAME_FLAG_READS; ++i)
        hooks[i] = (GuestHook){"exit game flag", sites.flag_reads[i], 5, {0},
                               i == NAVIGATION_READ ? exit_game_navigation_hook : exit_game_flag_hook};
    hooks[EXIT_GAME_FLAG_READS] = (GuestHook){"exit game confirm", sites.confirm_dispatch, 6, {0}, exit_game_confirm_hook};
    hooks[EXIT_GAME_FLAG_READS + 1] = (GuestHook){"exit game layout", sites.layout_bind, 7, {0}, exit_game_layout_hook};
    hooks[EXIT_GAME_FLAG_READS + 2] = (GuestHook){"exit game texture", sites.texture_copy, 5, {0}, exit_game_texture_hook};
    for (int i = 0; i < EXIT_GAME_HOOK_COUNT; ++i) memcpy(hooks[i].original, image + hooks[i].offset, hooks[i].length);
    const GuestImageId id = {size, entry};
    size_t bad = 0;
    const GuestHookStatus status = guest_hooks_install(image, size, entry, &id, hooks, EXIT_GAME_HOOK_COUNT, stubs, stubs_size, &bad);
    if (status != GUEST_HOOK_OK) {
        printf("Exit Game: hook verification failed (%s: %s), not installed\n", hooks[bad].name, guest_hook_status_text(status));
        return 0;
    }
    printf("Exit Game: installed (%s, %u languages)\n", build, (unsigned)asset.count);
    return 1;
}
