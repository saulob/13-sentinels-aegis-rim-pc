13 Sentinels: Aegis Rim - PC port
Version @VERSION@

13 Sentinels: Aegis Rim for PlayStation 4, running natively on Windows: the game's
own code runs on the PC with a replacement for the PS4 system libraries and a
Vulkan renderer.

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
- Windows 10 version 1903 or later, or Windows 11, 64-bit
- A processor with AVX: Intel Core 2nd generation (Sandy Bridge), AMD FX or
  newer. The LZCNT, MOVBE and BMI1 instructions some of these lack are handled
  in software.
- A graphics card and driver with Vulkan 1.3, VK_EXT_robustness2,
  VK_KHR_push_descriptor and VK_EXT_vertex_attribute_divisor
- Your own 13 Sentinels: Aegis Rim package, plus up to 5 GB of free disk space
  next to the program for the installed game (up to 1.3 GB more for an update)
- Nothing else: no Python or other tools are needed


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files").
2. Run 13_sentinels_aegis_rim.exe. The launcher opens.
3. On the Play page, click "Install game..." and select your base game .pkg
   file. The launcher checks the package and unpacks it into the "game" folder
   next to the program. The .pkg file is only read.
4. Optional: click "Install update..." and select an update .pkg of the same
   region. The base stays installed; the newest installed update starts.
   "Remove Update..." removes the installed updates and returns to the base.
5. Press PLAY.

To repair the game files, use "Reinstall game..." on the Play page. Your
settings and saves are kept. If an installation fails or is cancelled, the
previous one is left as it was.


PORT UPDATES
------------
The launcher checks the project's GitHub releases once per start. When a newer
release of the port exists, an update banner appears in the header; nothing is
installed until you choose UPDATE NOW and then RESTART TO UPDATE. Close the
game first. The update replaces only the port's own program files. Game files,
user data, saves, settings, trophies, logs and shader caches are kept. If the
update cannot be installed, the previous files are restored.


WINDOWS SMARTSCREEN
-------------------
The first time you run the program, Windows SmartScreen may show a warning,
because the program is not digitally signed. If you downloaded the release
from the project's GitHub releases page, choose "More info", then
"Run anyway".


PLAYING
-------
- F1 (or L3 + R3 on a controller) opens the Options menu.
- F11 switches between Windowed and Borderless Fullscreen. F2 shows the frame
  counter.
- Xbox controllers work without setup.
- The keyboard works too; the launcher's Controls page lists the keys.
- If the game closes right after PLAY while a recording, chat or performance
  overlay program is running, turn on Gameplay > Allow overlay programs in the
  launcher.
- To open the launcher again after turning it off, hold Shift while starting.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to 13_sentinels_aegis_rim.exe
- Logs:           the "logs" folder next to 13_sentinels_aegis_rim.exe
- Settings (tsr.ini), saves, trophy progress, the Trophy Key (keep it
                  private) and shader cache:
                  the "user" folder next to 13_sentinels_aegis_rim.exe

These folders are created when they are first needed and are not part of the
download. When updating the port, keep "user" (and "game", to skip
reinstalling).
If the game does not start or crashes, use the launcher's Bug Report page: it
sends a privacy-filtered report with one log only when you press Send report
and confirm, and can save the same report instead. You can also attach the
newest 13_sentinels_aegis_rim_*.log file from the "logs" folder to an issue at
https://github.com/saulob/13-sentinels-aegis-rim-pc/issues


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 or later (GPL-2.0-or-later). The full text is in
LICENSE.txt.

Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder, starting with THIRD-PARTY-LICENSES.txt.
The FFmpeg libraries included are GPL-3.0-or-later, so the package as a whole
is distributed under the GPL version 3.

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by Vanillaware, Atlus, SEGA,
Sony Interactive Entertainment or any other rights holder of 13 Sentinels:
Aegis Rim.
