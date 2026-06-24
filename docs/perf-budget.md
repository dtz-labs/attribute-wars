# Performance Budget

The game runs a fixed 50 Hz loop. Frame = **69,888 T-states**; after ULA
contention, the IM2 ISR, and a safety margin the practical ceiling is
**~55k usable T/frame**. Everything below has to fit under that or the loop
drops a frame.

`MAX_ENEMIES = 7` (`enemy.h`) and `MAX_BULLETS = 2` (`bullet.h`) are the caps
that bound the worst-case frame. The hand-asm loops in `enemy_update.asm` and
`collide.asm` are unrolled/sized to those caps and must move in lockstep with
them.

## Measured breakdown — 7-cap, worst case (2026-06-24)

Measured with `src/measure_main.c` (the non-shipped `markN()` harness) via
`z88dk-ticks`, 200 iterations per segment, wave-16 hunter mix, enemy/bullet
pools full (7 alive, 2 bullets live) — i.e. the most expensive steady-state
frame the game can produce.

| System                | T/frame | % subtotal | Scales with        |
|-----------------------|--------:|-----------:|--------------------|
| **render** (8× erase + 8× draw + 4 bullet ops) | **23,170** | 57.7% | sprite count       |
| enemies_update        |   7,021 | 17.5%      | enemy count        |
| **PT3 music tick** (AY, runs in IM2 ISR)       |  **6,224** | 15.5% | fixed, every frame |
| collide               |   3,195 |  8.0%      | only when bullets live |
| player_hit            |     512 |  1.3%      | —                  |
| **subtotal**          | **40,122** | 100%    |                    |

The subtotal excludes the lighter glue each frame (`input_read`,
`player_update`, `bullets_update`, `fx_render`, HUD widgets, border, score) —
all event-driven or small. **~15k T of headroom** remains in the worst case,
which is why 7-cap plays smoothly.

`player_hit` runs every frame (enemies alive, not invuln); `collide` runs only
on frames where a bullet is live (`main.c` skips the snapshot/collide/rescan on
the common bulletless frame).

### Comparison with the earlier 8-cap figures

The previous run (worst-case hunter mix, 2026-06-23, recorded in `CLAUDE.md`)
was at `MAX_ENEMIES = 8`:

| System          | 8-cap (2026-06-23) | 7-cap (2026-06-24) |
|-----------------|-------------------:|-------------------:|
| render          | 25.6k              | 23.2k              |
| enemies_update  |  8.0k              |  7.0k              |
| collide         |  3.3k              |  3.2k              |
| player_hit      |  0.5k              |  0.5k              |
| PT3 tick        |  6.2k              |  6.2k              |

render and enemies_update dropped proportionally to removing one enemy;
collide/player_hit/PT3 are unchanged, as expected. The cap was reduced from 8
to 7 for a smoother margin, not because 8 busted the budget — an 8th enemy adds
roughly 4k T (render + AI + collide), landing near ~44k, still under ~55k.

## Note on PT3 / AY music

PT3 is the **largest fixed cost after render** (6.2k T, ~15.5% of the subtotal)
and it is paid **every frame in the IM2 ISR regardless of what is happening on
screen** — more than `collide` + `player_hit` combined. It does not scale with
enemies, bullets, or effects.

That makes it the first lever to pull if the loop ever needs headroom (more
enemies, richer effects) without touching the render kernel, which is already
pre-shifted + zero-fill erase + inlined `DOWN` and has little left to give:

- ticking the PT3 player every **other** frame (music at 25 Hz) frees ~3.1k
  T/frame — often inaudible for a chiptune, needs an ear-check on real Timex;
- or a lighter player / banked layout.

The ZX128 page-flip build already ships without PT3 (`ZX128_NO_MUSIC`), so it
has this 6.2k free today; the lever only matters for the Timex/ZX48 AY builds.

## How to reproduce

```sh
make measure                 # builds build/measure_CODE.bin + prints marker addrs
```

Markers (read from `build/measure.map`; addresses shift if the harness changes):

| marker | addr   | segment that ends here     |
|--------|--------|----------------------------|
| mark0  | $8181  | (start, after setup)       |
| markA  | $818D  | enemies_update × 200        |
| markB  | $8199  | collide × 200               |
| markC  | $81A5  | player_hit × 200            |
| mark2  | $81B1  | render × 200                |
| mark3  | $81BD  | PT3 music_tick × 200        |

Each segment is `T(next) − T(this)` over 200 iterations; divide by 200 for the
per-frame cost (the count includes a small per-iter `for`-loop overhead — a few
tens of T — which is negligible against the measured work):

```sh
TICKS=~/Programowanie/z88dk/bin/z88dk-ticks
$TICKS -l 0x8000 -pc 0x8000 -start 0x8181 -end 0x818D build/measure_CODE.bin  # enemies_update
$TICKS -l 0x8000 -pc 0x8000 -start 0x818D -end 0x8199 build/measure_CODE.bin  # collide
$TICKS -l 0x8000 -pc 0x8000 -start 0x8199 -end 0x81A5 build/measure_CODE.bin  # player_hit
$TICKS -l 0x8000 -pc 0x8000 -start 0x81A5 -end 0x81B1 build/measure_CODE.bin  # render
$TICKS -l 0x8000 -pc 0x8000 -start 0x81B1 -end 0x81BD build/measure_CODE.bin  # PT3 tick
```

**Always profile with `z88dk-ticks` before claiming a perf change.**
