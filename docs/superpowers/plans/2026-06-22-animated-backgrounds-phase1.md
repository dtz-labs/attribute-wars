# Animated Backgrounds — Phase 1 (in-game patterns) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single fixed blue/black checker arena floor with a static attribute "shape" chosen per run (low-noise set) or re-rolled per wave (noisy set), at zero per-frame cost.

**Architecture:** Two new pure-logic, host-tested modules — `fxtab` (signed sine table + fixed multiply) and `bgpat` (generate an attribute pattern into a 768-byte RAM table) — plus target-only wiring in `main.c`: a `bg_cells[768]` table that `bg_attr()` reads (so explosion/telegraph restores follow the pattern for free) and `bg_paint()` block-copies into both attribute blocks. Selection state lives in `main.c`.

**Tech Stack:** C99 (host tests via system `cc`), z88dk `+zx` / `sdcc_iy` for the Z80 target, hand-tables (no FP).

**Spec:** `docs/superpowers/specs/2026-06-22-animated-backgrounds-design.md` (this is Phase 1 of the three phased pieces; Phases 2 game-over plasma and 3 title globe get their own plans).

## Global Constraints

- **No floating point, no `malloc`, no recursion.** Integer math, fixed-size arrays, precomputed tables only.
- **Never return a struct by value** — pass via out-pointer (SDCC Z80 backend SIGSEGVs otherwise).
- **The input module's header is `controls.h`, not `input.h`** (do not create `input.h`).
- **Port `0xFF` is owned only by `scld.c`** and only ever written `0x00`/`0x01`. Phase 1 touches no ports.
- **Attribute byte layout:** `ATTR(bright,paper,ink) = (bright<<6)|(paper<<3)|ink`. After Task 1 this macro lives in `include/types.h`.
- **In-game readability invariant:** every interior cell (rows 1–22, cols 1–30) must have `ink == 7` (white) and `paper != 7` (never white paper). Frame ring (rows 0/23, cols 0/31) = `ATTR(1,3,7)`.
- **Host tests** compile with `cc -std=c99 -Wall -Wextra -Werror -Iinclude` and must exit 0. **Target build** is `./build.sh` (ORG 0x8000, no `-zorg`).
- Safe paper palette used by patterns: `{black 0, blue 1, red 2, magenta 3, green 4, cyan 5}` — never `6` (yellow) or `7` (white).

---

### Task 1: Relocate the `ATTR` macro to `types.h`

So the pure modules (`bgpat`) and their host tests can build the attribute byte without depending on the HUD header.

**Files:**
- Modify: `include/types.h` (add the macro)
- Modify: `include/hud.h:22` (remove its definition; rely on `types.h`)

**Interfaces:**
- Produces: `ATTR(bright,paper,ink)` macro, now declared in `include/types.h`.

- [ ] **Step 1: Add the macro to `types.h`** — insert before the final `#endif`:

```c
/* Attribute byte: FLASH(7) BRIGHT(6) PAPER(5-3) INK(2-0). Shared everywhere. */
#define ATTR(bright, paper, ink) ((u8)(((bright) << 6) | ((paper) << 3) | (ink)))
```

- [ ] **Step 2: Remove the duplicate in `hud.h`** — delete the line at `include/hud.h:22`:

```c
#define ATTR(bright, paper, ink) ((u8)(((bright) << 6) | ((paper) << 3) | (ink)))
```

`hud.h` already `#include "types.h"`, so the macro is still in scope for every current includer.

- [ ] **Step 3: Build the target to verify no breakage**

Run: `./build.sh`
Expected: ends with `build/game.tap` listed, no `redefined`/`undeclared` errors.

- [ ] **Step 4: Run host tests to verify no breakage**

Run: `./test/run.sh`
Expected: `ALL HOST TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add include/types.h include/hud.h
git commit -m "refactor: move ATTR macro to types.h for pure-module reuse"
```

---

### Task 2: `fxtab` module — signed sine table + fixed multiply

**Files:**
- Create: `include/fxtab.h`
- Create: `src/fxtab.c`
- Create: `test/test_fxtab.c`
- Modify: `test/run.sh` (register the test)

