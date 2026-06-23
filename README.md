# Attribute Wars

[![ci](https://github.com/dtz-labs/attribute-wars/actions/workflows/ci.yml/badge.svg)](https://github.com/dtz-labs/attribute-wars/actions/workflows/ci.yml)

A Geometry-Wars-inspired **twin-stick shooter for the Timex TC2048** (Z80A @ 3.5 MHz,
48 KB RAM), written in C + hand-written Z80 assembly with the [z88dk](https://z88dk.org)
toolchain.

The design priority is **smooth and fast** gameplay over graphical richness — every
tradeoff favours frame-rate over colour and visual complexity. The game runs at a
locked 50 Hz using the Timex SCLD hardware page-flip for flicker-free double buffering.

> **Status:** in development. Targets the Timex TC2048 today; **AY-3-8910 music now
> plays on any AY-equipped machine** (ZX 128 / TS2068 / TC2068), auto-detected at
> runtime — see [Music](#music). The full TC2068/TS2068 and ZX Spectrum 128K
> *ports* (kernel swaps) are designed in
> [`docs/superpowers/specs/`](docs/superpowers/specs/).

## Hardware targets

| Machine | Status | Double buffer | Sound |
|---|---|---|---|
| Timex TC2048 | primary | SCLD page-flip | beeper |
| Timex TC2068 / TS2068 | designed | SCLD page-flip | AY-3-8912 |
| ZX Spectrum 128K | designed | shadow screen (`0x7FFD`) | AY-3-8912 |

## Build & run

```sh
./build.sh          # -> build/game.tap   (needs z88dk at ~/Programowanie/z88dk)
./run-zesarux.sh    # launch build/game.tap in ZEsarUX as a TC2048
```

`build.sh` compiles the C sources + the asm hot paths with
`zcc +zx -SO3 -clib=sdcc_iy` (ORG `0x8000`). ZEsarUX is the preferred Timex emulator
(`--enabletimexvideo`, Kempston joystick).

**Controls:** move with `5/6/7/8` (cursor) or a Kempston pad; fire with `0` or
`Q W E / A D / Z X C` (8-direction fire).

## Tests

The pure-logic modules (geometry, input, player, bullet, enemy, collision, rng,
score) are integer-only and host-testable — no Z80 toolchain, no emulator:

```sh
./test/run.sh       # builds & runs every test_*.c natively with -Wall -Wextra -Werror
```

Hardware-touching code (SCLD double-buffer, sprite blitter, asm inner loops) is
verified in the emulator and by cycle-counting with `z88dk-ticks`.

## Architecture

The cardinal rule: **only `src/scld.c` knows the display hardware** (port `0xFF`,
screen addresses `0x4000`/`0x6000`). Everything above that boundary is
buffer-agnostic — it asks the kernel for the hidden back-buffer address and blits
into whatever base it is handed. That isolation is what keeps the logic
host-testable and makes new machine targets a contained *kernel swap* rather than a
rewrite (see the 128K port spec).

- **Pure-logic, host-tested:** `geometry.c`, `input.c`, `player.c`, `bullet.c`,
  `enemy.c`, `collision.c`, `rng.c`, `score.c`.
- **Target-only hardware:** `scld.c` (SCLD double-buffer kernel), `sprite.c` /
  `blit.asm` (pre-shifted OR-blitter), `sprites.c` (pixel-art data), `sfx.asm`
  (beeper), `enemy_update.asm` / `collide.asm` (asm hot paths with host-C twins).
- **`main.c`** — the 50 Hz game loop, title→playing state, and incremental
  erase+redraw rendering.

Full design rationale and verified hardware gotchas live in
[`docs/superpowers/specs/`](docs/superpowers/specs/) and `CLAUDE.md`.

## Constraints

No floating point, no `malloc`, no recursion — integer math, fixed-size pools, and
precomputed tables only. Frame budget is 69,888 T-states @ 50 Hz; the enemy and
bullet caps are tuned to stay under it (re-measure with `z88dk-ticks` before raising
them).

## Music

On any machine with an AY-3-8910/8912 — ZX Spectrum 128/+2/+3, Timex TS2068/TC2068,
or a 48K with an AY interface — the game plays **AY chiptune music**, auto-detected at
runtime. The beeper-only TC2048 stays silent (with its beeper SFX as always); no
machine is left worse off.

The tune is **"Spectrumizer" by Pator** ([ZX-Art](https://zxart.ee/eng/authors/p/pator/spectrumizer/),
Lost Party 2023). Find Pator at **@paatorr on X** — thank you for the music.

Playback uses Sergey Bulba's Vortex Tracker II PT3 player (vendored from z88dk),
ticked once per frame from the main loop; the beeper SFX (`sfx.asm`) are untouched
and mix over the music in hardware.

**How the chip is found** (`src/music_ay.asm`) — designed to never disturb a
TC2048: first probe the *standard* AY at `0xFFFD`/`0xBFFD`. Those are **odd** ports,
so they can never be confused with the ULA — this safely covers the ZX 128K and any
TC2048/48K fitted with a standard AY interface. If none answers, identify the
machine by **ROM signature**: a TS2068/TC2068 has the string `"Timex"` at ROM
`0x113D` (its 1983 copyright line), a TC2048 does not — and *only* on a confirmed
2068 do we enable the native AY at `0xF5`/`0xF6`. (`0xF6` is an **even** port = the
ULA on a TC2048, so it is touched only once the ROM proves we are on a 2068;
blindly probing it scrambles the border + beeper.)

> **2068 note:** AY music on a TS2068/TC2068 is detected via the **native Timex HOME
> ROM**. If you run the game through a *Spectrum-emulator cartridge* (which swaps in
> the TC2048 ROM), the `"Timex"` signature won't match and the music stays silent —
> use the native ROM for AY music on a 2068. (Plugging a 48K/Spectrum ROM into a
> 2068 and expecting AY music is, reasonably, on you.)

## License

Code is released under the [MIT License](LICENSE) © 2026 Michał Pasternak.

**Note:** the bundled AY tune `Spectrumizer.pt3` (by *Pator*, ZX-Art id 413568) is a
third-party asset and is **not** covered by the MIT license — its redistribution
terms must be confirmed with the original author before any public release.
