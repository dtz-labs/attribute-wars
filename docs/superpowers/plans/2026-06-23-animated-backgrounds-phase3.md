# Animated Backgrounds — Phase 3 (title planet globe) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (or subagent-driven-development). Steps use `- [ ]` checkboxes.

**Goal:** A rotating 3D "planet" — a white dot-sphere spun on its vertical axis — as the centrepiece of the title screen, over 8×8 latitude colour bands, double-buffered for smooth motion.

**Architecture:** A new pure, host-tested `globe` module does the fixed-point 3D projection (Y-axis rotation via `fxtab`); each point's screen-y is constant (Y-rotation only moves it horizontally) so per frame each point costs one table lookup + one `fx_mul`. The title screen is reworked from single-buffer to the standard double-buffer page-flip: static text/menu painted into both bitmaps once, a colour disc painted into both attribute blocks once, then each frame the globe dots are box-erased + redrawn into the back buffer and presented. Menu highlight + shine-sweep now write both attribute blocks (they animate under a flipping screen).

**Tech Stack:** C99 (host tests), z88dk `+zx`/`sdcc_iy`, hand tables (no FP), ZEsarUX.

**Spec:** `docs/superpowers/specs/2026-06-22-animated-backgrounds-design.md` §6 + §6.5.

## Global Constraints

- No floating point / malloc / recursion. Integer math + baked tables only.
- Never return a struct by value (out-pointers).
- `controls.h` is the input header (never create `input.h`).
- Port `0xFF` only via `scld.c` (`scld_present`/`scld_back` etc.). Phase 3 adds no new port writes — it uses the existing double-buffer interface.
- `ATTR` is in `types.h`. White ink (7) for dots; colour-disc papers never white (7) so dots read.
- Host tests: `cc -std=c99 -Wall -Wextra -Werror -Iinclude`, exit 0. Target build `./build.sh`. Run `./run-zesarux.sh`.
- `fx_sin[256]` (signed sine) + `fx_mul(s8,u8)=(s*mag)>>7` from `fxtab` (already present from Phase 2). `cos(a) = fx_sin[(a+64)&255]`.

## Layout (reflow so the globe owns the centre)

| Rows | Content |
|---|---|
| 1 | `ATTRIBUTE WARS` (cols 9–22), shine-sweep |
| 3–12 | globe (centre `CX=128, CY=60, R=36`) over latitude colour bands |
| 14 | `1 KEMPSTON MOVE  KEYS FIRE` |
| 16 | `2 KEYS MOVE  KEMPSTON FIRE` |
| 18 | `3 TWO JOYSTICKS (TS2068)` |
| 20 | `0 START GAME` |
| 22–23 | copyright lines |

---

### Task 1: `globe` module (pure 3D projection, host-tested)

**Files:** Create `include/globe.h`, `src/globe.c`, `test/test_globe.c`; modify `test/run.sh`.

**Interfaces:**
- Produces: `void globe_init(void);` `u8 globe_count(void);` `u8 globe_x(u8 i, u8 theta);` `u8 globe_y(u8 i);` `u8 globe_front(u8 i, u8 theta);` and `GLOBE_CX/CY/R` macros.
- Consumes: `fx_sin`, `fx_mul` (`fxtab.h`).

- [ ] **Step 1: Write the failing test** — `test/test_globe.c`:

