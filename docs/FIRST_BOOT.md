# First boot: what is known statically

What the supported packages and executables contain, read from the packages themselves.

## Packages

| | Base 1.00 | Update 1.02 |
|---|---|---|
| CATEGORY | `gd` | `gp` (cumulative patch, delta info present) |
| APP_VER / VERSION | 01.00 / 01.00 | 01.02 / 01.00 |
| SDK (SYSTEM_VER) | 7.00 (`0x07008000`) | 9.50 (`0x09508000`) |
| Header vs file size | match | match |
| Image key | 3: fake PKG | 3: fake PKG |
| eboot.bin | 15,083,161 bytes, `cf7e9093…5950` | 13,791,075 bytes, `07525009…3ff1` |
| Archives | `ROBO_US.CPK` (3.8 GB), `ROBO_{FR,GE,IT,SP}.CPK` | `ROBO_PATCH_US.CPK`, `ROBO_PATCH2_{US,FR,GE,IT,SP}.CPK`, `sce_discmap_patch.plt` |

The update replaces `eboot.bin`, the three bundled modules and `sce_sys`, and adds archives. Its executable opens
`ROBO_PATCH_US.CPK` and `ROBO_PATCH2%s.CPK` as well as `ROBO%s.CPK`, so it never runs against a 1.00 tree. The profile
requires those archives for 1.02.

## Other regions

| Build | CATEGORY | APP_VER / VERSION | SDK | eboot.bin | Archives |
|---|---|---|---|---|---|
| CUSA19620 1.00 Europe | `gd` | 01.00 / 01.01 | 7.00 | 15,083,161 bytes, `e4df1d07…90e1` | as USA 1.00 |
| CUSA19620 1.01 Europe | `gp` | 01.01 / 01.00 | 7.00 (one packaging 5.05) | 15,096,185 bytes, `dafcc5f0…0533` / `123730cd…6b28` | `ROBO_PATCH_US.CPK` |
| CUSA10602 1.00 Japan | `gd` | 01.00 / 01.00 | 6.50 | 14,809,061 bytes, `6d6f67ce…5d0c` | `ROBO.CPK` |
| CUSA10602 1.03 Japan | `gp` | 01.03 / 01.00 | 7.50 | 15,394,613 bytes, `fdd1b5ad…c11a` | `ROBO_PATCH.CPK`, `ROBO_PATCH2.CPK` |
| CUSA10602 1.04 Japan | `gp` (delta info) | 01.04 / 01.00 | 9.50 | 14,089,055 bytes, `3bcaeac5…fbcb` | `ROBO_PATCH.CPK` (holds all of 1.03) |
| CUSA18093 1.00 Asia | `gd` | 01.00 / 01.01 | 5.05 | 16,000,069 bytes, `dd6d54e9…2bcf` | `ROBO_CN.CPK`, `ROBO_TW.CPK` |

The European executables are the American ones: the 1.00 image differs only in a build date string, 1.01 is the same
file, and its older-firmware packaging differs only in the SDK version fields. The Japanese and Asian executables are
builds of their own; every feature site (render size, Skip Intro, Exit Game, LZCNT) was located in each of them by
structure and checked against the real code. Their imports all bind; the Japanese 1.03/1.04 and Asian games also call
`sceSaveDataTransferringMount` (the prologue demo's save), which the runtime answers.

## Executable

Both eboots are plaintext PS4 SELF, x86-64 ELF type `0xfe10` with the usual layout: code at 0, SCE_RELRO at `0xc00000`,
data at `0xe00000`, no main-module TLS. The entry is `0x404cc0` in 1.00 and `0x3bdbd0` in 1.02. The bundled modules are
`libc.prx`, `libSceFios2.prx` and `libSceNpToolkit2.prx`, linked in that order as in the reference port.

`scripts/prepare_image.py` maps both builds and links the three modules (images of 27.0 MB and 26.9 MB). `libc`'s
`_init_env` is a bare RET in both, which the runtime's replacement relies on.

Middleware seen in the executable's strings: CRIWARE ADX2 (Atom, HCA-MX and an AT9 codec plugin, so ATRAC9 through
`libSceAjm`) and Sofdec/Mana (H.264 through `libSceVideodec2`). No AvPlayer. The executable asks `sceKernelIsNeoMode`
to choose between its 1080p and PS4 Pro render sizes.

## Import coverage

`python scripts/import_coverage.py game/prepared/CUSA19610-1.00 --eboot-only`:

| | Imports | Runtime | Bundled modules | Unbound |
|---|---:|---:|---:|---:|
| 1.00 eboot | 423 | 327 | 96 | 0 |
| 1.02 eboot | 426 | 330 | 96 | 0 |

With the bundled modules' own imports there are 68 unbound in 1.00, all inside `libSceNpToolkit2.prx`'s online
services (NpTus, Matching2, Score, WebApi). An unbound import stops the game only when it is called, with
`STOP: first unsupported PS4 import: ...` in the log.

## Added for this game

- `src/runtime_extra.c`: the SDK 7.00/9.50 libc start-up hooks (thread-exit and sanitizer queries), POSIX rwlocks,
  dialogs and console features the offline runtime answers.
- `src/runtime_ajm.c`: ATRAC9 decoding through LibAtrac9.
- `gpu/shadps4/core/libraries/videodec`: shadPS4's Videodec2 on FFmpeg.
- `src/runtime_redzone.c`: red-zone safety for guest functions on Windows.
- Guest mutexes start with a zeroed PS4 kernel object; guest threads start on a zeroed stack.

## Not carried over

Every Dragon's Crown game-specific patch, hook, offset and fingerprint (resolution sites, ultrawide, skip intro and
offers, gallery, menu buttons, NPC voices, exit-game row, title layout, LZCNT rewrite sites), the DC water and ultrawide
renderer workarounds, the controller prompt and translation patches, online play, cheats and Discord presence. The
self-updater is carried over for this repository's releases (docs\RELEASE_SECURITY.md). The LZCNT rewrite itself is carried over with sites of its own for every supported executable, used on a
processor without LZCNT (Intel Sandy Bridge, Ivy Bridge); `TSR_EMULATE_LZCNT=1` forces it on one that has LZCNT.
