#pragma once
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef TSR_SDL_COMPAT_POLICY_ONLY
#include <SDL3/SDL.h>
#endif

typedef enum { TSR_HIDAPI_LEAVE, TSR_HIDAPI_DISABLE, TSR_HIDAPI_PRESERVE } TsrHidapiAction;

static inline TsrHidapiAction tsr_hidapi_action(int under_wine, const char *configured) {
    if (configured && configured[0]) return TSR_HIDAPI_PRESERVE;
    return under_wine ? TSR_HIDAPI_DISABLE : TSR_HIDAPI_LEAVE;
}

static inline int tsr_running_under_wine(void) {
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != NULL;
#else
    return 0;
#endif
}

#ifndef TSR_SDL_COMPAT_POLICY_ONLY
/* Before the first SDL_INIT_GAMEPAD, which reads the hint; later calls see "0" and do nothing. */
static inline void tsr_prepare_sdl_gamepad(void) {
    if (tsr_hidapi_action(tsr_running_under_wine(), SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI)) != TSR_HIDAPI_DISABLE) return;
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    printf("Runtime: Proton/Wine detected; SDL HIDAPI gamepads disabled for compatibility\n");
}
#endif