**Interfaces:**
- Produces:
  - `extern const s8 fx_sin[256];` — `fx_sin[i] ≈ 127·sin(2π·i/256)`, one full period.
  - `s16 fx_mul(s8 s, u8 mag);` — returns `(s * mag) >> 7` (project a cos/sin onto a pixel magnitude). Used by Phase 3; included now since it belongs to this module.

- [ ] **Step 1: Write the failing test** — `test/test_fxtab.c`:

```c
/* test_fxtab.c -- host unit test for the fixed-point sine table + multiply. */
#include "fxtab.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    int i;

    /* Quadrant anchors. */
    check("sin[0]   == 0",    fx_sin[0]   == 0);
    check("sin[64]  == 127",  fx_sin[64]  == 127);
    check("sin[128] == 0",    fx_sin[128] == 0);
    check("sin[192] == -127", fx_sin[192] == -127);

    /* Antisymmetry: sin[i] == -sin[i+128]. */
    {
        int ok = 1;
        for (i = 0; i < 256; i++) {
            if (fx_sin[i] != (s8)(-fx_sin[(i + 128) & 255])) { ok = 0; break; }
        }
        check("antisymmetric over half period", ok);
    }

    /* Amplitude bound. */
    {
        int ok = 1;
        for (i = 0; i < 256; i++) if (fx_sin[i] > 127 || fx_sin[i] < -127) { ok = 0; break; }
        check("within +/-127", ok);
    }

    /* fx_mul exact cases (no rounding ambiguity). */
    check("mul 127*128>>7 == 127", fx_mul(127, 128) == 127);
    check("mul 0*200 == 0",        fx_mul(0, 200)   == 0);
    check("mul 64*128>>7 == 64",   fx_mul(64, 128)  == 64);
    check("mul -64*128>>7 == -64", fx_mul(-64, 128) == -64);

    if (failures == 0) { printf("test_fxtab: ALL PASS\n"); return 0; }
    printf("test_fxtab: %d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Create the header** — `include/fxtab.h`:

```c
/*
 * fxtab.h -- fixed-point helpers (no floating point): a signed sine table and
 * an 8-bit fractional multiply. Pure data + tiny function, host-testable.
 */
#ifndef FXTAB_H
#define FXTAB_H

#include "types.h"

/* One full period, amplitude +/-127: fx_sin[i] ~= 127 * sin(2*pi*i/256). */
extern const s8 fx_sin[256];

/* (s * mag) >> 7 : project a signed cos/sin (-127..127) onto magnitude `mag`. */
s16 fx_mul(s8 s, u8 mag);

#endif /* FXTAB_H */
```

- [ ] **Step 3: Create the implementation** — `src/fxtab.c` (table values are precomputed; verified against the test in Step 5):

```c
/*
 * fxtab.c -- see fxtab.h. fx_sin is round(127*sin(2*pi*i/256)) baked at build
 * time so the target needs no runtime trig.
 */
#include "fxtab.h"

const s8 fx_sin[256] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
      49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
      90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
     117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
     127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
     117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
      90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
      49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
       0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
     -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
     -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
     -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
     -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
};

s16 fx_mul(s8 s, u8 mag)
{
    return (s16)(((s16)s * (s16)mag) >> 7);
}
```

- [ ] **Step 4: Register the test** — in `test/run.sh`, after the `test_rng` block, add:

```sh
$CC $CFLAGS "$ROOT/test/test_fxtab.c" "$ROOT/src/fxtab.c" -o "$OUT/test_fxtab"
"$OUT/test_fxtab"
```

- [ ] **Step 5: Run the test**

Run: `./test/run.sh`
Expected: `test_fxtab: ALL PASS`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add include/fxtab.h src/fxtab.c test/test_fxtab.c test/run.sh
git commit -m "feat: fxtab signed sine table + fixed multiply (host-tested)"
```

---

### Task 3: `bgpat` module — low-noise generators (checker, diagonal, circles, lattice)

**Files:**
- Create: `include/bgpat.h`
- Create: `src/bgpat.c`
- Create: `test/test_bgpat.c`
- Modify: `test/run.sh` (register the test)

