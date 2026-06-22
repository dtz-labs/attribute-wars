# Animated Backgrounds — Phase 2 (game-over plasma) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A flowing colour plasma behind the GAME OVER screen, rendered in Timex hi-color 8×1 for smooth per-scanline colour, with an automatic standard-8×8 fallback so the same binary still looks right on 128K/48K.

**Architecture:** Reintroduce `fxtab` (baked signed sine table). Add a pure, host-tested plasma field (`plasma_field` + `plasma_palette`). Extend the hardware-boundary `scld.c` with a hi-color mode (port `0xFF` bit 1). Rework `game_over_screen` to draw its text into the `0x4000` bitmap and animate a **dual-plane** renderer each frame — writing the 8×1 attr map at `0x6000` *and* (piggybacked) the standard 8×8 block at `0x5800` — then turn hi-color on. On Timex the SCLD shows the smooth 8×1 plane; on 128K/48K `OUT 0xFF` is ignored so the ULA shows the 8×8 plane. No hardware detection.

**Tech Stack:** C99 (host tests via system `cc`), z88dk `+zx` / `sdcc_iy` for Z80, hand-tables (no FP), ZEsarUX for visual verification.

**Spec:** `docs/superpowers/specs/2026-06-22-animated-backgrounds-design.md` §5 + §6.5 (Phase 2 of three).

## Global Constraints

- **No floating point, no `malloc`, no recursion.** Integer math, fixed-size arrays, baked tables.
- **Never return a struct by value** — pass via out-pointer.
- **The input module's header is `controls.h`** (do not create `input.h`).
- **Port `0xFF` is owned ONLY by `scld.c`.** Phase 2 adds exactly one new legal value, `0x02` (bit 1 = hi-color). **Bits 6–7 MUST stay 0** (bit 6 is the interrupt kill-switch). Never write `0xFF` outside `scld.c`.
- **Use `z80_outp()`** from `<z80.h>` for port output. HALT/interrupt primitives from `<intrinsic.h>`.
- `ATTR(bright,paper,ink)` lives in `include/types.h` (Phase 1).
- **Text-readability invariant for the plasma:** every plasma attribute has `ink == 7` (white) and `paper != 7` (never white paper), so the GAME OVER / SCORE text stays legible.
- **Host tests:** `cc -std=c99 -Wall -Wextra -Werror -Iinclude`, must exit 0. **Target build:** `./build.sh`. **Run:** `./run-zesarux.sh`.
- Hi-color 8×1 layout: bitmap stays at `0x4000`; the attribute for column `x`, scanline `y` is at `scld_scanline(0x6000, y)[x]` (same interleaved addressing as the bitmap).

## File structure

- `include/fxtab.h`, `src/fxtab.c`, `test/test_fxtab.c` — restored from Phase 1 (signed sine + `fx_mul`).
- `include/plasma.h`, `src/plasma.c`, `test/test_plasma.c` — NEW pure field + palette.
- `include/scld.h`, `src/scld.c` — add `scld_hicolor_on/off`.
- `src/main.c` — rework `game_over_screen`; add the dual-plane `plasma_render`.
- `build.sh`, `test/run.sh` — register the new sources/tests.

---

### Task 1: Reintroduce `fxtab` (signed sine table + fixed multiply)

The exact module existed in Phase 1 at commit `02af6e3`; restore it verbatim rather than retyping the 256-entry table.

**Files:**
- Create (restore): `include/fxtab.h`, `src/fxtab.c`, `test/test_fxtab.c`
- Modify: `test/run.sh`

**Interfaces:**
- Produces: `extern const s8 fx_sin[256];` (`fx_sin[i] ≈ 127·sin(2π·i/256)`), `s16 fx_mul(s8 s, u8 mag)` = `(s*mag)>>7`.

- [ ] **Step 1: Restore the three files from git**

```bash
git show 02af6e3:include/fxtab.h     > include/fxtab.h
git show 02af6e3:src/fxtab.c         > src/fxtab.c
git show 02af6e3:test/test_fxtab.c   > test/test_fxtab.c
```

- [ ] **Step 2: Register the test** — in `test/run.sh`, immediately before the `test_bgpat` block, add:

```sh
$CC $CFLAGS "$ROOT/test/test_fxtab.c" "$ROOT/src/fxtab.c" -o "$OUT/test_fxtab"
"$OUT/test_fxtab"
```

- [ ] **Step 3: Run the tests**

