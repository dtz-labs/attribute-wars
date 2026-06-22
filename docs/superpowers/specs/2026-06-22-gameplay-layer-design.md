# Gameplay Layer — Boost, Scoring, Waves, Timer, Sound

**Date:** 2026-06-22 · **Target:** Timex TC2048 (Z80A @ 3.5 MHz, 50 Hz), z88dk sdcc_iy, ZEsarUX.
**Status:** Design for review — **revised after two independent adversarial sub-agent reviews**. Review 1 resolved 3 blockers (per-scheme boost/fire wiring, the wave/score state model, HUD vs the arena wall); review 2 (clean agent, on the revised spec) confirmed those closed and added fixes now folded in: BCD borrow-correctness, the FX **and telegraph** attribute-restore swap, pinned digit geometry + dark-paper D1 constraint, keeping the collide return for scoring, `enemies_spawn(es, wave)` indexing, and re-measuring the perf gate after full integration. Plus a visual-recoil juice feature.

Adds a gameplay layer on top of the optimised engine slice (render / `enemies_update` / `collide` are hand-asm; `player_update` is C): a real score economy, an energy-metered boost, varied wave spawn patterns, a per-wave timer, a 16-wave difficulty curve, lives/extra-lives, a game-over screen that can resume from the death wave, beeper SFX, and a title-screen shine effect.

Engine context that constrains every choice: frame = **69,888 T** @ 50 Hz (~55k usable); **no float / malloc / recursion**; **pass structs by out-pointer** (sdcc z80 crashes on struct-return-by-value); **sdcc is poor at 16/32-bit math**; only `scld.c` knows port `0xFF` and the `0x4000/0x6000` screens; the 1-bit beeper is port `0xFE` bit 4 (shares the port with the ULA border). The hand-asm `enemies_update`/`collide` bake in `MAX_ENEMIES`/`MAX_BULLETS`.

---

## 1. Goals & non-goals

**In scope:** three boost-aware control schemes; slightly faster base ship + energy boost; BCD scoring with floor-at-zero and extra lives; 5 spawn patterns; a 16-wave difficulty table; per-wave timer with early-clear bonus; 2 starting lives; game-over → resume-from-death-wave; 1-bit beeper SFX; a HUD that fits the border; a title shine-sweep.

**Non-goals:** music, high-score persistence, new enemy *types* (bounce/chase/hunter stay — only point values change), 2-player.

---

## 2. Controls — three boost-aware schemes

The ship's visual facing still follows its movement (`update_facing`, unchanged). Every scheme produces the same `intent_t`; only the input→intent mapping differs. The title menu keeps letting the player pick a scheme.

`intent_t` (in `controls.h`) gains **`u8 boost;`** (non-zero while a boost input is held). `aim_dx/aim_dy` + `fire` keep meaning "shoot this 8-way direction this frame."

| | **A — Kempston** (default, TC2048) | **B — Keyboard + stick** | **C — Dual-stick** (TS2068) |
|---|---|---|---|
| **Move** | Kempston stick + cursor `5/6/7/8` | `Q W E / A · D / Z X C` (8-way) | left stick |
| **Shoot** | `Q W E / A · D / Z X C` | Kempston **tilt** = aim+fire; **FIRE button** = shoot in heading | right stick (tilt = aim+fire) |
| **Boost** | **FIRE button** or **SPACE** | **S** key | left stick **FIRE button** |

