/* The memory image of a boot.bin (build\image or game\work, only read), laid out as the loader reads it; relocations
 * are left unapplied (no hook site has one). */
#ifndef TSR_TESTS_BOOT_IMAGE_H
#define TSR_TESTS_BOOT_IMAGE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    uint64_t size, entry;
} BootImage;

static uint64_t boot_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static int boot_image_load(const char *path, BootImage *image) {
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
    const uint64_t size = boot_le64(p), entry = boot_le64(p + 8), ns = boot_le64(p + 16), nr = boot_le64(p + 24),
                   imports = boot_le64(p + 32);
    p += 6 * 8 + 8 + 4 * 8;
    const uint64_t modules = boot_le64(p);
    p += 8 + modules * 7 * 8;
    const uint64_t bindings = boot_le64(p);
    p += 8 + bindings * 3 * 8 + ns * 3 * 8 + imports * 128 + nr * 4 * 8;
    if ((uint64_t)(p - file) + size != (uint64_t)length) { free(file); return 0; }
    image->bytes = malloc(size);
    memcpy(image->bytes, p, size);
    image->size = size;
    image->entry = entry;
    free(file);
    return 1;
}

/* With TSR_TEST_IMAGES set, every image must come from it: a missing one is a failure, never a skip. */
static int boot_image_required(void) {
    const char *root = getenv("TSR_TEST_IMAGES");
    return root && *root;
}

/* `folder` under TSR_TEST_IMAGES when set; else `folder` under build\image, else `work` under game\work. */
static int boot_image_load_build(const char *folder, const char *work, BootImage *image) {
    char path[512];
    if (boot_image_required()) {
        snprintf(path, sizeof(path), "%s/%s/boot.bin", getenv("TSR_TEST_IMAGES"), folder);
        return boot_image_load(path, image);
    }
    snprintf(path, sizeof(path), "%s/build/image/%s/boot.bin", TSR_SOURCE_DIR, folder);
    if (boot_image_load(path, image)) return 1;
    snprintf(path, sizeof(path), "%s/game/work/%s/boot.bin", TSR_SOURCE_DIR, work);
    return boot_image_load(path, image);
}

#endif
