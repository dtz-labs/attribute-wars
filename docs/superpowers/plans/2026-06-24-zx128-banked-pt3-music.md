# ZX128 Banked PT3 Music Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the ZX Spectrum 128K build the same PT3 music as Timex by parking the tune in a spare RAM bank and paging it in for each 50 Hz player tick.

**Architecture:** The ~10.3 KB tune ships as a trailing headerless tape block loaded into RAM **bank 4** at `$C000` at startup; the PT3 player lives resident; the IM2 ISR pages bank 4 into `$C000` around each player call using a `$7FFD` software shadow, then restores page 7 (the shadow screen). Dropping `ZX128_NO_MUSIC` also brings full Timex gameplay parity.

**Tech Stack:** z88dk (zcc, sdcc_iy, z80asm), Z80 assembly, C99, Python 3 (build tool), ZEsarUX (verification), GNU Make.

## Global Constraints

- **zx128 only.** `timex` and `zx48` `_CODE.bin` outputs MUST stay byte-identical. Verify by diffing.
- **Resident code+data+bss MUST stay below `$C000`.** `tools/check_zx128_layout.py` is the hard gate; it runs every zx128 build.
- **All `$7FFD` writes live in `src/zx128_page.asm`** (hardware-boundary rule).
- **Preserve ROM1** in every `$7FFD` write: `$10` bit set. Page values: `$17` (page7+screen5), `$1F` (page7+screen7), `$14` (bank4+screen5, used only during tune load). Using `$07/$0F` selects ROM0 and can reboot under IM1.
- **Tune bank = 4** (free, non-contended).
- **IM2 vector table = `$F000`** on zx128 (page 7), already in place.
- Project rules: no floating point, no `malloc`, no recursion; never return a struct by value; use `z80_outp()` / `<intrinsic.h>`; never clobber IY (sdcc_iy frame pointer) without save/restore.
- `REGISTER_SP=49152` (`$C000`), `CRT_ORG_CODE=24576` (`$6000`) on zx128.

---

### Task 1: Baseline commit — AY FX foundation (already in working tree)

The working tree already contains the verified-working FX foundation (ORG `$6000`, IM2 table at `$F000`, FX-only AY enabled). Commit it as the baseline before building music on top.

**Files:**
- Modify (already changed): `Makefile`, `src/music.c`, `src/music_ay.asm`

**Interfaces:**
- Produces: a building, layout-safe zx128 TAP with audible AY FX; `_zx128_page_show_a/b` unchanged; IM2 at `$F000` under `ZX128_PAGE_FLIP`.

- [ ] **Step 1: Confirm the zx128 build is green**

Run: `make zx128 2>&1 | grep -E "layout|END_tail|\.tap|Error"`
Expected: `ZX128 page-flip layout: safe`, `__BSS_END_tail=$A0D1`, a `.tap` line, no `Error`.

- [ ] **Step 2: Confirm Timex + 48K still build**

Run: `make timex zx48 2>&1 | grep -E "layout|\.tap|Error|safe"`
Expected: both TAPs built, no errors.

- [ ] **Step 3: Run host unit tests**

Run: `./test/run.sh`
Expected: `ALL HOST TESTS PASSED`.

- [ ] **Step 4: Commit the foundation**

