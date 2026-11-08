#include "exit_game_assets.h"

#ifdef _WIN32
#define TSR_EMBED(symbol, path) \
    __asm__(".section .rdata,\"dr\"\n.balign 16\n.global " #symbol "\n" #symbol ":\n.incbin \"" path "\"\n" \
            ".global " #symbol "_end\n" #symbol "_end:\n.text\n")
#else
#define TSR_EMBED(symbol, path) \
    __asm__(".section .rodata\n.balign 16\n.hidden " #symbol "\n.global " #symbol "\n" #symbol ":\n.incbin \"" path "\"\n" \
            ".hidden " #symbol "_end\n.global " #symbol "_end\n" #symbol "_end:\n.previous\n")
#endif
TSR_EMBED(tsr_exit_game_labels, TSR_EXIT_GAME_LABELS_PATH);
extern const unsigned char tsr_exit_game_labels[], tsr_exit_game_labels_end[];

const uint8_t *exit_game_embedded_asset(size_t *size) {
    *size = (size_t)(tsr_exit_game_labels_end - tsr_exit_game_labels);
    return tsr_exit_game_labels;
}