**Interfaces:**
- Consumes: `ATTR` (from `types.h`), `fx_sin` (from `fxtab.h`, used by the plasma generator added in Task 4).
- Produces:
  - pattern ids enum `BGPAT_CHECKER..BGPAT_STARFIELD`, `BGPAT_COUNT`, and tier ranges `BGPAT_LOWNOISE_FIRST/COUNT`, `BGPAT_NOISY_FIRST/COUNT`.
  - `#define BGPAT_FRAME_ATTR ATTR(1,3,7)`
  - `void bgpat_generate(u8 *cells, u8 id, u16 seed);` — fills 768 cells.
  - `u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd);` (implemented in Task 5).

- [ ] **Step 1: Create the header** — `include/bgpat.h`:

```c
/*
 * bgpat.h -- generate a 32x24 attribute "background shape" into a RAM table.
 * Pure logic, host-testable. Interior keeps ink=white so white sprites stay
 * readable on any paper; the frame ring is the HUD colour.
 */
#ifndef BGPAT_H
#define BGPAT_H

#include "types.h"

#define BGPAT_COLS   32u
#define BGPAT_ROWS   24u
#define BGPAT_CELLS  768u                 /* COLS*ROWS */
#define BGPAT_FRAME_ATTR ATTR(1, 3, 7)    /* bright magenta paper, white ink */

enum {
    BGPAT_CHECKER = 0, BGPAT_DIAGONAL, BGPAT_CIRCLES, BGPAT_LATTICE, /* low-noise */
    BGPAT_DIAMONDS, BGPAT_VBANDS, BGPAT_PLASMA, BGPAT_STARFIELD,     /* noisy     */
    BGPAT_COUNT
};

#define BGPAT_LOWNOISE_FIRST 0u
#define BGPAT_LOWNOISE_COUNT 4u
#define BGPAT_NOISY_FIRST    4u
#define BGPAT_NOISY_COUNT    4u

/* Fill cells[BGPAT_CELLS]: frame ring = BGPAT_FRAME_ATTR; interior = pattern
 * `id` (ink=7, paper from the safe palette). `seed` varies plasma/starfield
 * detail. Deterministic for a given (id, seed). */
void bgpat_generate(u8 *cells, u8 id, u16 seed);

/* Pick a pattern id from a tier [first, first+count), avoiding `prev`. `rnd` is
 * a fresh random byte. Pass prev=0xFF to allow any. */
u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd);

#endif /* BGPAT_H */
```

- [ ] **Step 2: Write the failing test** — `test/test_bgpat.c` (the invariant loop is bounded by `IMPL` so it grows as patterns are added across tasks; this task implements ids 0..3):

```c
/* test_bgpat.c -- host unit test for the background pattern generator. */
#include "bgpat.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

#define IMPL 4   /* number of pattern ids implemented so far (grows per task) */

static int paper_safe(u8 p) { return p <= 5u; }   /* {0..5}; never 6 or 7 */

static void check_invariants(u8 id)
{
    static u8 cells[BGPAT_CELLS];
    u8 r, c;
    char nm[48];
    int ink_ok = 1, paper_ok = 1, frame_ok = 1, filled_ok = 1;

    memset(cells, 0xAAu, sizeof cells);            /* sentinel: detect unwritten */
    bgpat_generate(cells, id, 0x1234u);

    for (r = 0; r < BGPAT_ROWS; r++) {
        for (c = 0; c < BGPAT_COLS; c++) {
            u8 v = cells[(u16)r * BGPAT_COLS + c];
            if (v == 0xAAu) filled_ok = 0;
            if (r == 0u || r == 23u || c == 0u || c == 31u) {
                if (v != (u8)BGPAT_FRAME_ATTR) frame_ok = 0;
            } else {
                if ((v & 7u) != 7u) ink_ok = 0;             /* ink must be white */
                if (!paper_safe((u8)((v >> 3) & 7u))) paper_ok = 0;
            }
        }
    }
    sprintf(nm, "id %u: all 768 cells written", id);     check(nm, filled_ok);
    sprintf(nm, "id %u: frame ring is FRAME_ATTR", id);  check(nm, frame_ok);
    sprintf(nm, "id %u: interior ink == white", id);     check(nm, ink_ok);
    sprintf(nm, "id %u: interior paper in safe set", id);check(nm, paper_ok);
}

int main(void)
{
    u8 id;
    for (id = 0; id < IMPL; id++) check_invariants(id);

    /* Determinism: same (id, seed) -> identical buffer. */
    {
        static u8 a[BGPAT_CELLS], b[BGPAT_CELLS];
        bgpat_generate(a, BGPAT_CHECKER, 0x9999u);
        bgpat_generate(b, BGPAT_CHECKER, 0x9999u);
        check("deterministic for (id,seed)", memcmp(a, b, sizeof a) == 0);
    }

    if (failures == 0) { printf("test_bgpat: ALL PASS\n"); return 0; }
    printf("test_bgpat: %d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cc -std=c99 -Wall -Wextra -Werror -Iinclude test/test_bgpat.c src/bgpat.c src/fxtab.c -o build/host/test_bgpat`