```bash
git add Makefile src/music.c src/music_ay.asm
git commit -m "Enable AY FX on zx128: ORG \$6000, IM2 table in page 7

The 128K page-flip build now ORGs resident code at \$6000 (reclaiming the
\$6000-\$7FFF hole that is free because screen B lives in page 7, not at
\$6000 as on Timex), which makes the AY sound-effect code fit under \$C000.
The IM2 vector table moves to page-7 free RAM at \$F000 for this build.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Tape-block tool `tools/make_tape_block.py`

A tiny tool that wraps a binary file into a single headerless ROM tape **data block** (flag `$FF`) so it can be appended to the zx128 `.tap` and loaded by `LOAD_BYTES`.

Tape data-block format: `[len_lo][len_hi]` then the block body `[flag=$FF][data…][checksum]`, where `len = 1 + len(data) + 1` (flag + data + checksum) and `checksum = XOR(flag, data…)`.

**Files:**
- Create: `tools/make_tape_block.py`
- Test: inline shell/python check (no C harness involved)

**Interfaces:**
- Produces: `python3 tools/make_tape_block.py <input.bin>` writes the tap data block to **stdout** (binary). `DE` for `LOAD_BYTES` = `len(data)` = `wc -c <input.bin>`.

- [ ] **Step 1: Write the failing check**

Create `tools/_tmp_tapeblock_fixture.bin` (3 bytes `01 02 03`) and a check that the tool's output is exactly `05 00 FF 01 02 03 FF`:

```bash
printf '\x01\x02\x03' > tools/_tmp_tapeblock_fixture.bin
python3 tools/make_tape_block.py tools/_tmp_tapeblock_fixture.bin | xxd -p
```

Hand-derivation: flag `FF`; body `FF 01 02 03`; checksum `FF^01^02^03 = FF`; block `FF 01 02 03 FF`; len `= 5` → `05 00`; output `05 00 FF 01 02 03 FF`.

- [ ] **Step 2: Run it to verify it fails**

Run the command above.
Expected: FAIL — `python3: can't open file '.../make_tape_block.py'` (tool not created yet).

- [ ] **Step 3: Create the tool**

```python
#!/usr/bin/env python3
"""Wrap a binary file into a single headerless ZX tape data block (flag $FF).

Output (to stdout): [len_lo][len_hi][flag=$FF][data...][checksum], where
len = len(data) + 2 and checksum = XOR over flag and all data bytes. This is
exactly what the ROM LD-BYTES routine ($0556) reads with A=$FF, DE=len(data).
"""
import sys

def make_block(data: bytes) -> bytes:
    flag = 0xFF
    body = bytes([flag]) + data
    checksum = 0
    for b in body:
        checksum ^= b
    block = body + bytes([checksum])
    length = len(block)  # = len(data) + 2
    return bytes([length & 0xFF, (length >> 8) & 0xFF]) + block

def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(f"usage: {argv[0]} <input.bin>\n")
        return 2
    with open(argv[1], "rb") as f:
        data = f.read()
    sys.stdout.buffer.write(make_block(data))
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

- [ ] **Step 4: Run the check to verify it passes**

Run: `python3 tools/make_tape_block.py tools/_tmp_tapeblock_fixture.bin | xxd -p`
Expected: `0500ff010203ff`

- [ ] **Step 5: Sanity-check against the real tune size**

Run:
```bash
echo "data len: $(wc -c < assets/spectrumizer.pt3)"
echo "block len: $(python3 tools/make_tape_block.py assets/spectrumizer.pt3 | wc -c)"
```
Expected: block len = data len + 4 (2 length bytes + flag + checksum).

- [ ] **Step 6: Remove the fixture and commit**

```bash
rm -f tools/_tmp_tapeblock_fixture.bin
git add tools/make_tape_block.py
git commit -m "Add make_tape_block.py: wrap a binary as a headerless \$FF tape block

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `$7FFD` shadow + `zx128_tune_in` / `zx128_tune_out`

Add the software shadow of `$7FFD` (updated on every flip) and the two bank-switch helpers. `zx128_page.asm` is compiled only in the zx128 build, so no `IFDEF` is needed inside it.

**Files:**
- Modify: `src/zx128_page.asm`

**Interfaces:**
- Produces (PUBLIC, called from `music_ay.asm`): `zx128_tune_in` (map bank 4 at `$C000`, preserve ROM/screen bits; clobbers A,BC), `zx128_tune_out` (restore from shadow; clobbers A,BC). Internal `zx128_7ffd` byte (DATA, not PUBLIC).

- [ ] **Step 1: Rewrite `src/zx128_page.asm` body**

Replace the whole file with (preserving the header comment intent):

