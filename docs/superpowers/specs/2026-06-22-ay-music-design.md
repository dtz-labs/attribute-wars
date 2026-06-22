# AY-3-8910 Music — auto-detected, beeper SFX kept

**Date:** 2026-06-22 · **Target:** Timex TC2048 (Z80A @ 3.5 MHz, 50 Hz), z88dk sdcc_iy, ZEsarUX.
**Status:** Design for review.

Adds **AY chiptune music** (Pator's *"Spectrumizer"*, a `.pt3` ProTracker-3 tune) on top of the gameplay slice, playing on the **title screen and during gameplay**, while the existing **1-bit beeper SFX stay exactly as they are**. The music is **conditional on an AY chip being present at runtime**: it auto-detects the AY (and which I/O ports answer), and on a machine without one — including the bare **TC2048**, which is beeper-only — every music call is a cheap no-op and the game is unchanged.

Engine context that constrains every choice: frame = **69,888 T** @ 50 Hz (~55k usable); **no float / malloc / recursion**; only `scld.c` knows port `0xFF` and the `0x4000/0x6000` screens; the 1-bit beeper is port `0xFE` bit 4; the loop `HALT`s exactly once per frame via `scld_present()`. This work follows a render optimisation that freed ~3.5k T/frame (zero-fill sprite erase), which is roughly the headroom the music player needs.

---

## 1. Hardware reality (why this is conditional)

The **TC2048 has no AY** — it is a 48K-class, beeper-only machine. The **AY-3-8912 is in the TS2068 / TC2068**; the **AY-3-8910/8912 is in the ZX Spectrum 128/+2/+3**. So an AY tune is **silent on real TC2048 hardware**. The two AY I/O port schemes differ by machine:

| Machine | register-select port | data port |
|---|---|---|
| ZX 128 / +2 / +3 (and 48K + AY interface) | `0xFFFD` | `0xBFFD` |
| TS2068 / TC2068 | `0x00F5` | `0x00F6` |

The design therefore **detects the chip and the port pair at runtime** and supports all of the above; absence ⇒ silence.

---

## 2. Goals & non-goals

**In scope:** play one looping AY tune (`Spectrumizer.pt3`) on the title screen and continuously through gameplay; runtime AY detection across the 128K and TS2068 port schemes; beeper SFX kept unchanged and mixed over the music; graceful silent fallback with zero gameplay change when no AY is present; a measured 50 Hz performance gate.

**Non-goals:** moving SFX onto the AY; per-screen / per-wave different tracks; music volume/mute UI; an interrupt-driven (IM2) player (explicitly rejected — see §5); SFX-ducking of the music; high-score or settings persistence. (`music_stop()` exists but is unused — YAGNI.)

---

## 3. Module boundary

A new **`music`** module owns *all* AY-port access, exactly as `scld.c` owns `0xFF` and `sfx.asm` owns the beeper. Nothing else in the codebase touches an AY port.

- **`include/music.h`** — the API:
  - `u8   music_init(void);`  — probe for an AY, latch the answering port pair, hand the tune to the PT3 player, start it. Returns 1 if an AY was found (music will play), 0 otherwise.
  - `void music_tick(void);`  — advance the player one 50 Hz frame (the PT3 "play one frame" call). No-op when no AY. Called once per `HALT` (see §6).
  - `void music_stop(void);`  — silence all AY channels (defined for completeness; unused in this slice).
- **`src/music.c`** — target-only C glue, `#ifdef __SDCC` like `sfx.c`; the `#else` host build makes every entry an empty no-op so host unit tests never pull in hardware. Holds the latched `sel`/`dat` port words and the `music_on` flag; thin wrappers over the asm (`pt3_init` / `pt3_play` / `pt3_mute`, parameterless, reading globals — project asm convention, spec §15.5 of the foundation doc).
- **`src/pt3player.asm`** — a public-domain Z80 ProTracker-3 player. Its single **AY-write primitive is parameterised**: `ld bc,(sel_port) / out (c),a` to select a register and `ld bc,(dat_port) / out (c),a` to write data, where `sel_port`/`dat_port` are the latched 16-bit port words (`0xFFFD`/`0xBFFD` **or** `0x00F5`/`0x00F6`). The `out (c),a` 16-bit form drives the correct high address byte for both schemes (`0xFF`/`0xBF` for the 128K; `0x00` for the TS2068, whose AY ignores the high byte). Also hosts the AY-detect probe (§4). Never touches **IY** (sdcc_iy frame pointer).
- **`src/tune.asm`** — `PUBLIC _pt3_tune` + z88dk `BINARY "../assets/spectrumizer.pt3"` (path relative to `src/`; the canonical asset location is §8). The linker places the player and tune in the `code_user` segment after the existing code.

Host-test boundary: `music.c` host build is a no-op shim; there is no pure-logic to unit-test here (it is all hardware), matching how `sfx.c`/`scld.c` are handled. Verification is by ZEsarUX (audio) + `z88dk-ticks` (budget).

---

## 4. AY detection + port routing

A probe routine (asm, called from `music_init`) tries the port pairs **in order**: `0xFFFD/0xBFFD` first, then `0x00F5/0x00F6`. For each pair:

1. Select AY register **R0** (channel-A tone fine — a full 8-bit, read-back-able register).
2. Write `0x55`, read it back; write `0xAA`, read it back.
3. **Both** patterns echo ⇒ an AY is decoding this pair. Latch `sel_port`/`dat_port`, set `music_on = 1`, restore R0 = 0, return found.

If neither pair echoes both patterns, `music_on = 0` and every later `music_tick()` returns immediately. Requiring **two distinct patterns** to echo defeats the floating-bus (`0xFF`) false positive on a no-AY machine. Writing to an undecoded port on a non-AY machine is harmless (no device latches it).

**Known soft spot:** AY detection is historically unreliable across exotic clones. The conservative failure mode here is a **false negative ⇒ silence**, never a crash; a false positive would only write to an undecoded port. Accepted.

---

## 5. Why main-loop driven, not interrupt-driven (IM2)

The player is ticked from the main loop, **keeping the existing IM1 setup** (`scld_init` does `im 1; ei`; `scld_present()`/`scld_wait()` HALT on the 50 Hz frame interrupt). Rationale:

- The loop already `HALT`s **exactly once per frame**, so a `music_tick()` next to each HALT is a clean, correct 50 Hz cadence with no extra timing machinery.
- An IM2 player would mean replacing the ROM ISR with an aligned IM2 vector table + a handler saving the **full register set including the alternate bank** (PT3 players use `EXX`) and IY — materially more risk in a codebase that is deliberately conservative about interrupts (the `0xFF` bit-6 kill-switch / IM1 boot-DI warnings).
- The AY chip **plays autonomously** between register updates, so a blocking beeper SFX inside a frame does **not** silence the music; it only defers the next register write to the next frame, which is still on time.

Rejected alternative (IM2) is recorded here so it is not re-litigated.

---

## 6. Player tick — the 50 Hz cadence

`music_tick()` is invoked once at **every place the code `HALT`s**, so the tune stays on tempo across all screens and frozen pauses. Sites, all in `main.c`:

| Site | Context |
|---|---|
| after `scld_present()` in the main game loop | per-frame gameplay |
| after `scld_wait()` in `title_screen()` | menu |
| after `scld_wait()` in `game_over_screen()` | game-over wait |
| inside `game_over_flash()`'s wait loop | flash |
| inside `death_anim()`'s `scld_wait()` loop | death freeze |

The tune is started by `music_init()` at boot and **loops natively** (PT3 carries its own loop point); it simply continues across waves, deaths, and the game-over screen — no stop/restart logic in this slice.

`pt3_play` is parameterless and preserves IY; sdcc treats the remaining registers as call-clobbered across the call, so no caller state is at risk.

---

## 7. SFX coexistence (unchanged)

`sfx.asm` / `sfx.c` are **untouched**. On an AY machine the beeper SFX are **mixed in hardware over** the AY music (both reach the speaker) — the requested "beeper SFX + AY music". The blocking SFX briefly defer the next `music_tick`, costing at most a one-frame-late register update (inaudible). SFX continue to work identically on beeper-only machines, with no music.

---

## 8. Memory & asset

- **Asset prerequisite:** the real `spectrumizer.pt3` must be in the repo at `assets/spectrumizer.pt3` (fetched from ZX-Art, or dropped in by the user) before the build links. PT3 tunes are typically 1–8 KB.
- Layout: program ORG `0x8000`, current code ≈ 19 KB (ends ≈ `0xCB00`); + PT3 player ≈ 2 KB + tune (few KB) + the player's ~few-hundred-byte work area, all linker-placed in `code_user`, ending well below the z88dk +zx stack near the top of RAM. The back buffer (`0x6000–0x7AFF`) is untouched. A build-time size check confirms no collision with the stack.

---

## 9. Build & files

**New:** `include/music.h`, `src/music.c`, `src/pt3player.asm`, `src/tune.asm`, `assets/spectrumizer.pt3`, `run-zesarux-128.sh` (a TS2068/128K run target so the music is actually audible in the emulator).

**Modified:** `main.c` (call `music_init()` after `scld_init()`; add the five `music_tick()` sites in §6), `build.sh` (add the three new sources), `measure_main.c` (add a `music_tick` marker for the perf gate, §10).

`test/run.sh` is unaffected — `music.c`'s host build is a no-op and nothing host-tested links it.

---

## 10. Performance gate

Add a `music_tick` marker segment to `measure_main.c` and `z88dk-ticks` it. Expected live worst case:

```
render ~19.9k + enemies_update ~6.0k + collide ~2.8k + player_hit ~0.5k
       + misc (input/player/bullets/hud/fx) ~5k + music_tick ~7k        ≈ 41k
       + worst live SFX (EXTRA_LIFE ~21.8k)                              ≈ 63k  < 69,888 T  ✓
```

The long *frozen-pause* SFX (DEATH ~/ BONUS) run with no render, so there is ample room. **If** the measured live worst case ever overruns, the fallback is to **skip `music_tick` on the rare frame a long SFX fires** (the AY coasts one frame — imperceptible). This is verified empirically with `z88dk-ticks`, never assumed.

---

## 11. Verification / success criteria

1. **No regression on TC2048:** `build/game.tap` on a ZEsarUX **TC2048** runs exactly as before — silent music, beeper SFX, smooth 50 Hz; `music_init()` returns 0.
2. **Music on AY machines:** on a ZEsarUX **128K** and **TS2068** model (and 48K-with-AY), *Spectrumizer* plays on the title, loops continuously in-game, with beeper SFX layered over it.
3. **Detection correctness:** no spurious music on the no-AY model; music present on AY models; no crash/hang in either case.
4. **Budget:** `z88dk-ticks` confirms the live worst-case frame (incl. `music_tick` and the longest live SFX) stays under ~55k usable T; no dropped frames.

---

## 12. Risks

- **AY detection on odd clones** — mitigation: two-pattern probe; silence-on-doubt is the safe failure.
- **Thin budget margin under the longest live SFX** — mitigation: measured gate + the one-frame-coast fallback (§10).
- **Sourcing a PT3 player that assembles cleanly under z88dk's `z80asm`** — z88dk may ship a usable AY/PT3 player; otherwise drop in a standard public-domain Z80 PT3 player and adapt only its AY-out primitive to the latched ports. Keep its working RAM out of the `0x4000–0x7FFF` screen region.
- **Obtaining the licensed-for-use tune file** — `Spectrumizer` is published on ZX-Art; confirm reuse is acceptable (chip-scene tunes are generally freely shared, but credit Pator in the title/README).