Expected: FAIL to **link/compile** — `bgpat.c` does not exist yet (or `bgpat_generate` undefined).

- [ ] **Step 4: Create the implementation** — `src/bgpat.c` (low-noise generators + framework; noisy ids fall through to checker until Task 4):

```c
/*
 * bgpat.c -- see bgpat.h. Integer-only pattern generators. Interior cells use
 * ink=7 (white) so white sprites read on any paper.
 */
#include "bgpat.h"
#include "fxtab.h"

#define ABS8(x) ((x) < 0 ? -(x) : (x))

/* Integer sqrt of a u16 (for concentric rings). */
static u8 isqrt_u16(u16 x)
{
    u16 res = 0u, bit = 1u << 14;
    while (bit > x) bit >>= 2;
    while (bit != 0u) {
        if (x >= res + bit) { x -= res + bit; res = (u16)((res >> 1) + bit); }
        else res >>= 1;
        bit >>= 2;
    }
    return (u8)res;
}

/* Attribute (ink=white) for one INTERIOR cell of pattern `id`. */
static u8 interior_attr(u8 id, u8 r, u8 c, u16 seed)
{
    s8 dr = (s8)((s8)r - 12);     /* centre approx (row 12, col 16) */
    s8 dc = (s8)((s8)c - 16);
    (void)seed;
    switch (id) {
    case BGPAT_CHECKER:
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
    case BGPAT_DIAGONAL:
        return ATTR(0, (((u8)(r + c) >> 1) & 1u) ? 1u : 0u, 7);
    case BGPAT_CIRCLES: {
        u8 ring = isqrt_u16((u16)(dr * dr + dc * dc));
        return ATTR(0, ((ring >> 1) & 1u) ? 1u : 0u, 7);
    }
    case BGPAT_LATTICE:
        return ATTR(0, ((r % 3u == 0u) || (c % 3u == 0u)) ? 1u : 0u, 7);
    default:
        /* noisy ids implemented in Task 4 */
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
    }
}

void bgpat_generate(u8 *cells, u8 id, u16 seed)
{
    u8 r, c;
    u16 i = 0u;
    for (r = 0; r < BGPAT_ROWS; r++) {
        for (c = 0; c < BGPAT_COLS; c++, i++) {
            if (r == 0u || r == 23u || c == 0u || c == 31u) {
                cells[i] = (u8)BGPAT_FRAME_ATTR;
            } else {
                cells[i] = interior_attr(id, r, c, seed);
            }
        }
    }
}
```

- [ ] **Step 5: Register the test** — in `test/run.sh`, after the `test_fxtab` block, add:

