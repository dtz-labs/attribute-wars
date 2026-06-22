# Gameplay Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **Companion spec (full design, read it):** `docs/superpowers/specs/2026-06-22-gameplay-layer-design.md`. This plan is the task contract; the spec holds the rationale and the per-cell/per-scheme detail.

**Goal:** Add the gameplay layer — three boost-aware control schemes, faster base + energy boost, BCD scoring with extra lives, 5 spawn patterns + a 16-wave difficulty table + per-wave timer, 2 lives, game-over/resume-from-wave, beeper SFX, a minimal HUD with a big-attribute-digit score background, a title shine-sweep, and visual shooting recoil.

**Architecture:** Pure-logic modules (`score`/`game_state`, wave/pattern tables, player boost, input decode) are host-tested with native `cc`; only `sfx.asm` and HUD/title attribute drawing touch hardware. The 50 Hz loop in `main.c` orchestrates; difficulty is driven entirely by a 1-based `wave` number via a `static const` table (the old `kills`-threshold mix is removed).

**Tech Stack:** C99 + Z80 asm, z88dk `zcc +zx -clib=sdcc_iy`, host tests via `cc` (`test/run.sh`), ZEsarUX (TC2048) for visual + `z88dk-ticks` for the perf gate.

## Global Constraints

- **No floating point, no `malloc`, no recursion.** Integer math, fixed arrays, object pools, precomputed tables.
- **Never return a struct by value — pass via out-pointer** (sdcc z80 SIGSEGVs on struct-return-by-value).
- **Only `scld.c` knows port `0xFF` and the `0x4000`/`0x6000` screens.** Beeper = port `0xFE` bit 4; the ULA border is the other `0xFE` bits and is canonical black `0x00` — beeper must mask only bit 4.
- **sdcc is poor at 16/32-bit math** → score is **6-digit BCD `u8[6]`**, not `u32`.
- **Host-test boundary:** target-only code is guarded `#ifdef __SDCC`; the C reference/no-op shim stays for the host build. Hardware verified visually in ZEsarUX + cycle-counted with `z88dk-ticks`.
- **Frame budget = 69,888 T @ 50 Hz (~55k usable).** `MAX_ENEMIES` starts at **6**; raising it requires editing `ld b,6` in `enemy_update.asm` + `collide.asm` and the `#if … MAX_ENEMIES!=6` guard in `collision.c`, and is gated on measurement (Task 9).
- **D1:** sprites are white ink everywhere → the score-digit background paper must be **dark** so white sprites stay readable.
- Build: `./build.sh` → `build/game.tap`; host tests: `./test/run.sh`; both must stay green. New sources are added to both.
- Commit message footer: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

## File structure

| File | Responsibility |
|---|---|
| `include/score.h` / `src/score.c` | **new** — BCD score economy + `game_state` reset (pure, host-tested) |
| `test/test_score.c` | **new** — score + game_state tests |
| `include/sfx.h` / `src/sfx.c` / `src/sfx.asm` | **new** — beeper SFX (target asm + C trigger; host no-op shim) |
| `include/hud.h` / `src/hud.c` | **new** — HUD: hearts/dots, timer/boost bars, score-as-big-digits background (`score_cell_attr`) |
| `include/controls.h` / `src/input.c` | mod — `intent_t.boost`; three boost-aware schemes; scheme-B tilt-aim |
| `include/player.h` / `src/player.c` | mod — faster base, boost energy, `ease()` signature |
| `include/enemy.h` / `src/enemy.c` | mod — 5 pattern tables, 16-wave table, `enemies_spawn(es, wave)`; remove `SX/SY`, `pick_level`, kill thresholds |
| `src/main.c` | mod — wave-indexed loop, timer, scoring/extra-life hooks, game-over/resume, SFX triggers, recoil, title shine-sweep, wire HUD |
| `src/measure_main.c` | mod — `enemies_spawn(es, 16)`; bump sprite count when measuring the cap |
| `enemy_update.asm`, `collide.asm`, `collision.c` | mod — **only if** the cap rises above 6 (Task 9) |
| `build.sh`, `test/run.sh` | mod — add `score.c`, `sfx.*`, `hud.c` |
| `test/test_enemy.c`, `test/test_input.c`, `test/test_player.c` | mod — updated expectations |

