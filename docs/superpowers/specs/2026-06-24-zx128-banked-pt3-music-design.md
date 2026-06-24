# ZX Spectrum 128K — banked PT3 music (Timex parity)

**Date:** 2026-06-24
**Status:** Approved design, pre-implementation
**Scope:** `zx128` build only. `timex` and `zx48` builds must stay byte-for-byte unchanged.

## Goal

Give the ZX Spectrum 128K (incl. +2 / +2A) build the **same PT3 music as the
Timex build** — same `assets/spectrumizer.pt3` tune, playing during gameplay,
with `MUSIC+FX` a real, selectable, default-when-AY-present menu option, and the
existing channel-C sound effects overlaid on top exactly as on Timex.

This is full parity with Timex. No reduced/alternate tune, no title-screen music.

**Gameplay parity bundled in (decided 2026-06-24).** `ZX128_NO_MUSIC` is an
overloaded macro: besides gating the PT3 player it also marks the
"memory-constrained 128K build" and selects *weaker* gameplay on 128K —
`enemy.c` uses `DODGE_DIST 48` / `HUNTER_FLEE_SPEED 2` (vs `64` / `3`), and a
killed hunter splits into 2 hunter clones instead of 2 chasers
(`enemy.c:enemies_spawn_chaser_clones` omitted, `main.c` split branch). Since we
now have the memory room, dropping the macro brings 128K to **full Timex parity
on both axes — music and gameplay** (tougher hunters, hunter→2-chaser splits).
No source edits to `enemy.c` / `main.c` are needed: dropping the define makes
them compile the existing Timex branches.

## Background / current baseline

The 128K build uses an SCLD-style page flip implemented by keeping **RAM page 7
mapped at `$C000`** as the shadow screen (`src/zx128_page.asm`, bit 3 of `$7FFD`
selects which physical page the ULA displays). The resident program therefore
must stay **below `$C000`**, gated by `tools/check_zx128_layout.py`.

Already done this session (the foundation this design builds on, currently in the
working tree):

- The 128K build now ORGs resident code at **`$6000`** (`CRT_ORG_CODE=24576`)
  instead of `$8000`, reclaiming ~8 KB of always-mapped RAM (on 128K there is no
  Timex screen B at `$6000`). This made AY **sound effects** fit and work.
- The IM2 vector table moved (for `ZX128_PAGE_FLIP` only) to page-7 free RAM at
  **`$F000`** (Timex/48K keep `$7B00`).
- AY FX output is confirmed audible on a real +2 128K.

What is still missing — and what this design adds — is the **PT3 tune + player**,
which do not fit in the resident window:

- Tune (`spectrumizer.pt3`): **~10.3 KB** (`$C902–$F214` in the Timex map).
- Player code (`pt3prom.asm`): **~2.5 KB**.
- Player work RAM: **~0.6 KB**.

The player + work RAM (~3.1 KB) fit resident (we have ~7 KB headroom after the
ORG move). The **tune does not** — and it cannot live in page 7 (only ~9.4 KB
free above the shadow screen, fragmented, and page 7 is the back buffer). So the
tune must live in a **separate RAM bank**, paged into `$C000` only while the
player reads it.

## Architecture

Real PT3 music on 128K = **tune in a spare RAM bank, player resident, the 50 Hz
IM2 ISR pages the tune bank into `$C000` for the duration of each player tick,
then restores page 7.** The ULA fetches the displayed screen from physical RAM
per `$7FFD` bit 3, independently of which page the CPU maps at `$C000`, so the
shadow-screen page flip and the banked tune coexist — the standard 128K trick.

### Memory map (zx128 at runtime)

| Region | Address | Notes |
|---|---|---|
| Resident code+data+bss (now incl. PT3 player) | `$6000 – ~$AD00` | below `$C000`, gate-checked |
| Stack | `$C000` ↓ | `REGISTER_SP=$C000` |
| Shadow screen (page 7) | `$C000 – $DAFF` | page-flip back buffer |
| Pre-shift sprite tables | `$DB00 – $DF7F` | existing |
| IM2 vector table + JP | `$F000 – $F1F3` (page 7) | existing (this branch) |
| **PT3 tune (~10.3 KB)** | **bank 4 @ `$C000 – ~$E912`** | paged in only during the player tick |

**Bank choice: bank 4.** Banks 5 (`$4000`), 2 (`$8000`) and 7 (`$C000` flip) are
taken by the fixed map + page flip. Bank 4 is free and **non-contended** on the
128K, keeping the player's tune reads fast inside the ISR.

### `$7FFD` software shadow (new, in `zx128_page.asm`)