Run: `./test/run.sh`
Expected: `test_fxtab: ALL PASS`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 4: Commit**

```bash
git add include/fxtab.h src/fxtab.c test/test_fxtab.c test/run.sh
git commit -m "feat: reintroduce fxtab (sine table + fixed multiply) for plasma"
```

---

### Task 2: Plasma field + palette module (pure, host-tested)

**Files:**
- Create: `include/plasma.h`, `src/plasma.c`, `test/test_plasma.c`
- Modify: `test/run.sh`

**Interfaces:**
- Consumes: `fx_sin` (`fxtab.h`), `ATTR` (`types.h`).
- Produces:
  - `#define PLA_KX 6u`, `PLA_KY 5u`, `PLA_KD 4u` (field frequency constants — used by the renderer too).
  - `s16 plasma_field(u8 x, u8 y, u8 phase);` — sum of three sines, range ≈ [-381, 381].
  - `u8 plasma_palette(s16 v);` — maps the field to an attribute byte (ink=white, paper a non-white rainbow colour).

- [ ] **Step 1: Write the failing test** — `test/test_plasma.c`:

```c
/* test_plasma.c -- host unit test for the game-over plasma field + palette. */
#include "plasma.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    u16 x, y, p;

    /* Determinism: same (x,y,phase) -> same field value. */
    check("field deterministic",
          plasma_field(7u, 19u, 100u) == plasma_field(7u, 19u, 100u));

    /* Field stays within the 3*127 amplitude bound. */
    {
        int ok = 1;
        for (p = 0; p < 256u; p += 17u)
            for (y = 0; y < 192u; y += 13u)
                for (x = 0; x < 32u; x++) {
                    s16 v = plasma_field((u8)x, (u8)y, (u8)p);
                    if (v > 381 || v < -381) ok = 0;
                }
        check("field within +/-381", ok);
    }

    /* Palette: text-readability invariant across the whole field range. */
    {
        int ink_ok = 1, paper_ok = 1;
        s16 v;
        for (v = -381; v <= 381; v++) {
            u8 a = plasma_palette(v);
            if ((a & 7u) != 7u) ink_ok = 0;                 /* ink must be white  */
            if (((a >> 3) & 7u) == 7u) paper_ok = 0;        /* paper never white  */
        }
        check("palette ink == white", ink_ok);
        check("palette paper != white", paper_ok);
    }

    /* Palette actually varies (not one flat colour). */
    check("palette varies", plasma_palette(-300) != plasma_palette(300));

    /* The renderer's separable decomposition must equal plasma_field (guards the
     * fast path in main.c against drift). */
    {
        int ok = 1;
        u8 ph = 77u, ph2 = (u8)(77u + 64u);
        for (y = 0; y < 192u; y += 11u)
            for (x = 0; x < 32u; x++) {
                s16 sep = (s16)fx_sin[(u8)(x * PLA_KX + ph)]
                        + (s16)fx_sin[(u8)(y * PLA_KY + ph)]
                        + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD + ph2)];
                if (sep != plasma_field((u8)x, (u8)y, ph)) ok = 0;
            }
        check("separable decomposition == plasma_field", ok);
    }

    if (failures == 0) { printf("test_plasma: ALL PASS\n"); return 0; }
    printf("test_plasma: %d FAILURE(S)\n", failures);
    return 1;
}
```

(The test includes `fx_sin` via `plasma.h` → `fxtab.h`.)

- [ ] **Step 2: Create the header** — `include/plasma.h`:

```c
/*
 * plasma.h -- game-over plasma field. Pure logic, host-testable: a sum of three
 * sines (fxtab) mapped to a non-white-paper rainbow attribute (ink stays white
 * so text reads). The renderer (main.c) computes the same field separably.
 */
#ifndef PLASMA_H
#define PLASMA_H

#include "types.h"
#include "fxtab.h"

#define PLA_KX 6u    /* x frequency */
#define PLA_KY 5u    /* y frequency */
#define PLA_KD 4u    /* diagonal frequency */

/* Sum of three sines at (x,y) with animation phase. Range approx [-381,381]. */
s16 plasma_field(u8 x, u8 y, u8 phase);

/* Map a field value to an attribute byte: ink=white(7), paper a cycling
 * non-white colour. */
u8 plasma_palette(s16 v);

#endif /* PLASMA_H */
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cc -std=c99 -Wall -Wextra -Werror -Iinclude test/test_plasma.c src/plasma.c src/fxtab.c -o build/host/test_plasma`
Expected: FAIL — `src/plasma.c` does not exist.

