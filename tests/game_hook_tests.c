/* Skip Intro hook: its sites in the real boot images (build\image or game\work, only read), refusal of the
 * wrong build, an unknown build and changed code, and the hook runs on the CPU over a small synthetic function. Whether
 * the game skips its logos stays a manual check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_image.h"
#include "guest_hook.h"
#include "skip_intro.h"
#include "test_pages.h"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static void test_real_sites(void) {
    BootImage base = {0}, first = {0}, update = {0};
    const int have_base = boot_image_load_build("CUSA19610-1.00", "image-1.00", &base);
    const int have_first = boot_image_load_build("CUSA19610-1.01", "image-1.01", &first);
    const int have_update = boot_image_load_build("CUSA19610-1.02", "image-1.02", &update);
    CHECK((have_base && have_first && have_update) || !boot_image_required());
    uint32_t site = 0;
    if (have_base) {
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.00", &site) == SKIP_INTRO_FOUND && site == 0x3aff67);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.02", &site) == SKIP_INTRO_MISMATCH);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA10602-1.00", &site) == SKIP_INTRO_MISMATCH);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA99999-1.00", &site) == SKIP_INTRO_UNKNOWN_BUILD);
        CHECK(skip_intro_locate(base.bytes, base.size, NULL, &site) == SKIP_INTRO_UNKNOWN_BUILD);
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.01", &site) == SKIP_INTRO_MISMATCH);

        uint8_t saved = base.bytes[0x3aff67 + 1];
        base.bytes[0x3aff67 + 1] ^= 0x10;
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.00", &site) == SKIP_INTRO_MISMATCH);
        base.bytes[0x3aff67 + 1] = saved;

        /* Each refusal says which check failed: unique at another offset, not found, ambiguous, call, function. */
        char why[160];
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.00", &site, why, sizeof(why)) == SKIP_INTRO_FOUND && !why[0]);
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.02", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              !strcmp(why, "logo scene code: unique match at 0x3aff4d, expected 0x36a92d"));
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA10602-1.00", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              !strcmp(why, "logo scene code: not found"));
        base.bytes[0x3aff67 + 1] ^= 0x10;
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.00", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              strstr(why, "button poll call: target 0x") && strstr(why, "expected 0x36f400"));
        base.bytes[0x3aff67 + 1] = saved;
        base.bytes[0x36f400] ^= 1;
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.00", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              !strcmp(why, "button poll function at 0x36f400: code differs"));
        base.bytes[0x36f400] ^= 1;
        base.bytes[0x3aff4d] ^= 1;
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.00", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              !strcmp(why, "logo scene code: not found"));
        base.bytes[0x3aff4d] ^= 1;
        uint8_t spare[64];
        memcpy(spare, base.bytes + 0x10000, sizeof(spare));
        memcpy(base.bytes + 0x10000, base.bytes + 0x3aff4d, sizeof(spare));
        CHECK(skip_intro_locate_why(base.bytes, base.size, "CUSA19610-1.00", &site, why, sizeof(why)) == SKIP_INTRO_MISMATCH &&
              !strcmp(why, "logo scene code: ambiguous (2 matches, expected 1)"));
        memcpy(base.bytes + 0x10000, spare, sizeof(spare));
        CHECK(skip_intro_locate(base.bytes, base.size, "CUSA19610-1.00", &site) == SKIP_INTRO_FOUND && site == 0x3aff67);

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

/* The Japanese and Asian executables (TSR_TEST_IMAGES): each scene shape matches once, at its build's site only. */
static void test_regional_sites(void) {
    static const struct {
        const char *build;
        uint32_t site;
    } kRegional[] = {
        {"CUSA10602-1.00", 0x3478bb}, {"CUSA10602-1.03", 0x3af6db}, {"CUSA10602-1.04", 0x368d18}, {"CUSA18093-1.00", 0x3b9bdb},
    };
    const size_t count = sizeof(kRegional) / sizeof(*kRegional);
    size_t verified = 0;
    for (size_t i = 0; i < count; ++i) {
        BootImage image = {0};
        if (!boot_image_load_build(kRegional[i].build, "", &image)) {
            CHECK(!boot_image_required());
            printf("skipped: no %s boot image (TSR_TEST_IMAGES)\n", kRegional[i].build);
            continue;
        }
        uint32_t site = 0;
        CHECK(skip_intro_locate(image.bytes, image.size, kRegional[i].build, &site) == SKIP_INTRO_FOUND &&
              site == kRegional[i].site);
        for (size_t j = 0; j < count; ++j)
            if (j != i) CHECK(skip_intro_locate(image.bytes, image.size, kRegional[j].build, &site) == SKIP_INTRO_MISMATCH);
        CHECK(skip_intro_locate(image.bytes, image.size, "CUSA19610-1.00", &site) == SKIP_INTRO_MISMATCH);
        const uint8_t saved = image.bytes[kRegional[i].site + 1];
        image.bytes[kRegional[i].site + 1] ^= 0x10;
        CHECK(skip_intro_locate(image.bytes, image.size, kRegional[i].build, &site) == SKIP_INTRO_MISMATCH);
        image.bytes[kRegional[i].site + 1] = saved;
        uint8_t *grown = realloc(image.bytes, image.size + 4096);
        if (!grown) break;
        image.bytes = grown;
        CHECK(skip_intro_install(image.bytes, image.size, image.entry, kRegional[i].build, image.bytes + image.size, 4096) == 1);
        CHECK(image.bytes[kRegional[i].site] == 0xe9);
        CHECK(skip_intro_claim() == 1);
        free(image.bytes);
        ++verified;
    }
    if (verified == count) printf("real boot images: Japan 1.00, 1.03, 1.04 and Asia 1.00 Skip Intro sites verified\n");
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
    test_regional_sites();
    uint8_t *code = test_pages(2 * 8192, 1);
    if (!code) {
        printf("FAIL: no executable memory\n");
        return 1;
    }
    test_skip_intro_runs(code);
    test_pages_free(code, 2 * 8192);
    printf("game hooks: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
