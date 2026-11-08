#include "code_signature.h"

int code_signature_matches(const uint8_t *at, const short *signature, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (signature[i] != CODE_ANY && at[i] != (uint8_t)signature[i]) return 0;
    }
    return 1;
}

size_t code_signature_count(const uint8_t *image, uint64_t limit, const short *signature, size_t length, uint64_t *first) {
    size_t count = 0;
    if (!length || signature[0] == CODE_ANY || limit < length) return 0;
    const uint8_t lead = (uint8_t)signature[0];
    for (uint64_t at = 0; at + length <= limit; ++at) {
        if (image[at] != lead || !code_signature_matches(image + at, signature, length)) continue;
        if (!count && first) *first = at;
        ++count;
    }
    return count;
}