`$7FFD` is write-only, so the ISR cannot read the current paging to know which
screen is displayed. `zx128_page.asm` gains a resident shadow byte
`zx128_7ffd` holding the last value written. `_zx128_page_show_a` /
`_zx128_page_show_b` update it on every flip. This keeps the hardware boundary
rule intact: **all `$7FFD` writes stay in `zx128_page.asm`.**

Two new helpers there:

- `zx128_tune_in`  — `out ($7FFD), (zx128_7ffd & $F8) | 4` (map bank 4 at
  `$C000`; preserve ROM-select bit 4 and display bit 3 from the shadow).
- `zx128_tune_out` — `out ($7FFD), zx128_7ffd` (restore page 7 / the displayed
  screen). Does **not** rewrite the shadow (the flip state is unchanged).

### Player tick bank-switching (in `music_ay.asm`)

The PT3 wrappers bracket the player calls with the helpers, only under
`ZX128_PAGE_FLIP`:

- `pt3_init`      → `zx128_tune_in` ; `call asm_VT_INIT` ; `zx128_tune_out`
- `pt3_play_safe` → `zx128_tune_in` ; `call asm_VT_PLAY` ; `zx128_tune_out`

Inside the tick the player reads the tune at `$C000` (bank 4) and writes the AY
via our `asm_vt_hardware_out` override. AY ports, `asm_VT_AYREGS` and the
`_asfx_*` SFX state are all **resident**, so they are reachable while bank 4 is
mapped. Because interrupts are only taken between instructions, the main loop's
own `$C000` (shadow-screen) writes always see page 7 — the ISR pages bank 4 in
and back out entirely between two main-loop instructions.

