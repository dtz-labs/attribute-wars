# Animated & varied backgrounds — design

**Date:** 2026-06-22
**Status:** approved (design); implementation pending
**Target:** Timex TC2048, z88dk (sdcc_iy), C + Z80 asm

## 1. Goal

Add visual life to the backgrounds without compromising the project's first
priority — SMOOTH and FAST 50 Hz gameplay. Three independent pieces:

- **Menu / title:** a rotating 3D "planet" globe behind the title + menu
  (standard double-buffer + 8×8 colour bands).
- **Game-over screen:** a flowing plasma in Timex hi-color 8×1, with an
  automatic standard-8×8 fallback for 128K/48K.
- **In-game arena:** a *static* attribute "shape" chosen at random per run
  (checker/diagonal/circles/lattice), replacing the one fixed blue/black checker.

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

Pure-logic modules (host-testable, integer-only, no hardware) plus target-only
integration at the per-screen draw sites and the `scld.c` hardware boundary.

- **`bgpat`** (`include/bgpat.h`, `src/bgpat.c`) — *Phase 1, done.* Fills a
  768-byte attribute table for a given pattern id (no seed; deterministic).
- **`fxtab`** (`include/fxtab.h`, `src/fxtab.c`) — *Phase 2.* A 256-entry signed
  sine table (±127, baked) and `s16 fx_mul(s8 s, u8 mag) = (s*mag)>>7`. Shared by
  the plasma field (§5) and the globe rotation (§6).
- **plasma field** (`plasma_attr`, Phase 2) and **globe projection** (Phase 3) —
  pure helpers, host-tested, hardware-agnostic.

**Hardware boundary:** `scld.c` remains the only owner of port `0xFF` and the
screen addresses. Phase 2 extends it with `scld_hicolor_on/off` (bit 1 only).
Per-screen rendering (arena `bg_paint`, `game_over_screen` plasma, `title_screen`
globe) lives target-side and draws through the abstracted buffer interface.

**Refactor (done, Phase 1):** the `ATTR(bright,paper,ink)` macro moved from
`hud.h` to `types.h` so the pure modules and host tests use it without depending
on the HUD; `hud.h` includes it from `types.h`.

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

> **Amendment (2026-06-23, as built):** the hi-color 8×1 approach below was tried
> and rejected by the user (text hard to read over bright colour, motion slow,
> pattern scrolled upward). **Shipped instead:** a **standard 8×8** plasma — a
> *static* interference field whose colours are **cycled by phase** (palette
> rotation → shimmers in place, no scroll), in a **dark non-bright palette** so
> the white text always reads. 768 cells written into both attribute blocks each
> frame → smooth at 50 Hz, and plain 8×8 attrs are portable to Timex/128K/48K
> with no special mode. The Timex **hi-color extension was removed** (unused).
> `plasma_field(x,y)` (static) + `plasma_palette(v,phase)` (dark, phase-rotated)
> are the pure, host-tested API. Treat this amendment as authoritative; the
> hi-color text below is kept for history.

A flowing plasma behind the GAME OVER text, rendered in **Timex hi-color 8×1**
for smooth per-scanline colour, with an automatic **standard-8×8 fallback** so
the same binary looks right on 128K/48K too (see §6.5 portability).

### `fxtab` (returns in this phase)
`include/fxtab.h` / `src/fxtab.c`: `extern const s8 fx_sin[256]` (one period,
±127, baked table — no runtime trig) + `s16 fx_mul(s8 s, u8 mag)` = `(s*mag)>>7`.
Host-tested (`test/test_fxtab.c`). Shared with the globe (§6). (This is the
module removed from Phase 1; reintroduce it here with its real consumers.)

### `scld.c` hi-color extension (the hardware boundary owns the mode)
- `void scld_hicolor_on(void)` — `OUT (0xFF), 0x02` (bit 1 ONLY; bits 6–7 stay 0,
  so the interrupt kill-switch is never touched). Now the display is bitmap
  `0x4000` + an **8×1 attribute map at `0x6000`** (6144 bytes, one attr per
  char-column per scanline).
