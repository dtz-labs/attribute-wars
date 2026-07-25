# Architecture

Attribute Wars is a 50 Hz twin-joystick shooter for Z80-based Timex and ZX
Spectrum machines. The code is intentionally split between host-testable game
logic and target-only hardware backends.

## Code Layout

- `src/main.c` owns the 50 Hz game loop, wave flow, economy, and render
  orchestration. The screens and effects it used to carry are their own
  modules: `background.c` (generated floor, frame, ULA border), `fx.c` (hit
  pops, death fireball, spawn telegraph), `text.c` (ROM-font blitting),
  `title.c`, `gameover.c`, and `sprite_art.c` (pre-shifted sprite tables).
- `src/player.c`, `src/bullet.c`, `src/enemy.c`, `src/collision.c`, `src/rng.c`,
  `src/score.c`, and `src/geometry.c` are pure logic and have host tests.
- `src/scld.c` is the shared screen backend facade. Game code asks it for the
  current back buffer and never writes hardware paging ports directly.
- `src/blit.asm`, `src/enemy_update.asm`, `src/collide.asm`, and `src/sfx.asm`
  hold hot Z80 paths.
- `src/ay_ports.asm` (detection + the latched AY port pair), `src/ay_sfx.asm`
  (FX-only channel C), `src/pt3_glue.asm` (player glue and the
  `asm_vt_hardware_out` override), `src/music_im2.asm` (the 50 Hz ISR),
  `src/pt3prom.asm` (vendored player) and `src/tune.asm` provide the AY/PT3
  path.
- `include/scld_geom.h` holds the two pure scanline-geometry helpers, kept out
  of `scld.h` because sdcc emits a dead out-of-line copy of each `static
  inline` into every unit that merely sees them.

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

The ZX128 build ships full AY music. The ~10 KB PT3 tune travels as a trailing
headerless tape block that the running program loads into RAM bank 4 at
`$C000`; the player is resident and the IM2 ISR pages bank 4 in for each 50 Hz
tick through the `$7FFD` software shadow in `zx128_page.asm`. The IM2 vector
table lives in page-7 free RAM at `$F000`.

### ZX Spectrum 48K

The 48K build aliases the back buffer to the normal screen and uses a simple
single-buffer display. Flicker is expected.

## Sound

The title screen exposes three SOUND modes:

- `BEEPER`
- `MUSIC+FX`
- `FX`

The AY/PT3 music path uses Pator's **Spectrumizer** tune. The Timex AY path is
active on TC2068/TS2068, and TC2048 users can keep the beeper default. All
three TAPs ship the same music; the 128K reaches it through the banked tune
described above.

## Rendering

The game avoids full-screen clears during gameplay. Each frame erases only the
objects previously drawn into the current back buffer, redraws the player,
enemies, bullets, and HUD deltas, then presents the page.

Sprites are preshifted once at startup. The hot render path calls assembly
blitters through small global parameter blocks to avoid SDCC call overhead.

Render is the dominant per-frame cost (~58% of the measured worst-case
subtotal). See [perf-budget.md](perf-budget.md) for the current T-state
breakdown, the PT3/AY headroom note, and how to reproduce the measurements.
