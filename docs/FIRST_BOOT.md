# First boot: what is known statically

Everything here was read from the user's own packages without running game code. It says what the loader can
handle, not that the game runs.

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

## Executable

Both eboots are plaintext PS4 SELF, x86-64 ELF type `0xfe10` with the usual layout: code at 0, SCE_RELRO at `0xc00000`,
data at `0xe00000`, no main-module TLS. The entry is `0x404cc0` in 1.00 and `0x3bdbd0` in 1.02. The bundled modules are
`libc.prx`, `libSceFios2.prx` and `libSceNpToolkit2.prx`, linked in that order as in the reference port.

`scripts/prepare_image.py` maps both builds and links the three modules (images of 27.0 MB and 26.9 MB). `libc`'s
`_init_env` is a bare RET in both, which the runtime's replacement relies on.

Middleware seen in the executable's strings: CRIWARE ADX2 (Atom, HCA-MX and an AT9 codec plugin, so ATRAC9 through
`libSceAjm`) and Sofdec/Mana (H.264 through `libSceVideodec2`). No Neo mode and no AvPlayer.

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
self-updater is carried over for this repository's releases (docs\RELEASE_SECURITY.md). The LZCNT rewrite itself is carried over with sites of its own for 1.00, 1.01 and 1.02, used on a
processor without LZCNT (Intel Sandy Bridge, Ivy Bridge); `TSR_EMULATE_LZCNT=1` forces it on one that has LZCNT.

## Unknown until the first boot

Whether startup reaches the game's entry, the first call the runtime cannot answer, the renderer's behaviour with this
game's shaders, the language selection (the five archives are present, but the executable's mapping of the system
language is not verified), and frame pacing. Render resolution and aspect ratio stay native until sites are verified
in this executable.
