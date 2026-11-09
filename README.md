<div align="center">

# 13 Sentinels: Aegis Rim - PC Port

### 13 Sentinels: Aegis Rim for PlayStation 4, running natively on Windows, and experimentally on Linux and Steam Deck, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/13-sentinels-aegis-rim-pc?style=for-the-badge&label=release&color=4f7fd7&labelColor=07101f&include_prereleases&cacheSeconds=300)](https://github.com/saulob/13-sentinels-aegis-rim-pc/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/13-sentinels-aegis-rim-pc/total?style=for-the-badge&color=4f7fd7&labelColor=07101f&cacheSeconds=300)](https://github.com/saulob/13-sentinels-aegis-rim-pc/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original PS4 x86-64 game code runs directly on the PC through replacement PS4 system libraries, with a Vulkan renderer derived from <a href="https://github.com/shadps4-emu/shadPS4">shadPS4</a>. <b>No game files included</b>: bring your own legally obtained, supported 13 Sentinels: Aegis Rim PS4 package.</sub>

</div>

> **Version 0.1.0, the first release**, for Windows (ZIP) and Linux (AppImage) together. The game installs, boots and
> plays on Windows with the Vulkan renderer. The Linux AppImage has the same applicable feature set and is
> **experimental**: it is built and checked headlessly but has not been played through yet. See [docs/LINUX.md](docs/LINUX.md).
>
> **Compatibility note:** CUSA19610 USA is the primary development build and has received the most gameplay testing. The
> Europe, Japan and Asia builds have been verified to install, boot, start the game and pass the port's focused
> compatibility checks, but they have not been fully played from start to finish. Region-specific issues may still exist.

> **No game files are included.** You need your own legally obtained copy of the game as a decrypted (fake) PKG. This
> project is not affiliated with Vanillaware, Atlus or Sony Interactive Entertainment.

## Supported versions

| Build | Package | Role | eboot.bin SHA-256 |
|---|---|---|---|
| CUSA19610 1.00 USA | `gd`, APP_VER 01.00, VERSION 01.00 | Base game, required, plays on its own | `cf7e9093…5950` |
| CUSA19610 1.01 USA | `gp`, APP_VER 01.01, VERSION 01.00 | Optional update, laid over the installed 1.00 | `dafcc5f0…0533` |
| CUSA19610 1.02 USA | `gp`, APP_VER 01.02, VERSION 01.00 | Optional update, laid over the installed 1.00 | `07525009…3ff1` |
| CUSA19620 1.00 Europe | `gd`, APP_VER 01.00, VERSION 01.01 | Base game, required, plays on its own | `e4df1d07…90e1` |
| CUSA19620 1.01 Europe | `gp`, APP_VER 01.01, VERSION 01.00 | Optional update, laid over the installed 1.00 | `dafcc5f0…0533` or `123730cd…6b28` |
| CUSA10602 1.00 Japan | `gd`, APP_VER 01.00, VERSION 01.00 | Base game, required, plays on its own | `6d6f67ce…5d0c` |
| CUSA10602 1.03 Japan | `gp`, APP_VER 01.03, VERSION 01.00 | Optional update, laid over the installed 1.00 | `fdd1b5ad…c11a` |
| CUSA10602 1.04 Japan | `gp`, APP_VER 01.04, VERSION 01.00 | Optional update, laid over the installed 1.00 (1.03 not needed) | `3bcaeac5…fbcb` |
| CUSA18093 1.00 Asia | `gd`, APP_VER 01.00, VERSION 01.01 | Base game, plays on its own | `dd6d54e9…2bcf` |

Packages are identified by their `param.sfo` (TITLE_ID, CONTENT_ID, CATEGORY, APP_VER, VERSION) and their executable
hash, never by file name; an update is only laid over the base of its own title ID. The European 1.01 update also comes
repackaged for older firmware; both packagings are the same game and install as the one build. The European executables
are the American ones (1.00 differs only in a build date string), so they share the American feature sites.

## Using it

1. Windows: run `13_sentinels_aegis_rim.exe`; the launcher opens. Linux: put the AppImage in a folder you can write to,
   `chmod +x 13SentinelsAegisRimPC-v0.1.0-x86_64.AppImage` and start it; the launcher opens.
