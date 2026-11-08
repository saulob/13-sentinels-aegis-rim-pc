/* The Exit Game title row's data (assets/exit_game/title-labels.bin, scripts/exit_game): per text language the label
 * art as BC7 blocks for the title's text atlas and the edits that point the title layout's third row at it. Both are
 * applied to the game's own loaded copies, each only when every byte it replaces is the expected original. */
#ifndef TSR_EXIT_GAME_ASSETS_H
#define TSR_EXIT_GAME_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_GAME_MAX_LANGUAGES 8
#define EXIT_GAME_MAX_REGIONS 4
#define EXIT_GAME_LAYOUT_SECTIONS 11

typedef struct {
    uint32_t bx, by, bw, bh; /* in 4x4 blocks */
    uint32_t original_crc;
    uint32_t pool_count, run_count;
    const uint8_t *pool; /* 16 bytes per block */
    const uint8_t *runs; /* u16 pool index, u16 length, row by row */
} ExitGameRegion;

typedef struct {
    char key[13]; /* the launcher's text language key */
    uint32_t width, height, pitch, data_size;
    uint32_t reference[4], reference_crc; /* blocks of the language's own second row */
    uint32_t region_count;
    ExitGameRegion regions[EXIT_GAME_MAX_REGIONS];
    uint32_t sections[EXIT_GAME_LAYOUT_SECTIONS], layout_size;
    uint32_t edit_count;
    const uint8_t *edits;
} ExitGameLabels;

typedef struct {
    uint32_t count;
    ExitGameLabels languages[EXIT_GAME_MAX_LANGUAGES];
} ExitGameAsset;

/* 1 when the bytes are a complete, intact asset. */
int exit_game_asset_parse(const uint8_t *data, size_t size, ExitGameAsset *out);

/* The asset built into the executable. */
const uint8_t *exit_game_embedded_asset(size_t *size);

uint32_t exit_game_crc32(const uint8_t *data, size_t size);

/* Byte offset of 4x4 block (bx, by) in a Thin_1dThin BC7 surface whose rows are `pitch` texels. */
size_t exit_game_tiled_offset(uint32_t bx, uint32_t by, uint32_t pitch);

typedef enum {
    EXIT_GAME_PATCHED,
    EXIT_GAME_ALREADY,
    EXIT_GAME_OTHER,    /* not this language's data: left alone */
    EXIT_GAME_MISMATCH  /* this language's data, but not the expected bytes: left alone */
} ExitGamePatchStatus;

/* The text atlas: `pixels` is the tiled BC7 data of a width x height texture with the given pitch and byte size. */
ExitGamePatchStatus exit_game_texture_patch(const ExitGameLabels *labels, uint32_t width, uint32_t height, uint32_t pitch,
                                            uint32_t size, uint8_t *pixels);

/* The loaded title layout: its section table holds pointers into the same buffer. */
ExitGamePatchStatus exit_game_layout_patch(const ExitGameLabels *labels, uint8_t *layout);

#ifdef __cplusplus
}
#endif

#endif
