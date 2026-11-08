# 13 Sentinels: Aegis Rim PC Port

A native Windows x64 port of **13 Sentinels: Aegis Rim** (PlayStation 4, CUSA19610 USA). The game's original x86-64
code runs directly on the PC through replacement PS4 system libraries and a Vulkan renderer derived from shadPS4.

> **Status: early development (0.1.0).** The installer, launcher, runtime and renderer are in place, but the game has
> **not yet been verified to boot**. Nothing here claims that it renders or is playable.

> **No game files are included.** You need your own legally obtained copy of the game as a decrypted (fake) PKG. This
> project is not affiliated with Vanillaware, Atlus or Sony Interactive Entertainment.

## Supported versions

| Build | Package | Role | eboot.bin SHA-256 |
|---|---|---|---|
| CUSA19610 1.00 USA | `gd`, APP_VER 01.00, VERSION 01.00 | Base game, required, plays on its own | `cf7e9093…5950` |
| CUSA19610 1.01 USA | `gp`, APP_VER 01.01, VERSION 01.00 | Optional update, laid over the installed 1.00 | `dafcc5f0…0533` |
| CUSA19610 1.02 USA | `gp`, APP_VER 01.02, VERSION 01.00 | Optional update, laid over the installed 1.00 | `07525009…3ff1` |

Packages are identified by their `param.sfo` (TITLE_ID, CONTENT_ID `UP0177-CUSA19610_00-13SENTINELSEFIGS`, CATEGORY,
APP_VER, VERSION) and their executable hash, never by file name. Other regions (CUSA10602 Japan, CUSA19620 Europe,
CUSA18093 Asia) are recognized and refused with a diagnostic block in the log.

## Using it

1. Open `13_sentinels_launcher.exe` (or `13_sentinels_aegis_rim.exe`).
2. **Play → Install game...** and select the USA 1.00 package. It is unpacked into `game\` next to the program
   (about 4.5 GB).
3. Optional: **Play → Install update...** and select the 1.01 or the 1.02 update package (either, or both; 1.01 is
   not needed for 1.02). Each update gets a folder of its own made of hard links to the base plus the update's files,
   so 1.00 stays installed exactly as it was (about 0.6 GB more each).
4. With more than one installed, **Game version** on the Play page chooses which one starts. Nothing is copied.
   **Remove update** deletes one update's own folder and boot image; 1.00, the other update, saves, trophies and
   settings are kept, and 1.00 becomes the game version when the removed update was.
5. **Play**.

The installer stages everything next to the active installation, verifies it (identity, required archives, eboot hash,
boot image) and only then swaps it in, with a backup that is put back if the swap fails. An interrupted or cancelled
install leaves the installed game as it was. The packages are only read. An update package selected in Install game,
or a base package in Install update, is refused before anything is unpacked.

## Launcher

TRG Launcher pages: **Play** (install game, install update, game version, remove update), **Display**, **Graphics**, **Gameplay**
(text language, voice language note, skip intro, frame readouts, overlay programs), **Controls** (detected controllers,
controller icons, keyboard bindings, port hotkeys),
**Trophies** (kept on this PC), **Bug Report**, **About** (folders, logs, diagnostics, sponsor). There is no Online
page and no Cheats page.

**Bug reports are never uploaded.** The page builds a privacy-filtered report from one chosen log, shows a preview,
saves it to `logs\reports\`, and opens the GitHub issue tracker only when asked. Attaching the file is your own step.

**Languages.** Text Language (menus and subtitles: English, French, German, Italian, Spanish) is a launcher choice and is
kept as chosen: a language a version lacks shows the nearest supported one and the choice stays. Voice Language is shown
for information only: 1.00 has the Japanese voices only; 1.01 and 1.02 ship the English ones too (`ROBO_PATCH_US.CPK`) and
the game's own Audio Language option picks between them. **Skip Intro** answers the startup logo scene's button poll, the
game's own skip. **Controller icons** Xbox repaints the button atlas and the icons on the built-in manual's pages in the
installed archives, reversibly (`scripts/prompts/README.md`).

## Building

Windows 10 1903+ with [MSYS2](https://www.msys2.org/) CLANG64:

```powershell
.\tools\setup.ps1              # checks the toolchain and lists missing packages
.\tools\build.ps1              # fetches the pinned dependencies (gpu\deps.txt) and builds
.\tools\test.ps1               # focused headless tests
```

The output is `build\cmake\clang64\bin\13_sentinels_aegis_rim.exe` and `13_sentinels_launcher.exe`. The PC port
version has one source of truth: `project(... VERSION ...)` in `CMakeLists.txt`.

## Layout

| Path | What |
|---|---|
| `src/` | Loader and PS4 runtime (kernel, threads, memory, files, save data, pad, audio, Ajm/ATRAC9, services) |
| `gpu/` | shadPS4-derived video core: Liverpool/Vulkan renderer, shader recompiler, GnmDriver, VideoOut, Videodec2 |
| `launcher/` | Installer, game checks, boot image builder, TRG Launcher pages, bug reports |
| `scripts/` | Read-only developer tools: package and module inspection, boot image preparation, import coverage |
| `docs/` | Technical notes, starting with [FIRST_BOOT.md](docs/FIRST_BOOT.md) |

## Credits

Built on the [Dragon's Crown Pro PC Port](https://github.com/saulob/dragons-crown-pro-pc) 1.0.0, with runtime fixes
from the Catherine: Full Body and Odin Sphere Leifthrasir ports, the Ajm decoder from MediEvil PC, and
[shadPS4](https://github.com/shadps4-emu/shadPS4). ATRAC9 decoding by [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)
(MIT). Launcher UI by [TRG Launcher](https://github.com/TekRantGaming/trg-launcher). Third-party licences are listed in
`packaging/licenses/THIRD-PARTY-LICENSES.txt`.

## License

GPL-2.0-or-later (see `LICENSE`).
