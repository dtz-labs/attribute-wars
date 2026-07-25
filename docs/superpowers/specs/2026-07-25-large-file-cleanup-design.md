# Large-file cleanup: splitting `main.c` and `music_ay.asm`

**Date:** 2026-07-25
**Branch:** `refactor/split-large-files` (off `experiment/zx128-divmmc-pageout`)
**Checkpoint tag:** `pre-refactor-1.2.0`

## 1. Goal and hard constraint

Break the two oversized project-owned source files into focused modules, and
delete the dead `ZX128_NO_MUSIC` build variant.

**Hard constraint from the user: the game must not run slower than it did
before the refactor.** This is a gate, not a report. Any measured regression
stops the work rather than being explained away.

Nothing about gameplay, timing, or visual output changes. This is code motion
plus dead-code removal.

## 2. Starting state

| file | lines | verdict |
|---|---:|---|
| `src/pt3prom.asm` | 1378 | **vendored verbatim** from z88dk — out of scope, must stay byte-identical |
| `src/main.c` | 1159 | split |
| `src/music_ay.asm` | 590 | split |
| `src/enemy.c` | 467 | leave |

`main.c` currently holds nine unrelated responsibilities: background attribute
generation, the explosion-effect pool, the death animation, the spawn telegraph,
ROM-font text rendering, the title screen and its shine sweep, the GAME OVER
screen, the pre-shifted sprite tables, and the 50 Hz game loop itself.

## 3. Why this is safe at the instruction level

Before touching anything, the baseline `main.c` was compiled to sdcc assembly
(`zcc ... -a`) and inspected. Every function targeted for extraction is emitted
as its own label with a real `CALL` at each call site:

```
712:_border_tick:      744:_bg_attr:        970:_fx_render:
1198:_put_cell:        1241:_death_anim:    1850:_hud_score:

820:	call	_bg_attr
1119:	call	_bg_attr
1135:	call	_put_attr
```

**sdcc performs no inlining of these functions**, so moving them to another
translation unit cannot change the emitted instruction stream — a `CALL` to a
file-local label and a `CALL` to a `PUBLIC` one assemble identically.

`enemy_sprite_by_level` (the one moved symbol on the per-frame draw path) is
referenced by absolute-address arithmetic:

```
4301:	add	a, +((_enemy_sprite_by_level) & 0xFF)
4304:	adc	a, +((_enemy_sprite_by_level) / 256)
```

which is unaffected by the symbol's linkage. The same reasoning covers the asm
split: making `ay_sel`/`ay_dat`/`ay_rd` and `sel_write`/`sel_read` `PUBLIC`
changes linker scope only, producing the same relocations and the same T-states.

## 4. Module split — `main.c` (1159 → ~560)

| new file | contents | ~lines |
|---|---|---:|
| `sprite_art.{c,h}` | pre-shift tables (`ps_*` incl. per-platform scratch variants), `PS_SHIP_DIR`, `enemy_sprite_by_level`, `ENEMY_SPRITE`, new `sprite_art_init()` | 90 |
| `background.{c,h}` | `BG_*`, `bg_pattern`, `bg_attr`, `bg_paint`, `bg_next_pattern()`, plus ULA border: `BORDER_BLACK/RED`, `border_flash_red()`, `border_tick()` | 100 |
| `fx.{c,h}` | explosion pool (`fx_clear/spawn/render`), `death_anim` + `put_cell`, spawn telegraph (`telegraph_blink/clear`, `TELEGRAPH_FRAMES`) | 200 |
| `text.{c,h}` | `put_char`, `put_text`, `put_text_both`, `put_score_digits`, `put_u8` (ROM font at `0x3C00`) | 80 |
| `title.{c,h}` | `title_screen`, `title_base_attrs`, `title_attr_row`, shine sweep + tables, `APP_VERSION_STR`, `CTRL_DUAL_LABEL` | 180 |
| `gameover.{c,h}` | `game_over_flash`, `game_over_screen`, named results `GAME_OVER_RESUME/NEW_GAME/MENU` replacing bare `0/1/2` | 80 |

`hud_score` moves from `main.c` into `hud.c` as `hud_draw_score()`. It is the
only cached HUD widget living outside `hud.c`; `hud.h` even documents the wart
("the SCORE is drawn as text by main.c"). After the move `hud.c` owns every
cached widget and consumes `text.h`.

`main.c` keeps the 50 Hz loop, `main()`, `wave_time_frames`, `step_sign`,
`enemies_alive_count`, `cell_t`/`prev[2][MAX_DRAW]`, and the incremental
erase+redraw. **The render hot path is not restructured.**

### Honest size expectation

`main()` alone is 440 lines, so `main.c` lands at **~560 lines**, not the ~380
sketched during brainstorming. That is under the 600-line threshold but the
larger number is the one being committed to. Getting materially below it would
require decomposing `main()` itself — the "aggressive" option that was not
chosen.

