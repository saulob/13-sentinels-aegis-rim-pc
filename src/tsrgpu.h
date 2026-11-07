/* Interface between the loader/runtime and the video core (shadPS4-derived, gpu/shim/tsrgpu.cpp). */
#ifndef TSRGPU_H
#define TSRGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* user folder: the pipeline cache lives in its cache folder */
    const char *settings_file;  /* tsr.ini */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
    uint32_t features;          /* TSR_GPU_FEATURE_*: what the game build supports (the Options menu shows only those) */
} TsrGpuConfig;
#define TSR_GPU_FEATURE_RENDER_RESOLUTION (1u << 0)
/* 1 when a video core is linked in, 0 for the stub. */
int tsrgpu_available(void);
/* Registers kernel event queues (needed with or without graphics). */
void tsrgpu_register_kernel(void);
/* Creates window, renderer and GPU command processor; 0 on success. */
int tsrgpu_init(const TsrGpuConfig *config);
/* Function for an imported NID ("NID#lib"), or 0 when the video core does not provide it. */
uintptr_t tsrgpu_resolve(const char *scoped_nid);
/* Called first by the loader's fault handler: 1 when a GPU page-tracking fault was handled. */
int tsrgpu_handle_fault(void *context, void *address);
void tsrgpu_dump_guest_writes(void *context);
/* Number of symbols registered by the video core (diagnostics). */
unsigned tsrgpu_symbol_count(void);
/* Queues the shutdown a closed window performs (clean-exit mark, log finalize, process exit). Callable from any thread;
 * 1 for the first request, 0 with no window or once already requested. */
int tsrgpu_request_close(void);
/* Keyboard text entry and list selection drawn by the video core; begin returns 0 without a window.
 * text poll: 0 typing, 1 confirmed, 2 cancelled. choice poll: -1 open, -2 cancelled, else the index. */
int tsrgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int tsrgpu_text_input_poll(char *out_utf8, uint64_t size);
int tsrgpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus);
int tsrgpu_choice_poll(void);
/* The choice list reads PS4 pad button bits from this callback every frame while it is open. */
void tsrgpu_choice_set_input(uint32_t (*buttons)(void));
/* 1 while the settings menu is open: the game's pad input should be held neutral. */
int tsrgpu_overlay_captures_input(void);
/* TSR_INPUT_LATENCY_TRACE=1: 1 when set; the SDL_GetTicksNS time of the newest host button or key press. */
int tsrgpu_latency_trace_enabled(void);
uint64_t tsrgpu_latency_press_ns(void);

#ifdef __cplusplus
}
#endif
#endif