Execution order respects dependencies: **1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9**. Tasks 1 and 5 are independent (parallelizable). Tasks 6/7/8 all edit `main.c` → keep sequential.

---

### Task 1: Score + game-state module (pure, host-tested)

**Files:**
- Create: `include/score.h`, `src/score.c`, `test/test_score.c`
- Modify: `test/run.sh` (add the test), `build.sh` (add `src/score.c` to the game link)

**Interfaces — Produces:**
```c
/* score.h */
#include "types.h"
typedef struct { u8 digits[6]; u16 next_extra; } score_t;  /* digits[0]=most significant */
typedef struct { u8 wave; score_t score; u8 lives; u8 shields; } game_state_t;

#define START_LIVES   2u
#define START_SHIELDS 3u
#define EXTRA_LIFE_EVERY 10000u

void score_reset(score_t *s);                 /* digits=0, next_extra=10000 */
u8   score_add(score_t *s, u16 pts);          /* BCD add; returns # of 10k thresholds newly crossed */
void score_sub(score_t *s, u16 pts);          /* BCD borrow; clamps at 0 */
u16  score_enemy_points(u8 level);            /* 200/—/400/600 keyed by ENEMY_* level (size-4, hole at 1) */
void game_new(game_state_t *g);               /* wave=1, score reset, lives=START_LIVES, shields=START_SHIELDS */
void game_resume_from_wave(game_state_t *g, u8 wave);  /* wave=wave, score reset, lives/shields reset */
```
Implementation notes: store the score as a 6-digit array; `score_add`/`score_sub` are decimal carry/borrow loops over the 6 digits (no `u32`, no division). `score_add` compares the new value against `next_extra` and, while `value >= next_extra`, counts a life and advances `next_extra += 10000` (monotonic). `score_sub` computes the full borrow-propagating subtract; **if the final borrow is set, zero all digits** (clamp). `score_enemy_points`: table `{200,0,400,600}` indexed by level (`ENEMY_BOUNCE=0,CHASE=2,HUNTER=3` from `enemy.h`).

- [ ] **Step 1: Write failing tests** (`test/test_score.c`)

```c
#include <assert.h>
#include <stdio.h>
#include "score.h"
#include "enemy.h"
static int checks = 0;
#define CHECK(c) do { assert(c); ++checks; } while (0)

static u32 val(const score_t *s){ u32 v=0; for(int i=0;i<6;i++) v=v*10+s->digits[i]; return v; }

static void test_add_carry(void){
    score_t s; score_reset(&s);
    score_add(&s, 600); CHECK(val(&s)==600);
    score_reset(&s); for(int i=0;i<6;i++) s.digits[i]=9; s.digits[0]=0; /* 099999 */
    score_add(&s, 5); CHECK(val(&s)==100004);          /* carry across the 9-run */
}
static void test_sub_borrow_and_clamp(void){
    score_t s; score_reset(&s);
    score_add(&s, 0); s.digits[0]=1;s.digits[1]=0;s.digits[2]=0;s.digits[3]=0;s.digits[4]=0;s.digits[5]=3; /* 100003 */
    score_sub(&s, 100); CHECK(val(&s)==99903);         /* borrow through the zero-run */
    score_reset(&s); score_add(&s, 50);
    score_sub(&s, 100); CHECK(val(&s)==0);             /* underflow clamps at 0, not wrap */
}
static void test_extra_life(void){
    score_t s; score_reset(&s);
    CHECK(score_add(&s, 600)==0);
    /* climb to just under 10000 then cross once */
    for(int i=0;i<15;i++) score_add(&s, 600);          /* 9600 */
    u8 lives = score_add(&s, 600);                      /* crosses 10000 once */
    CHECK(lives==1 && s.next_extra==20000);
    /* losing points must NOT let you re-earn the passed threshold */
    score_sub(&s, 5000); CHECK(score_add(&s, 4000)==0); /* back over 10000, but next_extra is 20000 */
}
static void test_enemy_points(void){
    CHECK(score_enemy_points(ENEMY_BOUNCE)==200);
    CHECK(score_enemy_points(ENEMY_CHASE)==400);
    CHECK(score_enemy_points(ENEMY_HUNTER)==600);
}
static void test_game_state(void){
    game_state_t g; game_new(&g);
    CHECK(g.wave==1 && g.lives==START_LIVES && g.shields==START_SHIELDS && val(&g.score)==0);
    g.score.digits[5]=9; g.wave=14; g.lives=0;
    game_resume_from_wave(&g, 14);
    CHECK(g.wave==14 && val(&g.score)==0 && g.score.next_extra==10000 && g.lives==START_LIVES && g.shields==START_SHIELDS);
}
int main(void){ test_add_carry(); test_sub_borrow_and_clamp(); test_extra_life();
                test_enemy_points(); test_game_state();
                printf("score: %d checks passed\n", checks); return 0; }
```