- [ ] **Step 4: Create the implementation** — `src/plasma.c`:

```c
/*
 * plasma.c -- see plasma.h. Integer-only plasma field + palette.
 */
#include "plasma.h"

s16 plasma_field(u8 x, u8 y, u8 phase)
{
    u8 ph2 = (u8)(phase + 64u);   /* quarter-turn offset for the diagonal term */
    return (s16)fx_sin[(u8)(x * PLA_KX + phase)]
         + (s16)fx_sin[(u8)(y * PLA_KY + phase)]
         + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD + ph2)];
}

u8 plasma_palette(s16 v)
{
    /* v in ~[-381,381] -> 0..11 band -> a non-white paper colour, ink white.
     * Bright on the upper bands for vividness. Papers chosen from {1..6} (blue,
     * red, magenta, green, cyan, yellow) -- never 7 (white) so text reads. */
    static const u8 paper[12] = { 1u, 2u, 3u, 4u, 5u, 6u, 5u, 4u, 3u, 2u, 1u, 6u };
    u16 band = (u16)((v + 384) >> 6);          /* 0..11 */
    if (band > 11u) band = 11u;
    return ATTR((band >= 6u) ? 1u : 0u, paper[band], 7u);
}
```

- [ ] **Step 5: Register the test** — in `test/run.sh`, after the `test_fxtab` block, add:

```sh
$CC $CFLAGS "$ROOT/test/test_plasma.c" "$ROOT/src/plasma.c" "$ROOT/src/fxtab.c" -o "$OUT/test_plasma"
"$OUT/test_plasma"
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `./test/run.sh`
Expected: `test_plasma: ALL PASS`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add include/plasma.h src/plasma.c test/test_plasma.c test/run.sh
git commit -m "feat: pure plasma field + palette (host-tested)"
```

---

### Task 3: `scld.c` hi-color mode extension

**Files:**
- Modify: `include/scld.h` (declare the two functions)
- Modify: `src/scld.c` (implement them)

**Interfaces:**
- Produces: `void scld_hicolor_on(void);` `void scld_hicolor_off(void);`
- Consumes: existing `scld_front` (static in scld.c), `z80_outp`.

- [ ] **Step 1: Declare in `scld.h`** — add before `#endif`, after `scld_clear`'s declaration:

```c
/* ---------------------------------------------------------------------------
 * Timex 8x1 HI-COLOUR mode (per-scanline attributes). OUT (0xFF), 0x02 keeps
 * the bitmap at 0x4000 but takes attributes from an 8x1 map at 0x6000 (one byte
 * per char-column per scanline, addressed with scld_scanline(0x6000, y)[x]).
 * This consumes the second screen file, so it CANNOT coexist with the page-flip
 * double buffer -- use it only on a single-buffered screen (e.g. game-over).
 * scld_hicolor_off() restores the standard double-buffer display.
 * Only bit 1 is set; bits 6-7 stay 0 (never the interrupt kill-switch).
 * ------------------------------------------------------------------------- */
extern void scld_hicolor_on(void);
extern void scld_hicolor_off(void);
```

- [ ] **Step 2: Implement in `scld.c`** — add at the end of the file, and a mode constant near the others (`#define SCLD_MODE_HICOLOR 0x02u` next to `SCLD_PAGE_A/B`):

```c
void scld_hicolor_on(void)
{
    /* bitmap 0x4000 + 8x1 attribute map at 0x6000; bits 6-7 stay 0. */
    z80_outp(SCLD_PORT, SCLD_MODE_HICOLOR);
}

void scld_hicolor_off(void)
{
    /* back to standard mode showing the current double-buffer page. */
    z80_outp(SCLD_PORT, scld_front);
}
```

- [ ] **Step 3: Build the target (no caller yet — just confirms it compiles/links)**

Run: `./build.sh`
Expected: `build/game.tap` listed, no warnings/errors.

- [ ] **Step 4: Commit**

```bash
git add include/scld.h src/scld.c
git commit -m "feat: scld hi-color (8x1) mode enter/exit (Timex)"
```

---

### Task 4: Rework `game_over_screen` for the dual-plane plasma

Text moves to the `0x4000` bitmap only (in hi-color, `0x6000` is the attr map, not pixels). The animation writes both planes; hi-color is on while the screen is up and off on exit. Built incrementally: first a gradient smoke-test to de-risk the mode in ZEsarUX, then the real plasma.

