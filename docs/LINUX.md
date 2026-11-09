# Linux port

The loader, runtime and video core run natively on Linux x86-64 as on Windows: the game's own code executes on the host
CPU, the PS4 system libraries are replaced by `src/runtime_*.c`, and the renderer is the same Vulkan video core. The
Linux release is an AppImage, **experimental**: built and checked headlessly, not yet played through. This page lists
how it is built and packaged, what differs from Windows, and what has been verified.

## Running the AppImage

```sh
chmod +x 13SentinelsAegisRimPC-v0.1.0-x86_64.AppImage
./13SentinelsAegisRimPC-v0.1.0-x86_64.AppImage
```

It needs x86-64, glibc 2.31 or newer, and a Vulkan 1.3 loader and driver (Ubuntu 20.04's own `libvulkan1` 1.2.131 is
too old: the launcher still opens and Play explains the problem). On the **Play** page, **Install game...** installs a
supported base `.pkg`, **Install update...** an update of the same region; see `packaging/README-linux.txt`.

| Data | Where |
| --- | --- |
| `game/` (installed builds, boot images), `user/` (`tsr.ini`, saves, trophies, Trophy Key, shader cache, `bug_reports.json`), `logs/` | next to the AppImage file (`$APPIMAGE`) |
| the same, when that folder is not writable | `$XDG_DATA_HOME/13SentinelsAegisRimPC` (default `~/.local/share/13SentinelsAegisRimPC`) |
| any other folder | `TSR_PORTABLE_DIR` |

The AppImage's own mount is read-only and nothing is written into it (`launcher/paths.cpp`). Without FUSE the
runtime unpacks the AppImage into a private folder under `/tmp`, runs it from there with the same arguments, passes
termination signals on, returns its exit status and removes the folder; `$APPIMAGE` still names the original file, so
the data stays next to it. `APPIMAGE_EXTRACT_AND_RUN=1` forces that mode.

## Building

Releases are built on Ubuntu 20.04, whose glibc 2.31 is the oldest the AppImage runs on. `packaging/linux/ci-deps.sh`
prepares a clean 20.04 system as root (it is Dragon's Crown Pro PC's environment script, unchanged: Clang 20, the GCC 14
C++ library, CMake 3.31.8, and pinned, checksum-verified builds of what 20.04 lacks in `/usr/local`: Vulkan headers, VMA,
glslang, Boost headers, fmt, Zydis, xxHash, robin-map, OpenSSL 3.5, SDL 3.4.16, and the libdecor and PipeWire headers SDL
needs). An existing `DcpFocalBuild` WSL distribution made with it builds this port as it is.

FFmpeg is not taken from the distribution: `tools/build-ffmpeg-linux.sh` builds the release pinned in
`packaging/linux/ffmpeg.env` (8.1.1, the version the Windows build gets from MSYS2; the tarball is signed by the FFmpeg
release key and pinned by SHA-256) into `build/deps/linux/ffmpeg`, with a pinned nasm for its x86 code. It is a shared,
LGPL-2.1 build of libavcodec, libavutil and libswscale with only the H.264 and HEVC decoders and parsers `sceVideodec2`
uses, and nothing autodetected, so its libraries need only glibc. `tools/build.sh` runs it and rebuilds it only when the
pins change.

| Command | Result |
| --- | --- |
| `tools/build.sh` | fetches `gpu/deps.txt`, builds FFmpeg once, builds `build/cmake/linux/bin/13_sentinels_aegis_rim` |
| `tools/build.sh --test` | also builds and runs the 23 headless Linux tests (`TSR_TEST_IMAGES`: the real boot images of the nine builds) |
| `tools/package-linux.sh` | `dist/13SentinelsAegisRimPC-v<version>-x86_64.AppImage` and `.sha256`; debug symbols in `dist/<name>-symbols/` |
| `tools/package-linux.sh --self-test` | runs every release check on fixtures that must pass and fixtures that must fail |
| `tools\package-release.ps1` (Windows) | the Windows ZIP and this AppImage in one run (below) |

To make the WSL2 release environment from scratch, import the Ubuntu 20.04.5 base image
(`ubuntu-base-20.04.5-base-amd64.tar.gz` from https://cdimage.ubuntu.com/ubuntu-base/releases/20.04/release/, SHA-256
`60e216b60947653dc8989be3821380268315b99b2959c29882781541bfe5a426`) and prepare it once, from the repository folder:

```powershell
wsl --import DcpFocalBuild <folder> ubuntu-base-20.04.5-base-amd64.tar.gz --version 2
wsl -d DcpFocalBuild -u root -- bash packaging/linux/ci-deps.sh
```