- [ ] **Step 2: Run to verify it fails** — add to `test/run.sh` a line `$CC $CFLAGS "$ROOT/test/test_score.c" "$ROOT/src/score.c" -o "$OUT/test_score" && "$OUT/test_score"`. Run `./test/run.sh`. Expected: FAIL (no `score.c`).
- [ ] **Step 3: Implement `include/score.h` + `src/score.c`** per the Interfaces + notes above (BCD carry/borrow loops, monotonic `next_extra`, clamp-on-borrow).
- [ ] **Step 4: Run `./test/run.sh`** — Expected: `score: N checks passed`, ALL HOST TESTS PASSED.
- [ ] **Step 5: Add `src/score.c` to `build.sh`'s source list; build `./build.sh`** — Expected: clean tap.
- [ ] **Step 6: Commit** — `feat: add BCD score + game_state module (host-tested)`.

---

### Task 2: Wave table + 5 spawn patterns + `wave`-indexed spawn

**Files:**
- Modify: `include/enemy.h`, `src/enemy.c`, `test/test_enemy.c`, `src/measure_main.c`

**Interfaces:**
- Consumes: nothing new.
- Produces: `void enemies_spawn(enemies_t *es, u8 wave);` (was `(es, total_kills)`); `enemy.h` exposes pattern/count for tests via the table.
```c
/* enemy.h — replaces WAVE_CHASE_AT/WAVE_HUNTER_AT/pick_level */
#define WAVE_COUNT 16
enum { PAT_PERIMETER, PAT_STAR, PAT_FLANKS, PAT_ROWS, PAT_DIAGONALS, PAT_N };
typedef struct { u8 count, n_bounce, n_chase, n_hunter, pattern; u16 time_frames; } wave_t;
extern const wave_t wave_table[WAVE_COUNT];   /* §5.4 of the spec, verbatim */
```
Notes (full detail in spec §5.1/§5.4): `enemies_spawn(es, wave)` indexes `wave_table[(wave>=1?wave-1:0)]`, looping at index 15 for wave>16. RNG order: (1) one `rng_byte()` to pick a pattern, rerolled while `== last_pattern` (a `static u8`), (2) place `count` positions from the chosen pattern's `static const` table, assigning levels first-bounce-then-chase-then-hunter from the wave row (no rng), (3) two `rng_byte()` per bouncer for `dx/dy`. Counts are clamped to `MAX_ENEMIES` (stays 6 this task; raised only in Task 9). Delete `SX[]/SY[]`, `pick_level`, the kill thresholds. Pattern tables hold up to 8 positions; only `count` are used.