2. **Play → Install game...** and select your region's base package. It is unpacked into `game\` next to the program
   (Linux: next to the AppImage) (3.5 to 5 GB).
3. Optional: **Play → Install update...** and select an update of the same region (USA 1.01 or 1.02, Europe 1.01,
   Japan 1.03 or 1.04; no earlier update is needed). Each update gets a folder of its own made of hard links to the base
   plus the update's files, so the base stays installed exactly as it was (0.5 to 1.3 GB more each).
4. Installed builds are detected automatically. The launcher starts the newest installed update of the region; with
   several regions installed, the region installed last (installing another region's base package again switches back
   to it). Every region and version has a folder of its own; there is no version to choose. **Remove update** returns the
   active region to its base game, keeping the base, saves and settings.
5. **Play**.

The installer stages everything next to the active installation, verifies it (identity, required archives, eboot hash,
boot image) and only then swaps it in, with a backup that is put back if the swap fails. An interrupted or cancelled
install leaves the installed game as it was. The packages are only read. An update package selected in Install game,
or a base package in Install update, is refused before anything is unpacked.

## Launcher

TRG Launcher pages: **Play** (install game, install update, remove update), **Display**, **Graphics**, **Gameplay**
(text language, voice language note, skip intro, frame readouts, overlay programs), **Controls** (detected controllers,
controller icons, keyboard bindings, port hotkeys),
**Trophies** (kept on this PC), **Bug Report**, **About** (folders, logs, diagnostics, sponsor). There is no Online
page and no Cheats page.

**Renderer** (Display page, also in the in-game Options menu): Vulkan is the default and the supported renderer. OpenGL
and Direct3D 11 (OpenGL ES through ANGLE, Windows only) are experimental and not yet verified with this game; Auto uses
Vulkan, and OpenGL when Vulkan cannot run. See [docs/RENDERERS.md](docs/RENDERERS.md).

**Render resolution** (Graphics page, also in the in-game Options menu; applies at the next start).
Render resolution (480p, 720p, Native 1080p, 1440p, 1800p, 4K) sets the size of the game's own render targets: the loader
rewrites the game's 12 verified choices between 1920 × 1080 and its PS4 Pro size 3840 × 2160 (display colour and depth
targets and the screen size every viewport and scene target reads), separately located in each supported executable,
and above 1080p answers PS4 Pro so the game takes the path it was built to run at 4K. If a site does not hold
its original instruction nothing is written and the game renders at Native, with the reason in the log. The game
always shows its original 16:9 picture, with black bars where the window or display is not 16:9 (Presentation Stretch
fills it instead). There is no Aspect Ratio setting and no True Ultrawide; an `aspect_ratio` line left in `tsr.ini` by an
earlier build is ignored. The log's `Render resolution:` lines show what was requested and used.

