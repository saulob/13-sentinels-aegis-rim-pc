# 13 Sentinels: Aegis Rim PC Port

A native Windows x64 port of **13 Sentinels: Aegis Rim** (PlayStation 4: USA, Europe, Japan and Asia). The game's original x86-64
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

1. Open `13_sentinels_launcher.exe` (or `13_sentinels_aegis_rim.exe`).
2. **Play → Install game...** and select your region's base package. It is unpacked into `game\` next to the program
   (3.5 to 5 GB).
3. Optional: **Play → Install update...** and select an update of the same region (USA 1.01 or 1.02, Europe 1.01,
   Japan 1.03 or 1.04; no earlier update is needed). Each update gets a folder of its own made of hard links to the base
   plus the update's files, so the base stays installed exactly as it was (0.5 to 1.3 GB more each).
4. Installed builds are detected automatically. The launcher starts the newest installed update of the region; with
   several regions installed, the region installed last (installing an installed region's package again switches back
   to it without copying anything). Every region and version has a folder of its own; there is no version to choose.
5. **Play**.

The installer stages everything next to the active installation, verifies it (identity, required archives, eboot hash,
boot image) and only then swaps it in, with a backup that is put back if the swap fails. An interrupted or cancelled
install leaves the installed game as it was. The packages are only read. An update package selected in Install game,
or a base package in Install update, is refused before anything is unpacked.

## Launcher

TRG Launcher pages: **Play** (install game, install update), **Display**, **Graphics**, **Gameplay**
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
background, there is no telemetry and no player identity, and the game is fully playable offline. Until the service is
deployed the page says it is unavailable and offers to save the same report to `logs\reports\` for a GitHub issue. See
[docs/BUG_REPORTS.md](docs/BUG_REPORTS.md); the service is in
[server/13-sentinels-bug-reports](server/13-sentinels-bug-reports/README.md).

**Port updates.** The launcher checks this repository's latest GitHub release once per start and, for a newer one,
shows a banner; it installs only when you choose UPDATE NOW and RESTART TO UPDATE, and not while the game is running.
`trg-updater.exe` replaces only the port's own files after checking the package's SHA-256, and restores them if
anything fails. Game files, the `user` folder and logs are kept. See [docs/RELEASE_SECURITY.md](docs/RELEASE_SECURITY.md).

**Languages.** Text Language (menus and subtitles) is a launcher choice among the languages the installed region
has: English, French, German, Italian and Spanish (USA, Europe), Japanese (Japan), Traditional and Simplified Chinese
(Asia). It is kept as chosen: a language a region lacks shows that region's default and the choice stays. Voice Language
is shown for information only: every build has the Japanese voices; USA and Europe 1.01 and 1.02 and Japan 1.04 ship the
English ones too, and the game's own Audio Language option picks between them. Saves are kept per region (title ID);
trophies are one shared list. The Japanese and Asian games can carry over a prologue demo save found in the user folder. **Skip Intro** answers the startup logo scene's button poll, the
game's own skip. **Controller icons** Xbox repaints the button atlas and the icons on the built-in manual's pages in the
installed archives, reversibly (`scripts/prompts/README.md`).

## Building

Windows 10 1903+ with [MSYS2](https://www.msys2.org/) CLANG64:

```powershell
.\tools\setup.ps1              # checks the toolchain and lists missing packages
.\tools\build.ps1              # fetches the pinned dependencies (gpu\deps.txt) and builds
.\tools\test.ps1               # focused headless tests
```

The output is `build\cmake\clang64\bin\13_sentinels_aegis_rim.exe`, `13_sentinels_launcher.exe` and the update helper
`trg-updater.exe`. The PC port
version has one source of truth: `project(... VERSION ...)` in `CMakeLists.txt`.

## Layout

| Path | What |
|---|---|
| `src/` | Loader and PS4 runtime (kernel, threads, memory, files, save data, pad, audio, Ajm/ATRAC9, services) |
| `gpu/` | shadPS4-derived video core: Liverpool/Vulkan renderer, shader recompiler, GnmDriver, VideoOut, Videodec2 |
| `launcher/` | Installer, game checks, boot image builder, TRG Launcher pages, bug reports |
| `server/` | The bug report service: a Cloudflare Worker and its private dashboard (deployed by hand) |
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
