# Controller icons: how `launcher/prompts/xbox-prompts-*.dcpx` are made

The game has two families of PlayStation button art:

- **The button atlas**, `HD_Interface_arranged000` (1024 × 1024 BC7) in `GUI/Interface.ftx`. Each button has a glossy white
  disc, a coloured disc, a flat white shape and outline, faint and glow variants that the prompt animations switch between;
  the `1` and `2` sprites build L1/R1/L2/R2 with the `L`/`R` letters, and START, SELECT, the PS logo and the touchpad have
  their own sprites. 1.00 and 1.01 read it from the base archives (`ROBO_US/FR/GE/IT/SP.CPK`), 1.02 from a rearranged
  copy in `ROBO_PATCH2_*.CPK`. The sprite rectangles are the UV quads of the build's own `Interface.mbs`
  (`draw_xbox.SPRITES` has one table per layout).
- **The built-in manual** (Help), `Manual/ManualNN.ftx` in each language's base archive: 1920 × 1080 page images with the
  face buttons (black circles) and the L1/L2/R1/R2/R3 pills drawn into the page. The USA updates do not change these pages.

Other regions:

| Patch | Archives | What the build reads |
| --- | --- | --- |
| `xbox-prompts-eu-1.00.dcpx` (EUR 1.00, 1.01) | `ROBO_US/FR/GE/IT/SP.CPK` | as USA 1.00; the French pages 05 to 08 differ |
| `xbox-prompts-jp-1.00.dcpx` | `ROBO.CPK` | base atlas, layout and manual |
| `xbox-prompts-jp-1.03.dcpx` | `ROBO.CPK`, `ROBO_PATCH.CPK` | base atlas with the `ROBO_PATCH2.CPK` layout; eleven pages from `ROBO_PATCH.CPK` |
| `xbox-prompts-jp-1.04.dcpx` | `ROBO.CPK`, `ROBO_PATCH.CPK` | the rearranged atlas and layout and eleven pages from `ROBO_PATCH.CPK` |
| `xbox-prompts-asia-1.00.dcpx` | `ROBO_CN.CPK`, `ROBO_TW.CPK` | Simplified Chinese reads `ROBO_CN.CPK`; Traditional lays `ROBO_TW.CPK` over it |

A patch lists what its build reads; the entries of the shared `ROBO.CPK` come out the same in every JPN patch. The JPN
and Chinese layouts also draw TOUCH and PAD words over the touch pad sprite: they are erased, as that sprite becomes VIEW.
Their manual pages are matched against the English templates too, also a pixel wider (`draw_manual.py --aligned`).

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
| `build_prompts.py` | Builds a region's patches from the original archive entries. |

## Rebuilding

Cut the originals out of the packages' archives (never out of an installed game that has been patched):

```
python scripts/prompts/cpk.py extract-matching ROBO_US.CPK "^(GUI/Interface\.(ftx|mbs)|Manual/)" orig/US --max-bytes 100000000   # and FR GE IT SP
python scripts/prompts/cpk.py extract-matching ROBO_PATCH2_US.CPK "^gui/interface\.(ftx|mbs)" orig/pUS                          # and FR GE IT SP
python scripts/prompts/build_prompts.py orig launcher/prompts --font Poppins-Bold.ttf --work work
python scripts/prompts/build_prompts.py orig-eu launcher/prompts --font Poppins-Bold.ttf --work work --region eu  # cut like orig
python scripts/prompts/build_prompts.py orig-jp launcher/prompts --font Poppins-Bold.ttf --work work --region jp --english orig/US/Manual
python scripts/prompts/build_prompts.py orig-asia launcher/prompts --font Poppins-Bold.ttf --work work --region asia --english orig/US/Manual
```

`orig-jp` holds `base` (`ROBO.CPK`), `p103` and `p103-2` (`ROBO_PATCH.CPK` and `ROBO_PATCH2.CPK` of 1.03) and `p104`
(`ROBO_PATCH.CPK` of 1.04); `orig-asia` holds `CN` and `TW`; each is cut with the same expression as above.
`xbox-prompts-1.00.dcpx` serves 1.00 and 1.01, `xbox-prompts-1.02.dcpx` serves 1.02. A DCPX3 patch lists, per archive file and
entry, the CRC32 of the original and of the result and the 16-byte runs between them. The launcher applies it to an archive
whole or not at all (`launcher/cpk_patch.h`), keeps an undo record beside the archive, and reverts byte for byte.

`tests/prompt_art_tests.py` (run by `tools\test.ps1 -Group Packages` with the real packages) decodes a real archive before
and after the patch and checks every sprite and manual icon. `--archives ROOT` (or `TSR_PROMPT_ARCHIVES`) checks every patch
against the archives unpacked from each package into `ROOT/usa100`, `usa102`, `eu100`, `jp100`, `jp103`, `jp104` and
`asia100`.