```
; zx128_page.asm -- ZX Spectrum 128K RAM page 7 screen flipping + banked-tune
; paging. Page 7 is kept mapped at $C000 (shadow screen 0xC000..0xDAFF); bit 3
; of $7FFD selects which display file the ULA shows. The PT3 tune lives in bank
; 4 and is paged into $C000 only around each player tick. $7FFD is write-only,
; so a software shadow (zx128_7ffd) tracks the last value written; ALL $7FFD
; writes are confined to this file.

        SECTION code_user

        PUBLIC  _zx128_page_show_a
        PUBLIC  _zx128_page_show_b
        PUBLIC  zx128_tune_in
        PUBLIC  zx128_tune_out
        PUBLIC  _zx128_load_tune

ZX128_PORT_7FFD EQU     $7FFD
ZX128_PAGE7     EQU     $07
ZX128_TUNEBANK  EQU     $04        ; spare, non-contended bank for the PT3 tune
ZX128_SCREEN7   EQU     $08
ZX128_ROM1      EQU     $10
ROM_LD_BYTES    EQU     $0556      ; 48K-ROM tape loader (needs ROM1 paged)
TUNE_ADDR       EQU     $C000      ; tune base in the banked window

; void zx128_page_show_a(void): map page 7, show the page-5 screen.
_zx128_page_show_a:
        ld      a,ZX128_ROM1 | ZX128_PAGE7
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; void zx128_page_show_b(void): map page 7, show its shadow screen.
_zx128_page_show_b:
        ld      a,ZX128_ROM1 | ZX128_PAGE7 | ZX128_SCREEN7
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; zx128_tune_in -- map bank 4 into $C000, preserving the ROM-select and
; displayed-screen bits from the shadow. Clobbers A,BC.
zx128_tune_in:
        ld      a,(zx128_7ffd)
        and     $F8                ; clear page bits 0..2
        or      ZX128_TUNEBANK     ; select bank 4
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; zx128_tune_out -- restore the normal mapping from the shadow (page 7 + the
; current displayed screen). Clobbers A,BC.
zx128_tune_out:
        ld      a,(zx128_7ffd)
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; void zx128_load_tune(void) -- pull the trailing tape block (the PT3 tune) into
; bank 4 at $C000 via the ROM loader. Runs once at startup, before music_init.
_zx128_load_tune:
        push    ix
        push    iy
        di
        ld      a,ZX128_ROM1 | ZX128_TUNEBANK   ; bank 4 at $C000, keep ROM1
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ld      ix,TUNE_ADDR
        ld      de,ZX128_TUNE_LEN               ; -Ca-D from the Makefile
        ld      a,$FF                           ; expect a data block
        scf                                     ; carry set = LOAD
        call    ROM_LD_BYTES
        ld      a,ZX128_ROM1 | ZX128_PAGE7      ; restore page 7 (screen A shown)
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ei
        pop     iy
        pop     ix
        ret

        SECTION data_user
zx128_7ffd:  defb  ZX128_ROM1 | ZX128_PAGE7     ; software shadow of port $7FFD
```

Note: `_zx128_load_tune` and `ZX128_TUNE_LEN` are added now but only become reachable/defined once Task 4 wires the Makefile and `main.c`. The assembler tolerates the `ZX128_TUNE_LEN` reference only when the `-Ca-D` define exists, so this task's build check (Step 2) is run **without** asserting `_zx128_load_tune` is called — it is exercised in Task 4. If the standalone build errors on the undefined `ZX128_TUNE_LEN`, proceed directly to Task 4 (they are co-dependent) and treat Task 3+4 as one commit.

- [ ] **Step 2: Build zx128 and confirm helpers assemble**

Run: `make zx128 2>&1 | grep -E "layout|END_tail|Error|error|tune"`
Expected: layout `safe`; if it errors on `ZX128_TUNE_LEN` undefined, that's expected — fold into Task 4 (see note above). Otherwise no errors.

- [ ] **Step 3: Confirm the shadow is wired into both flip paths**