### Approved extra step

The wave-(re)init sequence appears **three times** in `main()`: new game
(l. 786-813), wave cleared (l. 988-997), respawn after death (l. 1033-1054).
The copies have already drifted — the respawn path calls `fx_clear()` and
`hud_invalidate()`, the new-game path does not. Factoring this into one local
helper removes a live copy-paste hazard and brings `main.c` to ~530.

## 5. Module split — `music_ay.asm` (590 → 4 files)

| new file | contents | ~lines |
|---|---|---:|
| `ay_ports.asm` | `ay_sel/ay_dat/ay_rd`, `sel_write`, `sel_read`, `ay_probe`, `scld_present_p`, `sig_timex`, `_ay_detect`, `_ay_set_ports_std`, `_ay_default_sound`, `_ay_machine_status` | 200 |
| `ay_sfx.asm` | `_ay_sfx_out`, `_ay_sfx_mute` (channel-C FX-only voice) | 60 |
| `pt3_glue.asm` | `asm_vt_hardware_out[_A0]`, `vho_out`, `sfx_merge`, `_pt3_init`, `_pt3_play_safe`, `_pt3_mute` | 150 |
| `music_im2.asm` | `IM2_TABLE/FILL/VEC` (per-target), `_music_im2_init` (incl. the DivMMC page-out), `_music_im1_init`, `isr_main` | 120 |

All four stay in `SECTION code_user` and keep the project's asm conventions
(never touch `IY` except to save/restore; only AY ports written, never
`0xFF`/`0xFE`). `pt3prom.asm` is not touched.

The DivMMC/PicoDIV page-out from commit `adfd352` lives inside
`_music_im2_init` and moves with it into `music_im2.asm` — which is why this
branch is cut from the experiment branch rather than from `main`.

## 6. Dead code removal — `ZX128_NO_MUSIC`

12 conditional blocks in `music.c` and 3 in `music_ay.asm` guard a build variant
`CLAUDE.md` records as gone ("the old `ZX128_NO_MUSIC` memory-reduced variants
are gone"). No Makefile target defines it. The music-present branch is kept
everywhere; `music.c` drops 246 → ~195 lines and `music_init`/`music_tick`/
`music_stop` lose their doubled logic.

`docs/architecture.md` is stale in the same way — it claims the ZX128 TAP "is
deliberately built with `ZX128_NO_MUSIC`" and is "beeper-only until the AY
player and tune are moved into a bank-safe layout", which the shipped 128K build
contradicts. Its Code Layout, ZX128 and Sound sections are corrected.

## 7. Verification protocol

Baseline captured on `pre-refactor-1.2.0`, re-run after every step:

1. **Host tests** — `./test/run.sh`. Baseline: all pass (geometry 60, input 60,
   player 26, bullet 17, score 15, plus scld/collision/rng/enemy).
2. **Builds** — `make timex zx128 zx48` plus `check_zx128_layout.py` and
   `check_zx_stack_layout.py`.
3. **Size** — `wc -c build/*_CODE.bin`. Baseline: timex 31 608, zx128 19 373,
   zx48 32 057. A regression over ~200 B stops the work.
4. **T-states** — `make measure` + `z88dk-ticks`. Baseline (reproduces
   `docs/perf-budget.md` exactly):

   | segment | T/frame |
   |---|---:|
   | enemies_update | 7 020.7 |
   | collide | 3 194.9 |
   | player_hit | 511.8 |
   | render | 23 170.0 |
   | PT3 tick | 6 223.8 |

5. **Per-function instruction hashes** — `scratchpad/fnhash.py` extracts each
   function body from sdcc asm output, strips comments/directives, and hashes
   the instruction stream. Baseline recorded for all 66 symbols in `main.c`
   (e.g. `_bg_attr` 58 instructions, `_fx_render` 191, `_death_anim` 224,
   `_main` 1531). After the split the moved functions must hash **identically**.

Gate 5 is what actually enforces the user's constraint for `main.c`, because the
`measure_main.c` harness does not link `main.c` — its `render` segment measures
the harness's own copy of the draw loop. Gate 4 covers the asm and music changes,
where `PT3 tick` exercises `asm_vt_hardware_out`, `sfx_merge` and
`pt3_play_safe`.

## 8. Commit sequence

One commit per step so any of them can be reverted alone:

1. `sprite_art` 2. `background` 3. `fx` 4. `text` 5. `title` 6. `gameover`
7. `hud_draw_score` 8. wave-init helper 9. asm split 10. `ZX128_NO_MUSIC`
removal 11. docs

## 9. Out of scope

- `pt3prom.asm` (vendored)
- `enemy.c` / `enemy_update.asm` and their byte-identical C/asm twin contract
- the render kernel, `blit.asm`, `collide.asm`
- any gameplay, balance, or visual change