Notes:
- **A:** the fire button no longer shoots — it boosts. Shooting is the letter keys only. (`SPACE` also boosts; on Fuse's `--kempston` map SPACE doubles as stick-fire, harmless since fire is repurposed.)
- **B:** the letter keys *move*; `S` (the centre of the QWEADZXC cluster, currently unused) boosts; the Kempston stick *aims and fires* by deflection; the fire button fires in the current heading (the old facing-shot, kept here).
- **C:** the genuine two-physical-stick twin-stick for a real TS2068 — left = move (+ its button boosts), right = aim+fire.

**`input.c` / `make_intent` changes (the host-tested boundary):**
- `make_intent`'s existing `JOY_FIRE`→`out->fire` (fire-in-facing) branch is **deleted/guarded for scheme A**; that byte now drives `out->boost`. Because `read_cursor_joy` merges cursor **`0`** into the Kempston `JOY_FIRE` bit, **cursor `0` now boosts too** (consistent with the agreed "FIRE / SPACE / 0 = boost"; this changes the no-joystick fallback from fire to boost — intended).
- Scheme A/C `JOY_FIRE` → `out->boost`; `SPACE` via `IN_KEY_SCANCODE_SPACE` (confirmed present) → boost.
- **Scheme B is net-new wiring, not a tweak:** its `input_read` branch must read the **Kempston deflection as aim+fire** (stick byte → `aim_dx/dy` + `fire`), the **fire button → fire-in-heading**, the **letter keys → movement** (`decode_move_keys`), and **`S` (`IN_KEY_SCANCODE_S`, confirmed) → boost**. Today scheme B only does fire-in-heading; the tilt-aim path does not exist yet.
- `test_input.c` expectations are updated for the new `fire`/`boost` sourcing per scheme (the pure decode stays host-tested).
- Fix the stale `controls.h` comment that calls `X` the "no-shot centre" (`X` shoots South — `decode_aim_keys(KEY_X)=DIR_S`).

---

## 3. Player movement — faster base + energy boost

All in `player.c` / `player.h` (C; host-tested). Fixed-point at 1/32 px (`PLAYER_SUB=5`) unchanged.

**Faster base** (keep the drift): `PLAYER_MAXV` 64 → **80** (2.0 → **2.5 px/frame**). `PLAYER_FRICTION` stays 3, `PLAYER_ACCEL` stays 12. *The header's "64/32 = 2 px/frame" and drift-distance comments must be rewritten — they will otherwise lie.*

**Boost (energy meter):**
- `player_t` gains `u8 boost_energy` (0..`BOOST_MAX=100`).
- Drain `BOOST_DRAIN=2`/frame while boosting (~1.0 s full boost); recharge `BOOST_RECHARGE=1`/frame otherwise (~2.0 s to full).
- **Active** when `intent.boost` held **and** `boost_energy > 0`.
- Boost changes only the **target speed and accel**, never the friction term (so releasing boost coasts via the usual friction — the drift the user likes). While active: target = `PLAYER_MAXV_BOOST=144` (4.5 px/frame), accel = `PLAYER_ACCEL_BOOST=24`.
- **`ease()` signature change:** today `ease(v, target)` derives step from `target!=0`. It must take the step/accel and max for the call: `ease(v, target, accel_step, friction_step)` — `player_update` passes boosted-or-normal values. (A real structural change, not a constant tweak.) Velocities stay `s16` (boost 144 fits; `move_dx*144` fits `s16`).
- Host tests: held-with-energy drains + caps speed up; empty energy disables boost (falls back to base target); released recharges; drift unchanged.

---

## 3.5 Shooting feedback — visual recoil + muzzle flash (no physics)

Pure render juice — **no effect on `player_t` position/velocity** (a physics kick would fight the smooth/precise dodging the game is built around, and recoil opposite *aim* while moving would feel uncontrollable). On the frame a shot is fired:
- the ship sprite is **drawn offset by 1 px opposite the aim direction** for ~2 frames (a small `recoil_timer` + the stored aim `(dx,dy)` in `main.c`), then snaps back;
- a brief **muzzle flash** — one bright pixel/dot at the ship's front in the aim direction — for 1–2 frames.

Lives in the render path (the player-draw in `main.c`), keyed off "fired this frame." No logic module, no host test (it's a draw-time offset; player state is untouched). Costs nothing measurable (it only shifts where the existing blit lands + one `bul_draw`-style dot).

## 4. Scoring — `score.c` / `score.h` (new, host-tested)

Pure logic, no hardware. **Score is a 6-digit BCD array `u8 digits[6]`** (max 999,999) — *not* `u32`: sdcc's 32-bit add/divide is slow and we only ever show 6 digits and add ≤ +600. BCD add is digit-with-carry; `score_to_digits` is a copy; "÷10 for display" disappears.

| Event | Δ |
|---|---|
| Kill bouncer / chaser / hunter | **+200 / +400 / +600** |
| Bullet fired | **−5** |
| Shield hit (absorbed) | **−10** |
| Death (shields gone) | **−100** |
| Clear wave early | **+ (seconds left) × 10** |

**Rules:** subtractions **clamp at 0** (never negative). **Extra life** on crossing each 10,000 boundary; tracked by a **monotonic** `next_extra` (never decreases — later subtractions don't let you re-earn a passed threshold). On resume (§7) score resets to 0 and `next_extra` back to 10,000.

**BCD subtract (must be borrow-correct — review blocker):** `score_sub` is a borrow-propagating subtract across all 6 digits (a −100 or −10 borrows through runs of zero digits, e.g. `100,003 − 100`). Compute the full subtract first; **if the final borrow is set (underflow), write all-zeros** (the clamp) rather than wrapping. BCD add is the symmetric carry-propagate. Host tests must cover: −100 across a zero-run, subtract-past-zero (clamp), and add-carry across `...999`.

**`score_add` return:** number of 10,000 thresholds newly crossed — **may be > 1** in principle (a single large bonus could cross two); with these values (max +600/kill, max +300 bonus) it is always 0 or 1, but the API and caller loop treat it as a count.

**API (host-tested):**
```c
void score_reset(score_t *s);                 /* digits=0, next_extra=10000   */
u8   score_add(score_t *s, u16 pts);          /* returns # new extra lives     */
void score_sub(score_t *s, u16 pts);          /* clamps at 0                    */
u16  score_enemy_points(u8 level);            /* size-4 table, hole at level 1  */
/* digits live in score_t; HUD reads them directly */
```
`score_enemy_points` is a 4-entry table keyed by `level` (0=bounce, 1=unused, 2=chase, 3=hunter).

---

## 5. Waves — patterns, difficulty table, timer

### 5.1 Spawn patterns (`enemy.c`)

Five patterns, each a `static const` table of up to `MAX_ENEMIES` positions inside `[8..240]×[8..176]`:
1. **PERIMETER** — corners + edge midpoints (today's layout), up to 8.
2. **STAR** — tight central ring around the arena centre.
3. **FLANKS** — two vertical columns by the left/right walls.
4. **ROWS** — horizontal lines top and bottom.
5. **DIAGONALS** — an "X" along the diagonals.

**Indexing:** `wave` is 1-based (§6); the table is `waves[16]` 0-based, so spawn reads `waves[(wave-1) clamped to 0..15]` with **wave>16 looping at index 15** and a guard for `wave==0`. The old hand-filled `SX[MAX_ENEMIES]`/`SY[MAX_ENEMIES]` (exactly 6 entries) are **removed** — the pattern tables replace them (bumping `MAX_ENEMIES` without removing them would zero-fill enemies at (0,0) inside the wall).

**RNG-order contract (so existing seeded tests stay deterministic):** `enemies_spawn(es, wave)` consumes rng in a fixed order — (1) **one draw to pick the pattern**, rerolled if equal to `last_pattern` (a static), then (2) place `count` enemies (levels assigned by the wave table's mix, first-bounce-then-chase-then-hunter, *no* rng), then (3) two draws per bouncer for its `dx/dy` seed. `test_enemy.c` seed expectations are updated for this order.

### 5.2 `MAX_ENEMIES` and the performance gate

The difficulty table wants up to **8** enemies. Raising `MAX_ENEMIES` 6→N ripples into: `enemies_t.e[]`, `MAX_DRAW`→`prev[2][]`, `alive_before[MAX_ENEMIES]` (stack), **and the hand-asm**: `enemy_update.asm` (`ld b,6`), `collide.asm` (`ld b,6`), and the `#if MAX_BULLETS!=2 || MAX_ENEMIES!=6 #error` guard in `collision.c`. All of these must be bumped together.

> **Perf reality (corrected from the review):** post-asm-blit, an 8×8 sprite erase+draw measured ~**5 k T** (not 3 k). 8 enemies + player ≈ **~45 k T of render alone**, before logic + this layer — that likely **busts** the ~55 k usable budget. **Plan for a cap of 6–7** (if it lands at 6, *no asm change is needed* — the asm/guards are already 6; only bump `ld b,N` in `enemy_update.asm` + `collide.asm` and the `collision.c` `#if` guard **if the cap rises above 6**). Update `measure_main.c` to the target count (and use **`wave=16`** for the worst-case hunter-heavy spawn under the new `enemies_spawn(es, wave)` signature — the old `40`-as-kills no longer means anything). **The measurement must be re-run after the HUD (`fx_render`→`score_cell_attr`) and SFX exist (build step 8), not only at step 2** — those add real per-frame cost. Counts clamp to the measured cap (keeping mix ratios); the clamp is `log()`-noted — no silent 50 Hz drop.

### 5.3 Wave timer + early-clear bonus

- `wave_timer` counts **down in frames** from the wave's time budget. A separate `seconds_left = wave_timer / 50` is computed only when needed (clear/bonus and the HUD second tick) — one divide, infrequent.
- Clearing all enemies before 0 → bonus `= seconds_left × 10` (score add + extra-life check + bonus SFX).
- **Expiry:** if it hits 0 first, **no penalty, no bonus** — the wave just continues until cleared (reward mechanic, not punishment).
- The pre-wave telegraph does not count against the timer; the countdown starts when enemies go active.

### 5.4 Difficulty map (waves 1–16)

A single `static const wave_t waves[16]` drives **count, type-mix, pattern, and time budget** per wave (host-testable, trivially tunable). `pick_level` and the old `kills`-threshold mix (`WAVE_CHASE_AT`/`WAVE_HUNTER_AT`) are **removed** — the table is the only difficulty source.

> Enemy *speed* stays `ENEMY_SPEED=1` (the asm `enemies_update` asserts `#if ENEMY_SPEED!=1 #error`); faster enemies are a future asm change, not this spec.

| Wave | Count | B / C / H | Pattern | Time |
|---|---|---|---|---|
| 1 | 4 | 4/0/0 | Perimeter | 30 s |
| 2 | 5 | 5/0/0 | Rows | 30 |
| 3 | 6 | 6/0/0 | Flanks | 30 |
| 4 | 5 | 4/1/0 | Rows | 30 |
| 5 | 6 | 4/2/0 | Star | 30 |
| 6 | 6 | 3/3/0 | Flanks | 30 |
| 7 | 7 | 3/4/0 | Diagonals | 30 |
| 8 | 6 | 2/2/2 | Perimeter | 30 |
| 9 | 7 | 2/3/2 | Star | 25 |
| 10 | 7 | 1/3/3 | Flanks | 25 |
| 11 | 8 | 2/3/3 | Diagonals | 25 |
| 12 | 8 | 1/3/4 | Star | 25 |
| 13 | 8 | 0/4/4 | Flanks | 20 |
| 14 | 8 | 0/3/5 | Rows | 20 |
| 15 | 8 | 0/2/6 | Diagonals | 20 |
| 16 | 8 | 0/1/7 | Star | 20 |

Counts are clamped to the measured `MAX_ENEMIES` cap (§5.2), keeping ratios. **After wave 16:** loop at wave-16 settings with a random pattern each wave — endless scaling. Score/extra-lives keep accruing.

---

## 6. State model, lives, extra lives

**Single source of truth = `wave` (1-based).** A small `game_state_t { u8 wave; score_t score; u8 lives; u8 shields; ... }` plus **pure, host-tested** init/reset functions (no hardware) — this is the load-bearing model the review flagged, made explicit and testable:
- `game_new(state)` → wave=1, score 0, lives=`START_LIVES`, shields=`START_SHIELDS`.
- `game_resume_from_wave(state, wave)` → wave=N, score 0, lives=`START_LIVES`, shields=full (§7).
- `kills` is **dropped as a *difficulty* driver** (the wave table replaces it); difficulty, mix, pattern, and timer all read `wave`. Extra lives come from `score` only (§4).
- **But the per-frame `collide_bullets_enemies` return value is still captured** — it is the hit count / "which enemies died this frame," which the **scoring (points per kill), the explosion FX, and the kill SFX** all need. Only its old role as a cumulative difficulty counter goes away; the loop still reads kills-this-frame to award points and spawn `fx`/`SFX_EXPLODE`.

`START_LIVES` 3 → **2**; `START_SHIELDS` stays 3 (a shield absorbs one hit with i-frames; losing the last shield is a death). Extra lives increment `lives` and fire `SFX_EXTRA_LIFE`; the heart row caps at 8 on the HUD (existing `i<8` cap).

---

## 7. Game over & resume-from-wave

On `lives==0`: a **GAME OVER** screen shows the final score and the wave reached, offering:
- **FIRE / SPACE → resume from WAVE N** (the death wave) — `game_resume_from_wave(state, N)`: score 0 ("licznik zero procent"), lives=`START_LIVES`, full shields, timer reset, difficulty continues from wave N (the table is wave-indexed, so the mix/pattern are coherent).
- **`Q` → fresh game** — `game_new(state)` (wave 1).

Both transitions are the pure functions in §6 → **host-tested** (the most error-prone part is no longer "visual only").

---

## 8. Sound — 1-bit beeper SFX (`sfx.c`/`sfx.h` + `sfx.asm`, target-only)

TC2048 has only the 1-bit beeper (port `0xFE`, bit 4); no AY, no music.

- `sfx.asm`: a tight square-wave blip — toggle **only bit 4**, **preserving the border bits** (the border is the canonical black `0`; the routine ANDs/ORs bit 4 against value `0x00` so it never flickers the border). Target-only; the host build links a no-op `sfx.c` shim (only needed if a host-compiled unit calls it — the pure logic modules don't, so the shim is for completeness).
- Effects (each a short `(half-period, duration)`):
  - `SFX_SHOOT` — a **~1 ms click** (not a tone). It fires at most once per `FIRE_COOLDOWN` (=8 frames, ≈6×/s), *not* per frame; a 1 ms click ≈ ~3.5 k T worst case, comfortably inside the freed budget on the rare frames it plays.
  - `SFX_EXPLODE` / `SFX_HIT` — short blips on enemy death / shield absorb (also cooldown-bounded by kill/hit rate).
  - `SFX_DEATH` / `SFX_BONUS` / `SFX_EXTRA_LIFE` — longer; these play during the **frozen** `death_anim` / wave-clear pause, so their CPU time is free.
- Triggered from the main loop at the event sites. The implementation must confirm the per-frame SFX (shoot/explode/hit) plus render+logic still hold 50 Hz (same measurement pass as §5.2).

---

## 8.5 Title screen — shine sweep

The title loop runs until FIRE/boost is pressed and is **not** under the 50 Hz budget. A bright "glint" sweeps diagonally across the title letters, on attributes only: each frame a sweep position `s` advances (with a pause between passes); for each title cell, brightness is a function of `(col+row) − s` — the cell on the line gets BRIGHT WHITE ink, a trailing cell a lighter shade, the rest the base colour. A few dozen attribute writes/frame in the title loop — free. Lives in the title code; it uses different rows from the menu-highlight rows, so they don't fight. Keep the title's "start" key distinct from confusion with the in-game boost.

---

## 9. HUD layout — minimal (user-directed)

Keep it minimal. Four widgets, three places:

**Top border (row 0):** lives as **heart icons** (left) + shields as **dots** (right) — unchanged from today's style (`hud_draw`), drawn into both bitmaps. This is the only bitmap HUD.

**Bottom border (row 23):** two **bars**, distinguished by colour (no text labels — no room, none needed): **timer bar** (left half, depletes as the wave clock runs down) and **boost-energy bar** (right half, fills/drains with boost). Bars are cell-runs whose attribute paper colour marks the filled portion; cheap, redrawn only on change.

**Score = big attribute digits in the arena background (replacing the checker).** Instead of a tiny readout, the score is rendered as **large digits made of attribute cells across the play field** — the checker (`bg_attr`) is replaced by a chunky 6-digit number, like a scoreboard *behind* the action.

**Exact geometry (pinned — review blocker):** a **3×5-cell** digit font (3 wide, 5 tall), 1-col gap between digits → 6 digits = `6×3 + 5 = 23` cols. Origin: start col **4** (centres 23 cols in the 30-col interior, cols 4–26), rows **9–13** (5 rows, vertically centred in the 1–22 interior). A `static const u8 digitfont[10][5]` (5 rows × 3 bits) defines the glyphs. (3×5 is chosen over 4×6 so the layout fits with comfortable margin, not zero.)

**Dark paper for D1 readability (review blocker):** sprites are **white ink**; a lit score cell sets its **paper** colour, so the paper of lit cells **must be dark** (e.g. dark-blue or non-bright red, BRIGHT off) so white-ink sprites flying over still read. Unlit cells = black. This keeps the D1 "white ink everywhere is readable" guarantee, which otherwise held only against the old checker.

**Restore path (review blocker — all callers, not just FX):** `bg_attr(row,col)` is replaced by a pure `score_cell_attr(row,col)` that reads the cached digit bitmap. **Every** site that currently restores a cell to background must call it: `fx_render`, `death_anim`, **and `telegraph_blink` / `telegraph_clear`** (the telegraph pulses enemy spawn cells, which sit on the digit field — omitting it would punch holes in the score). `bg_paint` paints `score_cell_attr` into **both** attribute blocks (0x5800 + 0x7800).
- On score change, repaint **only the digit cells that changed** (cache last 6 digits) into both blocks — score changes are infrequent (kills; cooldown-gated −5/shot), so cheap.
- `fx_render` runs **every gameplay frame** and now calls `score_cell_attr` for the cells it restores → **measure** its added cost in the perf gate (death_anim is frozen, so its cost is free).

This is **experimental** — if the big-digit background reads poorly in ZEsarUX, fall back to a compact 6-digit readout in the top border (cols 1–6). Decide visually during implementation.

Bitmap glyphs (hearts) reuse the existing sprite path into both buffers; the digit background and bars are pure attribute work.

---

## 10. Architecture & module changes

**New:** `score.c/.h` (pure, tested); `sfx.c/.h` + `sfx.asm` (target; host no-op); `hud.c/.h` (extract HUD out of the ~20 KB `main.c`); a small `game_state`/reset unit (pure, tested — may live in a new `game.c/.h` or alongside score).

**Modified:** `controls.h`/`input.c` (boost intent, per-scheme wiring, stale-comment fix); `player.h`/`player.c` (faster base, boost energy, `ease()` signature); `enemy.h`/`enemy.c` (5 pattern tables, wave table, `wave`-indexed spawn, `MAX_ENEMIES`); `enemy_update.asm` + `collide.asm` + `collision.c` guard (`MAX_ENEMIES` bump if the cap rises); `main.c` (timer, scoring hooks, extra-life, game-over/resume, SFX triggers, wire HUD, wave-indexed loop); `measure_main.c` (8 enemies for the gate); `build.sh` / `test/run.sh` (new sources).

**Boundaries preserved:** `score`, player boost, pattern *positions*, timer math, and the game-state reset are pure and host-tested; only `sfx.asm` and HUD drawing touch hardware.

---

## 11. Testing strategy

- **Host unit tests (TDD):** `score` (values, floor-at-0, monotonic extra-life thresholds, digit/BCD carry); player boost (drain/recharge/cap, gated by energy, drift unchanged); spawn-pattern tables (count, in-bounds) + deterministic pattern selection (seeded, no immediate repeat); wave-timer bonus math; **`game_new` / `game_resume_from_wave`** state transitions.
- **Update existing tests:** `test_input.c` (fire/boost re-sourcing per scheme), `test_enemy.c` (new `enemies_spawn(es, wave)` arity + rng order). List every `enemies_spawn` call site to update: `main.c` (×3), `measure_main.c`, `test_enemy.c`.
- **Emulator (ZEsarUX):** per-scheme control feel; HUD correctness across page-flips; SFX (and border not flickering); game-over→resume; the **50 Hz hold at the chosen enemy cap**.
- **Performance gate:** `measure_main.c` at the target count (spawn `wave=16` for worst-case) **re-run after full integration (HUD `score_cell_attr` in `fx_render` + per-frame SFX exist)**, not only at step 2; total ≤ ~55 k T, else lower the enemy cap and clamp the wave table.

---

## 12. Defaults chosen (please confirm)

- Base `MAXV` 64→80 (2.5 px/f); boost 4.5 px/f; energy 100, drain 2/f (~1 s), recharge 1/f (~2 s).
- Points 200/400/600; bullet −5; shield −10; death −100; floor 0; extra life / 10,000.
- `WAVE_TIME` 30 s (→25 →20 by wave per §5.4); early-clear bonus = sec_left×10; expiry = no penalty.
- `START_LIVES` 2.
- 5 patterns, rng-picked per wave (no immediate repeat); 16-wave table; loop-at-16 after.
- Three control schemes as in §2; game-over offers resume-from-wave (score 0) or fresh.
- Full beeper SFX; title diagonal shine-sweep.
- **HUD minimal:** hearts+dots top, timer+boost bars bottom, **score as big attribute-cell digits in the background** (3×5 font, dark paper for white-ink readability; experimental, fallback to a compact readout).
- **Shooting feedback:** visual-only recoil (1 px kick opposite aim, ~2 frames) + muzzle flash — no physics.
- **`MAX_ENEMIES`: target 8, but realistically capped at 6–7 by the perf gate (§5.2) — confirmed by measurement, not assumed.**

---

## 13. Build order (sub-agent implementation)

State model + pure modules first; the perf gate early; integration last.
1. **`game_state` + `score.c/.h`** + tests — the load-bearing model, pure & isolated.
2. **`enemy.c` patterns + wave table + `wave`-indexed `enemies_spawn`**, bump `MAX_ENEMIES` target, update asm bounds + guard, update `measure_main.c` → **MEASURE the enemy cap** and fix `MAX_ENEMIES`.
3. **Player boost** (`player.c/.h`, `ease()` signature) + faster base + tests.
4. **Controls** (`controls.h`/`input.c` three schemes + boost) + `test_input.c`.
5. **`sfx.asm`/`.c/.h`** (+ host shim) + border-safe beeper.
6. **`hud.c/.h`**: hearts+dots (top), timer+boost bars (bottom), and the **score-as-big-attribute-digits background** replacing `bg_attr`/the checker (`score_cell_attr` + both-buffer repaint of changed digits + FX-restore path). Cache widgets; visually validate the big-digit background early (fallback: compact top readout).
7. **Title** shine-sweep.
8. **`main.c` integration:** wave-indexed loop, timer, scoring hooks, extra-life, game-over/resume, SFX triggers, HUD wiring, **visual recoil + muzzle flash** in the player-draw.
9. Build, host tests, ZEsarUX visual + **re-run the 50 Hz measurement with HUD+SFX live at the chosen cap**.