Run: `grep -n "zx128_7ffd" src/zx128_page.asm`
Expected: written in `show_a`, `show_b`, `zx128_load_tune` restore, and read in `tune_in`/`tune_out`.

- [ ] **Step 4: Commit (or defer to Task 4 if co-dependent)**

```bash
git add src/zx128_page.asm
git commit -m "zx128: \$7FFD software shadow + tune-bank paging helpers

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Load the tune into bank 4 (highest-risk — verify first)

Wire the Makefile to pass the tune length, append the tune block to the TAP, and call `zx128_load_tune()` at startup. This validates the riskiest piece (multi-block tape + `LOAD_BYTES` into bank 4) **before** the player depends on it. The build stays FX-only for this task (music not yet enabled), so success = boots cleanly and FX still works.

**Files:**
- Modify: `Makefile` (zx128 recipe)
- Modify: `src/main.c` (call `zx128_load_tune()` at startup)

**Interfaces:**
- Consumes: `_zx128_load_tune` (Task 3), `tools/make_tape_block.py` (Task 2).
- Produces: a zx128 TAP whose trailing block is the tune; bank 4 holds the tune after boot.

- [ ] **Step 1: Add the tune-length / tune-file vars to the Makefile**

After the `ZX128_STACK_TOP := 49152` line, add:

```make
ZX128_TUNE := assets/spectrumizer.pt3
ZX128_TUNE_LEN := $(shell wc -c < assets/spectrumizer.pt3 | tr -d ' ')
```

- [ ] **Step 2: Update the zx128 recipe — pass `ZX128_TUNE_LEN`, append tune block, add prereqs**

Change the zx128 recipe so the `zcc` line adds `-Ca-DZX128_TUNE_LEN=$(ZX128_TUNE_LEN)` (keep `-DZX128_NO_MUSIC` for now — music is enabled in Task 5), add `tools/make_tape_block.py` and `$(ZX128_TUNE)` to the prerequisites, and append the tune block after `APPMAKE_TAP`:

```make
$(ZX128_TAP): $(COMMON_C) $(COMMON_ASM) src/zx128_page.asm src/music_ay.asm tools/make_tape_block.py $(ZX128_TUNE) $(HEADERS) $(LOADING_SCREEN) tools/check_zx128_layout.py | $(BUILD)
	mkdir -p $(BUILD)/obj-zx128
	cd $(BUILD)/obj-zx128 && $(ZCC_BASE) \
		-DZX128_PAGE_FLIP -DZX_SINCLAIR_DUAL_STICK -DZX128_NO_MUSIC \
		-Ca-DZX128_PAGE_FLIP -Ca-DZX128_NO_MUSIC \
		-Ca-DZX128_TUNE_LEN=$(ZX128_TUNE_LEN) \
		-pragma-define:CRT_ORG_CODE=$(ZX128_ORG) \
		-pragma-define:REGISTER_SP=$(ZX128_STACK_TOP) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(ROOT)/src/zx128_page.asm \
		$(ROOT)/src/music_ay.asm \
		-o $(ROOT)/$(ZX128_CODE_BASE) -create-app -m
	$(CHECK_ZX128_LAYOUT) $(ZX128_CODE_BASE).map
	$(call APPMAKE_TAP,$(ZX128_CODE_BASE),$@)
	python3 tools/make_tape_block.py $(ZX128_TUNE) >> $@
	rm -f $(ZX128_CODE_BASE).tap
	ls -l $@
```

- [ ] **Step 3: Declare and call `zx128_load_tune()` in `main.c`**

Near the other `extern` declarations at the top of `src/main.c`, add:

```c
#ifdef ZX128_PAGE_FLIP
extern void zx128_load_tune(void);   /* pull the PT3 tune into bank 4 (zx128_page.asm) */
#endif
```

Then in `main()`, immediately after `hud_init();` and before the `main_menu:` label, add:

```c
#ifdef ZX128_PAGE_FLIP
    zx128_load_tune();                /* tune -> bank 4 before any music_init */
