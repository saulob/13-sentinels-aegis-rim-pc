# Controller icons: how `launcher/prompts/xbox-prompts-*.dcpx` are made

The game has two families of PlayStation button art:

- **The button atlas**, `HD_Interface_arranged000` (1024 × 1024 BC7) in `GUI/Interface.ftx`. Each button has a glossy white
  disc, a coloured disc, a flat white shape and outline, faint and glow variants that the prompt animations switch between;
  the `1` and `2` sprites build L1/R1/L2/R2 with the `L`/`R` letters, and START, SELECT, the PS logo and the touchpad have
  their own sprites. 1.00 and 1.01 read it from the base archives (`ROBO_US/FR/GE/IT/SP.CPK`), 1.02 from a rearranged
  copy in `ROBO_PATCH2_*.CPK`. The sprite rectangles are the UV quads of the version's own `Interface.mbs`.
- **The built-in manual** (Help), `Manual/ManualNN.ftx` in each language's base archive: 1920 × 1080 page images with the
  face buttons (black circles) and the L1/L2/R1/R2/R3 pills drawn into the page. The updates do not change these pages.

No other texture carries these discs or plates. Left alone on purpose: the directional pad, the sticks, the shoulder plates
(the `1`/`2` labels turn them into LB/RB/LT/RT), the manual's DualShock drawing and its OPTIONS and touch pad wording.

These scripts are development tools (Python 3.11+, `numpy`, `scipy`, `Pillow`; `tools\setup.ps1 -InstallPython`); players
never run them and the build does not need them: the patches are committed.

| Script | What it does |
| --- | --- |
| `cpk.py` | CRI CPK reader: `list`, `header`, `extract`, `extract-matching`. |
| `ftex.py` | Vanillaware FTEX packs: `list` the textures, `dump` them as PNG (GNF, BC7, Thin_1dThin tiling). |
| `draw_xbox.py` | Repaints the atlas sprites (letters in Poppins Bold, SIL OFL) and writes the mask of changed pixels. |
| `draw_manual.py`, `make_manual_templates.py` | Finds the face-button circles and the L/R pills on a manual page by matching them against icons cut from the English pages 06 and 07, and repaints them. Nothing below a close match is touched. |
| `patch_ftx.py`, `bc7.py` | Re-encodes only the changed 4 × 4 blocks (BC7 mode 6) into the pack. |
| `make_dcpx.py` | Writes the DCPX3 patch. |
| `build_prompts.py` | Builds both patches from the original archive entries. |

## Rebuilding

Cut the originals out of the packages' archives (never out of an installed game that has been patched):

```
python scripts/prompts/cpk.py extract-matching ROBO_US.CPK "^(GUI/Interface\.(ftx|mbs)|Manual/)" orig/US --max-bytes 100000000   # and FR GE IT SP
python scripts/prompts/cpk.py extract-matching ROBO_PATCH2_US.CPK "^gui/interface\.(ftx|mbs)" orig/pUS                          # and FR GE IT SP
python scripts/prompts/build_prompts.py orig launcher/prompts --font Poppins-Bold.ttf --work work
```

`xbox-prompts-1.00.dcpx` serves 1.00 and 1.01, `xbox-prompts-1.02.dcpx` serves 1.02. A DCPX3 patch lists, per archive file and
entry, the CRC32 of the original and of the result and the 16-byte runs between them. The launcher applies it to an archive
whole or not at all (`launcher/cpk_patch.h`), keeps an undo record beside the archive, and reverts byte for byte.

`tests/prompt_art_tests.py` (run by `tools\test.ps1 -Group Packages` with the real packages) decodes a real archive before
and after the patch and checks every sprite and manual icon.