**Files:**
- Modify: `build.sh` (add `src/fxtab.c src/plasma.c`)
- Modify: `src/main.c` (`#include`s, new `plasma_render`, rewritten `game_over_screen`)

**Interfaces:**
- Consumes: `plasma_field`, `plasma_palette`, `PLA_KX/KY/KD` (`plasma.h`); `fx_sin` (`fxtab.h`); `scld_hicolor_on/off`, `scld_scanline`, `SCLD_SCREEN_A`, `SCLD_ATTRS_A` (`scld.h`); existing `put_char`, `put_text`, `input_read`, `in_key_pressed`, `scld_wait`, `game_over_flash`.

- [ ] **Step 1: Register the new sources in `build.sh`** — change the source list to add `src/fxtab.c src/plasma.c` (next to `src/bgpat.c`):

```sh
    src/rng.c src/score.c src/sfx.c src/hud.c src/bgpat.c src/fxtab.c src/plasma.c \
```

- [ ] **Step 2: Add includes to `src/main.c`** — after `#include "bgpat.h"`:

```c
#include "fxtab.h"        /* fx_sin -- plasma renderer */
#include "plasma.h"       /* plasma_field/palette + PLA_* */
```

- [ ] **Step 3: Add the dual-plane renderer** — in `src/main.c`, above `game_over_screen` (after the `put_*` text helpers). This computes the field separably (fast) and writes both the 8×1 hi-color map (`0x6000`) and, every 8th scanline, the standard 8×8 block (`0x5800`):

```c
/* Game-over plasma: write the field into BOTH attribute planes so the same
 * binary degrades automatically -- Timex shows the 8x1 plane (smooth), 128K/48K
 * show the 8x8 plane (chunky). Separable precompute keeps the 6144-cell fill
 * affordable on the otherwise-idle game-over loop. */
static void plasma_render(u8 phase)
{
    static s8 sinx[32];
    static s8 siny[192];
    static s8 sind[223];          /* x+y in 0..222 */
    u8  x, y;
    u16 s;
    u8  ph2 = (u8)(phase + 64u);

    for (x = 0; x < 32u; x++)  sinx[x] = fx_sin[(u8)(x * PLA_KX + phase)];
    for (y = 0; y < 192u; y++) siny[y] = fx_sin[(u8)(y * PLA_KY + phase)];
    for (s = 0; s < 223u; s++) sind[s] = fx_sin[(u8)((u8)s * PLA_KD + ph2)];

    for (y = 0; y < 192u; y++) {
        u8 *hc   = scld_scanline(0x6000u, (u8)y);   /* 8x1 attr row (Timex)     */
        u8 *cell = ((u8)(y & 7u) == 0u)              /* 8x8 block row (fallback) */
                   ? ((u8 *)SCLD_ATTRS_A + (u16)(y >> 3) * 32u) : (u8 *)0;
        s8 sy = siny[y];
        for (x = 0; x < 32u; x++) {
            s16 v = (s16)sinx[x] + (s16)sy + (s16)sind[(u8)(x + (u8)y)];
            u8  a = plasma_palette(v);
            hc[x] = a;
            if (cell) { cell[x] = a; }
        }
    }
}
```

- [ ] **Step 4: Rewrite `game_over_screen`** — replace the whole function body. Text is drawn into screen A only; hi-color animates the plasma; mode is restored on exit:

