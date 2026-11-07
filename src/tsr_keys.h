/* Keyboard bindings: the actions, their defaults and the saved key names, shared by the launcher, the runtime pad
 * and the video core. Keys are SDL scancodes, saved as the shadPS4 names (space, lshift, f10, pgup...) or SDL's own
 * lowercase names for the rest. */
#ifndef TSR_KEYS_H
#define TSR_KEYS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TSR_KEY_LEFT_STICK_UP, TSR_KEY_LEFT_STICK_DOWN, TSR_KEY_LEFT_STICK_LEFT, TSR_KEY_LEFT_STICK_RIGHT,
    TSR_KEY_RIGHT_STICK_UP, TSR_KEY_RIGHT_STICK_DOWN, TSR_KEY_RIGHT_STICK_LEFT, TSR_KEY_RIGHT_STICK_RIGHT,
    TSR_KEY_CROSS, TSR_KEY_CIRCLE, TSR_KEY_SQUARE, TSR_KEY_TRIANGLE,
    TSR_KEY_L1, TSR_KEY_R1, TSR_KEY_L2, TSR_KEY_R2, TSR_KEY_L3, TSR_KEY_R3,
    TSR_KEY_DPAD_UP, TSR_KEY_DPAD_DOWN, TSR_KEY_DPAD_LEFT, TSR_KEY_DPAD_RIGHT,
    TSR_KEY_OPTIONS, TSR_KEY_TOUCHPAD_LEFT, TSR_KEY_TOUCHPAD_RIGHT,
    TSR_KEY_HOTKEY_WINDOW_MODE,
    TSR_KEY_COUNT
} TsrKeyAction;

#define TSR_KEY_FIRST_HOTKEY TSR_KEY_HOTKEY_WINDOW_MODE

typedef struct {
    const char *ini_key; /* the line in tsr.ini */
    const char *label;   /* the launcher's row */
    int default_scancode;
} TsrKeyInfo;

extern const TsrKeyInfo tsr_key_actions[TSR_KEY_COUNT];

typedef struct { int scancode[TSR_KEY_COUNT]; } TsrKeyMap;

void tsr_keys_defaults(TsrKeyMap *map);

/* The scancode a saved name stands for (case-insensitive), or 0 when it names no bindable key. */
int tsr_key_from_name(const char *name);
/* False for no key and for Escape, which cancels a capture. */
int tsr_key_bindable(int scancode);
/* The name saved for a scancode, written to `buffer`; empty when the key is not bindable. */
const char *tsr_key_name(int scancode, char *buffer, size_t size);

/* Applies one saved line to the map; false when the key is not an action or the value names no bindable key. */
int tsr_keys_apply(TsrKeyMap *map, const char *ini_key, const char *value);
/* Defaults, then every valid binding in the tsr.ini at `path`; a missing file or line keeps the default. */
void tsr_keys_load(const char *path, TsrKeyMap *map);

#ifdef __cplusplus
}
#endif

#endif
