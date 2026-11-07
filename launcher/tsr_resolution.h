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
    int pro_memory;   /* the game's PS4 Pro path: its larger graphics memory, sized for 3840 x 2160 */
} TsrResolution;

#define TSR_RESOLUTION_COUNT 6
extern const TsrResolution tsr_resolutions[TSR_RESOLUTION_COUNT];

/* Index of the key, 0 (native) when it is not listed. */
int tsr_resolution_index(const char *key);

typedef struct {
    const char *key;
    const char *name;
    int width, height;
} TsrAspectRatio;

#define TSR_ASPECT_COUNT 6
extern const TsrAspectRatio tsr_aspect_ratios[TSR_ASPECT_COUNT];

/* Index of the key, 0 (16:9 Original) when it is not listed. */
int tsr_aspect_index(const char *key);

/* The width is aligned to the 8-pixel tile; a target above the 4K pixel count keeps its ratio and loses height. */
TsrResolution tsr_render_target(const TsrResolution *resolution, const TsrAspectRatio *aspect);

/* One `mov reg, imm32` in the game that holds a render width or height. */
typedef struct {
    uint32_t offset; /* image offset of the opcode */
    uint8_t opcode_length;
    uint8_t opcode[2];
    uint32_t value;  /* the original immediate */
    int height;
} TsrResolutionSite;

/* Writes the resolution's width and height into every site after checking each one still holds the original
 * instruction. Returns 0 and a message in `error` when the image does not match; nothing is written then. */
int tsr_apply_resolution(unsigned char *image, uint64_t image_size, const TsrResolutionSite *sites, size_t count,
                         const TsrResolution *resolution, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
