# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Geometry-Wars-inspired **twin-stick shooter for the Timex TC2048** (Z80A @ 3.5 MHz, 48 KB RAM), written in C + Z80 asm with the **z88dk** toolchain. The stated priority from the user is **SMOOTH and FAST** gameplay over graphical richness — every design tradeoff favours frame-rate over colour/visual complexity. The full design rationale and hardware findings live in `docs/superpowers/specs/2026-06-21-timex-twin-stick-foundation-design.md` (read §2.1 and §15 first — they record empirically-verified hardware gotchas and override earlier sections where they conflict).

## Commands

- **Build targets:** `make` prints available targets. `make all` builds every platform TAP in parallel; `make timex`, `make zx128`, and `make zx48` build one platform. `make TARGET=zx128` is also supported.
- **Timex build:** `make timex` → `build/aw-1.0-timex.tap`. Requires z88dk at `~/Programowanie/z88dk` or on `PATH`. Compiles the C sources + hand asm with `zcc +zx -SO3 -clib=sdcc_iy`. ORG defaults to `0x8000` (no `-zorg`).
- **ZX Spectrum 128K build:** `make zx128` → `build/aw-1.0-zx128k.tap`. It defines `ZX128_PAGE_FLIP`, `ZX_SINCLAIR_DUAL_STICK`, and `ZX128_NO_MUSIC`: RAM page 7 is kept banked into `$C000`, bit 3 of port `$7FFD` flips between page 5/page 7, "two joysticks" means Sinclair 1/2, and PT3 music is disabled in this first 128K build to keep resident code/data/BSS below `$C000`. A checked experiment with PT3 enabled ended at about `$F1C2`, so ZX128 AY music needs a banked tune/player layout before it can ship with this page-flip renderer.
- **ZX Spectrum 48K build:** `make zx48` → `build/aw-1.0-zx48k.tap`. It defines `ZX48_SINGLE_BUFFER` and `ZX_SINCLAIR_DUAL_STICK`: no SCLD writes, screen B aliases screen A, `scld_present()` is just `HALT`, and "two joysticks" means Sinclair 1/2 rather than TS2068 AY joystick ports.
- **Run / verify:** `make run-tc2048` launches `build/aw-1.0-timex.tap` in **ZEsarUX** as a `TC2048` (`--enabletimexvideo`, `--joystickemulated Kempston`). `make run-tc2068` launches the Timex build as `TC2068`. `make run-zx128` launches `build/aw-1.0-zx128k.tap` as `128k`. `make run-zx48` launches `build/aw-1.0-zx48k.tap` as a plain `48k`. ZEsarUX is the preferred Timex emulator here — not Fuse (the macOS Fuse build has flaky HID joystick support and no headless screenshot). Controls: move with `5/6/7/8` (cursor) or a Kempston pad; fire with `0` or `Q W E / A D / Z X C`.
- **Host unit tests:** `./test/run.sh` builds and runs all `test_*.c` natively with the system `cc` (`-std=c99 -Wall -Wextra -Werror`) — no Z80 toolchain, no emulator, instant red/green. Binaries land in `build/host/`.
- **Run a single test:** after `./test/run.sh`, just re-run its binary, e.g. `./build/host/test_enemy`. To build one in isolation, mirror the `run.sh` line — e.g. `cc -std=c99 -Wall -Wextra -Werror -Iinclude test/test_player.c src/player.c src/geometry.c src/input.c -o build/host/test_player && ./build/host/test_player`. Each test links only the pure-logic sources it exercises.

`src/measure_main.c` is a **separate, non-shipped** T-state measurement harness (logic vs render cost via `z88dk-ticks`); build it with `make measure` when profiling.

## Architecture

### The hardware boundary (most important rule)

**Only `src/scld.c` / `include/scld.h` know the platform screen backend and display addresses.** On Timex that means SCLD port `0xFF` and screens `0x4000`/`0x6000`; on ZX128 it means `src/zx128_page.asm` writes `$7FFD` to keep RAM page 7 mapped at `$C000` and flip screen 5/7. Everything else is buffer-agnostic: the game loop asks `scld.c` for the address of the hidden back buffer and the drawing code blits into whatever base it's handed. This is what keeps the pure-logic modules host-testable on macOS.

### Two layers

- **Pure-logic, host-tested (TDD lives here):** `geometry.c`, `input.c` (interface in `controls.h`), `player.c`, `bullet.c`, `enemy.c`, `collision.c`, `rng.c`. Integer-only, no hardware, compile with native `cc`. Each has a matching `test/test_*.c`.
- **Target-only hardware:** `scld.c` (SCLD double-buffer kernel), `sprite.c` (8×8 pre-shifted OR-blitter + cheap bullet dots), `blit.asm` (hand-written hot drawing inner loop), `sprites.c` (8×8 pixel-art data). Verified visually in the emulator + by cycle-counting with `z88dk-ticks`.
- **Hot-path asm with a host C twin:** `enemy_update.asm` is a hand-written `enemies_update` (sdcc compiled the C to ~1600 T/enemy — it spilled the per-enemy skeleton to the IX frame). `enemy.c` keeps the C version behind `#ifndef __SDCC` as the host-tested reference and falls through to a thin global-passing wrapper on the `__SDCC` target. The two are kept **byte-identical** — verified by a differential RAM-dump test (C ref vs asm over thousands of randomized frames). Any change to enemy movement logic must update both halves and re-run that diff.
- **`main.c`** is the glue: the 50 Hz game loop, title→playing state, input → logic → incremental render → present.