`pt3_init` is handed the module address. On `ZX128_PAGE_FLIP` that is **`$C000`**
(the tune's bank-4 load address); otherwise it stays `_spectrumizer_pt3`
(Timex/48K, where the tune is linked resident).

**Important:** `tune.asm` is *not* linked on zx128, so the existing
`EXTERN _spectrumizer_pt3` in `music_ay.asm` must itself be guarded out under
`ZX128_PAGE_FLIP` — otherwise it's an unresolved symbol at link time. So the
`IFDEF` covers both the `EXTERN` declaration and the `ld hl,…` module-address
load.

## Load mechanism (Option B — custom, fully controlled)

The tune is **not** linked into the resident image on 128K. It rides as a final
raw block on the tape and is pulled into bank 4 by the running program.

### Tape layout

`BASIC loader` → `loading screen` → `main code block (ORG $6000)` →
**raw tune block** (a single headerless ROM data block: flag `$FF`, the
`spectrumizer.pt3` bytes, XOR checksum).

The standard `z88dk-appmake +zx` tap is built as today (loader + screen + main
code). A small build step then **appends** the tune as a headerless `$FF` data
block. A tiny helper (`tools/make_tape_block.py`, ~15 lines) wraps
`assets/spectrumizer.pt3` into the `[len_lo][len_hi][$FF][data…][xor]` block; the
Makefile concatenates it onto the zx128 `.tap`.

### Startup loader stub (`zx128_load_tune`, in `zx128_page.asm`)

Exposed as `extern void zx128_load_tune(void);`, called once from `main()` under
`#ifdef ZX128_PAGE_FLIP`, **after `scld_init()` and before the title menu**
(i.e. before any `music_init` that could call `pt3_init`):

1. `push ix/iy`; `di`
2. `ld a, ROM1|bank4 ($14)`; `out ($7FFD), a` — keep ROM1 so the ROM tape loader
   at `$0556` is present; map bank 4 at `$C000`.
3. `ld ix,$C000`; `ld de,<ZX128_TUNE_LEN>`; `ld a,$FF`; `scf`; `call $0556`
   (ROM `LOAD_BYTES`) — load the trailing tape block into bank 4.
4. `ld a, ROM1|page7 ($17)`; `out ($7FFD), a`; sync `zx128_7ffd`. `ei`;
   `pop iy/ix`.

`ZX128_TUNE_LEN` is passed to the assembler from the Makefile
(`-Ca-DZX128_TUNE_LEN=$(shell wc -c < assets/spectrumizer.pt3)`), the same number
used to build the tape block, so they cannot drift.

This runs while the crt is still in IM1 (set by `scld_init`); `LOAD_BYTES`
manages its own border/edge timing and returns to our code. IX/IY are saved
(IY is the sdcc_iy frame pointer).

## Build changes (Makefile, zx128 recipe only)

- **Drop** `-DZX128_NO_MUSIC` and `-Ca-DZX128_NO_MUSIC`. This re-enables the full
  `MUSIC+FX` path in `music.c` and the PT3 wrappers + `asm_vt_hardware_out`
  override + `sfx_merge` in `music_ay.asm` — identical to Timex.
- **Add** `src/pt3prom.asm` (the player) to the resident link. **Do not** add
  `src/tune.asm` (the tune ships as the bank-4 tape block instead).
- Keep `CRT_ORG_CODE=24576`, `REGISTER_SP=49152`, `-Ca-DZX128_PAGE_FLIP`.
- Add `-Ca-DZX128_TUNE_LEN=…` (tune size) for `zx128_load_tune`.
- After the normal `APPMAKE_TAP`, append the tune data block to the `.tap`.
- `check_zx128_layout.py` still runs and must pass.

`src/music.c`, `src/enemy.c` and `src/main.c` need **no edit** — dropping
`ZX128_NO_MUSIC` makes all three compile their Timex branches verbatim (music
`MUSIC+FX` path; tougher hunters; hunter→chaser splits). The bank-switching is
hidden inside the `music_ay.asm` wrappers, invisible to `music.c`.

Possible cleanup: once no build defines `ZX128_NO_MUSIC`, the 128K-only
`enemies_spawn_hunter_clones` split helper becomes unused. Remove it if the
target build flags it as dead code (otherwise leave it — low priority).

## Files touched

| File | Change |
|---|---|
| `Makefile` | zx128 recipe: drop `ZX128_NO_MUSIC`, add `pt3prom.asm`, pass `ZX128_TUNE_LEN`, append tune block |
| `tools/make_tape_block.py` | new — wrap `.pt3` into a headerless `$FF` tap block |
| `src/zx128_page.asm` | `$7FFD` shadow; `zx128_tune_in`/`zx128_tune_out`; `zx128_load_tune` |
| `src/music_ay.asm` | `IFDEF ZX128_PAGE_FLIP`: module addr `$C000`; bracket `pt3_init`/`pt3_play_safe` with tune_in/out |
| `src/main.c` | `#ifdef ZX128_PAGE_FLIP`: call `zx128_load_tune()` once at startup. (Hunter-split branch flips to chasers automatically via the dropped define — no edit there.) |
| `src/enemy.c` | no edit — dropped define gives Timex hunter params + chaser-clone code |
| `CLAUDE.md` | update zx128 build description (ORG `$6000`, full Timex parity: banked PT3 music in bank 4, IM2 in page 7, gameplay no longer memory-reduced) |

## Verification plan

1. `tools/check_zx128_layout.py` green (player adds ~3.1 KB; expect
   `__BSS_END_tail ~ $AD00`, well under `$C000`).
2. Host unit tests unaffected (`music.c` host build is a no-op) — `./test/run.sh`.
3. **Highest-risk, verify first** in ZEsarUX (`--machine 128k`): the multi-block
   tape + `LOAD_BYTES`-into-bank-4 at startup actually loads. Evidence: clean
   title screen, then a bank-4 hexdump at `$C000` equals the first bytes of
   `spectrumizer.pt3`.
4. Pick `MUSIC+FX`, start a game → the tune plays **and** the FX overlay (shoot /
   explosion) still sounds; no screen corruption; page flip stays clean across
   frames.
5. Confirm `MUSIC+FX` is the **default** menu selection when an AY is detected
   (parity with Timex).
6. **Gameplay-parity check:** 128K now runs the Timex enemy balance (tougher
   hunters, hunter→2-chaser splits). Confirm it still plays **smoothly** — the
   page-flip renderer has a different cost than Timex and the perf budget was
   tuned per platform. Splits stay bounded by `MAX_ENEMIES`. If frame pacing
   suffers, revisit (this is the one gameplay-parity risk).
7. `timex` and `zx48` `_CODE.bin` outputs are byte-identical to before (diff the
   built binaries) — proves the change is isolated to zx128.
8. Final confirmation on the real +2 128K (audio + load + smoothness) by the
   hardware tester.

## Risks & mitigations

- **Calling ROM `LOAD_BYTES` from the running program / multi-block tape** is the
  main risk (timing, the trailing block being positioned correctly, emulator vs
  real tape). It is a standard 128K technique; verified first (step 3) before
  building anything on top. If it proves flaky, the fallback is a dedicated
  machine-code loader stub invoked from the BASIC loader before `USR` (more tape
  surgery, same in-RAM result).
- **Bank 4 contention / wrong bank** — bank 4 chosen specifically as free +
  non-contended; verified by reading it back after load.
- **`$7FFD` shadow drift** — only `zx128_page.asm` writes `$7FFD`; the shadow is
  updated at the single source of every write.
- **Resident overflow** — player adds ~3.1 KB; `check_zx128_layout.py` is the
  hard gate, run every build.

## Non-goals

- No change to Timex or 48K behavior or binaries.
- No title-screen music (gameplay only, matching Timex).
- No alternate/compressed tune; the existing `.pt3` fits a bank as-is.
- Not attempting z88dk native `BANK_n` autoloading (Option A) — rejected in
  favor of the fully-controlled custom loader.