**Bug reports** are sent only when you press Send report and confirm. The page builds a privacy-filtered report from one
chosen log, shows exactly what would be sent, and then makes one outgoing HTTPS request to the developer's report service;
My Reports shows each report's status and the developer's reply when you press Refresh. Nothing is sent at startup or in the
background, there is no telemetry and no player identity, and the game is fully playable offline. When the service cannot
be reached, the page says so and offers to save the same report to `logs\reports\` for a GitHub issue. See
[docs/BUG_REPORTS.md](docs/BUG_REPORTS.md); the service is in
[server/13-sentinels-bug-reports](server/13-sentinels-bug-reports/README.md).

**Port updates.** The launcher checks this repository's latest GitHub release once per start and, for a newer one,
shows a banner; it installs only when you choose UPDATE NOW and RESTART TO UPDATE, and not while the game is running.
On Windows `trg-updater.exe` replaces only the port's own files after checking the package's SHA-256, and restores them
if anything fails; on Linux the AppImage file is replaced by the checked new one, keeps its permissions and is started
again from the same path, or put back if it cannot start. Game files, the `user` folder and logs are kept. See
[docs/RELEASE_SECURITY.md](docs/RELEASE_SECURITY.md).

**Languages.** Text Language (menus and subtitles) is a launcher choice among the languages the installed region
has: English, French, German, Italian and Spanish (USA, Europe), Japanese (Japan), Traditional and Simplified Chinese
(Asia). It is kept as chosen: a language a region lacks shows that region's default and the choice stays. Voice Language
is shown for information only: every build has the Japanese voices; USA and Europe 1.01 and 1.02 and Japan 1.04 ship the
English ones too, and the game's own Audio Language option picks between them. Saves are kept per region (title ID);
trophies are one shared list. The Japanese and Asian games can carry over a prologue demo save found in the user folder. **Skip Intro** answers the startup logo scene's button poll, the
game's own skip. **Controller icons** Xbox repaints the button atlas and the icons on the built-in manual's pages in the
installed archives, reversibly (`scripts/prompts/README.md`).

## Linux

The AppImage runs on x86_64 Linux with glibc 2.31 or newer and a Vulkan 1.3 loader and driver. `game`, `user` and `logs`
are kept next to the AppImage file (its own mount is read-only), or in `~/.local/share/13SentinelsAegisRimPC` when that
folder is not writable. Without FUSE it unpacks itself to a temporary folder and runs from there. Everything above is the
same on Linux, except what only Windows has: Direct3D 11 (ANGLE) and Allow overlay programs are not offered. The file
chooser is `zenity` or `kdialog` when installed, otherwise the launcher's own. See [docs/LINUX.md](docs/LINUX.md) for the
full Windows/Linux comparison and what has been verified.

## Building

Windows 10 1903+ with [MSYS2](https://www.msys2.org/) CLANG64:

```powershell
.\tools\setup.ps1              # checks the toolchain and lists missing packages
.\tools\build.ps1              # fetches the pinned dependencies (gpu\deps.txt) and builds
.\tools\test.ps1               # focused headless tests
```

The output is `build\cmake\clang64\bin\13_sentinels_aegis_rim.exe` (launcher and game in one program) and the update
helper `trg-updater.exe`. The PC port
version has one source of truth: `project(... VERSION ...)` in `CMakeLists.txt`.

Linux (Ubuntu 20.04 prepared by `packaging/linux/ci-deps.sh`, the release baseline): `tools/build.sh`, `tools/build.sh
--test` and `tools/package-linux.sh`. A release of both platforms is prepared on Windows by
`.\tools\package-release.ps1`, which builds the AppImage in WSL2; it publishes nothing
([docs/LINUX.md](docs/LINUX.md), [docs/RELEASE_SECURITY.md](docs/RELEASE_SECURITY.md)).

## Layout

| Path | What |
|---|---|
| `src/` | Loader and PS4 runtime (kernel, threads, memory, files, save data, pad, audio, Ajm/ATRAC9, services) |
| `gpu/` | shadPS4-derived video core: Liverpool/Vulkan renderer, shader recompiler, GnmDriver, VideoOut, Videodec2 |
| `launcher/` | Installer, game checks, boot image builder, TRG Launcher pages, bug reports |
| `server/` | The bug report service: a Cloudflare Worker and its private dashboard (deployed by hand) |
| `scripts/` | Read-only developer tools: package and module inspection, boot image preparation, import coverage |
| `docs/` | Technical notes, starting with [FIRST_BOOT.md](docs/FIRST_BOOT.md) |

## Sponsoring

13 Sentinels: Aegis Rim PC Port is maintained and developed in my free time. If you find it useful, consider supporting its continued development.

<p align="center">
<a href="https://github.com/sponsors/saulob" target="_blank"><img src="https://img.shields.io/badge/GitHub%20Sponsors-Sponsor-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="GitHub Sponsors"></a>
</p>

## Credits

Built on the [Dragon's Crown Pro PC Port](https://github.com/saulob/dragons-crown-pro-pc) 1.0.0, with runtime fixes
from the Catherine: Full Body and Odin Sphere Leifthrasir ports, the Ajm decoder from MediEvil PC, and
[shadPS4](https://github.com/shadps4-emu/shadPS4). ATRAC9 decoding by [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)
(MIT). Launcher UI by [TRG Launcher](https://github.com/TekRantGaming/trg-launcher). Third-party licences are listed in
`packaging/licenses/THIRD-PARTY-LICENSES.txt`.

## License

GPL-2.0-or-later (see `LICENSE`).