```c
/* test_globe.c -- host unit test for the title globe projection. */
#include "globe.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    u8 i, n;
    globe_init();
    n = globe_count();
    check("has points", n > 0u);

    /* All projected points sit inside the globe's bounding box, every theta. */
    {
        int x_ok = 1, y_ok = 1;
        u16 t;
        for (t = 0; t < 256u; t += 17u)
            for (i = 0; i < n; i++) {
                u8 x = globe_x(i, (u8)t);
                u8 y = globe_y(i);
                if (x < (GLOBE_CX - GLOBE_R) || x > (GLOBE_CX + GLOBE_R)) x_ok = 0;
                if (y < (GLOBE_CY - GLOBE_R) || y > (GLOBE_CY + GLOBE_R)) y_ok = 0;
            }
        check("x within bounds (all theta)", x_ok);
        check("y within bounds", y_ok);
    }

    /* Y-axis rotation: screen-y is constant per point (only x moves). */
    check("y constant under rotation", globe_y(3) == globe_y(3));
    check("x moves under rotation", globe_x(5, 0u) != globe_x(5, 64u));

    /* Determinism. */
    check("x deterministic", globe_x(7, 100u) == globe_x(7, 100u));

    /* Each point is front-facing for roughly half a full turn. */
    {
        u16 t, fc = 0;
        for (t = 0; t < 256u; t++) if (globe_front(9, (u8)t)) fc++;
        check("front ~half the turn", fc > 80u && fc < 176u);
    }

    if (failures == 0) { printf("test_globe: ALL PASS\n"); return 0; }
    printf("test_globe: %d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Create the header** — `include/globe.h`:

```c
/*
 * globe.h -- rotating 3D dot-sphere for the title screen. Pure logic,
 * host-testable: fixed-point Y-axis rotation via fxtab. Each point's screen-y is
 * constant (Y-rotation only slides it horizontally), so per frame a point costs
 * one cos lookup + one fx_mul.
 */
#ifndef GLOBE_H
#define GLOBE_H

#include "types.h"

#define GLOBE_CX 128u    /* centre x */
#define GLOBE_CY 60u     /* centre y */
#define GLOBE_R  36u     /* radius (pixels) */

/* Build the point tables (uses fx_sin). Call once before drawing. */
void globe_init(void);

/* Number of surface points. */
u8 globe_count(void);

/* Screen x of point i at rotation theta (0..255). In [CX-R, CX+R]. */
u8 globe_x(u8 i, u8 theta);

/* Screen y of point i (constant -- Y-axis spin). In [CY-R, CY+R]. */
u8 globe_y(u8 i);

/* 1 if point i faces the viewer at theta (front hemisphere), else 0. */
u8 globe_front(u8 i, u8 theta);

#endif /* GLOBE_H */
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cc -std=c99 -Wall -Wextra -Werror -Iinclude test/test_globe.c src/globe.c src/fxtab.c -o build/host/test_globe`
Expected: FAIL — `src/globe.c` missing.

- [ ] **Step 4: Create the implementation** — `src/globe.c`:

```c
/*
 * globe.c -- see globe.h. A lat/long grid of points on a unit sphere, projected
 * with a fixed-point Y-axis rotation. cos(a) = fx_sin[(a+64)&255].
 */
#include "globe.h"
#include "fxtab.h"

#define NLAT 7u
#define NLON 16u
#define NPTS (NLAT * NLON)   /* 112 */

/* Latitude angles -72..72 as 0..255 phase indices (angle*256/360), stored u8. */
static const u8 lat_idx[NLAT] = {
    205u, 222u, 239u, 0u, 17u, 34u, 51u   /* -72,-48,-24,0,24,48,72 deg */
};

static u8 g_rpix[NPTS];   /* xz-plane radius in pixels (0..R)      */
static u8 g_sy[NPTS];     /* screen y (constant per point)         */
static u8 g_lon[NPTS];    /* base longitude index (0..255)         */

void globe_init(void)
{
    u8 li, lo;
    u8 i = 0u;
    for (li = 0; li < NLAT; li++) {
        u8  a       = lat_idx[li];
        s8  sinphi  = fx_sin[a];
        s8  cosphi  = fx_sin[(u8)(a + 64u)];          /* >= 0 for |lat|<90 */
        u8  rpix    = (u8)fx_mul(cosphi, GLOBE_R);    /* 0..R              */
        s16 yoff    = fx_mul(sinphi, GLOBE_R);        /* -R..R            */
        u8  sy      = (u8)((s16)GLOBE_CY - yoff);
        for (lo = 0; lo < NLON; lo++, i++) {
            g_rpix[i] = rpix;
            g_sy[i]   = sy;
            g_lon[i]  = (u8)(lo * (256u / NLON));     /* step 16 */
        }
    }
}