- `void scld_hicolor_off(void)` — restore the standard double-buffer page
  (`OUT (0xFF), scld_front`).
- The 8×1 attr map reuses the existing interleaved addressing: the attribute for
  column `x`, scanline `y` is at `scld_scanline(0x6000, y)[x]` — no new address
  math. (Smoke-test this in ZEsarUX as the FIRST implementation step: fill
  `0x6000` with a vertical colour gradient under hi-color and eyeball it, to
  de-risk the mode before building the plasma on it.)

### Plasma field (pure, host-tested)
`u8 plasma_attr(u8 x, u8 y, u8 phase)` → an attribute byte. Field:
`v = fx_sin[(x*KX + phase)&255] + fx_sin[(y*KY + phase)&255] +
     fx_sin[((x+y)*KD + phase2)&255]`, mapped to a full-rainbow palette
(paper cycles through the colours; `bright` may vary). **Ink stays white (7)** so
the GAME OVER / SCORE text reads over any paper; **white paper is excluded**.
Deterministic for `(x,y,phase)`.

### Dual-plane render + auto-degradation (no hardware detection)
One per-frame loop writes BOTH attribute planes from the same field:
- **8×1 hi-color** at `0x6000` — the field per scanline (the smooth version).
- **8×8 standard** at `0x5800` — emitted on every 8th scanline (one cell row),
  piggybacked on the same loop, so it is nearly free.

Then `scld_hicolor_on()`. On a **Timex** the SCLD displays the 8×1 plane
(smooth); on a **128K/48K** `OUT (0xFF)` is ignored, so the ULA keeps showing the
standard 8×8 plane (chunky). Same binary, correct look on every machine, **zero
detection, no dead code** — each plane is the live one on its hardware.

### Flow
`game_over_screen`: clear + draw text into the `0x4000` bitmap once →
`scld_hicolor_on()` → idle input loop, each frame advance `phase` and write both
planes, HALT-paced → on FIRE/Q, `scld_hicolor_off()` and return; the caller
resumes standard mode (`scld_clear` + `bg_paint`). 6144 cells is 8× the work —
**measure**; run at 25 Hz (every other frame) if 50 Hz doesn't fit.

### Tests
- `plasma_attr` (host): output range/validity, determinism, and the text-safety
  invariant (ink stays 7, paper never 7) across a sweep of `(x,y,phase)`.
- The two renderers + hi-color mode are target-only → ZEsarUX visual verify.

## 6. Component C — title rotating globe (Phase 3, largest)

A rotating 3D "planet": a white dot/wireframe sphere spun in **standard
double-buffer** mode (flawless motion) over **8×8 attribute colour bands**
(latitude gradient) that make it read as a shaded planet.

### Layout reflow
Title screen becomes: **title text (row 3) · rotating globe (centre band) ·
control menu + copyright (bottom)**. The globe occupies an exclusive middle band
so it never overlaps text.

### Colour bands (8×8, standard attrs)
Horizontal latitude bands over the globe's bounding cells (e.g. top→bottom
white → cyan → blue → magenta → blue → cyan → white), written into **both**
attribute blocks once (so the page-flip never disturbs colour). Dots are white
ink → they show white over the band paper; the bands give the planet its shading.

### Math (no FP, host-tested)
- Precompute unit-sphere points (lat/long grid), ~120–180 points, as fixed-point
  coordinates. Per point precompute its xz-plane radius scaled by display radius
  (`r_xz·R`), its base-angle index (0–255), and its constant screen-y.
- Rotation about the Y axis only. Per frame, per point: screen-x needs **one**
  `fx_mul` with `fx_sin`/cos indexed by `(base_angle + theta)&255`; back-face cull
  is just the sign of the table entry (free).

