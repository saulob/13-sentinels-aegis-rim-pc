/* The game's PS4 code uses LZCNT, which a host without it runs as BSR (a mirrored result, no answer for zero) instead
 * of faulting. There each LZCNT of a known build is replaced in the loaded image by a jump to a stub that computes it;
 * nothing is written to eboot.bin, and an image that does not match is left untouched. */
#ifndef TSR_LZCNT_COMPAT_H
#define TSR_LZCNT_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#include "guest_hook.h"
#include "isa_emulate.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LZCNT_SITE_MAX 75
#define LZCNT_BUILD_COUNT 3
#define LZCNT_SHAPE_MAX 10

/* The bytes replaced at a site: whole instructions, at least a jmp rel32 long, with an LZCNT at `first` and a second
 * one at `second` when nonzero. */
typedef struct {
    uint8_t length;
    uint8_t first;
    uint8_t second;
    uint8_t bytes[LZCNT_SHAPE_MAX];
} LzcntShape;

typedef struct {
    const char *build;
    GuestImageId image;
    const LzcntShape *shapes;
    const uint32_t *offsets;
    size_t count;
} LzcntBuild;

extern const LzcntBuild lzcnt_builds[LZCNT_BUILD_COUNT];

typedef enum {
    LZCNT_COMPAT_OK = 0,
    LZCNT_COMPAT_UNKNOWN_IMAGE, /* size or entry is not a known build's */
    LZCNT_COMPAT_BAD_SITE,      /* the bytes differ, overlap, are out of range or cannot be rewritten */
    LZCNT_COMPAT_NO_ROOM        /* the stub area is too small or too far from the image */
} LzcntCompatStatus;

const char *lzcnt_compat_status_text(LzcntCompatStatus status);

/* Whether the host needs the rewrite. */
int lzcnt_compat_needed(IsaHostFeatures host);

/* Bytes of stub area the rewrite of any known build needs. */
size_t lzcnt_compat_stub_bytes(void);

/* Checks every site, then rewrites them; nothing is written when a check fails. `stubs` must be within 2 GiB of the
 * image and become executable once the caller protects it. */
LzcntCompatStatus lzcnt_compat_apply(uint8_t *image, uint64_t size, const uint32_t *offsets, const LzcntShape *shapes,
                                     size_t count, uint8_t *stubs, size_t stubs_size);

/* The same for the build whose size and entry the image has; `build` gets it. */
LzcntCompatStatus lzcnt_compat_install(uint8_t *image, uint64_t size, uint64_t entry, uint8_t *stubs, size_t stubs_size,
                                       const LzcntBuild **build);

#ifdef __cplusplus
}
#endif

#endif
