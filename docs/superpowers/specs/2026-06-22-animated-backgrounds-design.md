# Animated & varied backgrounds — design

**Date:** 2026-06-22
**Status:** approved (design); implementation pending
**Target:** Timex TC2048, z88dk (sdcc_iy), C + Z80 asm

## 1. Goal

Add visual life to the backgrounds without compromising the project's first
priority — SMOOTH and FAST 50 Hz gameplay. Three independent pieces:

- **Menu / title:** a rotating 3D vector globe behind the title + menu.
- **Game-over screen:** a flowing attribute plasma.
- **In-game arena:** a *static* attribute "shape" chosen per run (replacing the
  one fixed blue/black checker), plus an optional tier of busier shapes that
  re-roll every wave.

The in-game arena is the one place with no spare frame budget, so its effect is
deliberately **static** — painted once, then untouched — costing **zero
T-states per frame**. The menu and game-over screens run otherwise-idle loops,
so they can afford genuine per-frame animation.

## 2. Context, constraints & measured costs

### Hardware boundary (unchanged)
Only `scld.c` knows port `0xFF` and the screen addresses. Backgrounds are the
attribute plane: screen A attrs `0x5800`, screen B attrs `0x7800`, 768 bytes
each (32×24 cells). Double-buffering flips both bitmap *and* attributes, so any
colour shown on both pages must be written to both blocks.

### The sprite-readability rule (the key in-game constraint)
Sprites (ship, enemies) are **white-ink bitmap**. A cell's ink colour applies to
every set pixel in that 8×8 cell. Therefore an **in-game** background may only
vary **`paper`** (and must keep `bright` consistent); it must keep **`ink = 7`
(white)** and must never use **white paper** (white-on-white = invisible
sprite). This is why plasma (which recolours ink) is fine for the game-over
screen but wrong for the arena.

The arena **frame ring** (row 0, row 23, col 0, col 31) carries the HUD and must
stay `ATTR(1,3,7)` (bright magenta paper, white ink). Background patterns only
touch the **interior** (rows 1–22, cols 1–30). What is actually live on the ring
today: lives + shields (top, bitmap), the score as plain text on row 23 cols
1–6, and a one-cell dash-ready dot (row 0 col 15). The `hud_draw_timer` bar is
defined but **not currently wired** (dead code), and there is no boost bar — so
"timer/dash bars" should not be assumed present. The interior is **100% free**
of HUD/score in the current code (verified).

### `bg_attr` is the single source of truth
`fx_render` (enemy-death pops) and the spawn telegraph restore cells by calling
`bg_attr(row,col)` — confirmed at `fx_render` (`main.c:214`), `telegraph_blink`
(`main.c:314`), `telegraph_clear` (`main.c:324`). If the chosen pattern flows
through `bg_attr`, every restore path follows it automatically with no further
change.

### Stale-comment landmine (delete during integration)
A reverted feature ("score as big attribute-cell digits behind the action",
`score_cell_attr`, `hud_paint_background`) left **misleading comments** in
`main.c` (header block lines ~18–28, the `#include "hud.h"` comment at
`main.c:42`, and `main.c:598`). None of those symbols exist anymore (the
big-digit code was added in `fc252ef` and reverted in `fc371b1`). The
implementer must **delete/fix these stale comments** while wiring `bg_cells`,
and must not believe the interior is occupied by score digits — it is not.

### Measured paint cost (z88dk-ticks, throwaway harness, 100 iters ÷ 100)

These numbers were measured once with a throwaway harness (since removed); they
are recorded here so the architecture decision below stands on its own.

| Method | T-states | Fraction of a 69,888 T frame |
|---|---|---|
| One attribute block (768 B) via `LDIR` | ~16,200 | 0.23 |
| Both buffers (1,536 B) block-copy | ~32,400 | 0.46 |
| Compute checker cell-by-cell in C → both | ~201,000 | 2.9 |

