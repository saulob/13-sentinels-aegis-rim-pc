13 Sentinels: Aegis Rim - PC port (Linux AppImage, experimental)
Version @VERSION@

13 Sentinels: Aegis Rim for PlayStation 4, running natively on Linux x86-64: the
game's own code runs on the PC with a replacement for the PS4 system libraries
and a Vulkan renderer. The AppImage has the same applicable features as the
Windows release; it has been built and checked, not yet played through.

Source code and issue tracker:
https://github.com/saulob/13-sentinels-aegis-rim-pc


NO GAME FILES ARE INCLUDED
--------------------------
You need your own legally obtained copy of the game: a decrypted 13 Sentinels:
Aegis Rim PS4 package (.pkg). This port runs:

  USA:    Title ID CUSA19610, versions 1.00 (base), 1.01 and 1.02
  Europe: Title ID CUSA19620, versions 1.00 (base) and 1.01
  Japan:  Title ID CUSA10602, versions 1.00 (base), 1.03 and 1.04
  Asia:   Title ID CUSA18093, version 1.00 (base)
  (each update is installed over the base package of the same region)

The base game plays on its own; an update can be installed later from the
launcher's Play page and leaves the base installed. The launcher detects the
installed builds by itself and starts the newest supported update of the
region you installed last; there is no version to pick. Any other version is
refused. A package still encrypted for a PS4 console cannot be unpacked on a
PC.

Compatibility note: CUSA19610 USA is the primary development build and has
received the most gameplay testing. The Europe, Japan and Asia builds have been
verified to install, boot, start the game and pass the port's focused
compatibility checks, but they have not been fully played from start to
finish. Region-specific issues may still exist.


REQUIREMENTS
------------
- Linux x86-64 with glibc 2.31 or newer
- A Vulkan 1.3 loader and a driver with Vulkan 1.3, VK_EXT_robustness2,
  VK_KHR_push_descriptor and VK_EXT_vertex_attribute_divisor (Ubuntu 20.04's
  own Vulkan loader is too old)
- X11 or Wayland, and PipeWire, PulseAudio or ALSA for sound
- A processor with AVX: Intel Core 2nd generation (Sandy Bridge), AMD FX or
  newer. The LZCNT, MOVBE and BMI1 instructions some of these lack are handled
  in software.
- Your own 13 Sentinels: Aegis Rim package, plus up to 5 GB of free disk space
  next to the AppImage for the installed game (up to 1.3 GB more for an update)
- Optional: zenity or kdialog for the desktop's file chooser, curl for port
  updates


INSTALL
-------
1. Put 13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage in a folder you can
   write to, for example ~/Games/13Sentinels.
2. Make it executable and start it:
     chmod +x 13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage
     ./13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage
   The launcher opens.
3. On the Play page, click "Install game..." and select your base game .pkg
   file. The launcher checks the package and unpacks it into the "game" folder
   next to the AppImage. The .pkg file is only read.
4. Optional: click "Install update..." and select an update .pkg of the same
   region. The base stays installed; the newest installed update starts.
   "Remove Update..." removes the installed updates and returns to the base.
5. Press PLAY.

The file chooser is the desktop's own (zenity or kdialog) when one is
installed, otherwise the launcher's own, which works with mouse, keyboard and
controller. TSR_FILE_CHOOSER=builtin or native picks one.

Without FUSE (a container, some restricted desktops) the AppImage unpacks
itself into a private folder under /tmp, runs from there and removes it when
the game ends; your data still stays next to the AppImage file.
APPIMAGE_EXTRACT_AND_RUN=1 forces that.


PORT UPDATES
------------
The launcher checks the project's GitHub releases once per start. When a newer
release exists, an update banner appears in the header; nothing is installed
until you choose UPDATE NOW and then RESTART TO UPDATE. The new AppImage is
downloaded and checked against its published SHA-256, then it replaces this
AppImage file, keeps its permissions and is started again. If it cannot start,
the previous AppImage is put back. Game files, user data, saves, settings,
trophies, logs and shader caches are kept. Port updates need curl.


PLAYING
-------
- F1 (or L3 + R3 on a controller) opens the Options menu.
- F11 switches between Windowed and Borderless Fullscreen. F2 shows the frame
  counter.
- Xbox and PlayStation controllers work without setup.
- The keyboard works too; the launcher's Controls page lists the keys.
- To open the launcher again after turning it off, start the AppImage with
  --launcher, use the desktop entry's "Show launcher" action, or hold Shift
  while starting (X11 sessions).


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to the AppImage
- Logs:           the "logs" folder next to the AppImage
- Settings (tsr.ini), saves, trophy progress, the Trophy Key (keep it
                  private) and shader cache:
                  the "user" folder next to the AppImage

The AppImage itself is read-only. If its folder is not writable, these
folders are kept in $XDG_DATA_HOME/13SentinelsAegisRimPC (usually
~/.local/share/13SentinelsAegisRimPC). TSR_PORTABLE_DIR names another folder.
When updating the port by hand, keep "user" (and "game", to skip
reinstalling).
If the game does not start or crashes, use the launcher's Bug Report page: it
sends a privacy-filtered report with one log only when you press Send report
and confirm, and can save the same report instead. You can also attach the
newest 13_sentinels_aegis_rim_*.log file from the "logs" folder to an issue at
https://github.com/saulob/13-sentinels-aegis-rim-pc/issues


DIFFERENCES FROM WINDOWS
------------------------
- Renderers: Vulkan (supported) and OpenGL (experimental). Direct3D 11 is a
  Windows renderer and is not offered.
- "Allow overlay programs" is a Windows option: Linux overlays (Vulkan layers,
  LD_PRELOAD) attach to the game without it.
- Window positions are restored on X11; a Wayland compositor places windows
  itself.


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 or later (GPL-2.0-or-later). The full text is in
usr/share/licenses/13SentinelsAegisRimPC/LICENSE.txt inside the AppImage
(./13SentinelsAegisRimPC-v@VERSION@-x86_64.AppImage --appimage-extract shows
it).

Third-party components keep their own licenses. Their license files and
notices are in the same folder, starting with THIRD-PARTY-LICENSES.txt and
SYSTEM-PACKAGES.txt. The FFmpeg libraries in the AppImage are an LGPL-2.1
build (decoders only); system/ffmpeg/SOURCE.txt names their exact source.

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by Vanillaware, Atlus, SEGA,
Sony Interactive Entertainment or any other rights holder of 13 Sentinels:
Aegis Rim.