- [ ] **Step 1: Write/юupdate failing tests** (`test/test_enemy.c`): for a fixed `rng_seed`, `enemies_spawn(&es, 1)` → 4 alive, all `ENEMY_BOUNCE`, all positions in `[ARENA_L..ARENA_R]×[ARENA_T..ARENA_B]`; `enemies_spawn(&es, 8)` → 6 alive with mix 2/2/2 (clamped from the table's 6); two consecutive spawns never pick the same pattern; existing movement tests still pass with the new arity.

```c
/* representative new assertions (keep existing enemies_update tests) */
static void test_spawn_wave1(void){
    enemies_t es; rng_seed(0xACE1u); enemies_spawn(&es, 1);
    u8 alive=0,b=0; for(u8 i=0;i<MAX_ENEMIES;i++){ if(es.e[i].alive){alive++; if(es.e[i].level==ENEMY_BOUNCE)b++;
        CHECK(es.e[i].x>=ARENA_L && es.e[i].x<=ARENA_R && es.e[i].y>=ARENA_T && es.e[i].y<=ARENA_B);} }
    CHECK(alive==4 && b==4);
}
static void test_pattern_no_repeat(void){
    enemies_t es; rng_seed(0x1234u); u8 first, second;
    enemies_spawn(&es,5); first=enemy_last_pattern();
    enemies_spawn(&es,6); second=enemy_last_pattern();
    CHECK(first!=second);
}
```
(Expose `u8 enemy_last_pattern(void);` for the test, returning the static.)

- [ ] **Step 2: Run `./test/run.sh`** — Expected: FAIL (arity / missing symbols).
- [ ] **Step 3: Implement** the wave table (spec §5.4), the 5 pattern position tables (spec §5.1), the new `enemies_spawn`, `enemy_last_pattern`. Update **all `enemies_spawn` call sites to the new arity**: `main.c` lines ~413/467/494 (pass the wave number — placeholder `1` until Task 8 wires `wave`), `measure_main.c` line ~62 → `enemies_spawn(&enemies, 16)` (worst-case mix).
- [ ] **Step 4: Run `./test/run.sh`** — Expected: all pass.
- [ ] **Step 5: `./build.sh`** — Expected: clean (still `MAX_ENEMIES=6`; no asm change yet).
- [ ] **Step 6: Commit** — `feat: wave-indexed spawn with 5 patterns + 16-wave table`.

---

### Task 3: Controls — `intent_t.boost` + three boost-aware schemes

**Files:**
- Modify: `include/controls.h`, `src/input.c`, `test/test_input.c`

**Interfaces — Produces:** `intent_t` gains `u8 boost;`. Decode helpers per spec §2; `make_intent` no longer sets `fire` from `JOY_FIRE`.
- Consumes: existing `decode_move`, `decode_aim_keys`, `decode_move_keys`, `update_facing`.

Notes (spec §2, the authoritative table): Scheme A — Kempston/cursor move, QWEADZXC `fire`, `JOY_FIRE`(incl cursor `0`)+`SPACE`→`boost`. Scheme B (net-new wiring) — letter keys move (`decode_move_keys`), Kempston tilt→`aim`+`fire`, fire button→fire-in-heading, `S`→`boost`. Scheme C — left stick move + its fire→`boost`, right stick→`aim`+`fire`. Add `IN_KEY_SCANCODE_SPACE`/`..._S` reads on the target path; the pure `make_intent` gains a `u8 boost_in` param (or the branches set `out->boost` directly — keep `make_intent` pure & host-tested). Fix the stale `controls.h` "X = no-shot centre" comment.

- [ ] **Step 1: Update failing tests** (`test/test_input.c`): with `JOY_FIRE` set, `make_intent` now yields `out->fire==0` and `out->boost!=0` (scheme A); QWEADZXC still set `aim`+`fire`; scheme-B helper maps Kempston tilt → aim+fire and `S` → boost.
- [ ] **Step 2: Run `./test/run.sh`** — Expected: FAIL.
- [ ] **Step 3: Implement** the `boost` field, the three `input_read` branches, the `make_intent` change, the scheme-B tilt-aim path. Keep the decode pure/host-tested; the hardware key reads stay target-only.
- [ ] **Step 4: Run `./test/run.sh`** — Expected: pass.
- [ ] **Step 5: `./build.sh`** — Expected: clean (`__SPECTRUM` key reads compile).
- [ ] **Step 6: Commit** — `feat: three boost-aware control schemes; fire/boost re-sourced`.

---

### Task 4: Player — faster base + energy boost

**Files:**
- Modify: `include/player.h`, `src/player.c`, `test/test_player.c`

**Interfaces:**
- Consumes: `intent_t.boost` (Task 3).
- Produces: `player_t` gains `u8 boost_energy;`. `ease()` signature becomes `static s16 ease(s16 v, s16 target, s16 accel, s16 fric)`.
```c
/* player.h additions */
#define PLAYER_MAXV        80     /* 2.5 px/frame (was 64) — rewrite the header comment block, it currently says "2 px/frame" */
#define PLAYER_ACCEL       12
#define PLAYER_FRICTION    3
#define PLAYER_MAXV_BOOST  144    /* 4.5 px/frame while boosting */
#define PLAYER_ACCEL_BOOST 24
#define BOOST_MAX          100
#define BOOST_DRAIN        2
#define BOOST_RECHARGE     1
```
Notes: `player_update(p, in)` reads `in->boost`. Boost **active** iff `in->boost && p->boost_energy>0`; active → drain energy by `BOOST_DRAIN` (floor 0) and pass `PLAYER_MAXV_BOOST`/`PLAYER_ACCEL_BOOST` as the target/accel to `ease()`; inactive → recharge by `BOOST_RECHARGE` (cap `BOOST_MAX`) and pass `PLAYER_MAXV`/`PLAYER_ACCEL`. Friction term unchanged in both (drift preserved).

- [ ] **Step 1: Update failing tests** (`test/test_player.c`): holding move+boost with energy → reaches a higher top speed than without and `boost_energy` decreases; boost held with `boost_energy==0` → tops out at base speed; releasing boost → `boost_energy` increases (cap `BOOST_MAX`); the existing drift/wall/facing tests still pass (drift loop counts already updated for the longer coast).

```c
static intent_t mvb(s8 dx,s8 dy,u8 b){ intent_t in={0,0,0,0,0,0}; in.move_dx=dx; in.move_dy=dy; in.boost=b; return in; }
static void test_boost_faster_and_drains(void){
    player_t p; player_init(&p,100,80); intent_t in=mvb(1,0,1);
    u8 e0=p.boost_energy; for(u8 k=0;k<10;k++) player_update(&p,&in);
    CHECK(p.boost_energy < e0);            /* drained */
    CHECK(p.vx > (s16)(PLAYER_MAXV));      /* exceeds base top speed while boosting (fixed-point) */
}
static void test_boost_empty_caps_at_base(void){
    player_t p; player_init(&p,100,80); p.boost_energy=0; intent_t in=mvb(1,0,1);
    for(u8 k=0;k<30;k++) player_update(&p,&in);
    CHECK(p.vx <= (s16)PLAYER_MAXV);       /* no boost without energy */
}
```
(Confirm the exact `vx` fixed-point scale against `PLAYER_SUB`; the assertions compare in the same `1/32 px` units as `vx`.)

- [ ] **Step 2: Run `./test/run.sh`** — Expected: FAIL.
- [ ] **Step 3: Implement** the `ease()` signature change (update both call sites in `player_update`), `boost_energy`, faster base, **rewrite the lying header comments**.
- [ ] **Step 4: Run `./test/run.sh`** — Expected: pass.
- [ ] **Step 5: `./build.sh`** — Expected: clean.
- [ ] **Step 6: Commit** — `feat: faster base ship + energy-metered boost`.

---

### Task 5: Sound — beeper SFX (target asm + host shim)

**Files:**
- Create: `include/sfx.h`, `src/sfx.c`, `src/sfx.asm`
- Modify: `build.sh` (add `src/sfx.c` + `src/sfx.asm`), `test/run.sh` (only if a host-compiled unit calls sfx — none do, so likely no change)

**Interfaces — Produces:**
```c
/* sfx.h */
enum { SFX_SHOOT, SFX_EXPLODE, SFX_HIT, SFX_DEATH, SFX_EXTRA_LIFE, SFX_BONUS, SFX_N };
void sfx_play(u8 id);     /* synchronous beeper blip; target only, host no-op */
```
Notes (spec §8): `sfx.c` under `#ifdef __SDCC` sets a globals pair `(half_period, duration)` from a `static const` table keyed by `id` and calls an asm `sfx_blip` that toggles **only bit 4** of port `0xFE` (border bits stay `0x00`) in a timed loop; under `#else` it is an empty no-op. `SFX_SHOOT` ≈ 1 ms click (gated ≤6×/s by `FIRE_COOLDOWN`); `SFX_DEATH/BONUS/EXTRA_LIFE` longer (play during frozen pauses). Pattern mirrors `blit.asm`: `PUBLIC`/`EXTERN` globals, `SECTION code_user`, no IY.

- [ ] **Step 1: Implement** `sfx.h`, `sfx.c` (table + `#ifdef __SDCC` wrapper / no-op), `sfx.asm` (`sfx_blip` toggling bit 4, border-safe).
- [ ] **Step 2: Add to `build.sh`; `./build.sh`** — Expected: clean tap.
- [ ] **Step 3: Run `./test/run.sh`** — Expected: still green (host unaffected).
- [ ] **Step 4: Commit** — `feat: 1-bit beeper SFX (border-safe), host no-op`.
- [ ] (Audible verification happens in Task 9 on ZEsarUX.)

---

### Task 6: HUD module — hearts/dots, bars, score-as-big-digits background

**Files:**
- Create: `include/hud.h`, `src/hud.c`
- Modify: `src/main.c` (move `hud_draw` + the attribute helpers; swap `bg_attr`→`score_cell_attr` at **all** restore sites), `build.sh`, `include/score.h` consumed.

**Interfaces — Produces:**
```c
/* hud.h */
#include "score.h"
u8   score_cell_attr(u8 row, u8 col);                 /* big-digit background colour for a cell (replaces bg_attr) */
void hud_paint_background(const score_t *s);          /* paint score_cell_attr into BOTH attr blocks */
void hud_score_changed(const score_t *s);             /* repaint only changed digit cells, both blocks */
void hud_draw_lives(u8 lives);                        /* hearts, both bitmaps */
void hud_draw_shields(u8 shields);                    /* dots, both bitmaps */
void hud_draw_timer(u16 frames_left, u16 frames_total);   /* bottom-left bar, both blocks */
void hud_draw_boost(u8 energy);                       /* bottom-right bar, both blocks */
```
Notes (spec §9): 3×5 digit font `static const u8 digitfont[10][5]`, origin row 9 col 4, dark paper for lit cells. `score_cell_attr` reads a cached `u8 last_digits[6]`. **Every** background-restore caller switches from `bg_attr` to `score_cell_attr`: `fx_render`, `death_anim`, `telegraph_blink`, `telegraph_clear`, `bg_paint`(→`hud_paint_background`). Make `bg_attr` for rows 0/23 the HUD black background and keep side rails magenta (spec §9). All HUD glyph/bar writes target both `SCLD_SCREEN_A` and `_B`. Cache last-rendered lives/shields/timer-second/boost-bar-step → redraw only on change.

- [ ] **Step 1: Implement** `hud.c/.h`; move the relevant helpers out of `main.c`; implement `score_cell_attr` + the font + bars + hearts/dots.
- [ ] **Step 2: Swap all `bg_attr` restore callers to `score_cell_attr`** (fx, death, telegraph, paint). Build `./build.sh`.
- [ ] **Step 3: Run `./test/run.sh`** — Expected: green (HUD is target-only; no host test, but the link must stay clean).
- [ ] **Step 4: ZEsarUX smoke** — title→play, confirm the score-digit background renders, sprites stay readable on the dark paper, an explosion restores the digits (no holes). If it reads poorly, switch to the compact top-readout fallback (spec §9) — note the decision.
- [ ] **Step 5: Commit** — `feat: HUD (hearts/dots, timer/boost bars, score-digit background)`.

---

### Task 7: Title shine-sweep

**Files:** Modify: `src/main.c` (`title_screen`).

Notes (spec §8.5): in the title loop, advance a sweep position `s` each frame (with a pause between passes); for each title-letter cell set BRIGHT WHITE ink on the cell where `(col+row)≈s`, a lighter trailing cell, base colour elsewhere — attributes only, both attr blocks. Don't fight the menu-highlight rows (different rows). Title loop is unbounded → no budget concern.

- [ ] **Step 1: Implement** the sweep in `title_screen`.
- [ ] **Step 2: `./build.sh`; ZEsarUX** — confirm the glint sweeps and the menu still works.
- [ ] **Step 3: Commit** — `feat: title-screen diagonal shine-sweep`.

---

### Task 8: `main.c` integration

**Files:** Modify: `src/main.c`.

Wire everything (spec §5.3, §6, §7, §3.5, §8 triggers):
- Replace the implicit-`kills` loop with a `game_state_t` driven by `wave`; `enemies_spawn(es, g.wave)`; advance `g.wave` on wave-clear (loop ≥16).
- **Wave timer:** `wave_timer` (frames) counts down once enemies are active; on clear before 0, `score_add` the bonus (`(wave_timer/50)*10`), apply returned extra lives (+`SFX_EXTRA_LIFE`), `SFX_BONUS`. Expiry: no penalty.
- **Scoring hooks:** capture the `collide_bullets_enemies` return → for each killed enemy add `score_enemy_points(level)` + `fx_spawn` + `SFX_EXPLODE`; on fire add `score_sub(5)` + `SFX_SHOOT`; on shield hit `score_sub(10)` + `SFX_HIT`; on death `score_sub(100)` + `SFX_DEATH`. Apply `score_add` extra-life returns to `g.lives` (+`SFX_EXTRA_LIFE`).
- **Game over** (`g.lives==0`): screen with final score + wave; FIRE/SPACE → `game_resume_from_wave(&g, death_wave)`; `Q` → `game_new(&g)`.
- **Visual recoil** (spec §3.5): on a shot, set `recoil_timer=2` + store aim `(dx,dy)`; in the player-draw, offset the ship 1 px opposite aim while `recoil_timer>0` and draw a muzzle-flash dot at the front. No `player_t` change.
- Wire HUD calls (background on wave start/score change; lives/shields/timer/boost on change).

- [ ] **Step 1:** Integrate state model + spawn + timer.
- [ ] **Step 2:** Integrate scoring + extra-life + SFX triggers + recoil.
- [ ] **Step 3:** Game-over/resume screen + HUD wiring.
- [ ] **Step 4: `./build.sh`; `./test/run.sh`** — Expected: clean + green.
- [ ] **Step 5: Commit** — `feat: integrate gameplay layer into the main loop`.

---

### Task 9: Performance gate + full verification

**Files:** Modify (conditionally): `enemy.h` (`MAX_ENEMIES`), `enemy_update.asm`, `collide.asm`, `collision.c` guard, `measure_main.c`.

- [ ] **Step 1: Measure** — build `measure_main.c` (spawn `wave=16`) at the target counts with HUD (`fx_render`→`score_cell_attr`) and SFX live; `z88dk-ticks` the per-frame segments (per the spec's measurement recipe). Confirm ≤ ~55k T at `MAX_ENEMIES=6`.
- [ ] **Step 2: Decide the cap** — if 7 fits, bump `MAX_ENEMIES`→7 and **in lockstep** edit `ld b,6`→`ld b,7` in `enemy_update.asm` + `collide.asm` and the `#if … MAX_ENEMIES!=6` guard in `collision.c`; re-measure. Else stay 6 and `log()` that the wave table clamps 8→6. (8 is not expected to fit.)
- [ ] **Step 3: Re-run the asm differential tests** if `MAX_ENEMIES` changed (the `enemy_update.asm`/`collide.asm` byte-identical benches from the optimisation pass).
- [ ] **Step 4: Full host suite** `./test/run.sh` green; **ZEsarUX** play-through: all three schemes, boost bar, recoil, score-digit background, timer bonus, extra life, death→resume-from-wave, all SFX, 50 Hz hold.
- [ ] **Step 5: Commit** — `perf: set enemy cap from measurement; verify 50 Hz` (+ a CLAUDE.md/memory note on the final cap).

---

## Self-review (plan vs spec)

- **Coverage:** controls/boost (T3), faster base+boost (T4), BCD score+extra life (T1), 5 patterns+16-wave table+timer (T2,T8), 2 lives+resume (T1,T8), SFX (T5,T8), HUD+score-background (T6), title sweep (T7), recoil (T8), perf gate+cap (T9). All spec sections map to a task.
- **Types consistent:** `score_t`/`game_state_t`/`enemies_spawn(es,wave)`/`ease(v,target,accel,fric)`/`score_cell_attr` are used with the same signatures across tasks.
- **Open risks flagged in-task:** the enemy cap (T9 measurement, likely 6), the score-digit-background readability (T6 visual gate + fallback). No silent caps.
