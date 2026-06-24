# Architecture

Attribute Wars is a 50 Hz twin-joystick shooter for Z80-based Timex and ZX
Spectrum machines. The code is intentionally split between host-testable game
logic and target-only hardware backends.

## Code Layout

- `src/main.c` owns the title screen, game loop, HUD calls, wave flow, and render
  orchestration.
- `src/player.c`, `src/bullet.c`, `src/enemy.c`, `src/collision.c`, `src/rng.c`,
  `src/score.c`, and `src/geometry.c` are pure logic and have host tests.
- `src/scld.c` is the shared screen backend facade. Game code asks it for the
  current back buffer and never writes hardware paging ports directly.
- `src/blit.asm`, `src/enemy_update.asm`, `src/collide.asm`, and `src/sfx.asm`
  hold hot Z80 paths.
- `src/music_ay.asm`, `src/pt3prom.asm`, and `src/tune.asm` provide the AY/PT3
  path for builds that include AY music.

## Platform Builds

### Timex TC2048 / TC2068 / TS2068

The Timex build uses the SCLD standard-resolution second display file:

- screen A bitmap/attributes: `$4000` / `$5800`
- screen B bitmap/attributes: `$6000` / `$7800`
- page flip port: `$FF`, written only as `0` or `1`

TC2068/TS2068 machines also provide Timex AY ports and native joystick ports.

### ZX Spectrum 128K / +2

The ZX128 build uses RAM page 7 as the shadow display:

- screen A is the normal page-5 display at `$4000`
- screen B is RAM page 7 mapped at `$C000`
- page flip port: `$7FFD`

The page values must preserve ROM1:

- `$17` maps RAM page 7 at `$C000` and shows screen 5
- `$1F` maps RAM page 7 at `$C000` and shows screen 7

Using `$07/$0F` also maps RAM page 7, but switches to ROM0 and can reboot under
IM1 interrupts after the BASIC loader has started the program from ROM1.

The resident code, data, BSS, and stack must stay below `$C000`. The build is
checked by `tools/check_zx128_layout.py`. Preshifted sprite tables for this
build live in unused RAM page 7 space above the shadow screen.

The current ZX128 page-flip TAP is deliberately built with `ZX128_NO_MUSIC`.
Including the PT3 player and bundled tune in the resident image pushes the map
to roughly `$F1C2`, which means code/data would be banked out whenever RAM page
7 is mapped at `$C000`. Shipping AY music on this target needs a bank-aware
tune/player layout or a renderer that pages the shadow screen only for short
writes.

### ZX Spectrum 48K

The 48K build aliases the back buffer to the normal screen and uses a simple
single-buffer display. Flicker is expected.

## Sound

The title screen exposes three SOUND modes:

- `BEEPER`
- `MUSIC+FX`
- `FX`

The AY/PT3 music path uses Pator's **Spectrumizer** tune. The Timex AY path is
active on TC2068/TS2068, and TC2048 users can keep the beeper default. The ZX128
page-flip build currently defines `ZX128_NO_MUSIC`, so that TAP is beeper-only
until the AY player and tune are moved into a bank-safe layout.

## Rendering

The game avoids full-screen clears during gameplay. Each frame erases only the
objects previously drawn into the current back buffer, redraws the player,
enemies, bullets, and HUD deltas, then presents the page.

Sprites are preshifted once at startup. The hot render path calls assembly
blitters through small global parameter blocks to avoid SDCC call overhead.

Render is the dominant per-frame cost (~58% of the measured worst-case
subtotal). See [perf-budget.md](perf-budget.md) for the current T-state
breakdown, the PT3/AY headroom note, and how to reproduce the measurements.
