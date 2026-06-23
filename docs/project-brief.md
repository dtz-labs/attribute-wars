# Project Brief

Attribute Wars is a twin-stick arcade shooter for Timex and ZX Spectrum
computers, inspired by Robotron 2084, Smash TV, Llamatron, and Geometry Wars.

## Current Targets

- Timex TC2048 / TC2068 / TS2068
- ZX Spectrum 128K / +2
- ZX Spectrum 48K

The Timex builds are the primary quality target. The ZX Spectrum 128K build has
its own shadow-screen renderer. The ZX Spectrum 48K build is intentionally a
lower-quality compatibility target because it has no hardware page flipping.

## Development Environment

- z88dk installed at `~/Programowanie/z88dk`, or available on `PATH`
- ZEsarUX for emulator runs on macOS, expected at
  `/Applications/ZEsarUX.app/Contents/MacOS/zesarux`
- Build commands are documented in [build.md](build.md)

## Hardware Constraints

- Z80A at 3.5 MHz
- 48 KB baseline RAM
- No `malloc`, `free`, recursion, floating point, or large stack allocations
- Prefer fixed-size arrays, object pools, integer arithmetic, and predictable
  memory layouts
- Assembly is reserved for measured performance-critical paths

## Gameplay Priorities

1. Fast movement
2. Responsive controls
3. Large enemy counts
4. Satisfying shooting
5. Short play sessions
6. High-score chasing

Avoid RPG mechanics, inventory systems, story systems, dialog systems, and
complex menus.

## Engineering Principles

- Start with the simplest working solution.
- Prefer a playable prototype over theoretical architecture.
- Before optimizing: make it work, make it correct, measure it, then optimize.
- Keep functions small and names clear.
- Comment non-obvious code and all assembly code.
- Explain hardware tradeoffs in terms of CPU, RAM, and video memory cost.
- Distinguish clearly between Timex and ZX Spectrum hardware behavior.
