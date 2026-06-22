# Twin-Stick Shooter — Foundation (Engine Vertical Slice) Design

**Date:** 2026-06-21
**Target hardware:** Timex TC2048 (primary). Z80A @ 3.5 MHz, 48 KB RAM.
**Toolchain:** Z88DK (`zcc` v23854, Oct 2025) at `~/Programowanie/z88dk`; Fuse emulator at `/Applications/Fuse.app`.
**Status:** Approved foundation scope — first of several specs. **Revised 2026-06-21 after an independent technical review** that verified the load-bearing claims against the installed z88dk + Timex docs and corrected two blockers (see §2.1). Later milestones (full enemy roster, collision, score, waves, sound, polish) each get their own spec → plan → implementation cycle.

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
| D3 | ~~**Hybrid drawing:** enemies/bullets/particles = pre-rendered sprites or pixels; **player ship = live vector shape**.~~ **✏ REVISED at M1 (§15): EVERYTHING is an 8×8 sprite, including the player. Line/vector drawing deferred.** | True runtime vector line-drawing for *many* objects is too expensive on a 3.5 MHz Z80 — and at M1 we confirmed even *one* vector ship is not worth the line primitive's cost/complexity yet. All-sprites is simpler and uniform. Directional ship = 8 sprite frames (data only). Lines revisited only with a concrete need + measurement (§15). |
| D4 | **Concrete Timex control scheme** (see §6) replaces the prompt's abstract Mode A/B/C for now. | The user specified the exact Timex scheme: Kempston move + Kempston-fire-in-facing + QWEADZXC 8-direction fire. Sinclair modes deferred behind the same `intent_t` abstraction. |
| D5 | **Pac-Man-style toroidal wrap** on both axes (no solid arena border). | User requirement. Objects near an edge are drawn twice (`pos` and `pos ± screen size`) with line/sprite clipping for the seamless look. |
| D6 | ~~**Full-clear-and-redraw-all** per back buffer each frame (not dirty-rectangles).~~ **✏ REVISED at M1 (§15): INCREMENTAL erase+redraw (per-buffer dirty list). Full clear is too slow.** | Measured: a full memset clear of one 6144-byte buffer = **129,034 T = 1.85 frames** — it alone busts the 50 Hz budget. So we erase only what moved and redraw the scene. With SCLD page-flip each buffer is 2 frames stale, so each buffer erases *its own* last-drawn positions (`prev[2][]`). This is the dirty-rect approach, pulled forward out of necessity. |
| D7 | **No floating point, no malloc, no recursion.** Integer math, fixed-size arrays, object pools, precomputed tables. | Project hard constraint. Ship rotation uses a precomputed integer rotated-vertex table (no runtime trig). |
| D8 | **Program ORG at `0x8000`**, leaving `0x6000–0x7AFF` reserved for the screen-B back buffer. | The back buffer lives in the middle of the normal program load area; the linker must not emit anything there. |

---

## 2.1 Verified hardware facts & gotchas (independent review, 2026-06-21)