### Rendering model (double-buffer-aware, incremental)

Double buffering uses the SCLD page-flip (`scld_present()` = HALT to 50 Hz, then flip). A full per-frame screen clear is **too slow** (~129k T-states measured — busts the frame budget), so the loop does **incremental erase+redraw**: each buffer erases only the positions *it* drew last time (it's 2 frames stale — tracked in `prev[2][MAX_DRAW]` in `main.c`), then redraws player + enemies + bullets. Sprites are **pre-shifted once at startup** (`spr_preshift`) so the per-row bit-shift is off the hot path. Background colour is painted once into *both* attribute blocks so the flip never disturbs it.

### Performance budget

Frame = **69,888 T-states** @ 50 Hz (~55k usable). The page-flip itself is ~free; the cost is drawing + per-enemy logic. Current hard caps: **`MAX_ENEMIES = 7`** (`enemy.h`) and **`MAX_BULLETS = 2`** (`bullet.h`); the hand asm loops in `enemy_update.asm` and `collide.asm` must stay in lockstep with those caps. Latest 7-cap measurement (worst-case wave-16 hunter mix, via `z88dk-ticks` on `measure_main.c`, 2026-06-24) was: **render ≈ 23.2k T** (8 sprite erase+draw + bullets), **enemies_update ≈ 7.0k T**, PT3 tick ≈ 6.2k, collide ≈ 3.2k, player_hit ≈ 0.5k; measured subtotal ≈ **40.1k T**, leaving ≈15k T to the conservative ~55k usable budget before miscellaneous HUD/input/fx costs. The cap was reduced from 8 to 7 for smoother play (an 8th enemy is ≈4k T → still under budget; 7 is margin, not a hard limit). **Render is 58% of the subtotal and the kernel is already pre-shift + zero-fill + inlined `DOWN`; PT3 is the largest *fixed* cost (every frame in the IM2 ISR) and the first lever for headroom that doesn't touch the renderer.** Full table, PT3 note, and the exact reproduction commands live in `docs/perf-budget.md`. **Always profile with `z88dk-ticks` before claiming a perf change** — build `measure_main.c` (a non-shipped harness with `markN()` border-OUT markers), read marker addresses from the `.map`, and run `z88dk-ticks -l 0x8000 -pc 0x8000 -start 0x<a> -end 0x<b> build/measure_CODE.bin`.

## Project-specific constraints & gotchas

These are real, verified-on-hardware rules — violating them produces crashes or subtle hangs, not just style issues:

- **No floating point, no `malloc`, no recursion.** Integer math, fixed-size arrays, object pools, precomputed tables only.
- **Never return a struct by value — pass via out-pointer.** SDCC's Z80 backend SIGSEGVs on struct-return-by-value (`make_intent`/`input_read` take `intent_t *out`). It's also faster on Z80.
- **The input module's header is `controls.h`, not `input.h`.** A header named `input.h` on the include path shadows z88dk's system `<input.h>` (SDCC's `-iquote` still searches it for angle includes). Implementation stays `src/input.c`.
- **Port `0xFF` is only ever written `0x00` or `0x01`.** Bit 6 is a hardware interrupt kill-switch that software `EI` cannot override — setting it freezes the HALT-paced loop. `scld.c` owns these writes.
- **ZX Spectrum 128K page flipping lives in its separate build.** The Timex AY/music binary still reaches high memory, but `make zx128` removes PT3 music, keeps resident memory below `$C000`, sets `SP=$C000`, validates with `tools/check_zx128_layout.py`, maps RAM page 7 into `$C000`, and flips display bit 3 on port `$7FFD`. The page values must preserve ROM1 (`$17` for screen 5, `$1F` for screen 7); using `$07/$0F` switches to ROM0 and can reboot under IM1.
- **Interrupts boot DISABLED.** The z88dk newlib crt starts with DI, so a `HALT` would never wake. `scld_init()` runs `im 1; ei` before the loop — don't bypass it.
- **Use `z80_outp()` from `<z80.h>`** for port output (`outp()` is undeclared under `sdcc_iy` → implicit-decl bug). Interrupt/HALT primitives come from `<intrinsic.h>`.

## Conventions

- Shared fixed-width types and the 8-way `DIR_*` compass live in `include/types.h` (`u8/s8/u16/s16`). Screen coords: x grows right, y grows down; x is a `u8` (wraps 0–255 for free), y must be wrapped manually against `SCREEN_H = 192`.
- Toroidal (Pac-Man) wrap on both axes; there's no solid arena border collision.
- Directional ship = 8 sprite frames indexed by `player.facing` (`spr_ship_dir[DIR_*]`). Cardinal frames are algorithm-rotated from the North frame; diagonal frames are hand-drawn/mirrored (45° can't rotate losslessly on an 8×8 grid). See comments in `src/sprites.c`.
