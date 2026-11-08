/* The render resolutions the launcher offers and the game code that selects them. */
#ifndef TSR_RESOLUTION_H
#define TSR_RESOLUTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;  /* render_resolution in tsr.ini */
    const char *name; /* launcher label */
    int width, height;
    int pro_memory;   /* above 1920 x 1080: the game's PS4 Pro path, which it sizes for 3840 x 2160 */
} TsrResolution;

#define TSR_RESOLUTION_COUNT 6
extern const TsrResolution tsr_resolutions[TSR_RESOLUTION_COUNT];

/* Index of the key, 0 (native) when it is not listed. */
int tsr_resolution_index(const char *key);

/* One `mov reg, imm32` in the game that holds a render width or height. */
typedef struct {
    uint32_t offset; /* image offset of the opcode */
    uint8_t opcode_length;
    uint8_t opcode[2];
    uint32_t value;  /* the original immediate */
    int height;
} TsrResolutionSite;

/* Writes the resolution's width and height into every site after checking that each one still holds its original
 * instruction and that no two overlap. Returns 0 and a message in `error` otherwise; nothing is written then. */
int tsr_apply_resolution(unsigned char *image, uint64_t image_size, const TsrResolutionSite *sites, size_t count,
                         const TsrResolution *resolution, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
