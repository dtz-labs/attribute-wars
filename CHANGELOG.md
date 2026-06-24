# Changelog

## [1.2.0] - 2026-06-24

### Added
- **ZX Spectrum 128K: PT3 music, at full parity with the Timex build.** The
  ~10 KB tune is parked in RAM bank 4 and paged into `$C000` for each 50 Hz
  player tick, so the page-flip shadow screen (page 7) and the banked tune
  coexist. `MUSIC+FX` is now a real, default-when-AY-present menu option on 128K,
  with sound effects overlaid on the tune exactly as on Timex.
- **128K: AY sound effects now work** — previously the 128K build shipped with
  the AY path stubbed out (silent). Resident code moved to ORG `$6000` to reclaim
  the `$6000–$7FFF` hole (free on 128K, since screen B lives in page 7, not at
  `$6000` as on Timex), and the IM2 vector table moved to page-7 RAM at `$F000`.
- **128K: distinct enemy sprites** (chaser, hunter, vertical/horizontal bouncer)
  instead of every enemy rendering as the bouncer — full visual parity, zero
  extra per-frame render cost.
- `tools/make_tape_block.py` — wraps a binary into a headerless tape data block
  (used to append the tune to the 128K `.tap`).

### Changed
- **128K gameplay now matches Timex**: tougher hunters (dodge 64 / flee 3) and
  hunters that split into 2 chasers, replacing the old memory-reduced 128K
  variants. The `ZX128_NO_MUSIC` build flag was retired from the gameplay code.
- Timex (`TC2048`/`TC2068`) and ZX 48K builds are byte-for-byte unchanged — the
  work is isolated to the `zx128` target.

## [1.1.2] - 2026-06-23

### Fixed
- Fixed background restore after hit effects so explosions no longer leave
  permanent marks on the active arena pattern.

### Changed
- Arena backgrounds now rotate per new game between checker, dark-blue diagonal
  stripes, and dark-blue grid patterns.
- Added a red ULA border flash on enemy hits, player hits, and player death.
- Shortened the red hit border flash and synchronized the GAME OVER flash with
  the ULA border.
- Moved Timex enemy pre-shift sprite tables into the free post-screen scratch
  area, preserving distinct enemy sprites while increasing stack margin.
- Timed the ZX48 single-buffer gameplay render to start immediately after the
  frame interrupt, reducing the visible erase/redraw blink window.
- The ZX128 title screen drops the moving shine effect to preserve stack margin.

## [1.1.1] - 2026-06-23

### Fixed
- Fixed Timex/ZX48 startup memory layout by moving the runtime stack to the top
  of RAM and deduplicating enemy clone spawn code.
- Added Timex/ZX48 stack layout checks so CI fails before a TAP can return to
  BASIC from insufficient stack gap.
- README now links to the general GitHub Releases page instead of hardcoding a
  specific release asset.
- The title-screen version now comes from the same Makefile `VERSION` value that
  names the TAP files.

### Changed
- Release ZIP packaging now orders Spectrum TAPs before the Timex TAP, so browser
  emulators that auto-pick the first ZIP entry start with a Spectrum-compatible
  build.

## [1.1] - 2026-06-23

### Changed
- **Chasers (followers) now require 3 hits to kill** (previously 2)
  - First wound: 8px jump (down from 24px)
  - Second wound: 12px jump
  - Third hit: kills with chance to split into bouncers
- **Visual feedback added for all wounds** - hit effect (white→yellow→red burst) now plays on every hit, not just kills
- **Chaser death bonus** - 50% chance to spawn 2 bouncers (one horizontal, one vertical) on kill
- **Version display** - title screen now shows "version 1.1" without date

### Gameplay impact
Chasers are now significantly tougher:
- Requires 3 precise shots vs 2
- Smaller jumps make them less likely to teleport off-screen
- Visual feedback confirms each hit landed
- Death can spawn additional bouncers, increasing wave complexity

## [1.0] - 2026-06-23
### Initial release
- Twin-stick shooter for Timex TC2048 / ZX Spectrum
- 3 enemy types: bouncers, chasers, hunters
- 16-wave difficulty progression
- AY music + sound effects
- Timex double-buffering at 50 Hz
- ZX Spectrum 48K/128K builds