```c
static u8 game_over_screen(const game_state_t *g, u8 death_wave)
{
    u8 phase = 0u;
    u8 i;

    game_over_flash();

    /* Hi-color: GAME OVER text lives in the 0x4000 bitmap; 0x6000 becomes the
     * 8x1 colour map. Draw text into screen A ONLY (never 0x6000 here). */
    scld_clear(SCLD_SCREEN_A);
    put_text(SCLD_SCREEN_A, 11,  6, "GAME OVER");
    put_text(SCLD_SCREEN_A,  7, 10, "SCORE");
    for (i = 0; i < 6u; i++) {
        put_char(SCLD_SCREEN_A, (u8)(13u + i), 10u, (u8)('0' + g->score.digits[i]));
    }
    put_text(SCLD_SCREEN_A,  7, 12, "WAVE");
    put_char(SCLD_SCREEN_A, 13u, 12u, (u8)('0' + (death_wave / 10u) % 10u));
    put_char(SCLD_SCREEN_A, 14u, 12u, (u8)('0' + (death_wave % 10u)));
    put_text(SCLD_SCREEN_A,  3, 17, "FIRE/SPACE  RESUME WAVE");
    put_char(SCLD_SCREEN_A, 27u, 17u, (u8)('0' + (death_wave / 10u) % 10u));
    put_char(SCLD_SCREEN_A, 28u, 17u, (u8)('0' + (death_wave % 10u)));
    put_text(SCLD_SCREEN_A,  3, 19, "Q           NEW GAME");

    scld_hicolor_on();

    for (;;) {
        intent_t in;
        plasma_render(phase);
        phase++;
        input_read(DIR_NONE, &in);    /* scheme-agnostic read */
        if (in.boost || in.fire || in_key_pressed(IN_KEY_SCANCODE_SPACE)) {
            scld_hicolor_off();
            return 0u;                 /* resume from the death wave */
        }
        if (in_key_pressed(IN_KEY_SCANCODE_q)) {
            scld_hicolor_off();
            return 1u;                 /* fresh game */
        }
        scld_wait();
    }
}
```

- [ ] **Step 5: Build the target**

Run: `./build.sh`
Expected: `build/game.tap` listed; no warnings. (If `put_text_both`/`put_score_digits`/`put_u8` become unused and trigger a warning, that's expected only if they were used solely here — check; if so, leave them, they may be used elsewhere. Do NOT delete without grepping.)

- [ ] **Step 6: Verify it compiles cleanly + host tests still pass**

Run: `./build.sh 2>&1 | grep -iE "warning|error" || echo clean` then `./test/run.sh`
Expected: `clean`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 7: Visual verification in ZEsarUX (the hi-color de-risk + plasma check)**

Run: `./run-zesarux.sh`, then play until GAME OVER (let lives hit 0).
Confirm by observation:
- The GAME OVER / SCORE / WAVE text is readable (white) over a **smooth, per-scanline colour plasma** that flows/cycles (no 8×8 blockiness — that confirms hi-color 8×1 is engaged).
- FIRE/SPACE resumes the death wave; Q starts a new game; on return the arena repaints correctly in standard mode (no leftover hi-color, colours correct).
- The plasma motion is smooth enough; if it looks slow/choppy, note the rate (next step).

- [ ] **Step 8: Measure & set the frame rate if needed**

The 6144-cell fill is heavy. If Step 7 looked choppy or input felt laggy, gate the animation to every other frame: add a `static u8 tick; if ((tick++ & 1u) == 0u) plasma_render(phase++);` style throttle in the loop (advance `phase` only when rendering). Re-verify in ZEsarUX. (Optional: build a `measure_*`-style harness per CLAUDE.md to get the exact T-cost; not required if it visibly holds.)

- [ ] **Step 9: Commit**

```bash
git add build.sh src/main.c
git commit -m "feat: game-over hi-color plasma (dual-plane, auto-degrades to 8x8)"
```

---

## Self-Review

**Spec coverage (§5 + §6.5):**
- `fxtab` reintroduced → Task 1. ✓
- Pure `plasma_field` + `plasma_palette`, text-safe (ink=white, paper≠white) → Task 2 + tests. ✓
- `scld.c` hi-color `on/off`, bit-1-only, 8×1 addressing via `scld_scanline(0x6000,…)` → Task 3. ✓
- Dual-plane render (8×1 + piggybacked 8×8), separable precompute → Task 4 Step 3. ✓
- Auto-degradation (no detection): both planes written, hi-color on → Timex shows 8×1, others 8×8 → Task 4 (inherent). ✓
- Text into `0x4000` only; hi-color on/off around the loop → Task 4 Step 4. ✓
- ZEsarUX hi-color de-risk + plasma verify; measure/throttle to 25 Hz → Task 4 Steps 7–8. ✓
- Build/test wiring → Task 1/2 (`run.sh`), Task 4 (`build.sh`). ✓

**Placeholder scan:** none — every code step is complete; `fxtab` restored verbatim via `git show`. ✓

**Type consistency:** `plasma_field(u8,u8,u8)->s16`, `plasma_palette(s16)->u8`, `PLA_KX/KY/KD`, `scld_hicolor_on/off(void)`, `plasma_render(u8)` used consistently across Tasks 2–4. The separable form in `plasma_render` (Task 4) matches `plasma_field` (Task 2) and is guarded by the decomposition test (Task 2 Step 1). ✓
