/* The launcher, called by the loader before the guest and the game window exist. */
#ifndef TSR_LAUNCHER_H
#define TSR_LAUNCHER_H

#include <stddef.h>
#include <stdint.h>

#include "tsr_resolution.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the processor and graphics system can run the game. Otherwise 0, with what is wrong and what to do about it in
 * the buffers. */
typedef int (*TsrGraphicsCheck)(char *cause, size_t cause_size, char *advice, size_t advice_size);

typedef struct {
    const char *app0;          /* prepared game folder (/app0) */
    const char *image_dir;     /* folder with boot.bin, content.bin and image.json */
    const char *user_dir;      /* user data; tsr.ini lives here unless settings_file is set */
    const char *settings_file; /* NULL: <user_dir>/tsr.ini */
    const char *logs_dir;      /* NULL when unknown */
    int skip_once;             /* --tsr-skip-launcher: do not show it unless the game files are not ready */
    int force_show;            /* --launcher: show it even when "Show this launcher" is off, like holding Shift */
    TsrGraphicsCheck check_graphics; /* NULL: Play does not check; see tsr_check_vulkan */
    int game_process;          /* Windows: with Allow overlay programs on, Play starts the game as a process of its own */
    const char *log_file;      /* this run's log, continued by the game process; NULL when there is none */
} TsrLauncherConfig;

/* NULL asks for the default, which is relative to the executable (launcher/paths.h). */
typedef struct {
    const char *boot;
    const char *content_profile;
    const char *app0;
    const char *user_dir;
    const char *logs_dir;
} TsrPathRequest;

typedef struct {
    char boot[4096];
    char content_profile[4096];
    char app0[4096];
    char image_dir[4096];
    char user_dir[4096];
    char logs_dir[4096];
    char error[512]; /* why the paths cannot be used, when it is known (TSR_GAME_BUILD) */
} TsrPaths;

/* 0 when the executable's folder is unknown, a path does not fit or TSR_GAME_BUILD cannot be used. */
int tsr_resolve_paths(const TsrPathRequest *request, TsrPaths *paths);

/* Headless installation (tsr --install <package.pkg|game folder>): the launcher's installer, progress on stdout.
 * 0 when installed or already up to date; otherwise 1, with the reason in `detail`. */
int tsr_install_game(const char *source, const char *app0, const char *image_dir, char *detail, size_t detail_size);
/* Headless readiness check (tsr --check-game): 1 when the game files are ready to play, `detail` says which build. */
int tsr_check_game(const char *app0, const char *image_dir, char *detail, size_t detail_size);

/* The folder holding this run's log: the log_file's parent when one is given, else logs_dir. 0 when
 * neither yields a path. */
int tsr_logs_directory(const char *logs_dir, const char *log_file, char *out, size_t size);

/* Index into tsr_resolutions (tsr_resolution.h) of the render_resolution in the settings file, among the resolutions the
 * build image_dir was built from: 0 (native) when the file or the value is missing or the build has no verified render
 * resolution sites. */
int tsr_render_resolution(const char *settings_file, const char *image_dir);


/* The system language the game is told for the text_language in the settings file, among the languages the prepared
 * game folder app0 holds of the build image_dir was built from: the first of them (English when the build is unknown)
 * when the file or the value is missing or not one of them. */
int tsr_text_language(const char *settings_file, const char *image_dir, const char *app0);

/* 1 when skip_intro is on in the settings file (off by default). */
int tsr_skip_intro(const char *settings_file);

/* 1 when the settings file keeps the logs of normal sessions (detailed_logging; 1 by default). */
int tsr_detailed_logging(const char *settings_file);

/* What the loader decided for this run: the parts of the "Settings:" log lines the settings file alone does not say. */
typedef struct TsrRunSettings {
    const char *game_build;        /* label; NULL for an unknown build */
    int render_width, render_height;
    const char *render_resolution; /* tsr_resolutions key */
} TsrRunSettings;

/* Prints the startup state support needs as "Settings:" lines: port version, platform, game build and the display and
 * gameplay settings in effect. `run` may be NULL: the settings file's values then. */
void tsr_log_settings_summary(const char *settings_file, const char *image_dir, const char *app0,
                              const TsrRunSettings *run);

/* Starts the game process tsr_launcher_run created suspended. Call it once nothing writes to the log any more; 1 when
 * it runs. A process that could not be started is ended. */
int tsr_resume_game_process(void);

/* Fills `names` with the distinct names of the Vulkan devices the renderer can use and returns how many (none without a
 * usable Vulkan on Linux); enumerates only, no device is created. */
int tsr_gpu_names(char (*names)[256], int max);

/* A TsrGraphicsCheck: 1 when Vulkan can run the renderer. Otherwise logs why and returns 0. It asks again on every call
 * until it succeeds, so a driver installed meanwhile is found. */
int tsr_check_vulkan(char *cause, size_t cause_size, char *advice, size_t advice_size);

/* tsr_check_vulkan as far as the renderer saved in settings_file needs Vulkan: OpenGL does not, and Auto can do without. */
int tsr_check_renderer(const char *settings_file, char *cause, size_t cause_size, char *advice, size_t advice_size);

/* Builds boot.bin, content.bin and image.json in `out` from the prepared game folder `app0` (UTF-8 paths). 0 on
 * success; otherwise 1 and the reason in `error`. */
int tsr_build_boot_image(const char *app0, const char *out, char *error, size_t error_size);

typedef struct {
    const char *label;
    const TsrResolutionSite *resolution_sites;
    size_t resolution_site_count;
    const char *title_id;
    const char *version;         /* the build's profile version: with title_id, the build key */
    const char *code_key;        /* the build key whose executable code it runs; empty: its own */
    const char *region;
} TsrGameBuild;

/* The supported game build a boot image folder was built from (its image.json); 0 when it is not one. */
int tsr_game_build(const char *image_dir, TsrGameBuild *build);

/* Why tsr_game_build finds no supported build there, as one line for the log. */
void tsr_game_build_problem(const char *image_dir, char *out, size_t size);

typedef enum {
    TSR_LAUNCHER_PLAY = 0,
    TSR_LAUNCHER_QUIT = 1,
    TSR_LAUNCHER_BLOCKED = 2,
    TSR_LAUNCHER_GAME_STARTED = 3 /* game_process: the game process exists, suspended; this process ends */
} TsrLauncherResult;

/* Shows the launcher when its rules say so (game files not ready, "Show this launcher" on, or Shift held) and returns
 * once the player pressed Play or quit. Returns TSR_LAUNCHER_PLAY at once when it is not shown.
 * While check_graphics fails, Play is refused: the launcher says why and stays open. A launcher that is not shown
 * explains in a window of its own and returns TSR_LAUNCHER_BLOCKED.
 * With game_process and Allow overlay programs on, Play creates the game process instead and returns
 * TSR_LAUNCHER_GAME_STARTED; when it cannot be created, the launcher stays open with the reason (or explains in a window
 * of its own). */
TsrLauncherResult tsr_launcher_run(const TsrLauncherConfig *config);

#ifdef __cplusplus
}
#endif

#endif