**Conclusion that drives the architecture:** *copying* a precomputed table is
~6× cheaper than *computing* per cell. So we generate a pattern once into a RAM
table and then block-copy it. A full both-buffers repaint is under half a frame.

### Frame budget reminder
Frame = 69,888 T @ 50 Hz, ~55k usable. Heavy-combat steady state already spends
~35k (render 23.4k + enemies 4.9k + collide 5.9k + hit 0.9k); rich SFX erode the
rest. This is why the in-game effect must be static (zero per-frame cost).

## 3. Architecture

Two new pure-logic modules (host-testable, integer-only, no hardware) plus
target-only integration in `main.c`.

- **`fxtab`** (`include/fxtab.h`, `src/fxtab.c`) — a 256-entry signed sine table
  (amplitude ±127) and an 8-bit fixed-point multiply helper
  (`s16 fx_mul(s8 a, u8 b)` style, `(a*b)>>shift`). Shared by the plasma field
  and the globe rotation.
- **`bgpat`** (`include/bgpat.h`, `src/bgpat.c`) — fills a 768-byte attribute
  table for a given pattern id + seed.

**Refactor:** move the `ATTR(bright,paper,ink)` macro from `hud.h` to `types.h`
(its natural home) so the pure modules and host tests use it without depending
on the HUD. `hud.h` includes it from `types.h` for source compatibility.

Layering follows the existing split in CLAUDE.md: `fxtab` and `bgpat` join the
pure-logic, host-tested tier; the per-screen draw/animation lives in `main.c`
(target-only).

## 4. Component A — in-game backgrounds (Phase 1)

> **Amendment (2026-06-22, as built):** the noisy tier (diamonds/vbands/plasma/
> starfield) and the per-wave re-roll were **dropped** at the user's request.
> Shipped behaviour: a new game picks **one of four static shapes** (checker,
> diagonal, circles, lattice) at random (no immediate repeat) and keeps it for
> the whole run — respawns and waves never change it. `bgpat_generate` takes no
> `seed`; `fxtab` is deferred to Phase 2 (where the plasma uses it). The sections
> below describe the original richer design; treat the amendment as authoritative.

### Data model
- `static u8 bg_cells[768]` in `main.c` — the active background (frame ring +
  interior).
- `bg_attr(r,c)` becomes `return bg_cells[(u16)r*32 + c];` (cheap lookup).
- `bg_paint()` becomes `memcpy((u8*)SCLD_ATTRS_A, bg_cells, SCLD_ATTRS_LEN);
  memcpy((u8*)SCLD_ATTRS_B, bg_cells, SCLD_ATTRS_LEN);` (~32k T;
  `SCLD_ATTRS_LEN == 768`). Called at game start and after every death — this
  *replaces* today's ~3-frame cell-by-cell recompute, so respawns get faster.
  (Confirm z88dk's `memcpy` lowers to `LDIR`, not a byte loop; `<string.h>` is
  already included in `main.c`.)

### `bgpat` API
```c
/* pattern ids */
enum {
    BGPAT_CHECKER, BGPAT_DIAGONAL, BGPAT_CIRCLES, BGPAT_LATTICE,   /* low-noise  */
    BGPAT_DIAMONDS, BGPAT_VBANDS, BGPAT_PLASMA, BGPAT_STARFIELD,   /* noisy      */
    BGPAT_COUNT
};
#define BGPAT_LOWNOISE_COUNT 4u   /* ids [0..3] */
#define BGPAT_NOISY_COUNT    4u   /* ids [4..7] */

/* Fill cells[768]: frame ring = ATTR(1,3,7); interior = pattern `id`, ink=7,
 * paper from the dark safe palette. `seed` varies intra-pattern detail
 * (plasma phase, star layout). Deterministic for a given (id, seed). */
void bgpat_generate(u8 *cells, u8 id, u16 seed);
```

