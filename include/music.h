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

#endif /* MUSIC_H */