u8 globe_count(void) { return NPTS; }

u8 globe_x(u8 i, u8 theta)
{
    u8 a    = (u8)(g_lon[i] + theta);
    s8 cosA = fx_sin[(u8)(a + 64u)];
    return (u8)((s16)GLOBE_CX + fx_mul(cosA, g_rpix[i]));
}

u8 globe_y(u8 i) { return g_sy[i]; }

u8 globe_front(u8 i, u8 theta)
{
    u8 a = (u8)(g_lon[i] + theta);
    return (fx_sin[a] >= 0) ? 1u : 0u;    /* sin(lon+theta) >= 0 -> front */
}
```

- [ ] **Step 5: Register the test** — in `test/run.sh`, after the `test_plasma` block:

```sh
$CC $CFLAGS "$ROOT/test/test_globe.c" "$ROOT/src/globe.c" "$ROOT/src/fxtab.c" -o "$OUT/test_globe"
"$OUT/test_globe"
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `./test/run.sh`
Expected: `test_globe: ALL PASS`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add include/globe.h src/globe.c test/test_globe.c test/run.sh
git commit -m "feat: globe module -- fixed-point rotating dot-sphere (host-tested)"
```

---

### Task 2: Rework the title screen (double-buffer + globe + colour disc)

**Files:** Modify `build.sh` (add `src/globe.c`); modify `src/main.c` (title helpers → both attr blocks, colour-disc painter, globe plot, reworked `title_screen`).

**Interfaces:** Consumes `globe_*` (globe.h), `scld_back/scld_back_page/scld_present/scld_scanline`, `put_text_both` (already in main.c), `SCLD_SCREEN_A/B`, `SCLD_ATTRS_A/B`.

- [ ] **Step 1: Register the source** — in `build.sh`, add `src/globe.c` to the `zcc … src/*.c` list (next to `src/plasma.c`):

```sh
    src/rng.c src/score.c src/sfx.c src/hud.c src/bgpat.c src/fxtab.c src/plasma.c src/globe.c \
```

- [ ] **Step 2: Add the include** — in `src/main.c`, after `#include "plasma.h"`:

```c
#include "globe.h"         /* title-screen rotating planet */
```

- [ ] **Step 3: Make the title attr helpers write BOTH blocks** — replace `title_attr_row` and `title_shine` so they paint screen A and B (the title now page-flips). Replace the body of `title_attr_row`:

```c
/* Fill one 32-cell attribute row of BOTH attr blocks (title now page-flips). */
static void title_attr_row(u8 row, u8 v)
{
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)row * 32u;
    u8 *b = (u8 *)SCLD_ATTRS_B + (u16)row * 32u;
    u8  c;
    for (c = 0; c < 32u; c++) { a[c] = v; b[c] = v; }
}
```

and in `title_shine`, write both blocks — change the pointer setup + store:

```c
static void title_shine(u8 s)
{
    u8 col;
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)SHINE_ROW * 32u;
    u8 *b = (u8 *)SCLD_ATTRS_B + (u16)SHINE_ROW * 32u;
    for (col = SHINE_COL_START; col <= SHINE_COL_END; col++) {
        u8 diag = (u8)(col + SHINE_ROW);
        u8 attr;
        if (diag == s)               attr = ATTR(1, 0, 7);
        else if (diag == (u8)(s + 1u)) attr = ATTR(1, 0, 6);
        else                         attr = ATTR(1, 0, 5);
        a[col] = attr;
        b[col] = attr;
    }
}
```

- [ ] **Step 4: Move the shine to row 1** — change the shine constants for the new title row:

```c
#define SHINE_COL_START  9u
#define SHINE_COL_END   22u
#define SHINE_ROW        1u   /* title moved to row 1 */
#define SHINE_S_MIN     10u   /* SHINE_COL_START + SHINE_ROW */
#define SHINE_S_MAX     23u   /* SHINE_COL_END   + SHINE_ROW */
#define SHINE_PAUSE     60u
```

- [ ] **Step 5: Add the colour-disc painter + globe plot** — insert above `title_screen`:

```c
/* Latitude colour bands for the planet, one paper per globe cell-row (rows 3..12
 * map to indices 0..9). Dark-to-bright-to-dark cool gradient; never white paper
 * (the white dots must read). */