### Generators (all integer, no FP)
- **checker** — `(r+c)&1` → blue / black.
- **diagonal** — `((r+c)>>1)&1` → blue / black bands.
- **circles** — bands of squared distance from arena centre (compare `dr*dr +
  dc*dc` against precomputed ring thresholds) → blue / black.
- **lattice** — `(r%3==0 || c%3==0)` blue lines on black ("tron floor").
- **diamonds** — Manhattan distance `|dr|+|dc|` bands, accent every 4th ring
  magenta.
- **vbands** — `(c/2)%3` → {black, blue, magenta} columns.
- **plasma (frozen)** — one frozen frame of the `fxtab` field (see §5), quantised
  to a small safe-paper palette.
- **starfield** — deterministic hash of `(r,c,seed)`; sparse bright cells
  (cyan/blue/magenta) on black.

Low-noise set is restricted to **black + blue** (dark-blue mono look, per user
preference). Noisy set may use the wider safe palette.

**Safe paper palette:** `{black(0), blue(1), red(2), magenta(3), green(4)}` at
bright 0, plus selected bright accents for starfield — **never white(7)**.

### Selection state machine (in `main.c`, tunable constants)
A `bg_new_run()` helper rolls the tier and generates the first `bg_cells`:
- `BG_MODE_STATIC` → pick one low-noise id (avoid immediate repeat), generate
  once, keep for the whole run.
- `BG_MODE_WAVE` → set noisy mode and immediately pick + generate the first
  noisy shape (so the opening wave already shows it).
- Odds tunable via `BG_NOISY_PERCENT` (default ~50).

**Where it is called (verified line numbers, HEAD `3a216ec` — re-confirm before
editing):**
- **Startup:** call `bg_new_run()` before the first `bg_paint()` (currently
  `main.c:639`).
- **Leaving the game-over screen:** the game-over branch (`main.c:789–793`) has
  two outcomes — `game_resume_from_wave` (`:790`) and `game_new` (`:792`). Treat
  **both** as a fresh start: call `bg_new_run()` in that branch, **before** the
  shared cleanup block's `bg_paint()` at `main.c:799`. (Decision: resume-from-
  wave re-rolls the background, like a new game — the player perceives both as
  "the game starting again".)
- **In-run respawn** (lives remaining, the `else` at `main.c:794–796`): do
  **not** re-roll. The shared `bg_paint()` at `:799` repaints the existing
  `bg_cells` unchanged.
- **Wave advance** (`main.c:757–765`, where `g.wave++` at `:759` precedes
  `enemies_spawn` at `:761`): **there is no `bg_paint()` here today** — this is
  net-new wiring. In `BG_MODE_WAVE`, pick a noisy id (avoid immediate repeat),
  regenerate `bg_cells`, then `bg_paint()`. In `BG_MODE_STATIC`, do nothing.
  Regeneration (~1–2 frames) runs on the same frame as `enemies_spawn`, before
  the `TELEGRAPH_FRAMES` (=80) telegraph during which enemies are inert, so the
  hitch is hidden.

### Tests (`test/test_bgpat.c`)
- Readability invariant: for every interior cell of every pattern (across a
  range of seeds) `ink==7`, `paper != 7`, `paper` in safe set.
- Frame ring == `ATTR(1,3,7)` for every pattern.
- Determinism: `bgpat_generate` twice with same `(id,seed)` → identical buffer.
- Coverage: all 768 cells written (no uninitialised cells).

### Per-frame cost in gameplay: **zero** (static; nothing runs in the loop).

## 5. Component B — game-over plasma (Phase 2)

`game_over_screen` already draws white-ink text, sets attributes, and idles
waiting for input. Add per-frame animation:

- Advance a `phase` counter each frame.
- For each cell compute an `fxtab` plasma field:
  `v = sin[(c*kx + phase) & 255] + sin[(r*ky + phase2) & 255] + sin[((r+c)*kd +
  phase3) & 255]`, map `v` to a paper colour (palette cycling).