`tools\package-release.ps1 -WslDistro <name>` uses another distribution prepared the same way.

## Release flow

`tools\package-release.ps1` prepares both platforms in one run and publishes nothing. It takes two inputs that are not
in Git from the ignored `cache\` folder: `cache\test-images\<build>\boot.bin`, the real boot images of the nine builds
(missing ones are copied from `game\image` and `build\image`, where the launcher and `tools\build.ps1` make them), and
`cache\appimage-runtime\runtime-x86_64`, the reviewed AppImage runtime (below). It stops if either is unavailable.

1. Windows: `tools\package.ps1`, `tools\test.ps1` (every headless test), and the Windows feature contract.
2. Linux, in WSL: `tools/package-release-linux.sh` mirrors the sources into `~/.cache/13-sentinels-aegis-rim-pc-build`,
   runs `tools/build.sh --test`, writes the Linux feature contract, runs `tools/package-linux.sh --self-test` and
   `tools/package-linux.sh`, and copies the AppImage, its `.sha256` and the symbols back into `dist\`.
3. Gates: both `.sha256` files match, the Windows release folder is checked again, every name and contract carries the
   project VERSION, and the two feature contracts (`build\release\feature-contract-*.txt`) are identical.
4. `dist\release-notes-v<version>.md` with the SHA-256 of every file to scan and each VirusTotal entry PENDING.

If either platform fails, the release is not prepared. VirusTotal comes after it, by hand (`docs/RELEASE_SECURITY.md`).

## AppImage

`usr/bin/13_sentinels_aegis_rim` has `DT_RPATH=$ORIGIN/../lib`; the bundled libraries (`usr/lib`) have
`RUNPATH=$ORIGIN`. No `LD_LIBRARY_PATH` is set, so programs the launcher starts (`zenity`, `kdialog`, `xdg-open`, `curl`)
are not affected. `libstdc++` and `libgcc_s` are carried in `usr/lib/compat` and used by `AppRun` only when the system's
are older than the build needs.

Bundled: SDL3, FreeType, libpng, OpenSSL (libssl and libcrypto), fmt, Zydis and Zycore, xxHash, the pinned FFmpeg
(libavcodec, libavutil, libswscale) and what they need. From the player's system: glibc, the Vulkan loader and drivers,
OpenGL/EGL, X11, Wayland and libdecor, the audio servers (PipeWire, PulseAudio, ALSA), D-Bus and udev, which SDL loads at
run time. `tools/package-linux.sh` checks, before and after writing the AppImage:

- every file against the expected layout, and no game or user data by name (any case: `eboot.bin`, `ROBO_US.CPK`,
  `param.sfo`, `tsr.ini`, `game/`, `user/`, `logs/`, `cache/`, saves, PKGs, boot images, logs, bug reports) or by content
  (PS4 package, SELF, CPK and param.sfo signatures);
- no local path, `DEBUG_SAULO` marker, private key, token, RenderDoc or Vulkan layer;
- every ELF (found by content): the newest `GLIBC_` version it needs, which may not be newer than 2.31; its
  RPATH/RUNPATH, which may only be the `$ORIGIN` ones above; every `DT_NEEDED` library, which must be bundled or one of
  the listed host libraries; and no part of glibc and no host library in the package;
- the licence texts: this project's, the built-in components', the Ubuntu package or source each bundled library comes
  from (`SYSTEM-PACKAGES.txt`), and FFmpeg's LGPL text with its exact source (`system/ffmpeg/SOURCE.txt`);
- that the runtime at the front of the AppImage is the pinned runtime, byte for byte apart from the sections
  `appimagetool` fills, and that the extracted AppImage holds exactly the checked AppDir.

### The AppImage runtime

The released upstream runtime stops when FUSE fails, so the project builds its own, the same one Dragon's Crown Pro PC
ships: `patches/appimage-runtime/baseline.env` pins [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime)
commit `dd6cebe` (tag `20251108`), the Alpine root file system it is built in, and the SHA-256 of the reviewed result;
`0001-extract-and-run-when-fuse-fails.patch` adds the fallback above. `tools/build-appimage-runtime.sh` builds it in a
chroot (root or passwordless `sudo`, network for the pinned sources) and packaging stops if the result differs from the
pinned SHA-256. `appimagetool` 1.9.1 is downloaded once and checked against its pinned SHA-256; it never fetches a
runtime of its own.

The chroot installs Alpine 3.21's current packages, so a fresh build stops reproducing the reviewed runtime when those
move on (on 2026-11-08 it gave `6b1da184…` instead of `9e458da0…`, and packaging refused it). The reviewed build is
therefore kept in `cache\appimage-runtime\runtime-x86_64`. `tools\package-release.ps1` uses that file only when its
SHA-256 is the pinned one; when it is missing, `tools/import-appimage-runtime.sh` copies it from a WSL packaging cache
that holds the same reviewed file (this project's, Dragon's Crown Pro PC's or Odin Sphere Leifthrasir PC's), checking
the hash before and after the copy, and `tools/build-appimage-runtime.sh` (`TSR_APPIMAGE_RUNTIME`) checks it again.
Any other file is refused. Changing the pin means reviewing a new build first.

## Windows parity

Linux offers every Windows feature that can exist there. The parity gate (`tests/platform_parity_tests.cpp`, run by
both test suites) computes the shared feature contract of the nine builds from the tables both platforms compile, checks
each platform-specific counterpart below, that the Windows-only options are not offered on Linux, that both test suites
run the same tests, and that the release names agree; `tools\package-release.ps1` requires the Windows and Linux
contracts to be identical.

| Feature | Linux |
| --- | --- |
| Nine builds (USA 1.00/1.01/1.02, Europe 1.00/1.01, Japan 1.00/1.03/1.04, Asia 1.00), Install Game, Install Update, Remove Update, cross-CUSA update refusal, active build choice | same code and tables |
| Skip Intro, Exit Game, render resolutions (480p to 4K) and the PS4 Pro path, 60 FPS, LZCNT rewrite, BMI1 and MOVBE emulation, GPU command snapshots | same code and site tables |
| Xbox and PlayStation prompts, controllers (SDL3), text languages, voice behaviour | same code and patches |
| Saves per TITLE_ID, prologue demo transfer, trophies | same code; files under `user/` |
| Window title with build, resolution and frame rate, frame counter, Options menu | same code |
| Vulkan, OpenGL (experimental) | same video core |
| Bug Report | same client and privacy filter; HTTPS through OpenSSL with the distribution's CA bundle instead of WinHTTP; still sent only from **Send report** |
| Hashes and ciphers (PKG, trophies) | `launcher/crypto_openssl.cpp` instead of CNG, identical results |
| Guest memory, faults, threads, fibers | `memfd` and `mmap` below 1 TiB, `SIGSEGV`, `arch_prctl` (the same as Dragon's Crown Pro PC) |
| Logs | `runtime_log_posix.c`: `logs/13_sentinels_aegis_rim_NNN.log`, echoed to the terminal |
| File chooser for Install | `zenity` or `kdialog`, else the launcher's own chooser (`TSR_FILE_CHOOSER=builtin` or `native`) |
| Open folder | `xdg-open` |
| Shift at start | read from the X server; on Wayland use `--launcher` or the desktop entry's **Show launcher** |
| Port updates | the AppImage file (`$APPIMAGE`, never its mount, an extracted copy or `/tmp/.mount_*`) is replaced after its SHA-256 is checked, keeps its mode, is restarted from the same path, and is put back if the new one cannot start; downloads use `curl` |

Windows only, by technical necessity, and not offered on Linux:

- **Direct3D 11 (ANGLE)**: a Windows API. The Renderer lists show Auto, Vulkan and OpenGL; a `d3d11` setting from a
  copied `tsr.ini` runs Vulkan.
- **Allow overlay programs**: starts the game as a clean Windows process for overlay DLLs. Linux overlays are Vulkan
  layers and `LD_PRELOAD` libraries that attach to every process the same way.
- `trg-updater.exe`: the Windows helper that replaces files the running launcher holds; the AppImage replaces itself.

## Verification

Done on Ubuntu 20.04 (WSL2, `DcpFocalBuild`), headless:

- configure and full build of the game and all 23 Linux test targets with Clang 20, no compiler warnings in the game;
- `tools/build.sh --test` with the real boot images of the nine builds: runtime, CPU, saves, packages, build parity,
  platform parity, launcher, Bug Report, updater (including the AppImage replacement and rollback), Skip Intro, Exit Game,
  Xbox prompts, Ajm, frame pacing, command snapshots, renderer selection, OpenGL backend and render resolution tests;
- `tools/package-linux.sh --self-test`;
- `tools/package-linux.sh --skip-build` in the WSL packaging cache (not a release): the AppDir and the written AppImage
  passed every check above; the Windows and Linux feature contracts of the nine builds were identical.

Not verified: running the game, the launcher or the AppImage on any Linux desktop, controllers, audio output, video
playback, a real port update, NVIDIA, AMD and Intel drivers, Steam Deck, gamescope, and performance. Please report what
you find.