### Animation & double-buffering
- The title currently uses screen A only (no flip). Double-buffer it: paint
  static text/menu/shine into **both** buffers once, then each frame draw the
  globe into the back buffer and `scld_present()`. Because the globe draws via the
  abstracted `scld_back()/scld_present()`, it rides a future 128K shadow-screen
  kernel unchanged (see §6.5).
- **Incremental erase:** store the pixel addresses plotted last frame and clear
  just those next frame (the game loop's technique), not the whole bounding box.
- Menu highlight + shine-sweep attribute writes must target **both** attribute
  blocks once they animate under a flipping title.

### Cost
Needs its own `z88dk-ticks` check during implementation. Target 50 Hz; fall back
to 25 Hz (every other frame) if the point budget can't hold it — fine for a slow
spin.

### Tests
- Globe projection (host): rotation determinism, projected points stay within the
  globe bounding box, front/back cull matches the table sign.
- The blit + title rework are target-only → ZEsarUX visual verify.

## 6.5 Portability (Timex / 128K / 48K)

The repo's `2026-06-22-zx128-port-design.md` plans a 128K port by swapping the
screen kernel behind the existing `scld_back/present/wait/init` interface
(128K double-buffers via the shadow screen, bit 3 of `0x7FFD`; it has **no SCLD,
no hi-color, no `OUT 0xFF`**).

- **Globe — portable.** It uses the abstracted double-buffer + standard 8×8
  attributes (which every target has), so it works on Timex now and on 128K once
  that kernel lands (48K: single-buffer, flickery). No globe-specific 128K work.
- **Plasma — natively portable (as built).** The shipped 8×8 plasma (§5
  amendment) uses only standard ULA attributes, which every target has, so it
  works identically on Timex/128K/48K with no hi-color, no dual-plane, and no
  detection. (The original hi-color/auto-degrade scheme is moot.)

## 7. Build phasing

1. **Phase 1 — DONE.** `bgpat` + in-game integration + host tests. Ships the
   four static per-run arena shapes (checker/diagonal/circles/lattice). `fxtab`
   was *not* needed here and was removed (see §4 amendment).
2. **Phase 2** — game-over plasma: reintroduce `fxtab`, add the `scld.c`
   hi-color extension, the pure `plasma_attr` field, and the dual-plane renderer.
3. **Phase 3** — title globe: title double-buffer rework + 8×8 colour bands +
   fixed-point 3D (reuses `fxtab`).

Each phase is independently shippable and verified in ZEsarUX.

**Mechanical build wiring (don't forget):** when reintroduced, register
`src/fxtab.c` (Phase 2) in `build.sh` (the `zcc … src/*.c` list) and add
`test/test_fxtab.c` + `test/test_plasma.c` (Phase 2) / `test/test_globe.c`
(Phase 3) to `test/run.sh`. Pure-logic modules link host-side; `main.c` and
`scld.c` stay target-only (not host-built).

## 8. Tooling

- Paint-cost numbers (§2) were taken with a one-off harness, now removed. For
  future perf work, mirror the `measure_main.c` + `measure.sh` + `z88dk-ticks`
  pattern (border-OUT markers, addresses from the `.map`).
- Add a globe per-frame measurement in Phase 3.
- Per CLAUDE.md: always `z88dk-ticks`-measure before claiming a perf change.

## 9. Tunable parameters (collected)

- `bgpat` pattern set membership (the four static shape ids). *(Phase 1, done.)*
- Safe paper palette set.
- Plasma palette/theme, the `KX/KY/KD` field constants, and 50/25 Hz rate.
- Globe point count, colour-band scheme, and 50/25 Hz rate.

## 10. Out of scope (YAGNI)

- Animated in-game backgrounds (rejected: no per-frame budget; static is the
  point).
- Per-life background changes (respawn keeps the run's pattern).
- Bitmap-textured planet / parallax starfield in-game (too heavy; the menu globe
  covers the "rotating ball / 3D" desire).
- New colours beyond the Spectrum 8×2 palette.