```sh
$CC $CFLAGS "$ROOT/test/test_bgpat.c" "$ROOT/src/bgpat.c" "$ROOT/src/fxtab.c" -o "$OUT/test_bgpat"
"$OUT/test_bgpat"
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `./test/run.sh`
Expected: `test_bgpat: ALL PASS`, then `ALL HOST TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add include/bgpat.h src/bgpat.c test/test_bgpat.c test/run.sh
git commit -m "feat: bgpat low-noise generators (checker/diagonal/circles/lattice)"
```

---

### Task 4: `bgpat` noisy generators (diamonds, vbands, plasma, starfield)

**Files:**
- Modify: `src/bgpat.c` (replace the `default:` cases with real generators; add a hash helper)
- Modify: `test/test_bgpat.c:` (raise `IMPL` to cover all ids)

**Interfaces:**
- Consumes: `fx_sin` (plasma).
- Produces: full `bgpat_generate` coverage for ids 0..7.

- [ ] **Step 1: Raise the test coverage** — in `test/test_bgpat.c` change:

```c
#define IMPL 4   /* number of pattern ids implemented so far (grows per task) */
```
to:
```c
#define IMPL BGPAT_COUNT   /* all patterns implemented */
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./test/run.sh`
Expected: FAILs for ids 4..7 are unlikely (the `default` returns a *valid* checker), so instead verify the **distinctness** gap: the noisy ids currently equal checker. Add this distinctness check to `main()` in `test/test_bgpat.c` (before the determinism block):

```c
    /* Noisy patterns must not be identical to the checker fallback. */
    {
        static u8 ck[BGPAT_CELLS], nz[BGPAT_CELLS];
        u8 id; char nm[48];
        bgpat_generate(ck, BGPAT_CHECKER, 0x55u);
        for (id = BGPAT_NOISY_FIRST; id < BGPAT_NOISY_FIRST + BGPAT_NOISY_COUNT; id++) {
            bgpat_generate(nz, id, 0x55u);
            sprintf(nm, "noisy id %u differs from checker", id);
            check(nm, memcmp(ck, nz, sizeof ck) != 0);
        }
    }