These were checked against the installed z88dk (`~/Programowanie/z88dk`), the linker map of a test build, and the [WoS Timex reference](https://worldofspectrum.org/faq/reference/tmxreference.htm). The two ⛔ items are real bugs that would have surfaced at M1.

- ⛔ **Interrupts start DISABLED.** The default z88dk newlib crt sets "disable interrupts at start" (`__crt_enable_eidi=0x01`). A Z80 `HALT` with IFF1=0 **never wakes on the frame interrupt → the loop deadlocks.** **Required:** run `im 1; ei` during init *before* the game loop (inline asm / intrinsic). This was the spec's one factual error ("interrupt enabled by default" — false).
- ⛔ **Port `0xFF` bit 6 is a hardware interrupt kill-switch.** On the SCLD, bit 6 disables the 50 Hz timer interrupt and acts as a hardware DI that software `EI` cannot override. Every write to `0xFF` (the page-flip) **must keep bits 6–7 = 0.** Therefore the only legal page-flip bytes are:
  - `OUT (0xFF), 0x00` → display **screen A** (bitmap `0x4000`, attrs `0x5800`)
  - `OUT (0xFF), 0x01` → display **screen B** (bitmap `0x6000`, attrs `0x7800`)
  Never OR in other bits. (Full SCLD mode field, bits 0–2: `000`=std@0x4000, `001`=std@0x6000, `010`=hi-colour, `110`=hi-res.)
- ✅ **SCLD standard-res page-flip 0x4000↔0x6000 is real** and is the genuine TC2048-only capability the design rests on. Fuse's `tc2048` machine model emulates the SCLD, so a plain `+zx` tap reaches it via `OUT 0xFF` — no special build.
- ✅ **Joystick byte is *normalised*, not raw port bits** — masks `FIRE=0x80, RIGHT=0x08, LEFT=0x04, DOWN=0x02, UP=0x01`. **API depends on the clib (verified by compiling):**
  - `sdcc_iy` (newlib — what we use) → `<input.h>` exposes `in_stick_kempston()`, `in_key_pressed(scancode)` (a macro → `_fastcall`), `IN_STICK_*`, `IN_KEY_SCANCODE_*` (pulled from `<input/input_zx.h>` under `__SPECTRUM`).
  - `default` (classic) → the *different* `in_JoyKempston()`, `in_KeyPressed()`. (This is what the review's V4 referenced; we are **not** on classic.)
  - The `IN_STICK_*` bit values equal our `JOY_*` masks, so the pure decode logic is API-independent. `input.c` decodes against the masks, never raw `000FUDLR`.
- ✅ **Header-name clash:** any header of ours named `input.h` on the include path **shadows z88dk's `<input.h>`** (SDCC's `-iquote` still searches it for angle includes). Our input-module interface is therefore named **`controls.h`** (implementation stays `src/input.c`).
- ✅ **SDCC z80 crashes on struct-return-by-value** (`gen.c` SIGSEGV when returning a struct that is itself a function's struct return). **Rule for the whole codebase: pass structs via out-pointers, never return them by value.** (`make_intent`/`input_read` take `intent_t *out`.) This is also faster on Z80 (no struct byte-copy).
- ✅ **Memory map confirmed by the linker map:** `CRT_ORG_CODE=$8000`, all code/data/bss/stdio-heap in `$8000–$94C2`, **nothing below 0x8000 and nothing in 0x6000–0x7AFF**, stack `__register_sp=$FF58` (top of RAM, grows down — cannot reach the back buffer). D8 needs *no* `-zorg` override; 0x8000 is the default.

---

## 3. Scope of this slice

**In scope**
- Build system producing a `.tap` that loads and runs on Fuse-as-TC2048.
- Timex video init: set monochrome attributes once on both screens; double-buffer page-flip.
- 50 Hz double-buffered game loop with a `TITLE → PLAYING` state machine.
- **Welcome screen** explaining the controls (static, drawn via z88dk's console driver to screen A at `0x4000`; *not* the ROM `rst 0x10` routine); press fire to start. On TITLE→PLAYING, explicitly reset both screens' attributes and the `0xFF` mode register to known values so the console driver's cursor/attr state can't bleed into gameplay (S5).
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
src/input.c    read Kempston + QWEADZXC -> unified intent_t  (interface in controls.h, NOT input.h -- §2.1)
src/player.c   player state, movement, aim/facing, wrap, calls render
src/bullet.c   bullet pool: spawn, update, wrap/despawn, draw
src/particle.c thruster particle pool (optional)
src/enemy.c    stationary enemy placement + draw (minimal)

include/video.h  render.h  controls.h  player.h  bullet.h  particle.h  enemy.h  game.h
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

**Risk:** if the linker places code/data inside `0x6000–0x7AFF`, the back buffer corrupts it (and vice-versa). *Verified clear* in the M1 test build (see §2.1) — the default ORG already leaves this region free.

**SCLD display register — port `0xFF` (the only two bytes we write):**

| Byte out | Mode | Displayed bitmap / attrs | Bits 6–7 |
|----------|------|--------------------------|----------|
| `0x00` | standard | screen A `0x4000` / `0x5800` | **must be 0** |
| `0x01` | standard | screen B `0x6000` / `0x7800` | **must be 0** |

Bit 6 set would disable the frame interrupt (§2.1) and hang the `HALT` loop, so we never write anything but `0x00`/`0x01`.

---

## 6. Control scheme (Timex twin-stick)

True twin-stick: move and shoot are independent.

- **Kempston joystick** → 8-way movement. (`in_stick_kempston()`, confirmed present; reads port `$1F` and returns the **normalised** masks `JOY_UP/DOWN/LEFT/RIGHT/FIRE`, *not* raw port bits — verified §2.1/§15.)
- **Cursor (Protek) joystick → 8-way movement + fire** *(added at M1)*: keys **5=left, 6=down, 7=up, 8=right, 0=fire**. Read as a normalised `JOY_*` byte and **OR-merged with the Kempston byte**, so it flows through the same `make_intent()` with no logic change; `0` sets `JOY_FIRE` → shoots in the facing direction. This keeps the game fully playable with **no joystick hardware** — important because macOS Fuse's HID/gamepad path (`IOCreatePlugInInterfaceForService`) frequently fails (§15.5).
- **Kempston / cursor FIRE button** → shoot in the **facing** direction (the ship faces the direction it is moving).
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

`input.c` reads the normalised joystick byte + decoded keys and fills `intent_t`. The decode logic (joystick masks → `move`, key set → `aim` direction) is **pure and host-tested**; the actual `in_JoyKempston()` / keyboard reads are a thin target-only wrapper around that logic. The facing direction is derived from the most recent non-zero `move` axis (so the ship keeps pointing where it last moved when the stick is centred). Deferred: Sinclair 1/2 modes — they slot into the same struct later.

---

## 7. The 50 Hz double-buffered loop

```
init (ONCE, before any HALT):
    im 1 ; ei                      ; CRITICAL: crt starts with DI -> HALT would hang (§2.1)
    set screen A & B attrs = white-on-black
    OUT (0xFF), 0x00               ; show screen A, bits 6-7 = 0

state = TITLE

TITLE:
    draw welcome/controls text to screen A (0x4000), standard mode, page 0 shown
    wait for fire  ->  reset attrs + mode reg (S5); state = PLAYING, init game

PLAYING (per frame):  shown_page in {0x00, 0x01}
    HALT                          ; sync to the 50 Hz frame interrupt (top of vblank)
    OUT (0xFF), shown_page         ; flip: display the buffer just finished (ONLY 0x00/0x01!)
    back = the OTHER buffer base    ; (shown 0x00 -> draw 0x6000 ; shown 0x01 -> draw 0x4000)

    read input            -> intent
    update player(intent)          ; integer math, wrap
    update bullets / particles     ; integer math, wrap/despawn

    clear(back)                    ; fast stack-PUSH fill to black (whole buffer)
    draw wall(back)                ; static, if present
    draw enemies(back)             ; static sprites
    draw particles(back)
    draw bullets(back)
    draw player(back)              ; vector lines (with wrap-duplicates + clipping)

    shown_page ^= 0x01             ; next frame, show what we just drew
```

The page-flip happens right after `HALT` (during vblank) so the buffer becomes visible cleanly. The OUT byte is *only ever* `0x00` or `0x01` (§2.1 — bit 6 would kill the interrupt). All drawing for frame N must finish before the next `HALT`; an overrun degrades that single frame to 25 Hz (measurable, recoverable), it does not crash. *(Tear-free-ness of flip-right-after-HALT to be visually confirmed in Fuse at M1 — U3.)*

---

## 8. Rendering primitives (`render.c`) — no floating point

> **Note (S2):** z88dk's pixel-address helpers (`zx_pxy2saddr` etc.) are hardcoded to the `0x4000` screen (the only alternative is the 128K shadow screen at `0xC000`, *not* Timex's `0x6000`). So we **cannot** reuse them for the back buffer — `render.c` computes the Spectrum interleaved address itself from a `base` parameter (`0x4000` or `0x6000`). This is consistent with "only video.c/render.c know addresses," it just means we own the address math (and it's a prime asm candidate).

- **`plot(base, x, y)`** — set one pixel in the given buffer, computing the interleaved screen address from `base` ourselves (no library helper for a `0x6000` screen).
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
# VERIFIED working (test build): sdcc_iy clib, default ORG already = 0x8000 (no -zorg needed).
# -iquote<abs path>/include puts our headers on the quote-include path (use an
# ABSOLUTE path, attached form). Our headers use distinct names (controls.h, not
# input.h) so they don't shadow z88dk system headers (§2.1).
zcc +zx -SO3 -clib=sdcc_iy -iquote"$PWD/include" src/*.c -o build/game -create-app

# Run on Fuse, Timex TC2048 machine model.
# CORRECTED at M1 SCLD smoke test: this macOS Fuse build's machine id is `2048`,
# NOT `tc2048` (the earlier spec value made Fuse abort: "Machine id 'tc2048'
# unknown"). The binary's machine-id strings are 2048 / 2068 / ts2068 / pentagon.
# `--auto-load` is also rejected by this build at runtime (auto-load is on by
# default regardless), so pass only --machine + --tape.
/Applications/Fuse.app/Contents/MacOS/Fuse --machine 2048 --tape build/game.tap
```

Notes (verified in the M1 test build): `-clib=sdcc_iy` works; classic `default` clib chokes on empty `for(;;){}` bodies (sccz80 optimiser) — use a non-empty idle or `sdcc_iy`. `-create-app` emits the `.tap` (default `+zx` subtype). `-zorg` is **not** needed (ORG defaults to 0x8000). This **GUI** Fuse build exposes **no headless screenshot/SVG flag**, so visual checks are a manual GUI step.

---

## 11. Performance budget (honest)

- Frame = **69,888 T-states** @ 3.5 MHz / 50 Hz; usable ≈ 60,000 after interrupt/border overhead.
- **Full clear** of one 6144-byte buffer via stack-`PUSH` ≈ **34,000 T** (≈ half the frame). This is the dominant cost of the full-clear-redraw model.
- Redraw of this slice's content (vector ship ~a few lines + ~16 particles + ~16 bullet pixels + 2 enemy blits) ≈ low thousands of T — comfortably within the remaining budget. The slice **should** hold 50 Hz, but this is **not proven until measured** (see contention caveat).
- **Memory-contention caveat (real hardware, downgraded after review — S4/U1):** the whole 16 KB bank `0x4000–0x7FFF` is contended (incl. the `0x6000` back buffer), tied to active-display timing regardless of which screen is shown. WoS states the **TC2048's exact contention timings are formally *unknown*** (assumed ~48K-like). A tight `PUSH` clear under 48K-style contention can exceed ~1.4×, so the ~34,000 T clear could realistically land near **~45,000–50,000 T**, leaving little headroom in a 69,888 T frame. Mitigation: do as much drawing as possible during the non-contended vblank/border window right after `HALT`. **Treat all T-state figures here as uncontended lower bounds; confirm the real clear/draw cost with `z88dk-ticks` / Fuse before claiming 50 Hz.**
- **Scaling note (logged, not hidden):** the full clear cost is fixed regardless of object count, so when a later spec adds 30+ enemies the redraw grows and we reclaim the clear cost by switching to dirty-rectangles and (if measured necessary) an asm blitter. The clear/blit/line routines are the designated asm-optimisation candidates — *after* "make it work → correct → measure".

---

## 12. Testing strategy

- **Host unit tests (TDD lives here):** pure-integer logic — input decode (normalised joystick byte + key set → `intent_t`), movement & wrap math, bullet/particle update, rotated-vertex lookup — compiles with native `cc` on macOS, no emulator, fast red-green-refactor. *(Established: `test/run.sh` builds & runs these; `geometry` module green.)*
- **Emulator integration:** hardware-touching code (SCLD `OUT 0xFF`, screen addressing, double-buffer flip, 50 Hz timing) verified **visually by the user in the Fuse GUI** (this Fuse build has no headless screenshot — S3). The agent confirms what it can headlessly via `z88dk-ticks` (run the binary, dump screen RAM to check the right addresses were written); flicker/tearing is a human visual check.
- **Cycle counting:** `z88dk-ticks` for measuring hot paths (clear, line, blit) — also the way we settle the contention/50 Hz question (S4/U1).

---

## 13. Open items to resolve during planning/M1

1. ~~**Bullet edge behaviour:** wrap or despawn?~~ **Resolved: despawn at the edge** (finite range reads better) — implemented in `bullet.c`.
2. ~~Exact `zcc`/crt flags and the back-buffer-reserve mechanism.~~ **Resolved** (§10, §2.1): `+zx -clib=sdcc_iy -create-app`, ORG defaults to 0x8000.
3. **Tear-free page-flip (U3)** and **interrupt/bit-6 sequence on real metal (U4):** visual + hardware confirmation at M1 — the prompt forbids emulator-only solutions, and the `im1;ei`/`OUT 0xFF` interaction is exactly the kind that can pass in Fuse yet hang on hardware.
4. **Real contention magnitude / does the slice hold 50 Hz (S4/U1):** measure clear+draw with `z88dk-ticks` before claiming the frame budget.
5. Particle pool size and spawn rate (tune for look vs budget).
6. N = 8 vs a larger rotation table if "animated" should mean smoother spin (data-only change).
7. Whether to drop `printf` on the title screen for a lighter text routine to avoid pulling in the stdio library heap (N4 — not a real "no malloc" violation, but avoidable).

---

## 14. Milestone mapping

This slice corresponds to the prompt's **Milestone 1** (build, launch in Fuse, sprite/ship on screen, read keyboard + Kempston) and **Milestone 2** (player movement, arena/loop), plus an early sliver of **Milestone 3** (bullets, no cleanup-by-collision yet). Subsequent specs resume at full Milestone 3+ (collision, score, enemy AI, waves, sound, polish).

---

## 15. Milestone 1 — implemented & MEASURED (revision, 2026-06-21)

What was actually built and measured on the installed z88dk + Fuse. Where this
contradicts earlier sections, **this section wins** (it is empirical).

### 15.1 Status

A **playable slice runs on Fuse-as-TC2048**: an 8×8 player ship moving via
Kempston (wrap), 2 stationary 8×8 enemies, a 16-bullet pool firing
(Kempston-fire-in-facing or QWEADZXC 8-way), all flicker-free double-buffered.
Built in four verified steps: build smoke test → SCLD page-flip smoke test →
double-buffered motion → playable sprite slice.

### 15.2 The reusable Timex SCLD library (the "homage" kernel)

The Timex-specific core is its own self-contained, documented, host-tested
module — designed to be extracted later as a standalone library (there is
essentially no documented/tested TC2048 SCLD double-buffer C online):

```
include/scld.h  src/scld.c   Timex SCLD standard-res DOUBLE BUFFERING:
                             scld_init / scld_back / scld_back_page /
                             scld_present (HALT+flip) / scld_clear, plus pure
                             host-tested addressing: scld_scanline,
                             scld_next_scanline, scld_row_off[] table.
                             The ONLY code that knows port 0xFF + 0x4000/0x6000.
include/sprite.h src/sprite.c pixel-positioned masked 8×8 OR-blit + box erase,
                             using the scld_row_off[] line-address table.
include/sprites.h src/sprites.c 8×8 bitmaps (ship, enemy, bullet).
include/enemy.h  src/enemy.c  stationary enemies (placeholder for AI later).
```
Existing pure-logic modules unchanged: `player.c bullet.c input.c geometry.c`
(host-tested). `main.c` is the glue (input → logic → incremental render →
present) and the API's first consumer. Host tests now include `test_scld`
(addressing math, all y). `video.c` from §4 is realised as **`scld.c`**.

### 15.3 Measured performance (z88dk-ticks, T-states; frame = 69,888 T @ 50 Hz, ~55k usable)

| Operation | Cost | Verdict |
|---|---|---|
| SCLD page-flip (`OUT 0xFF`) | **123 T** (0.18%) | free — double buffering itself is a non-issue |
| `memset` clear, 6144 B (full screen) | **129,034 T** (1.85 frames) | ❌ too slow per frame → drove D6 to incremental |
| stack-`PUSH` asm clear, 6144 B (prototype) | **35,452 T** (5.8 T/B) | ✅ 3.6× faster, *if* a full clear is ever needed (not integrated — see 15.5) |
| 8×8 sprite erase+draw, **naive C** (recompute addr/row) | **9,199 T** | ❌ ~5 sprites |
| 8×8 sprite erase+draw, **C + line-address table** | **~9,000 T** | ⚠ ~6 sprites/50 Hz — the table fixed correctness, not the C codegen cost |

**Headline finding:** double-buffering is essentially free; the limiter is the
**cost of blitting in C** (~9k T per 8×8 sprite erase+draw), giving only ~6
sprites at 50 Hz. It is **not lines specifically** that "kill the Z80" — both
masked sprite blits *and* line draws are heavy in compiled C. The real lever is
hand-written **asm for the hot drawing inner loop** (expected ~5–10×).

### 15.4 Rendering model (incremental, double-buffer-aware)

Per frame, into the hidden buffer: erase the positions THIS buffer drew last
time (it is 2 frames stale — tracked in `prev[bi][]`), then redraw player +
enemies + active bullets, recording new positions, then `scld_present()`. No
per-frame full clear. Sprites currently clip at the right/bottom edges (toroidal
wrap-duplicate drawing — D5 — is not yet implemented for sprites).

### 15.5 Toolchain facts nailed down at M1

- **Fuse machine id is `2048`, NOT `tc2048`** (§10). `--kempston` maps Kempston to Q/A/O/P/Space (overlaps our QWEADZXC fire keys — prefer a real pad bound to Kempston). This GUI Fuse has no headless screenshot.
- **Port output:** under `sdcc_iy`, `outp()` from `<stdlib.h>` is *not* declared (implicit-decl → wrong call). Use **`z80_outp()` from `<z80.h>`**. Interrupts/HALT via `<intrinsic.h>` (`intrinsic_im_1/ei/halt`). `__SPECTRUM` is defined for `+zx` (so `input.c`'s hardware read compiles).
- **Headless verification with z88dk-ticks:** `-counter N -output` reliably dumps RAM mid-run; `-end 0x0000` is unreliable (exit path varies). The Fuse window remains the authority for what's actually displayed.
- **Hand-asm has a calling-convention wrinkle** under sdcc_iy (single-arg register passing for `__z88dk_fastcall` did not behave as a naive port expected). Parameterless / fixed-address asm worked; argument-passing needs care before the asm blitter lands.

### 15.6 Line drawing — verdict

**Deferred.** Not built. A Bresenham line is per-pixel address+mask work — in C
that is as costly as (or worse than) sprite blitting, which already needs asm.
There is no point carrying a slow C line routine "just in case." It returns only
when (a) a concrete feature needs it and (b) it is written/measured in asm.

### 15.7 Next steps

Done since first draft:
- ✅ **Asm 8×8 blit + erase** (`blit.asm`, shift in asm): 9k → ~5.1k T/sprite (erase+draw), ~10 sprites @ 50 Hz. Calling convention resolved via globals (§15.5).
- ✅ **Cursor (Protek) keyboard joystick** (5/6/7/8 + 0) merged into the Kempston byte (§6) — playable without joystick hardware (macOS Fuse HID often fails).
- ✅ **Collision** bullet↔enemy (`collision.c`, host-tested) wired into the loop: bullets destroy enemies, a cleared wave respawns; fire cooldown caps the bullet/sprite load.
- ✅ **Randomly-wandering enemies** (`enemy.c` + `rng.c` 16-bit LFSR, host-tested): enemies drift and re-roll direction occasionally, toroidal wrap. *No* player tracking yet (deliberate).
- ✅ **Player death**: `player_hit` (host-tested) → on contact the ship resets to centre and the wave respawns (safe spots, no instant re-death).
- ✅ **Colour background**: static 8×8 attributes (blue arena frame + blue/black checker, white ink) painted once on **both** buffers (`bg_paint` in main.c) so the page-flip never disturbs colour — first use of D1 with actual colour. Black ULA border.
- ✅ **`run-zesarux.sh`**: saved launch (TC2048 + `--enabletimexvideo --joystickemulated Kempston --verbose 0`); ZEsarUX is the smoother Timex target than this macOS Fuse build.

Remaining (rough priority):
1. Push the rest of the blit (address calc) into asm → ~20–30 sprites @ 50 Hz (current limiter is the C wrapper, not the inner loop).
2. Directional ship: 8 sprite frames indexed by `player.facing` (data only).
3. Score + HUD (digit sprites), lives, and a TITLE→PLAYING state with the welcome screen.
4. Enemy variety: player-tracking AI, spawn waves of increasing size (Milestone 4/6).
5. Toroidal wrap-duplicate sprite drawing (D5) + wrap-aware collision (enemies now roam to edges).
6. Optional: integrate the asm `scld_clear` only if a full-clear path is ever needed.
