/* Exit Game on the title menu. The title has a third menu row of its own that a save flag normally hides (EXTRA, which
 * does nothing when chosen); its three-row layout moves the menu up to make room. When the row's label art and layout
 * (exit_game_assets.h) are in place for the loaded language, the title's reads of that flag answer "shown", the row
 * reads EXIT GAME in that language, and the game's own confirm on it asks the host to close the window, as the
 * window's close button does. Cancel and the other rows keep their own handling. Every site is checked first; a
 * build, layout or atlas that differs leaves the title as it was. */
#ifndef TSR_EXIT_GAME_H
#define TSR_EXIT_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "exit_game_assets.h"
#include "guest_hook.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_GAME_FLAG_READS 7
#define EXIT_GAME_HOOK_COUNT (EXIT_GAME_FLAG_READS + 3)
#define EXIT_GAME_ROW 2

typedef struct {
    const char *build;
    uint32_t flag_getter;
    uint32_t flag_reads[EXIT_GAME_FLAG_READS]; /* calls of the getter followed by a read of the flag byte */
    uint32_t confirm_dispatch;                 /* mov rax,[rbp-0x38]; mov eax,[rax]: the confirmed row */
    uint32_t layout_bind;                      /* mov rsi,[r13+layout resource]: the title binds its layout */
    uint32_t texture_copy;                     /* test byte [r14+0x20],8: a GNF texture's pixels are about to be used */
    uint32_t resource_layout_getter;           /* returns the parsed layout of a layout resource */
    uint32_t layout_resource;                  /* offset of the title's layout resource in the title scene */
    uint32_t parsed_layout;                    /* offset of the parsed layout in a layout resource */
} ExitGameSites;

typedef enum {
    EXIT_GAME_FOUND,
    EXIT_GAME_UNKNOWN_BUILD,
    EXIT_GAME_SITE_MISMATCH
} ExitGameLocate;

/* The verified sites of a build key ("CUSA19610-1.02"); on a mismatch `reason` names the site. */
ExitGameLocate exit_game_locate(const uint8_t *image, uint64_t size, const char *build, ExitGameSites *sites,
                                const char **reason);

/* Asks the host to close; nonzero when the request was taken. */
typedef int (*ExitGameHandler)(void);
void exit_game_set_handler(ExitGameHandler handler);

size_t exit_game_stub_bytes(void);

/* Installs the hooks when the build's sites and the embedded asset check out; 0 (image untouched) otherwise. */
int exit_game_install(uint8_t *image, uint64_t size, uint64_t entry, const char *build, uint8_t *stubs, size_t stubs_size);

/* For the hook tests: the asset and handlers without an image. */
int exit_game_use_asset(const uint8_t *data, size_t size);
void exit_game_use_sites(const ExitGameSites *sites);
void exit_game_reset(void);
int exit_game_ready(void); /* 1 + language index while the third row is Exit Game, else 0 */
int exit_game_requested(void);

void GUEST_HOOK_ABI exit_game_flag_hook(GuestRegs *regs);
void GUEST_HOOK_ABI exit_game_navigation_hook(GuestRegs *regs);
void GUEST_HOOK_ABI exit_game_confirm_hook(GuestRegs *regs);
void GUEST_HOOK_ABI exit_game_layout_hook(GuestRegs *regs);
void GUEST_HOOK_ABI exit_game_texture_hook(GuestRegs *regs);

#ifdef __cplusplus
}
#endif

#endif
