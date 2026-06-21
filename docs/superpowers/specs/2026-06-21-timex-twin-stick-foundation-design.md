# Twin-Stick Shooter — Foundation (Engine Vertical Slice) Design

**Date:** 2026-06-21
**Target hardware:** Timex TC2048 (primary). Z80A @ 3.5 MHz, 48 KB RAM.
**Toolchain:** Z88DK (`zcc` v23854, Oct 2025) at `~/Programowanie/z88dk`; Fuse emulator at `/Applications/Fuse.app`.
**Status:** Approved foundation scope — first of several specs. Later milestones (full enemy roster, collision, score, waves, sound, polish) each get their own spec → plan → implementation cycle.

---

## 1. Purpose & priorities

Build the **engine vertical slice** for a Geometry-Wars-inspired twin-stick shooter. The slice deliberately exercises the *hardest engine risks first* — flicker-free double buffering, a steady 50 Hz loop, vector drawing, and screen wrap — before any real gameplay content is layered on top.

**Stated priority (from the user):** *SMOOTH and FAST* gameplay over graphical richness. Every design choice below favours smoothness and frame-rate over colour or visual complexity.

Non-goals for this slice: colour, collision, scoring, waves, sound, enemy AI, menus beyond a single welcome screen.

---

## 2. Key decisions (and why)

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | **Standard 256×192 video mode, run monochrome** (all attributes set once to white-on-black, never touched again). | Pixel-plot cost is the same in every Spectrum/Timex mode; colour is not the priority. Uniform attributes ⇒ colour clash *cannot occur* and ⇒ zero per-frame attribute work. Clean white-on-black "vector" look. |
| D2 | **Timex hardware double buffering** via SCLD page-flip (`OUT (0xFF)`): screen A at `0x4000`, screen B at `0x6000`. | The TC2048 has two full display files and can flip which is shown. This is the one feature a stock 48K Spectrum *cannot* do, and it is the single biggest contributor to "smooth" (flicker-free, tear-free). Hi-colour and hi-res modes are rejected precisely because they consume both pages for one image and so forbid page-flip double buffering. |
| D3 | **Hybrid drawing:** enemies/bullets/particles = pre-rendered sprites or pixels; **player ship = live vector shape**. | True runtime vector line-drawing for *many* objects is too expensive on a 3.5 MHz Z80. Pre-rendered blits are cheap and plentiful; a single live-vector ship is trivially affordable and gives the player real "feel" (ship rotates to face aim). |
| D4 | **Concrete Timex control scheme** (see §6) replaces the prompt's abstract Mode A/B/C for now. | The user specified the exact Timex scheme: Kempston move + Kempston-fire-in-facing + QWEADZXC 8-direction fire. Sinclair modes deferred behind the same `intent_t` abstraction. |
| D5 | **Pac-Man-style toroidal wrap** on both axes (no solid arena border). | User requirement. Objects near an edge are drawn twice (`pos` and `pos ± screen size`) with line/sprite clipping for the seamless look. |
| D6 | **Full-clear-and-redraw-all** per back buffer each frame (not dirty-rectangles) — for this slice only. | With few objects plus statics (wall, stationary enemies) plus wrap-duplicates, dirty-rect bookkeeping is error-prone (a moving object's erase would punch holes in statics). Full clear + redraw is the *simplest correct* solution and fits the frame budget at this object count. Dirty-rect optimisation is deferred to the spec that introduces the large enemy swarm. |
| D7 | **No floating point, no malloc, no recursion.** Integer math, fixed-size arrays, object pools, precomputed tables. | Project hard constraint. Ship rotation uses a precomputed integer rotated-vertex table (no runtime trig). |
| D8 | **Program ORG at `0x8000`**, leaving `0x6000–0x7AFF` reserved for the screen-B back buffer. | The back buffer lives in the middle of the normal program load area; the linker must not emit anything there. |

---

## 3. Scope of this slice

**In scope**
- Build system producing a `.tap` that loads and runs on Fuse-as-TC2048.
- Timex video init: set monochrome attributes once on both screens; double-buffer page-flip.
- 50 Hz double-buffered game loop with a `TITLE → PLAYING` state machine.
- **Welcome screen** explaining the controls (static, drawn with ROM text to `0x4000`); press fire to start.
- Input abstraction reading Kempston joystick + QWEADZXC keys into one `intent_t`.
- **Player ship**: live vector shape, faces 1 of 8 directions, moves smoothly (1–2 px/frame), wraps toroidally.
- **Thruster particles** *(simple, optional)*: small fixed pool, spawned behind the ship when moving, single-pixel, drift + die after a few frames.
- **1–2 stationary enemies**: pre-rendered 8×8 sprites (minimal sprite blit — pulled forward to de-risk the swarm system).
- **Bullets, no collision**: fixed-size pool; fire in facing dir (Kempston button) or 8 dirs (QWEADZXC); travel, wrap/despawn at edge; drawn as dots/short dashes. Bullets pass through enemies (collision is a later milestone).
- *(Optional)* a static **wall** element (drawn line/rectangle, no collision yet).

**Out of scope (future specs)**
Collision detection, score/HUD, enemy AI/movement, multiple enemy types, waves & difficulty scaling, sound effects, hi-colour polish, Sinclair control modes, dirty-rectangle rendering optimisation, asm blitter (introduced only once measurement justifies it).

---

## 4. Module architecture

Subset of the prompt's suggested layout. Boundary rule: **only `video.c` knows port `0xFF` and the `0x4000`/`0x6000` addresses.** Everything else receives a "back-buffer base address" parameter, so the engine is buffer-agnostic and the pure-logic parts are host-testable.

```
src/main.c     init; hand off to game loop
src/game.c     50 Hz loop, TITLE/PLAYING state machine, frame orchestration
src/video.c    Timex SCLD mode set, attribute fill, page-flip, fast back-buffer clear   [Timex-specific core]
src/render.c   integer clipped line draw; rotated-vertex player draw; pixel plot; 8x8 OR-blit   [drawing primitives]
src/input.c    read Kempston + QWEADZXC -> unified intent_t
src/player.c   player state, movement, aim/facing, wrap, calls render
src/bullet.c   bullet pool: spawn, update, wrap/despawn, draw
src/particle.c thruster particle pool (optional)
src/enemy.c    stationary enemy placement + draw (minimal)

include/video.h  render.h  input.h  player.h  bullet.h  particle.h  enemy.h  game.h
```

Each unit has one clear purpose, a small interface, and (for the logic-only ones) host-compilable behaviour.

---

## 5. Memory map (must be correct on real hardware)

```
0x4000–0x57FF  Screen A bitmap   (6144 b)   ─┐ shown when page = 0
0x5800–0x5AFF  Screen A attrs    ( 768 b)   ─┘ set ONCE to white-on-black
0x5B00–0x5FFF  system area (printer buffer, system variables)
0x6000–0x77FF  Screen B bitmap   (6144 b)   ─┐ shown when page = 1   ← RESERVED back buffer
0x7800–0x7AFF  Screen B attrs    ( 768 b)   ─┘ set ONCE to white-on-black
0x7B00–0x7FFF  (small gap)
0x8000–0xFFFF  program code + data (~32 KB)  ← ORG here; linker must emit nothing below 0x8000
```

**Risk:** if the linker places code/data inside `0x6000–0x7AFF`, the back buffer corrupts it (and vice-versa). Milestone 1 verifies the map in Fuse before anything else is built on top.

---

## 6. Control scheme (Timex twin-stick)

True twin-stick: move and shoot are independent.

- **Kempston joystick** → 8-way movement. (`in_JoyKempston()`, confirmed present in Z88DK.)
- **Kempston FIRE button** → shoot in the **facing** direction (the ship faces the direction it is moving).
- **Keyboard Q W E / A · D / Z X C** → shoot in 8 absolute directions:
  ```
  Q W E      NW  N  NE
  A · D  =    W  ·   E      (centre / S = no shot)
  Z X C      SW  S  SE
  ```

Unified intent produced each frame:

```c
typedef struct {
    int8_t  move_dx, move_dy;   /* -1 / 0 / +1  : movement axis (Kempston)        */
    int8_t  aim_dx,  aim_dy;    /* -1 / 0 / +1  : shoot direction (QWEADZXC, or   */
                                /*                facing dir when Kempston-fire)   */
    uint8_t fire;               /* non-zero if a shot is requested this frame      */
} intent_t;
```

`input.c` reads raw ports/keys and fills `intent_t`. The facing direction is derived from the most recent non-zero `move` axis (so the ship keeps pointing where it last moved when the stick is centred). Deferred: Sinclair 1/2 modes — they slot into the same struct later.

---

## 7. The 50 Hz double-buffered loop

```
state = TITLE

TITLE:
    draw welcome/controls text to screen A (0x4000), standard mode, page 0 shown
    wait for fire  ->  state = PLAYING, init game

PLAYING (per frame):
    HALT                          ; sync to the 50 Hz frame interrupt (top of vblank)
    OUT (0xFF), shown_page        ; flip: display the buffer we just finished  -> tear-free
    back = the OTHER buffer base

    read input            -> intent
    update player(intent)          ; integer math, wrap
    update bullets / particles     ; integer math, wrap/despawn

    clear(back)                    ; fast stack-PUSH fill to black (whole buffer)
    draw wall(back)                ; static, if present
    draw enemies(back)             ; static sprites
    draw particles(back)
    draw bullets(back)
    draw player(back)              ; vector lines (with wrap-duplicates + clipping)

    swap(shown_page <-> back)
```

The page-flip happens right after `HALT` (during vblank) so the buffer becomes visible cleanly. All drawing for frame N must finish before the next `HALT`; an overrun degrades that single frame to 25 Hz (measurable, recoverable), it does not crash.

---

## 8. Rendering primitives (`render.c`) — no floating point

- **`plot(base, x, y)`** — set one pixel in the given buffer (handles Spectrum interleaved addressing).
- **`draw_line(base, x0,y0,x1,y1)`** — integer Bresenham, **clipped** to 0–255 × 0–191. Clipping is required for wrap-duplicate drawing near edges. First candidate for hand-written asm *iff* measurement demands it.
- **`blit8(base, x, y, *sprite)`** — draw an 8×8 sprite by OR-ing onto the (black) background; clipped at edges. No mask needed on black. Used for stationary enemies; foundation of the future swarm blitter.
- **`clear(base)`** — fast full-buffer clear to black via stack-`PUSH` fill (~34,000 T-states; see §11).
- **Player ship draw** — the ship is a small polygon (e.g. 3–4 vertex arrowhead). To rotate toward facing **without runtime trig**, an offline-generated table holds the rotated vertex offsets for **N = 8** directions (matching 8-way input) as signed integers:
  ```c
  /* generated offline; signed pixel offsets from ship centre */
  static const int8_t ship_verts[8][NUM_VERTS][2] = { ... };
  ```
  Runtime: pick `ship_verts[facing]`, add ship `(x,y)`, draw the connecting lines (clipped, with wrap-duplicates). Smoother rotation later is a pure data change (N = 16/32 + step-toward-target), no code change.

---

## 9. Entities

All entities use **fixed-size arrays / object pools**, integer positions in screen space (x: 0–255, y: 0–191), toroidal wrap.

- **Player** (`player.c`): `{ x, y, facing }`. Movement: `move` axis → velocity (small px/frame); position wraps. `facing` ← last non-zero move axis (1 of 8). Drawn as vector ship (§8) with wrap-duplicates.
- **Bullets** (`bullet.c`): pool `bullet_t bullets[MAX_BULLETS]` (e.g. 16). `{ x, y, dx, dy, active }`. Spawn on `fire` in `aim` direction (8-way unit velocity scaled to bullet speed). Update: advance, wrap or despawn at edge (decide one — see §13). Drawn as single pixel or 2-px dash. **No collision.**
- **Particles** (`particle.c`, optional): pool `particle_t particles[MAX_PARTICLES]` (e.g. 16). `{ x, y, dx, dy, life }`. Spawn 1–2 behind the ship when moving; drift; `life--` each frame, despawn at 0. Single pixel.
- **Enemies** (`enemy.c`): 1–2 **stationary** entries `{ x, y, alive }`, drawn as 8×8 sprites via `blit8`. No AI, no collision this slice.
- **Wall** (optional): one or more static line segments / a rectangle drawn each frame into the back buffer. No collision.

---

## 10. Build & run

Starting point (exact flags **finalised empirically in Milestone 1** — per project rule, no invented Z88DK APIs/flags):

```bash
export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"

# Build a Spectrum .tap (TC2048 runs Spectrum software; SCLD reached via OUT 0xFF).
zcc +zx -SO3 -clib=sdcc_iy src/*.c -o build/game -create-app -zorg=0x8000

# Run on Fuse, Timex TC2048 machine model.
/Applications/Fuse.app/Contents/MacOS/Fuse --machine tc2048 build/game.tap
```

Open items to confirm in M1: precise `-clib`/crt (`-startup`) choice, `-create-app` vs `-subtype=…` for `.tap` output, the `-zorg`/reserve mechanism that guarantees nothing lands in `0x6000–0x7AFF`, and Fuse's exact `--machine`/binary path. Getting a known-good build+launch *is* Milestone 1.

---

## 11. Performance budget (honest)

- Frame = **69,888 T-states** @ 3.5 MHz / 50 Hz; usable ≈ 60,000 after interrupt/border overhead.
- **Full clear** of one 6144-byte buffer via stack-`PUSH` ≈ **34,000 T** (≈ half the frame). This is the dominant cost of the full-clear-redraw model.
- Redraw of this slice's content (vector ship ~a few lines + ~16 particles + ~16 bullet pixels + 2 enemy blits) ≈ low thousands of T — comfortably within the remaining ~26,000 T. **The slice holds 50 Hz.**
- **Memory-contention caveat (real hardware):** both display files live in the contended 16 KB bank (`0x4000–0x7FFF`). Writing the back buffer at `0x6000` while the SCLD displays screen A incurs ULA/SCLD contention during the *active* display window, so the real clear/draw cost is ~1.3–1.5× the uncontended figure for work done during display. Mitigation: do as much drawing as possible during the non-contended vblank/border window right after `HALT`, and treat all T-state figures here as uncontended lower bounds to be confirmed with `z88dk-ticks` / Fuse in M1.
- **Scaling note (logged, not hidden):** the full clear cost is fixed regardless of object count, so when a later spec adds 30+ enemies the redraw grows and we reclaim the clear cost by switching to dirty-rectangles and (if measured necessary) an asm blitter. The clear/blit/line routines are the designated asm-optimisation candidates — *after* "make it work → correct → measure".

---

## 12. Testing strategy

- **Host unit tests (TDD lives here):** pure-integer logic — input decode (raw port byte → `intent_t`), movement & wrap math, bullet/particle update, rotated-vertex lookup — compiles with native `cc` on macOS, no emulator, fast red-green-refactor.
- **Emulator integration:** hardware-touching code (SCLD `OUT 0xFF`, screen addressing, double-buffer flip, 50 Hz timing) verified visually in Fuse-as-TC2048; screenshots captured to confirm rendering and the absence of flicker/tearing.
- **Cycle counting:** `z88dk-ticks` for measuring hot paths (clear, line, blit) when optimisation is on the table.

---

## 13. Open items to resolve during planning/M1

1. **Bullet edge behaviour:** wrap toroidally like the ship, or despawn at the edge? (Leaning: despawn — simpler, and a finite bullet range reads well. Confirm in plan.)
2. Exact `zcc`/crt flags and the back-buffer-reserve mechanism (§10).
3. Fuse CLI machine id / binary path for headless screenshot capture (§12).
4. Particle pool size and spawn rate (tune for look vs budget).
5. N = 8 vs a larger rotation table if "animated" should mean smoother spin (data-only change).

---

## 14. Milestone mapping

This slice corresponds to the prompt's **Milestone 1** (build, launch in Fuse, sprite/ship on screen, read keyboard + Kempston) and **Milestone 2** (player movement, arena/loop), plus an early sliver of **Milestone 3** (bullets, no cleanup-by-collision yet). Subsequent specs resume at full Milestone 3+ (collision, score, enemy AI, waves, sound, polish).