#endif
```

- [ ] **Step 4: Build and confirm layout still safe**

Run: `make zx128 2>&1 | grep -E "layout|END_tail|Error|error"`
Expected: `ZX128 page-flip layout: safe`; `.tap` built. (FX-only build still; player not linked yet, so `__BSS_END_tail` is still ~`$A0D1`.)

- [ ] **Step 5: Confirm the tune block was appended**

Run:
```bash
echo "tap size:   $(wc -c < build/aw-1.1.2-zx128k.tap)"
echo "tune+4:     $(( $(wc -c < assets/spectrumizer.pt3) + 4 ))"
```
Expected: tap size increased by exactly (tune size + 4) versus a build without the append.

- [ ] **Step 6: Emulator smoke test (manual — no headless MCP)**

Run: `make run-zx128` (launches ZEsarUX as 128k). Observe:
- The game **boots cleanly** to the title (no hang/crash/reboot — proves `LOAD_BYTES` returned and paging was restored).
- FX still audible in-game (pick `6 FX`, `0`, shoot).

Optional definitive check (ZEsarUX debugger): page bank 4 (`$7FFD` low bits = 4) and hexdump `$C000`; first bytes must equal `head -c 16 assets/spectrumizer.pt3 | xxd`. (Otherwise, music playing in Task 5 is the definitive confirmation.)

- [ ] **Step 7: Commit**

```bash
git add Makefile src/main.c
git commit -m "zx128: load the PT3 tune into bank 4 from a trailing tape block

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Enable music + gameplay parity (drop `ZX128_NO_MUSIC`) + player bank-switching

Link the player, drop `ZX128_NO_MUSIC` (enabling the `MUSIC+FX` path in `music.c` and Timex gameplay in `enemy.c`/`main.c`), and bracket the player calls with the bank-switch helpers, pointing the player at the tune in bank 4.

**Files:**
- Modify: `Makefile` (zx128 recipe: drop `ZX128_NO_MUSIC`, add `pt3prom.asm`)
- Modify: `src/music_ay.asm` (guard `_spectrumizer_pt3` EXTERN; bracket `pt3_init`/`pt3_play_safe`; `$C000` module address)

**Interfaces:**
- Consumes: `zx128_tune_in`/`zx128_tune_out` (Task 3), tune in bank 4 (Task 4), `asm_VT_INIT`/`asm_VT_PLAY`/`asm_VT_SETUP` (pt3prom.asm).
- Produces: working `MUSIC+FX` on zx128 identical to Timex.

- [ ] **Step 1: Guard the resident-tune EXTERN in `music_ay.asm`**

The PT3 symbol block currently has `EXTERN _spectrumizer_pt3` under `IFNDEF ZX128_NO_MUSIC`. The tune is NOT linked on zx128, so guard that EXTERN out for the page-flip build. Replace:

```
        EXTERN  _spectrumizer_pt3       ; tune.asm: the PT3 module (label = addr)
```

with:

```
        IFNDEF  ZX128_PAGE_FLIP
        EXTERN  _spectrumizer_pt3       ; tune.asm: resident module (Timex/48K)
        ELSE
        EXTERN  zx128_tune_in           ; zx128: tune is banked at $C000
        EXTERN  zx128_tune_out
        ENDIF
```

- [ ] **Step 2: Bracket `_pt3_init` with the bank helpers + `$C000` module address**

Replace the body of `_pt3_init` with:

```
_pt3_init:
        push    ix
        push    iy
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_in
        ld      hl,$C000                ; tune base in bank 4
        ELSE
        ld      hl,_spectrumizer_pt3    ; resident module address
        ENDIF
        call    asm_VT_INIT
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_out
        ENDIF
        pop     iy
        pop     ix
        ret
```

- [ ] **Step 3: Bracket `_pt3_play_safe` (covers play + loop-restart, both read the tune)**

Replace the body of `_pt3_play_safe` with:

