/*
 * music.h -- AY chiptune music (target-only). Owns all AY-port access, exactly
 * as scld.c owns port 0xFF and sfx.asm owns the beeper. The beeper SFX layer is
 * untouched and mixes over the music in hardware.
 *
 * Plays Pator's "Spectrumizer.pt3" via z88dk's VortexTracker2 player, on any
 * machine with an AY (ZX 128/+2/+3, TS2068/TC2068, or a 48K + AY interface),
 * auto-detected at runtime. On a beeper-only machine (e.g. the TC2048)
 * music_init() returns 0 and every other entry is a cheap no-op.
 */
#ifndef MUSIC_H
#define MUSIC_H

#include "types.h"

/* Probe for an AY, latch its port pair, load + start the tune. Returns 1 if an
 * AY was found (music will play), 0 otherwise. Call once after scld_init(). */
u8   music_init(void);

/* Advance the player one 50 Hz frame. Call once per HALT. No-op without an AY. */
void music_tick(void);

/* Stop the music (silences the AY). Defined for completeness; unused for now. */
void music_stop(void);

/* ---- AY sound effects (only when an AY is present) -------------------------
 * While the tune plays, the BLOCKING beeper SFX stall the loop and make the
 * music wobble, so SFX move onto AY channel C instead -- instant register
 * writes, no busy-loop. The PT3 player keeps channels A+B; channel C is the SFX.
 * On a beeper-only machine these are unused and sfx.c keeps the beeper. */

/* 1 if an AY was detected (i.e. music is playing and AY SFX should be used). */
u8   music_is_on(void);

/* Trigger sound effect `id` (an SFX_* from sfx.h) on AY channel C. */
void music_sfx(u8 id);

/* Short random-noise crackle on channel C (explosion grain). */
void music_sfx_noise(void);

#endif /* MUSIC_H */
