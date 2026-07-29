# Performance Budget

The game runs a fixed 50 Hz loop. Frame = **69,888 T-states**; after ULA
contention, the IM2 ISR, and a safety margin the practical ceiling is
**~55k usable T/frame**. Everything below has to fit under that or the loop
drops a frame.

`MAX_ENEMIES = 8` (`enemy.h`) and `MAX_BULLETS = 2` (`bullet.h`) are the caps
that bound the worst-case frame. The hand-asm loops in `enemy_update.asm` and
`collide.asm` are unrolled/sized to those caps and must move in lockstep with
them.

## Measured breakdown — 8-cap, worst case, char-boundary-split blitter (2026-07-08)

Measured with `src/measure_main.c` (the non-shipped `markN()` harness) via
`z88dk-ticks`, 200 iterations per segment, wave-16 hunter mix, enemy/bullet
pools full (8 alive, 2 bullets live) — i.e. the most expensive steady-state
frame the game can produce. `blit.asm` is the "char-boundary split" kernel
(scanline advance is a bare `inc h` within a character cell, with a single
boundary step per sprite instead of a per-row `DOWN` test) — unchanged by this
pass, just re-measured at the new cap.

| System                | T/frame | % subtotal | Scales with        |
|-----------------------|--------:|-----------:|--------------------|
| **render** (9× erase + 9× draw + 4 bullet ops) | **24,007** | 56.9% | sprite count       |
| enemies_update        |   7,974 | 18.9%      | enemy count        |
| **PT3 music tick** (AY, runs in IM2 ISR)       |  **6,224** | 14.8% | fixed, every frame |
| collide               |   3,432 |  8.1%      | only when bullets live |
| player_hit            |     512 |  1.2%      | —                  |
| **subtotal**          | **42,149** | 100%    |                    |

The subtotal excludes the lighter glue each frame (`input_read`,
`player_update`, `bullets_update`, `fx_render`, HUD widgets, border, score) —
all event-driven or small. **~13k T of headroom** remains in the worst case
against the conservative ~55k usable budget, which is why 8-cap plays smoothly.

`player_hit` runs every frame (enemies alive, not invuln); `collide` runs only
on frames where a bullet is live (`main.c` skips the snapshot/collide/rescan on
the common bulletless frame). Note: in this harness `player_hit` and `collide`
came in below the naive per-enemy extrapolation because the enemies have spent
200 frames of `enemies_update` chasing the fixed measurement player position
and several have already converged into the 8×8 hit box, so both loops break
out early on more of the 200 sampled iterations than a uniformly-spread worst
case would — the render and `enemies_update` segments, which always do fixed,
unconditional work per enemy, matched the pre-change extrapolation closely.

### Comparison with the 7-cap figures (post-blitter-optimisation baseline)

The previous run (worst-case hunter mix, 2026-06-24, `MAX_ENEMIES = 7`, same
char-boundary-split blitter):

| System          | 7-cap (2026-06-24) | 8-cap (2026-07-08) | Δ      |
|-----------------|--------------------:|--------------------:|-------:|
| render          | 23,170              | 24,007              | +837   |
| enemies_update  |  7,021              |  7,974              | +953   |
| collide         |  3,195              |  3,432              | +237   |
| player_hit      |    512              |    512              |    ~0  |
| PT3 tick        |  6,224              |  6,224              |    0   |
| **subtotal**    | **40,122**          | **42,149**          | +2,027 |

render and enemies_update grew roughly in line with adding one more enemy
(the 8th slot in the wave-16 mix is a hunter, the most expensive AI level);
collide grew by less than a flat per-enemy estimate for the early-break reason
noted above; PT3 is unaffected (fixed cost, doesn't scale with enemy count).
Still **~13k T under the ~55k usable budget** — comfortable margin.

### Historical: 7-cap vs the original 8-cap figures (pre-blitter-optimisation)

For reference, the very first 8-cap vs 7-cap comparison (2026-06-23/24, before
the char-boundary-split blitter existed):

| System          | 8-cap (2026-06-23) | 7-cap (2026-06-24) |
|-----------------|-------------------:|-------------------:|
| render          | 25.6k              | 23.2k              |
| enemies_update  |  8.0k              |  7.0k              |
| collide         |  3.3k              |  3.2k              |
| player_hit      |  0.5k              |  0.5k              |
| PT3 tick        |  6.2k              |  6.2k              |

render and enemies_update dropped proportionally to removing one enemy;
collide/player_hit/PT3 were unchanged, as expected. The cap was reduced from 8
to 7 at the time for a smoother margin, not because 8 busted the budget. The
cap was raised back to 8 on 2026-07-08 once the char-boundary-split blitter
(see above) had already clawed back headroom in `render`, confirmed safe by
the fresh measurement in the top table.

## Note on PT3 / AY music

PT3 is the **largest fixed cost after render** (6.2k T, ~14.8% of the subtotal)
and it is paid **every frame in the IM2 ISR regardless of what is happening on
screen** — more than `collide` + `player_hit` combined. It does not scale with
enemies, bullets, or effects.

That makes it the first lever to pull if the loop ever needs more headroom
(more enemies, richer effects) without touching the render kernel, which is
already pre-shifted + zero-fill erase + char-boundary-split scanline advance
and has little left to give:

- ticking the PT3 player every **other** frame (music at 25 Hz) frees ~3.1k
  T/frame — often inaudible for a chiptune, needs an ear-check on real Timex;
- or a lighter player / banked layout.

**All three shipping builds pay this cost every frame now.** Since the ZX128
build reached full Timex parity (banked PT3 tune in RAM bank 4, paged into
`$C000` around each tick — see `CLAUDE.md`), the old memory-reduced
`ZX128_NO_MUSIC` variant is gone; ZX128 is no longer exempt from the PT3 cost.
The lever above applies equally to Timex, ZX48, and ZX128 now.

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