```
_pt3_play_safe:
        push    ix
        push    iy
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_in
        ENDIF
        call    asm_VT_PLAY
        ld      a,(asm_VT_SETUP)
        bit     7,a                     ; player passed the PT3 loop/end point
        jr      z,pt3_play_done
        IFDEF   ZX128_PAGE_FLIP
        ld      hl,$C000                ; restart from the tune base in bank 4
        ELSE
        ld      hl,_spectrumizer_pt3
        ENDIF
        call    asm_VT_INIT
pt3_play_done:
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_out
        ENDIF
        pop     iy
        pop     ix
        ret
```

- [ ] **Step 4: Makefile — drop `ZX128_NO_MUSIC`, add the player**

In the zx128 recipe, remove `-DZX128_NO_MUSIC` and `-Ca-DZX128_NO_MUSIC`, and add `$(ROOT)/src/pt3prom.asm` to the link (do NOT add `tune.asm`). Add `src/pt3prom.asm` to the prerequisites. Resulting `zcc` invocation:

```make
	cd $(BUILD)/obj-zx128 && $(ZCC_BASE) \
		-DZX128_PAGE_FLIP -DZX_SINCLAIR_DUAL_STICK \
		-Ca-DZX128_PAGE_FLIP \
		-Ca-DZX128_TUNE_LEN=$(ZX128_TUNE_LEN) \
		-pragma-define:CRT_ORG_CODE=$(ZX128_ORG) \
		-pragma-define:REGISTER_SP=$(ZX128_STACK_TOP) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(ROOT)/src/zx128_page.asm \
		$(ROOT)/src/music_ay.asm $(ROOT)/src/pt3prom.asm \
		-o $(ROOT)/$(ZX128_CODE_BASE) -create-app -m
```

Also add `src/pt3prom.asm` to the prerequisite list of the `$(ZX128_TAP):` rule.

- [ ] **Step 5: Build zx128 and confirm it still fits under `$C000`**

Run: `make zx128 2>&1 | grep -E "layout|END_tail|Error|error"`
Expected: `ZX128 page-flip layout: safe`; `__BSS_END_tail` now larger (~`$AD00`) but `<= $C000`; `.tap` built. If it FAILS the gate, STOP — the player pushed resident over `$C000`; revisit (e.g. lower `CRT_ORG_CODE`).

- [ ] **Step 6: Confirm Timex + 48K are byte-identical (isolation proof)**

```bash
git stash; make timex zx48 >/dev/null 2>&1
md5sum build/aw-1.1.2-timex_CODE.bin build/aw-1.1.2-zx48k_CODE.bin > /tmp/aw_base.md5
git stash pop; make timex zx48 >/dev/null 2>&1
md5sum -c /tmp/aw_base.md5
```
Expected: both `OK`. (If `git stash` has nothing to stash because work is committed, instead compare against the `main` branch build.)

- [ ] **Step 7: Host unit tests still green**

Run: `./test/run.sh`
Expected: `ALL HOST TESTS PASSED`.

- [ ] **Step 8: Emulator verification (manual)**

Run: `make run-zx128`. Confirm:
- Title shows `HW ZX128    AY ZX`; `MUSIC+FX` is the **default**-highlighted sound option.
- Pick `5 MUSIC+FX`, `0` to start → **the tune plays** AND shooting/explosions overlay FX on channel C.
- No screen corruption; page flip stays clean; gameplay smooth (Timex enemy balance now active — tougher hunters, hunter→2-chaser splits).

- [ ] **Step 9: Commit**

