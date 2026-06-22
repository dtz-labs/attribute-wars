# ZX Spectrum 128K Port — Design (General / Architectural)

**Date:** 2026-06-22
**Targets:** ZX Spectrum 128K / +2 / +3 (new). Existing: Timex TC2048 (primary), TC2068 / TS2068 (near-free secondary).
**Toolchain:** Z88DK (`zcc +zx`, `-clib=sdcc_iy`), same as the main build.
**Status:** Exploratory design. **General by intent** — this spec describes the *shape* of the port (what kind of change it is, what moves and what doesn't, the load-bearing hardware facts and gotchas). It deliberately does **not** name individual routines or prescribe line-level edits; that belongs in a later plan → implementation cycle. Derived from the 2026-06-22 design conversation.

---

## 1. Purpose

The game today targets the **Timex TC2048** (a 48K Spectrum + the SCLD video chip). The whole "smooth and fast" feel rests on the **SCLD hardware page-flip** double buffer. Two questions motivated this spec:

1. Can we add **AY-3-8910/8912 music** and have one game work on both a Timex 2068 and a "ZX Spectrum"?
2. What would it actually take to make the game run *well* on a real **Sinclair ZX Spectrum 128K** — the most common Spectrum that has both an AY chip and enough capability for this game's smoothness target?

The short version that this spec expands: **the 128K port is a contained *kernel swap*, not a rewrite** — because the engine already hides every hardware access behind one boundary. The AY work is orthogonal and almost free. The 48K is a separate, much worse tier and is out of scope here (see §10).

---

## 2. The central thesis: this is a kernel swap, not a rewrite

The codebase enforces one rule (see `CLAUDE.md` → "The hardware boundary"):

> **Only `scld.c` / `scld.h` know port `0xFF` and the screen addresses `0x4000` / `0x6000`.** Everything else is buffer-agnostic: the loop asks the kernel for the address of the hidden back buffer and the drawing code blits into whatever base it is handed.

That rule is exactly what makes a second machine cheap. The 128K has a *different* double-buffer mechanism, but it is still "draw into a hidden buffer, then flip." So porting means: **implement the same small double-buffer interface against the 128K mechanism, and leave everything above the boundary untouched.** The closer the new kernel matches the existing contract (`back()`, `back_page()`, `present()`, `wait()`, `init()`), the smaller the blast radius.

This is the single most important design decision: **the port lives entirely inside the hardware-boundary layer.** If any change is required *above* that layer, that's a signal the boundary has a leak that should be fixed first.

---

## 3. Hardware delta — what is actually different

The three target families differ in exactly two subsystems: **how you double-buffer** and **where the AY lives.** Everything else (Z80, 256×192 standard screen layout, 50 Hz interrupt, beeper) is common.

| Capability | TC2048 (current) | TC2068 / TS2068 | ZX Spectrum 128K |
|---|---|---|---|
| **Double buffer** | SCLD page-flip | SCLD page-flip (same) | Shadow screen via memory paging |
| Flip mechanism | `OUT (0xFF), 0/1` | `OUT (0xFF), 0/1` | bit 3 of port `0x7FFD` |
| Front buffer addr | `0x4000` | `0x4000` | `0x4000` (RAM page 5, always mapped) |
| Back buffer addr | `0x6000` (always mapped) | `0x6000` (always mapped) | RAM page 7 — **only** mapped when banked into `0xC000` |
| Both buffers CPU-addressable at once? | Yes (fixed) | Yes (fixed) | Yes, but only by spending the `0xC000` window |
| **AY chip** | ❌ none (beeper only) | ✅ AY-3-8912 | ✅ AY-3-8912 |
| AY register-select port | — | `0xF5` | `0xFFFD` |
| AY data port | — | `0xF6` | `0xBFFD` |
| Screen bitmap layout | standard interleaved | standard interleaved | **byte-identical** standard interleaved |

The last row is what makes the blitter portable: the ZX/Timex standard screen format is identical, so the scanline-address math and the OR-blitter work against *any* base — `0x4000`, `0x6000`, or `0xC000` — with no change beyond the base value.

---

## 4. What carries over vs. what changes

**Carries over unchanged** (the payoff of the boundary rule):

- All **pure-logic modules** — geometry, input/`controls`, player, bullet, enemy, collision, rng. They never touched hardware and stay host-testable.
- The **blitter** (`sprite.c`, `blit.asm`) and the **pre-shifted sprite data** — they take a base address and draw; the screen format is byte-identical across all targets.
- The **incremental erase+redraw** scheme (the per-buffer 2-frames-stale `prev[2][…]` dirty list). It assumes exactly **two** buffers, and the 128K shadow screen *is* two buffers — so it maps 1:1.
- **HUD, score, game-loop orchestration, title screen.** They already speak only through the kernel interface and the SFX API.

**Changes** (and only these):

1. **A new screen kernel** — a sibling to `scld.c` implementing the same interface against the 128K shadow screen (§5).
2. **The SFX/music layer** gains AY support with a runtime-selected port pair, plus a beeper fallback (§7).
3. **The memory map / stack placement** — one real constraint imposed by 128K paging (§6).
4. **Boot-time machine + AY detection** to pick the right kernel and port pair (§8).
5. **Build/linker config** — keep code in the region common to all targets; reserve the right holes (§6, §9).

---

## 5. The rendering port (the core of the work)

### How the 128K double-buffers

The 128K has **no SCLD and no `OUT (0xFF)` display register.** What it has instead is the **shadow screen**, driven by the memory pager at port `0x7FFD`:

- **Bit 3 of `0x7FFD`** selects which RAM page the video hardware scans out: `0` = page 5 (the normal screen at `0x4000`), `1` = page 7 (the shadow screen). **This bit is the page-flip** — toggle it in vblank and the display swaps, tear-free, exactly like the SCLD flip.
- **To draw into the off-screen buffer**, the CPU must reach the page that is *not* being shown. Page 5 is permanently at `0x4000`. Page 7 is reached by **banking it into the `0xC000` window** (bits 0–2 of `0x7FFD` = 7). You can hold page 5 at `0x4000` *and* page 7 at `0xC000` simultaneously, so both buffers are addressable at once — the cost is the `0xC000–0xFFFF` window.

So the new kernel implements the existing contract as:

| Interface call | SCLD kernel (today) | 128K shadow kernel |
|---|---|---|
| `back()` (hidden buffer address) | `0x4000` or `0x6000` | `0x4000` or `0xC000` |
| `present()` (HALT, then flip) | `OUT (0xFF), front^1` | HALT, then flip bit 3 of the `0x7FFD` shadow value and write it |
| `init()` | set IM1/EI, attrs, clear, show A | set IM1/EI, attrs, clear, bank page 7, show page 5 |
| `back_page()` / `wait()` | unchanged semantics | unchanged semantics |

Everything above the boundary keeps calling `back()` / `present()` and never learns which mechanism is underneath.

### Note on attributes and the static-colour trick

The game paints attributes once into *both* display files and never touches them per frame. On the 128K both attribute blocks must likewise be initialised — page 5's at `0x5800`, page 7's at the corresponding offset inside the `0xC000` window. Same idea, two locations.

---

## 6. Verified-facts & gotchas (the load-bearing details)

These are the 128K equivalents of the foundation spec's §2.1 hardware gotchas — the things that cause silent hangs or crashes, not just style nits.

- ⛔ **Port `0x7FFD` is WRITE-ONLY.** There is no read-back. The kernel **must keep a software shadow copy** of the last value written and modify *that*, never read-modify-write the port. Flipping the screen bit means `shadow ^= bit3; OUT (0x7FFD), shadow` — preserving the ROM-select and RAM-bank bits every time.
- ⛔ **Banking page 7 into `0xC000` evicts whatever lived there — including the stack.** A `+zx` build normally parks `SP` near `0xFFFF` (the current map shows `__register_sp=$FF58`). The instant page 7 is banked in, that stack memory is gone → the next `push`/`call`/interrupt corrupts or crashes. **The stack (and any hot data/IM handler state) must live at or below `0xBFFF`** for the 128K path. This stack relocation is *the* structural change the port forces.
- ⛔ **Never set bit 5 of `0x7FFD` (paging-disable lock).** Once set it freezes the pager until a hard reset — the back buffer becomes permanently unreachable. The kernel must keep it clear in every write.
- ⚠ **The performance budget does not transfer.** The 6-enemy cap and all T-state figures were measured on the **TC2048**. On the 128K the screen pages are **contended** (the exact contended-page set varies by model — issue 2/+2 vs +2A/+3), and the shadow buffer at `0xC000` lands on a contended page too, so *both* buffers cost more than the uncontended case. **Re-measure on a 128K before trusting the enemy cap**; it may need to drop.
- ⚠ **Keep code/data in the always-mapped region.** Code lives at `0x8000–0xBFFF`, which on the 128K default layout is a fixed RAM page (not the `0xC000` paging window and not page 5/7). Nothing static may sit in `0xC000–0xFFFF` on the 128K path. The Timex back-buffer hole (`0x6000–0x7AFF`) stays reserved too (harmless dead space on the 128K).
- ✅ **Interrupts / IM1 are the same story as the Timex.** The newlib crt boots with interrupts disabled; the kernel must `im 1; ei` before the first HALT, exactly as `scld_init` does today. The 128K's 50 Hz IM1 at `0x0038` behaves like the 48K's.
- ✅ **Screen format is byte-identical**, so the existing scanline math and blitter need only the new base address — no new interleave logic.

---

## 7. Audio (AY) — orthogonal, and nearly free

The AY work was designed alongside the port and is independent of the rendering change.

- **The AY lives at different ports per machine:** 128K = `0xFFFD`/`0xBFFD`, 2068 = `0xF5`/`0xF6`, TC2048/48K = no AY. That port difference is the *only* reason one might think separate builds are needed — and it isn't, because it's a runtime decision.
- **Runtime autodetect with graceful 3-way fallback:** at boot, probe each AY port pair (select a readable register, write a value, read it back; a round-trip means an AY is present). Probe the **Timex ports first** to avoid bus-collision corner cases (on a 128K, the even Timex data port disturbs the ULA and correctly fails the probe). Store the detected `(select_port, data_port)`; the replayer writes only to those. No AY → keep the existing beeper SFX.
- **The AY *helps* the frame budget.** Today's beeper SFX are **blocking** busy-loops (~7–22k T each). A frame-synced AY replayer is non-blocking (~14 register writes per frame, a few hundred T). Music goes on channels A/B, SFX on channel C — they coexist, and the loop *reclaims* budget versus the beeper.
- A music format + replayer (e.g. a PT3/Vortex-style player) is an implementation detail, not a feasibility blocker.

---

## 8. One binary vs. separate builds (the key decision)

**Audio:** trivially one binary — the AY port pair is just two runtime variables.

**Video:** one binary is feasible *if and only if* three conditions hold, all already within reach:
1. Code/data fit in `0x8000–0xBFFF` (they do today: `0x8000–0x94C2`).
2. The stack is relocated to `≤ 0xBFFF` (the one required change, §6).
3. The screen kernel is **selected at runtime** by a machine probe, and the Timex back-buffer hole (`0x6000–0x7AFF`) stays reserved (wasted but harmless on the 128K).

**Recommended architecture: a single "fat" binary** that, at boot, probes the machine and selects:

- a **rendering kernel** ∈ { SCLD page-flip (Timex) | shadow-screen paging (128K) | single-buffer fallback (bare 48K, if ever wanted) }, and
- an **audio path** ∈ { AY @ F5/F6 (2068) | AY @ FFFD/BFFD (128K) | beeper (TC2048/48K) }.

This mirrors the AY story (detect → select → fall back) and gives one artifact that "just runs" on the whole family.

**Alternative:** ship **separate per-machine `.tap`s** if the single-binary memory juggling (stack relocation + dual layout) proves more trouble than it's worth. Cleaner internally, but two build targets and two downloads. Decision deferred to the plan once the kernel exists and the stack move is proven.

---

## 9. Machine & capability detection (general approach)

Detection is small and boot-time only. The principle: **probe for the *capability*, not the brand** — that's what the runtime selection actually needs.

- **AY presence/port** — probe both port pairs as in §7 (Timex order first).
- **Double-buffer capability** — distinguish "has SCLD second screen" (Timex) from "has 128K paging" from "has neither" (48K). Candidate signals: a `0x7FFD` paging round-trip test (page a known byte and read it back through the window), an SCLD `0xFF` behaviour probe, or a ROM signature — the plan picks the most robust combination. The output is simply *which kernel to install*.
- Detection must be **conservative**: an ambiguous result falls back to the safest tier (single-buffer + beeper) rather than risking a hang.

Exact probe sequences are an implementation concern, intentionally left to the plan.

---

## 10. Out of scope / explicitly deferred

- **Bare ZX Spectrum 48K as a quality tier.** It has *no* page-flip of any kind (single fixed screen at `0x4000`, no paging, no SCLD) and *no* AY (beeper only). A 48K build means single-buffer flicker (reworking the two-buffer erase scheme down to one) **and** no music — both the hardest and the worst-looking version. The fat-binary's single-buffer/beeper fallback could *technically* light up on a 48K, but making it good is its own effort and is not pursued here.
- **The PT3/AY music content and replayer internals** — separate spec/plan.
- **+3 disk loading, 128K-specific loaders, ROM-paging tricks** — not needed; the existing `.tap` path is sufficient.

---

## 11. Effort shape (high level — not a line-level plan)

Rough sequencing, to size the work, not to prescribe edits:

1. **Confirm the boundary is airtight** — verify nothing outside the kernel touches `0xFF`/`0x4000`/`0x6000`. Fix any leak first; the whole port depends on this.
2. **Write the 128K shadow-screen kernel** behind the existing interface (§5), with the `0x7FFD` write-only shadow value and bit discipline (§6).
3. **Relocate the stack** below `0xC000` and adjust the crt/linker config; prove it on both a Timex and a 128K.
4. **Boot-time detection** selecting kernel + audio path (§8, §9), with conservative fallback.
5. **Re-measure the budget on a 128K** (contention) and adjust the enemy cap if needed.
6. **AY driver** with the dual port set + beeper fallback (§7); music content tracked separately.

The ordering matters: steps 1–3 are the real engineering; 4–6 are additive. If step 1 reveals the boundary already holds (it should), the 128K version is genuinely "a new kernel + a stack move," with the rest of the game riding along unchanged.

---

## 12. Summary

| Question | Answer |
|---|---|
| Does the 128K port require a new graphics engine? | **No — a new rendering *kernel*** behind the existing boundary. The engine, blitter, logic, HUD, and loop are reused. |
| Does the 128K support double buffering? | **Yes**, via the shadow screen (bit 3 of `0x7FFD`); it just lacks a one-bit SCLD-style display mode. |
| Where's the real work? | The `0x7FFD` shadow kernel, the **stack relocation**, and **re-measuring contention**. |
| Can it be one binary with the Timex version? | **Yes**, a fat binary with boot-time machine + AY detection and graceful fallback (recommended); separate builds are the fallback option. |
| Is the AY music feasible across machines? | **Yes** — autodetect the AY port pair (`FFFD/BFFD` vs `F5/F6`), beeper fallback; it also frees frame budget. |
| What about a bare 48K? | Out of scope — no page-flip, no AY; lowest tier only if ever pursued. |
