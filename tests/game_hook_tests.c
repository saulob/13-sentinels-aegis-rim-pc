/* Skip Intro hook: its sites in the real boot images (build\image or game\work, only read), refusal of the
 * wrong build, an unknown build and changed code, and the hook runs on the CPU over a small synthetic function. Whether
 * the game skips its logos stays a manual check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "guest_hook.h"
#include "skip_intro.h"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

typedef struct {
    uint8_t *bytes;
    uint64_t size, entry;
} Image;

static uint64_t le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

/* The memory image of a boot.bin, laid out as the loader reads it; relocations are left unapplied (no site has one). */
static int load_image(const char *path, Image *image) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    const long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)length);
    const int read_all = file && fread(file, 1, (size_t)length, f) == (size_t)length;
    fclose(f);
    if (!read_all || length < 8 * 15 || memcmp(file, "TSRBOOT1", 8)) { free(file); return 0; }
    const uint8_t *p = file + 8;
    const uint64_t size = le64(p), entry = le64(p + 8), ns = le64(p + 16), nr = le64(p + 24), imports = le64(p + 32);
    p += 6 * 8 + 8 + 4 * 8;
    const uint64_t modules = le64(p);
    p += 8 + modules * 7 * 8;
    const uint64_t bindings = le64(p);
    p += 8 + bindings * 3 * 8 + ns * 3 * 8 + imports * 128 + nr * 4 * 8;
    if ((uint64_t)(p - file) + size != (uint64_t)length) { free(file); return 0; }
    image->bytes = malloc(size);
    memcpy(image->bytes, p, size);
    image->size = size;
    image->entry = entry;
    free(file);
    return 1;
}

static int load_build(const char *folder, const char *work, Image *image) {
    char path[512];
    snprintf(path, sizeof(path), "%s/build/image/%s/boot.bin", TSR_SOURCE_DIR, folder);
    if (load_image(path, image)) return 1;
    snprintf(path, sizeof(path), "%s/game/work/%s/boot.bin", TSR_SOURCE_DIR, work);
    return load_image(path, image);
}

static void test_real_sites(void) {
    Image base = {0}, first = {0}, update = {0};
    const int have_base = load_build("CUSA19610-1.00", "image-1.00", &base);
    const int have_first = load_build("CUSA19610-1.01", "image-1.01", &first);
    const int have_update = load_build("CUSA19610-1.02", "image-1.02", &update);
    uint32_t site = 0;
    if (have_base) {
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.00", &site) == SKIP_INTRO_FOUND && site == 0x3aff67);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.02", &site) == SKIP_INTRO_MISMATCH);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA10602-1.00", &site) == SKIP_INTRO_UNKNOWN_BUILD);
        CHECK(skip_intro_locate(base.bytes, base.size, NULL, &site) == SKIP_INTRO_UNKNOWN_BUILD);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.01", &site) == SKIP_INTRO_MISMATCH);

        uint8_t saved = base.bytes[0x3aff67 + 1];
        base.bytes[0x3aff67 + 1] ^= 0x10;
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.00", &site) == SKIP_INTRO_MISMATCH);
        base.bytes[0x3aff67 + 1] = saved;

        uint8_t stubs[256];
        CHECK(!skip_intro_install(base.bytes, base.size, base.entry, "CUSA19610-1.02", stubs, sizeof(stubs)));
        CHECK(base.bytes[0x3aff67] == 0xe8);
    } else {
        printf("skipped: no USA 1.00 boot image (build\\image or game\\work)\n");
    }
    if (have_first) {
        CHECK(skip_intro_locate(first.bytes, first.size, "CUSA19610-1.01", &site) == SKIP_INTRO_FOUND && site == 0x3b1457);
        CHECK(skip_intro_locate(first.bytes, first.size, "CUSA19610-1.02", &site) == SKIP_INTRO_MISMATCH);
        const uint8_t saved = first.bytes[0x3b1457 + 1];
        first.bytes[0x3b1457 + 1] ^= 0x10;
        CHECK(skip_intro_locate(first.bytes, first.size, "CUSA19610-1.01", &site) == SKIP_INTRO_MISMATCH);
        first.bytes[0x3b1457 + 1] = saved;
        uint8_t stubs[256];
        CHECK(!skip_intro_install(first.bytes, first.size, first.entry, "CUSA19610-1.00", stubs, sizeof(stubs)));
        CHECK(first.bytes[0x3b1457] == 0xe8);
    } else {
        printf("skipped: no USA 1.01 boot image (build\\image or game\\work)\n");
    }
    if (have_update) {
        CHECK(skip_intro_locate(update.bytes, update.size, "CUSA19610-1.02", &site) == SKIP_INTRO_FOUND && site == 0x36a947);
        CHECK(skip_intro_locate(update.bytes, update.size, "CUSA19610-1.00", &site) == SKIP_INTRO_MISMATCH);
        CHECK(skip_intro_locate(update.bytes, update.size, "CUSA19610-1.01", &site) == SKIP_INTRO_MISMATCH);
    } else {
        printf("skipped: no USA 1.02 boot image (build\\image or game\\work)\n");
    }
    if (have_base && have_first && have_update) printf("real boot images: USA 1.00, 1.01 and 1.02 sites verified\n");
    free(base.bytes);
    free(first.bytes);
    free(update.bytes);
}

/* A synthetic "logo poll" caller: push rbp; mov rbp,rsp; call poll; pop rbp; ret, with poll = xor eax,eax; ret. */
static void test_skip_intro_runs(uint8_t *code) {
    static const uint8_t caller[] = {0x55, 0x48, 0x89, 0xe5, 0xe8, 0, 0, 0, 0, 0x5d, 0xc3};
    static const uint8_t poll[] = {0x31, 0xc0, 0xc3};
    memcpy(code, caller, sizeof(caller));
    memcpy(code + 64, poll, sizeof(poll));
    const int32_t rel = 64 - 9;
    memcpy(code + 5, &rel, 4);
    GuestHook hook = {"skip intro", 4, 5, {0}, skip_intro_hook};
    memcpy(hook.original, code + 4, 5);
    size_t bad = 0;
    CHECK(guest_hooks_install(code, 128, 0, NULL, &hook, 1, code + 4096, 4096, &bad) == GUEST_HOOK_OK);
    typedef int (GUEST_HOOK_ABI *Poll)(void);
    const Poll run = (Poll)(void *)code;
    CHECK((run() & 0xff) == 0);
    skip_intro_arm();
    CHECK((run() & 0xff) == 1);
    CHECK((run() & 0xff) == 0);
    CHECK(!skip_intro_claim());
}

int main(void) {
    test_real_sites();
    uint8_t *code = VirtualAlloc(NULL, 2 * 8192, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!code) {
        printf("FAIL: no executable memory\n");
        return 1;
    }
    test_skip_intro_runs(code);
    VirtualFree(code, 0, MEM_RELEASE);
    printf("game hooks: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
