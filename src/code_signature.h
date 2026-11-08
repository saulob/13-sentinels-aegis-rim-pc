/* Byte signatures over the loaded game image: a value of 0..255 must match, CODE_ANY matches anything. */
#ifndef TSR_CODE_SIGNATURE_H
#define TSR_CODE_SIGNATURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CODE_ANY (-1)

int code_signature_matches(const uint8_t *at, const short *signature, size_t length);

/* How many places in image[0, limit) match; *first gets the first of them. */
size_t code_signature_count(const uint8_t *image, uint64_t limit, const short *signature, size_t length, uint64_t *first);

#ifdef __cplusplus
}
#endif

#endif