- Write the attribute block(s). Text stays white-ink → readable over any paper
  (white paper excluded). No sprites here, so the **full** paper palette and
  `bright` may be used.

Reuses the same field function as the frozen-plasma pattern (§4). Loop is
otherwise idle; the per-frame cost (compute ~30k + copy) fits a frame, but
**measure** and drop to 25 Hz (animate every other frame) if needed — plasma at
25 Hz looks fine.

## 6. Component C — title rotating globe (Phase 3, largest)

### Layout reflow
Title screen becomes: **title text (row 3) · rotating globe (centre band) ·
control menu + copyright (bottom)**. The globe occupies an exclusive middle band
so it never overlaps text. (Optional: mock the layout in the visual companion
before building.)

### Math (no FP)
- Precompute unit-sphere points (lat/long grid), ~120–180 points, as fixed-point
  coordinates. For each point precompute its xz-plane radius scaled by display
  radius (`r_xz·R`), its base-angle index (0–255), and its constant screen-y.
- Rotation is about the Y axis only. Per frame, per point: screen-x needs **one**
  `fx_mul` with `fx_sin`/cos indexed by `(base_angle + theta) & 255`; back-face
  cull is just the sign of the table entry (free).
- Plot front points brighter; cull or dim back points.

### Animation & double-buffering
- The title currently uses screen A only (no flip). For smooth animation,
  double-buffer it: paint static text/menu/shine into **both** buffers once,
  then each frame draw the globe into the back buffer and `scld_present()`.
- **Incremental erase:** store the pixel addresses plotted last frame and clear
  just those next frame (the same technique the game loop uses), not the whole
  bounding box.
- Menu highlight + shine-sweep attribute writes must target both attribute
  blocks once they animate under a flipping title.

### Cost
This is the one piece whose per-frame cost needs its own `z88dk-ticks` check
during implementation. Target 50 Hz; fall back to 25 Hz (every other frame) if
the point budget can't hold it — fine for a slow spin.

## 7. Build phasing

1. **Phase 1** — `fxtab` + `bgpat` + in-game integration + host tests.
   Cheapest, highest value, fully host-testable. Ships the per-run / per-wave
   arena backgrounds.
2. **Phase 2** — game-over plasma (reuses `fxtab` plasma field).
3. **Phase 3** — title globe (title double-buffer rework + fixed-point 3D).

Each phase is independently shippable and verified in ZEsarUX.

**Mechanical build wiring (don't forget):** register new sources `src/fxtab.c`
and `src/bgpat.c` in `build.sh` (the `zcc … src/*.c` list, ~`build.sh:24-27`),
and add `test/test_fxtab.c` + `test/test_bgpat.c` to `test/run.sh` (mirror the
existing per-test compile lines). The two new modules are pure-logic and link
cleanly host-side; `main.c` stays target-only (not host-built).

## 8. Tooling

- Paint-cost numbers (§2) were taken with a one-off harness, now removed. For
  future perf work, mirror the `measure_main.c` + `measure.sh` + `z88dk-ticks`
  pattern (border-OUT markers, addresses from the `.map`).
- Add a globe per-frame measurement in Phase 3.
- Per CLAUDE.md: always `z88dk-ticks`-measure before claiming a perf change.

## 9. Tunable parameters (collected)

- `BG_NOISY_PERCENT` — odds a run uses the per-wave noisy tier (default ~50).
- Low-noise vs noisy tier membership (`bgpat` ids).
- Safe paper palette set.
- Globe point count and 50 Hz/25 Hz rate.
- Plasma 50 Hz/25 Hz rate and palette.

## 10. Out of scope (YAGNI)

- Animated in-game backgrounds (rejected: no per-frame budget; static is the
  point).
- Per-life background changes (respawn keeps the run's pattern).
- Bitmap-textured planet / parallax starfield in-game (too heavy; the menu globe
  covers the "rotating ball / 3D" desire).
- New colours beyond the Spectrum 8×2 palette.