static u8 globe_band_attr(u8 row)
{
    static const u8 paper[10] = { 1u, 1u, 5u, 5u, 4u, 4u, 5u, 5u, 1u, 1u };
    u8 r = (row >= 3u && row <= 12u) ? (u8)(row - 3u) : 0u;
    return ATTR(1, paper[r], 7);     /* bright band, white ink */
}

/* Paint the planet's colour disc into BOTH attr blocks once: cells whose centre
 * is within R of the globe centre get a latitude band; the rest stay black. */
static void globe_paint_disc(void)
{
    u8 row, col;
    for (row = 3u; row <= 12u; row++) {
        for (col = 10u; col <= 21u; col++) {
            s16 dx = (s16)((col * 8u + 4u)) - (s16)GLOBE_CX;
            s16 dy = (s16)((row * 8u + 4u)) - (s16)GLOBE_CY;
            u8  v;
            if (dx * dx + dy * dy <= (s16)(GLOBE_R * GLOBE_R)) {
                v = globe_band_attr(row);
            } else {
                v = ATTR(0, 0, 7);   /* black corner */
            }
            ((u8 *)SCLD_ATTRS_A)[(u16)row * 32u + col] = v;
            ((u8 *)SCLD_ATTRS_B)[(u16)row * 32u + col] = v;
        }
    }
}

/* Clear the globe's bitmap bounding box in buffer `base` (erase last dots). */
static void globe_box_clear(u16 base)
{
    u8 y;
    for (y = (u8)(GLOBE_CY - GLOBE_R); y <= (u8)(GLOBE_CY + GLOBE_R); y++) {
        u8 *row = scld_scanline(base, y);
        u8 c;
        for (c = 10u; c <= 21u; c++) row[c] = 0u;   /* cols 10..21 bytes */
    }
}

