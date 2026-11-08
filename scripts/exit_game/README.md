# Exit Game title row: how `assets/exit_game/title-labels.bin` is made

The title menu's rows are baked art in `GUI/title.ftx` (texture `4K_title_arranged043`), laid out by `GUI/title.mbs`.
The title already has a hidden third row (EXTRA, shown by a save flag, inert) and three-row layout parts
(`NEW_GAME_P00` … `EXTRA_GAME_P01`) that move the menu up to make room. In the French, German, Italian and Spanish
layouts those parts still show English placeholder rows and an empty third row.

`build_exit_game.py` makes, per text language, the third row's label from the title's own letters: bold rows are white
plates with the letters cut out, thin rows are white strokes; the letters are cut from the rows of all five USA atlases
(one face and size), spaced with sidebearings fitted to those rows. No atlas has a J, so J is the U without its left
stem above the bowl; French takes its Q from the Spanish rows. The label goes into atlas space no sprite samples, and the
layout edits point the third row at it (and, where needed, the placeholder rows back at the language's own rows).

The Japanese (CUSA10602) and Asian (CUSA18093) titles show the English rows in the same face, but each atlas is arranged
differently, so EXIT GAME is built into each of them, from its own letters first and the USA pool for the rest. The
Japanese updates replace `title.mbs` (one layout for 1.03 and 1.04) but not `title.ftx`: its label is drawn once, in
space neither layout samples, with edits for each layout. The game picks the layout whose original records are in place.
Simplified (`ROBO_CN.CPK`) and Traditional (`ROBO_TW.CPK`) Chinese have the same rows, so their atlases are told apart
by another sprite's blocks.

When a row is selected its plate writes on: white strokes drawn under a black plate show through the letter holes. The
native third-row strokes spell EXTRA, so they are replaced: each frame lights every letter of the label from the top by
the thirds that the second row's letter at the same place lights in that frame, and the plate is drawn last. Keyframes
and timing are the game's own.

| Language | Key | Label |
| --- | --- | --- |
| English | `english` | EXIT GAME |
| French | `french` | QUITTER LE JEU |
| German | `german` | SPIEL BEENDEN |
| Italian | `italian` | ESCI DAL GIOCO |
| Spanish | `spanish` | SALIR DEL JUEGO |
| Japanese (base and update layouts) | `japanese` | EXIT GAME |
| Simplified Chinese | `schinese` | EXIT GAME |
| Traditional Chinese | `tchinese` | EXIT GAME |

The game applies the result in memory only (`src/exit_game.c`): every atlas block and layout record it replaces is
checked against the original first. Development tool only (Python 3.11+, `numpy`, `scipy`, `Pillow`); the build uses the
committed asset. Every folder below holds the game's unmodified archives:

```
python scripts/exit_game/build_exit_game.py <USA 1.00> --patch2 <USA 1.02> --japan <JP 1.00> ^
    --japan-update <JP 1.03> --japan-update <JP 1.04> --asia <Asia 1.00> --preview work\exit-game
```

`--preview` writes each language's (and layout's) three-row states, its labels beside the rows they imitate, and every
write-on keyframe of the second row and of the third row (selected and confirmed).
`tests/exit_game_layout_tests.py` (run by `tools\test.ps1 -Group Features` for USA) checks the asset against the real
files; with `--japan`, `--japan-update` and `--asia` (or `TSR_EXIT_GAME_JAPAN`, `TSR_EXIT_GAME_JAPAN_UPDATES`,
`TSR_EXIT_GAME_ASIA`) it checks those regions too.