```bash
git add Makefile src/music_ay.asm
git commit -m "zx128: enable banked PT3 music + full Timex parity

Drop ZX128_NO_MUSIC (enables the MUSIC+FX path in music.c and Timex enemy
balance in enemy.c/main.c), link the resident PT3 player, and page bank 4
in around each player tick (pt3_init/pt3_play_safe), with the module based
at \$C000.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Dead-code cleanup + docs

Remove the now-unused 128K-only hunter-split helper (if the build flags it) and update `CLAUDE.md` to describe the new zx128 reality.

**Files:**
- Modify (maybe): `src/enemy.c`, `include/enemy.h` (remove `enemies_spawn_hunter_clones` if unused)
- Modify: `CLAUDE.md`

**Interfaces:**
- Produces: clean build of all targets, accurate docs.

- [ ] **Step 1: Check whether `enemies_spawn_hunter_clones` is now unused**

Run: `grep -rn "enemies_spawn_hunter_clones" src/ include/ test/`
Expected: only its definition/declaration remain (no callers), since the only caller was the dropped `ZX128_NO_MUSIC` branch in `main.c`. If there are no callers anywhere, remove the function from `src/enemy.c` and its prototype from `include/enemy.h`. If a caller exists (e.g. a test), leave it.

- [ ] **Step 2: Rebuild all targets + host tests after any removal**

Run: `make all && ./test/run.sh 2>&1 | tail -3`
Expected: all three TAPs build; `ALL HOST TESTS PASSED`.

- [ ] **Step 3: Update `CLAUDE.md` zx128 description**

In the "ZX Spectrum 128K build" bullet, replace the "PT3 music is disabled… to keep resident code/data/BSS below `$C000`" sentence with an accurate description:

> `make zx128` → `build/aw-1.0-zx128k.tap`. Defines `ZX128_PAGE_FLIP` and `ZX_SINCLAIR_DUAL_STICK`. RAM page 7 is kept banked into `$C000` (shadow screen); bit 3 of `$7FFD` flips screen 5/7; "two joysticks" means Sinclair 1/2. Resident code ORGs at `$6000` (`CRT_ORG_CODE=24576`) — the `$6000–$7FFF` hole is free on 128K because screen B lives in page 7, not at `$6000` — keeping code+data+bss below `$C000` (gated by `tools/check_zx128_layout.py`). PT3 music now plays at **full Timex parity**: the ~10 KB tune ships as a trailing tape block loaded into RAM **bank 4** at `$C000` (`zx128_load_tune`), the player is resident, and the 50 Hz IM2 ISR pages bank 4 in for each tick (via a `$7FFD` software shadow in `zx128_page.asm`). The IM2 vector table sits in page-7 free RAM at `$F000`.

Also update the gotcha bullet that says ZX128 "removes PT3 music" to reflect the banked-tune layout.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md src/enemy.c include/enemy.h
git commit -m "docs+cleanup: zx128 now ships banked PT3 music at Timex parity

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage:**
- Tune in bank 4, paged per tick → Tasks 3, 5. ✓
- `$7FFD` shadow + helpers → Task 3. ✓
- Custom tape-block loader (tool + trailing block + `LOAD_BYTES` stub + startup call) → Tasks 2, 3, 4. ✓
- Drop `ZX128_NO_MUSIC` → music + gameplay parity → Task 5 (+ enemy.c/main.c auto). ✓
- `_spectrumizer_pt3` EXTERN guarded → Task 5 Step 1. ✓
- `pt3_init` module address `$C000` → Task 5 Steps 2–3. ✓
- Layout gate, host tests, Timex/48K byte-identical, emulator checks, perf/smoothness → Task 5 Steps 5–8, Task 1, Task 6. ✓
- Dead-code + CLAUDE.md → Task 6. ✓
- IM2 at `$F000` (foundation) → Task 1. ✓

**2. Placeholder scan:** No TBD/TODO; every code/asm/Make step shows the actual content. ✓

**3. Type/symbol consistency:** `zx128_tune_in`/`zx128_tune_out`/`_zx128_load_tune`/`zx128_7ffd`/`ZX128_TUNE_LEN`/`ZX128_TUNEBANK`/`$C000` used identically across Tasks 3–5. C call `zx128_load_tune()` ↔ asm `_zx128_load_tune`. `make_tape_block.py` stdout contract ↔ Makefile `>> $@` and `LOAD_BYTES DE=len(data)`. ✓

**Known risk (carried from spec):** Task 4 Step 6 — `LOAD_BYTES` from the running program / multi-block tape. Verified before the player depends on it. Fallback if flaky: a machine-code loader stub invoked from the BASIC loader before `USR` (same in-RAM result).