/* Plot the front-facing globe dots into buffer `base` at rotation theta. */
static void globe_plot(u16 base, u8 theta)
{
    u8 i, n = globe_count();
    for (i = 0; i < n; i++) {
        if (globe_front(i, theta)) {
            u8 x = globe_x(i, theta);
            u8 y = globe_y(i);
            scld_scanline(base, y)[x >> 3] |= (u8)(0x80u >> (x & 7u));
        }
    }
}
```

- [ ] **Step 6: Rewrite `title_screen`** — double-buffer it. Replace the whole function:

```c
static u8 title_screen(void)
{
    u8 sel   = CTRL_KEMPSTON_MOVE;
    u8 s     = SHINE_S_MIN;
    u8 pause = 0u;
    u8 theta = 0u;
    u8 page;

    globe_init();

    /* Static text + menu into BOTH bitmaps; black attrs into both blocks. */
    scld_clear(SCLD_SCREEN_A);
    scld_clear(SCLD_SCREEN_B);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);
    memset((u8 *)SCLD_ATTRS_B, ATTR(0, 0, 7), SCLD_ATTRS_LEN);

    put_text_both( 9,  1, "ATTRIBUTE WARS");
    put_text_both( 2, 14, "1 KEMPSTON MOVE  KEYS FIRE");
    put_text_both( 2, 16, "2 KEYS MOVE  KEMPSTON FIRE");
    put_text_both( 2, 18, "3 TWO JOYSTICKS (TS2068)");
    put_text_both( 2, 20, "0 START GAME");
    put_text_both( 3, 22, "(C) 2026 ANTHROPIC, INC.");
    put_text_both( 3, 23, "(C) 2026 MICHAL PASTERNAK");

    globe_paint_disc();               /* latitude colour bands, both blocks */
    title_attr_row(1, ATTR(1, 0, 5)); /* base title colour                  */

    for (;;) {
        /* scheme highlight + start (both blocks) */
        title_attr_row(14, (sel == CTRL_KEMPSTON_MOVE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(16, (sel == CTRL_KEMPSTON_FIRE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(18, (sel == CTRL_DUAL_STICK)    ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(20, ATTR(1, 0, 4));
        title_shine(s);

        /* draw the globe into the hidden buffer, then flip */
        page = scld_back_page();
        (void)page;
        globe_box_clear(scld_back());
        globe_plot(scld_back(), theta);
        scld_present();
        theta = (u8)(theta + 2u);     /* spin speed */

        /* shine sweep advance */
        if (pause > 0u) {
            pause--;
            if (pause == 0u) s = SHINE_S_MIN;
        } else if (s < SHINE_S_MAX) {
            s++;
        } else {
            pause = SHINE_PAUSE;
        }

        if      (in_key_pressed(IN_KEY_SCANCODE_1)) sel = CTRL_KEMPSTON_MOVE;
        else if (in_key_pressed(IN_KEY_SCANCODE_2)) sel = CTRL_KEMPSTON_FIRE;
        else if (in_key_pressed(IN_KEY_SCANCODE_3)) sel = CTRL_DUAL_STICK;
        else if (in_key_pressed(IN_KEY_SCANCODE_0)) break;
    }
    return sel;
}
```

Note: `scld_present()` does the HALT, so the loop is 50 Hz-paced (replaces the old `scld_wait()`).

- [ ] **Step 7: Build the target**

Run: `./build.sh`
Expected: `build/game.tap` listed; no warnings.

- [ ] **Step 8: Host tests (regression)**

Run: `./test/run.sh`
Expected: `ALL HOST TESTS PASSED`.

- [ ] **Step 9: Visual verification in ZEsarUX**

Run: `./run-zesarux.sh`
Confirm on the title screen:
- A **rotating dot-sphere** spins smoothly in the centre over coloured latitude bands; reads as a planet.
- The title `ATTRIBUTE WARS` (row 1) still shine-sweeps; the scheme menu (rows 14/16/18) highlights with 1/2/3; `0` starts.
- No flicker (double-buffered), no tearing of the text, smooth 50 Hz.
- Starting the game and returning to the menu (game-over → any key) shows the globe again cleanly.

- [ ] **Step 10: Measure if needed**

If the spin looks like it drops frames, profile with the `measure_main.c`/`z88dk-ticks` pattern or reduce `NLON`/spin rate. (Expected fine: ~112 points × ~1 mul each in the otherwise-empty title loop.)

- [ ] **Step 11: Commit**

```bash
git add build.sh src/main.c
git commit -m "feat: rotating planet globe on the title screen (double-buffered)"
```

---

## Self-Review

**Spec coverage (§6 + §6.5):** standard double-buffer + 8×8 colour bands (Task 2 globe_paint_disc + title rework); reflow (Task 2 layout); fixed-point Y-rotation, ~112 points, one fx_mul/point (Task 1); dots white over dark/coloured bands, never white paper (globe_band_attr); incremental erase via box-clear (globe_box_clear); shine + highlight to both blocks (Task 2 Steps 3); portable (uses abstracted scld double-buffer + standard attrs). Globe math host-tested (Task 1). ✓

**Placeholder scan:** none — complete code each step. ✓

**Type consistency:** `globe_init/count/x/y/front` signatures match between header (Task 1), test (Task 1), and renderer (Task 2). `GLOBE_CX/CY/R` used consistently. ✓