```
Run: `./test/run.sh`
Expected: FAIL — `noisy id 4..7 differs from checker` (they still fall through to checker).

- [ ] **Step 3: Implement the noisy generators** — in `src/bgpat.c`, add the hash helper above `interior_attr`:

```c
/* Cheap deterministic hash -> 0..255, for the static starfield. */
static u8 hash3(u8 r, u8 c, u16 seed)
{
    u16 h = (u16)((u16)(r * 61u) + (u16)(c * 113u) + seed);
    h ^= (u16)(h << 7);
    h ^= (u16)(h >> 9);
    h = (u16)(h * 0x2545u);
    return (u8)(h >> 8);
}
```

Then replace the `default:` case in `interior_attr` with the four real cases (keep a final `default` for safety):

```c
    case BGPAT_DIAMONDS: {
        u8 dm = (u8)(ABS8(dr) + ABS8(dc));
        u8 band = (u8)((dm >> 1) & 1u);
        u8 accent = (u8)(((dm >> 1) % 4u) == 0u);
        return ATTR(0, accent ? 3u : (band ? 1u : 0u), 7);   /* magenta accent rings */
    }
    case BGPAT_VBANDS: {
        u8 k = (u8)((c / 2u) % 3u);
        u8 p = (k == 0u) ? 0u : (k == 1u) ? 1u : 3u;          /* black/blue/magenta cols */
        return ATTR(0, p, 7);
    }
    case BGPAT_PLASMA: {
        s16 v = (s16)fx_sin[(u8)((u8)(c * 10u) + (u8)seed)]
              + (s16)fx_sin[(u8)(r * 12u)]
              + (s16)fx_sin[(u8)((u8)((r + c) * 7u) + (u8)(seed >> 3))];
        u8 q = (u8)((v + 384) / 192);                          /* 0..3 */
        static const u8 pp[4] = { 0u, 1u, 3u, 2u };            /* black/blue/magenta/red */
        if (q > 3u) q = 3u;
        return ATTR(0, pp[q], 7);
    }
    case BGPAT_STARFIELD: {
        u8 h = hash3(r, c, seed);
        if (h < 8u)  return ATTR(1, 5u, 7);                    /* bright cyan star  */
        if (h < 16u) return ATTR(1, 1u, 7);                    /* bright blue star  */
        if (h < 22u) return ATTR(1, 3u, 7);                    /* bright magenta    */
        return ATTR(0, 0u, 7);                                 /* black space       */
    }
    default:
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
```

Also remove the now-unused `(void)seed;` line at the top of `interior_attr` (seed is now used).

- [ ] **Step 4: Run the test to verify it passes**

Run: `./test/run.sh`
Expected: `test_bgpat: ALL PASS` — invariants hold for all 8 ids and each noisy id differs from checker.

- [ ] **Step 5: Commit**

```bash
git add src/bgpat.c test/test_bgpat.c
git commit -m "feat: bgpat noisy generators (diamonds/vbands/plasma/starfield)"
```

---

### Task 5: `bgpat_pick` — tier selection with immediate-repeat avoidance

**Files:**
- Modify: `src/bgpat.c` (add `bgpat_pick`)
- Modify: `test/test_bgpat.c` (add selection tests)

**Interfaces:**
- Produces: `u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd);`

- [ ] **Step 1: Add the failing test** — in `test/test_bgpat.c` `main()`, before the determinism block:

```c
    /* bgpat_pick stays in range and avoids the previous id. */
    {
        u8 rnd, prev, id; int in_range = 1, no_repeat = 1;
        for (prev = BGPAT_NOISY_FIRST; prev < BGPAT_NOISY_FIRST + BGPAT_NOISY_COUNT; prev++) {
            for (rnd = 0; rnd < 255u; rnd++) {
                id = bgpat_pick(BGPAT_NOISY_FIRST, BGPAT_NOISY_COUNT, prev, rnd);
                if (id < BGPAT_NOISY_FIRST || id >= BGPAT_NOISY_FIRST + BGPAT_NOISY_COUNT) in_range = 0;
                if (id == prev) no_repeat = 0;
            }
        }
        check("pick stays in tier range", in_range);
        check("pick avoids immediate repeat", no_repeat);
        /* prev=0xFF allows any id in range. */
        check("pick with prev=0xFF in range",
              bgpat_pick(BGPAT_LOWNOISE_FIRST, BGPAT_LOWNOISE_COUNT, 0xFFu, 200u) < BGPAT_LOWNOISE_COUNT);
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./test/run.sh`
Expected: FAIL to link — `bgpat_pick` undefined.

- [ ] **Step 3: Implement** — add to `src/bgpat.c`:

```c
u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd)
{
    u8 id = (u8)(first + (rnd % count));
    if (count > 1u && id == prev) {
        id = (u8)(first + (u8)(((u8)(id - first) + 1u) % count));
    }
    return id;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `./test/run.sh`
Expected: `test_bgpat: ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/bgpat.c test/test_bgpat.c
git commit -m "feat: bgpat_pick tier selection with no immediate repeat"
```

---

### Task 6: Wire the background into `main.c` (target-only) + build registration

Replace the hardcoded checker with the `bg_cells` table, add the selection state machine, and verify on hardware (ZEsarUX). This task has no host test (it's the SCLD/loop layer); it is gated by a clean `./build.sh` and a ZEsarUX visual check.

**Files:**
- Modify: `build.sh:24-27` (add the two new sources)
- Modify: `src/main.c` (header comments, includes, `bg_attr`, `bg_paint`, new selection helpers, three call sites)

**Interfaces:**
- Consumes: `bgpat_generate`, `bgpat_pick`, ids/tier macros (from `bgpat.h`); `SCLD_ATTRS_A/B`, `SCLD_ATTRS_LEN` (from `scld.h`); `memcpy` (`<string.h>`, already included).

- [ ] **Step 1: Register the new sources in `build.sh`** — change the source list (currently `build.sh:24-27`) to add `src/bgpat.c src/fxtab.c`:

```sh
zcc +zx -SO3 -clib=sdcc_iy -iquote"$ROOT/include" \
    src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
    src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
    src/rng.c src/score.c src/sfx.c src/hud.c src/bgpat.c src/fxtab.c \
    src/blit.asm src/enemy_update.asm src/collide.asm src/sfx.asm \
    -o build/game -create-app
```

- [ ] **Step 2: Add the include** — near the other includes in `src/main.c` (e.g. after `#include "hud.h"` at `main.c:42`):

```c
#include "bgpat.h"        /* per-run / per-wave background shapes */
```

- [ ] **Step 3: Delete the stale comments** (reverted-feature landmines). Remove the references to "big attribute-cell digits", `score_cell_attr`, and `hud_paint_background`:
  - In the `#include "hud.h"` line (`main.c:42`), drop `score_cell_attr` from the comment so it reads e.g. `/* ATTR macro, put_attr, HUD widgets */`.
  - In the file header block (`main.c:~18-28`) delete the sentences describing the score as a "big attribute-digit background" and the cell-restore-asks-the-HUD claim.
  - At `main.c:~598` delete the `the HUD reads g.score for its big-attribute-digit background` comment.
  (These describe code removed in `fc371b1`; leaving them misleads future readers.)

- [ ] **Step 4: Replace `bg_attr` and `bg_paint`** — replace the current bodies (`main.c:120-146`) with the table-backed versions and the selection state. Replace the whole `bg_attr`/`bg_paint` block with:

```c
/* The per-run background lives in this RAM table (frame ring + interior). It is
 * generated once per game (or per wave in noisy mode) and block-copied into both
 * attribute blocks by bg_paint(). bg_attr() is the single source of truth that
 * fx_render() and the spawn telegraph read to restore a cell. */
static u8  bg_cells[BGPAT_CELLS];
static u8  bg_mode;        /* BG_MODE_STATIC or BG_MODE_WAVE                  */
static u8  bg_cur_id;      /* current pattern id                             */
static u16 bg_rng = 0xC0DEu;   /* private LCG so we never perturb game rng    */

#define BG_MODE_STATIC   0u
#define BG_MODE_WAVE     1u
#define BG_NOISY_PERCENT 50u   /* odds a run uses the per-wave noisy tier     */

static u8 bg_rand(void)
{
    bg_rng = (u16)(bg_rng * 25173u + 13849u);
    return (u8)(bg_rng >> 8);
}

static u8 bg_attr(u8 row, u8 col)
{
    return bg_cells[(u16)row * 32u + col];
}

static void bg_paint(void)
{
    memcpy((u8 *)SCLD_ATTRS_A, bg_cells, SCLD_ATTRS_LEN);
    memcpy((u8 *)SCLD_ATTRS_B, bg_cells, SCLD_ATTRS_LEN);
}

/* Choose this run's background. ~BG_NOISY_PERCENT of runs use the noisy tier
 * (re-rolled each wave by bg_next_wave); the rest pick one low-noise shape and
 * keep it. Generates bg_cells but does NOT paint (caller paints). */
static void bg_new_run(void)
{
    if (bg_rand() < (u8)((BG_NOISY_PERCENT * 256u) / 100u)) {
        bg_mode   = BG_MODE_WAVE;
        bg_cur_id = bgpat_pick(BGPAT_NOISY_FIRST, BGPAT_NOISY_COUNT, 0xFFu, bg_rand());
    } else {
        bg_mode   = BG_MODE_STATIC;
        bg_cur_id = bgpat_pick(BGPAT_LOWNOISE_FIRST, BGPAT_LOWNOISE_COUNT, 0xFFu, bg_rand());
    }
    bgpat_generate(bg_cells, bg_cur_id, bg_rng);
}

/* At a wave boundary, re-roll + repaint when in noisy mode; no-op otherwise. */
static void bg_next_wave(void)
{
    if (bg_mode != BG_MODE_WAVE) {
        return;
    }
    bg_cur_id = bgpat_pick(BGPAT_NOISY_FIRST, BGPAT_NOISY_COUNT, bg_cur_id, bg_rand());
    bgpat_generate(bg_cells, bg_cur_id, bg_rng);
    bg_paint();
}
```

- [ ] **Step 5: Seed the first run at startup** — at `main.c:~638`, immediately before the first `bg_paint();` (currently `main.c:639`), add the run roll. The sequence becomes:

```c
    game_new(&g);                     /* wave 1, score 0, START_LIVES/SHIELDS   */
    bg_new_run();                     /* pick this run's background shape       */
    bg_paint();                       /* copy the chosen pattern into both attrs */
```

- [ ] **Step 6: Re-roll when leaving the game-over screen** — in the `if (g.lives == 0u)` block (`main.c:~785-793`), after the `if (game_over_screen(...) == 0u) game_resume_from_wave(...); else game_new(...);`, add a roll so BOTH outcomes get a fresh shape, before the shared `bg_paint()` at `main.c:~799`:

```c
                        if (game_over_screen(&g, death_wave) == 0u) {
                            game_resume_from_wave(&g, death_wave);  /* score 0, keep wave */
                        } else {
                            game_new(&g);                            /* wave 1     */
                        }
                        bg_new_run();    /* fresh background for the new/resumed game */
```
(The in-run respawn `else` branch is left unchanged; the shared `bg_paint()` at `:799` repaints the existing `bg_cells`.)

- [ ] **Step 7: Re-roll on wave advance** — in the wave-advance block (`main.c:~757-765`, where `g.wave++` precedes `enemies_spawn`), add `bg_next_wave();` right after the new wave is spawned, e.g.:

```c
        g.wave++;
        enemies_spawn(&enemies, g.wave);
        bg_next_wave();                  /* noisy mode: new shape this wave    */
        spawn_timer = TELEGRAPH_FRAMES;  /* telegraph the new wave             */
```
(Match the exact existing statements; only insert the `bg_next_wave();` line. It regenerates + repaints during the telegraph window when enemies are inert, so the ~1–2 frame cost is hidden.)

- [ ] **Step 8: Build the target**

Run: `./build.sh`
Expected: ends with `build/game.tap` listed; no warnings/errors. (If `memcpy` warns about an implicit decl, confirm `#include <string.h>` is present near the top of `main.c` — it is at `main.c:45`.)

- [ ] **Step 9: Run host tests (regression)**

Run: `./test/run.sh`
Expected: `ALL HOST TESTS PASSED` (main.c is not host-built; this confirms the module changes didn't break anything).

- [ ] **Step 10: Visual verification in ZEsarUX**

Run: `./run-zesarux.sh`
Confirm by observation:
- The arena floor is a shape from the set (checker/diagonal/circles/lattice, or a noisy one), not always the old checker.
- White ship + enemies are clearly readable over the background (no white-on-white).
- The magenta HUD frame, hearts, shields, score, and dash dot are intact.
- Gameplay is smooth (no new stutter) — the background never changes mid-wave in static mode.
- Start a few new games (die → Q new game) to see the shape change between runs.
- If a run is in noisy mode, advancing a wave swaps the shape during the telegraph (no visible hitch).

- [ ] **Step 11: Commit**

```bash
git add build.sh src/main.c
git commit -m "feat: per-run/per-wave static background shapes (bgpat) in the arena"
```

---

## Self-Review

**Spec coverage (Component A, §4 of the spec):**
- `bg_cells` table + `bg_attr` lookup + `bg_paint` memcpy → Task 6. ✓
- 8 generators (4 low-noise, 4 noisy incl. plasma via fxtab) → Tasks 3–4. ✓
- Readability invariant (ink=white, paper!=white, safe set) → enforced by generators (Tasks 3–4), tested in Task 3+ (`check_invariants`). ✓
- Frame ring = `ATTR(1,3,7)` untouched → generator + test. ✓
- Determinism `(id,seed)` → Task 3 test. ✓
- Selection: per-run tier roll, per-wave re-roll, respawn keeps pattern, resume re-rolls → Task 5 (`bgpat_pick`) + Task 6 (`bg_new_run`/`bg_next_wave` + 3 call sites). ✓
- `ATTR` macro move → Task 1. ✓
- Build/test wiring → Task 2/3 (`run.sh`), Task 6 (`build.sh`). ✓
- Stale-comment cleanup → Task 6 Step 3. ✓
- Zero per-frame cost (static) → no per-frame work added; only generate (once/run or once/wave) + memcpy on existing paint sites. ✓

Out of Phase-1 scope (own plans): game-over plasma (§5), title globe (§6) — `fxtab.fx_mul` is built now but unused until Phase 3, intentionally.

**Placeholder scan:** no TBD/TODO; every code step shows complete code. ✓

**Type consistency:** `bgpat_generate(u8*,u8,u16)`, `bgpat_pick(u8,u8,u8,u8)`, `bg_attr(u8,u8)->u8`, `bg_paint(void)`, `bg_new_run(void)`, `bg_next_wave(void)` used consistently across Tasks 3–6. `BGPAT_*` macro names match between header (Task 3) and uses (Tasks 4–6). `fx_sin`/`fx_mul` signatures match between Task 2 and the plasma generator (Task 4). ✓
