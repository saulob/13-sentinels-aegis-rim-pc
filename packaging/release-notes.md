## 13 Sentinels: Aegis Rim PC Port v@VERSION@

**v@VERSION@ is the first release.** 13 Sentinels: Aegis Rim for PlayStation 4 runs natively on Windows and Linux: the game's own code runs on the PC with a replacement for the PS4 system libraries and a Vulkan renderer. Windows and Linux ship together, from the same version and with the same game support.

No game files are included. You need your own legally obtained, decrypted 13 Sentinels: Aegis Rim PS4 package (.pkg) of a supported build.

## Highlights

- **Nine supported game builds** in four regions: USA, Europe, Japan and Asia, base games and updates.
- **Launcher** with **Install Game**, **Install Update** and **Remove Update**; the newest installed update of the region you installed last starts, with no version to pick.
- **Render resolutions** from 480p to 4K, with the game's PS4 Pro path above 1080p, at **60 FPS**.
- **Xbox controller prompts** throughout the game, or the original PlayStation art.
- **Skip Intro** and **Exit Game** on the title menu.
- **Bug Report** from the launcher, sent only when you press **Send report**.
- **Windows x64 ZIP** and **Linux x86_64 AppImage** with the same applicable features.

## Supported game versions

| Region | Title ID | Versions |
| --- | --- | --- |
| USA | CUSA19610 | 1.00 (base), 1.01, 1.02 |
| Europe | CUSA19620 | 1.00 (base), 1.01 |
| Japan | CUSA10602 | 1.00 (base), 1.03, 1.04 |
| Asia | CUSA18093 | 1.00 (base) |

Each update is installed over the base package of the same region; an update for another Title ID is refused. Any other build is refused with an explanation.

CUSA19610 USA is the primary development build and has received the most gameplay testing. Europe, Japan and Asia have been verified to install, boot, start the game and pass focused compatibility checks, but they have not been fully played from start to finish. Region-specific issues may still exist.

## Windows and Linux

The Linux AppImage has the same applicable feature set as Windows. Both are built from the same sources, the same supported-build tables and the same game patches, and the release gate compares the two platforms' feature contracts for all nine builds before a release is prepared. The gate is headless: the Linux AppImage has been built and checked, not played through, so it stays **experimental**.

What differs is what only one operating system has:

- **Direct3D 11 (ANGLE)** is a Windows renderer; Linux offers Vulkan and OpenGL.
- **Allow overlay programs** is a Windows option; Linux overlays (Vulkan layers, `LD_PRELOAD`) attach without it.
- Updates: Windows replaces the program files with `trg-updater.exe`; the AppImage replaces itself, keeps its permissions and starts again from the same file.
- **Install Game** uses the Windows file dialog, or on Linux `zenity`/`kdialog` when installed and the launcher's own file chooser otherwise.

## Features

- **Install Game**, **Install Update**, **Remove Update** and **Reinstall game**, with saves and settings kept and a failed install leaving the previous one as it was.
- **Render resolution**: 480p, 720p, Native (1080p), 1440p, 1800p and 4K. The picture stays 16:9, with black bars on other window shapes.
- **60 FPS**, VSync, Windowed and Borderless Fullscreen (F11), monitor and graphics card choice, SMAA, texture filtering, FSR 1 and RCAS.
- **Text language** of the build's own languages, and the game's own **voice** options.
- **Controllers**: Xbox controllers with Xbox button prompts in the menus, help screens and talk prompts; PlayStation controllers with the original prompts; keyboard.
- **Skip Intro** and **Exit Game** in every supported build.
- **Saves** per Title ID, so regions never mix, and the Japanese and Asian builds read a prologue demo save when one is present.
- **Trophies** kept locally, per game.
- **Window title** with the running build (`13 Sentinels: Aegis Rim | CUSA19610 1.02`), and optionally the render resolution and frame rate; an optional frame counter.
- **Renderers**: Vulkan (supported), OpenGL (experimental), Direct3D 11 on Windows (experimental).
- **Older processors**: BMI1, MOVBE and LZCNT are handled in software where the CPU lacks them.
- **Port updates**: the launcher offers a newer release and installs it only when you choose **UPDATE NOW**.
- **Portable**: game files, settings, saves and logs stay in folders next to the program (Windows) or the AppImage (Linux).

## Install

- **Windows**: extract **13SentinelsAegisRimPC-v@VERSION@-windows-x64.zip** anywhere you can write to (not Program Files) and run **13_sentinels_aegis_rim.exe**: the launcher opens. On the **Play** page click **Install game...** and select your base game .pkg, optionally **Install update...** with an update of the same region, then press **PLAY**.
- **Linux AppImage (experimental)**: download **13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage** into a folder you can write to, run `chmod +x 13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage` and start it. `game`, `user` and `logs` are created next to the AppImage. Without FUSE the AppImage unpacks itself to a temporary folder and runs from there. See `docs/LINUX.md`.

## Known limitations

- Only USA has been played extensively; the other regions have passed focused checks only.
- The Linux AppImage is experimental: built and headless-tested, not yet gameplay-tested.
- OpenGL and Direct3D 11 are experimental and not verified with this game; Vulkan is the supported renderer.
- 16:9 only: no ultrawide or other aspect ratio.
- The German manual page Manual10 keeps one PlayStation L2 button.

## Reporting problems

Use the launcher's **Bug Report** page: it shows exactly what will be sent, removes names, folders and addresses first, and sends nothing until you press **Send report** and confirm. You can also open an issue at https://github.com/saulob/13-sentinels-aegis-rim-pc/issues and attach the newest `13_sentinels_aegis_rim_*.log` file from the `logs` folder, with a line about what you were doing.

## Requirements

- **Windows**: Windows 10 version 1903 or later, or Windows 11, 64-bit.
- **Linux**: x86_64 with glibc 2.31 or newer and a Vulkan 1.3 loader and driver (Ubuntu 20.04's own Vulkan loader is too old; current distributions and SteamOS have one). FUSE is optional.
- A processor with AVX (Intel Sandy Bridge, AMD FX or newer).
- A graphics card and driver with Vulkan 1.3, `VK_EXT_robustness2`, `VK_KHR_push_descriptor` and `VK_EXT_vertex_attribute_divisor`.
- Your own supported 13 Sentinels: Aegis Rim package and up to 5 GB of free space next to the program (up to 1.3 GB more for an update).
- Port updates and Bug Report need an internet connection; nothing else does.

## Checksums

Each file has its SHA-256 published next to it: `13SentinelsAegisRimPC-v@VERSION@-windows-x64.zip.sha256` and `13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage.sha256`.

| File | SHA-256 |
| --- | --- |
| `13SentinelsAegisRimPC-v@VERSION@-windows-x64.zip` | `@WINDOWS_ZIP_SHA256@` |
| `13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage` | `@LINUX_APPIMAGE_SHA256@` |

## VirusTotal

- Windows ZIP: PENDING
- Linux AppImage: PENDING
- Windows executable (`13_sentinels_aegis_rim.exe`, SHA-256 `@GAME_EXE_SHA256@`): PENDING
- Updater (`trg-updater.exe`, SHA-256 `@UPDATER_EXE_SHA256@`): PENDING
