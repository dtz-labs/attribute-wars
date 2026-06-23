# Changelog

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
- Fixed background restore after hit effects so explosions no longer leave
  permanent marks on the active arena pattern.

### Changed
- Release ZIP packaging now orders Spectrum TAPs before the Timex TAP, so browser
  emulators that auto-pick the first ZIP entry start with a Spectrum-compatible
  build.
- Arena backgrounds now rotate per new game between checker, dark-blue diagonal
  stripes, and dark-blue grid patterns.
- Added a red ULA border flash on enemy hits, player hits, and player death.
- The ZX128 title screen drops the moving shine effect to preserve stack margin.

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
