/* Skip Intro: the startup logo scene asks every frame whether any player pressed a button and, when one did, fades
 * out to the title screen. With the setting on, its first poll is answered "pressed", so the game takes its own skip;
 * the title screen and everything after it are untouched. */
#ifndef TSR_SKIP_INTRO_H
#define TSR_SKIP_INTRO_H

#include <stddef.h>
#include <stdint.h>

#include "guest_hook.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SKIP_INTRO_FOUND,
    SKIP_INTRO_UNKNOWN_BUILD, /* not a verified build, or no build key */
    SKIP_INTRO_MISMATCH       /* a verified build whose code is not the expected one */
} SkipIntroLocate;

/* Finds the logo scene's poll call for a build key ("CUSA19610-1.00"): the scene code must match once in the image, at
 * the build's offset, and call the build's button poll. *site gets the call's image offset. */
SkipIntroLocate skip_intro_locate(const uint8_t *image, uint64_t size, const char *build, uint32_t *site);

size_t skip_intro_stub_bytes(void);

/* Hooks the poll call and arms the one-shot press. Writes nothing and returns 0, saying why, when the build or its code
 * is not the verified one or the stub area does not fit. */
int skip_intro_install(uint8_t *image, uint64_t size, uint64_t entry, const char *build, uint8_t *stubs,
                       size_t stubs_size);

/* 1 the first time after skip_intro_arm, then 0. */
void skip_intro_arm(void);
int skip_intro_claim(void);
void GUEST_HOOK_ABI skip_intro_hook(GuestRegs *regs);

#ifdef __cplusplus
}
#endif

#endif
