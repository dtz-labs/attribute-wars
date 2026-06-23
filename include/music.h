/*
 * music.h -- AY chiptune music (target-only). Owns all AY-port access, exactly
 * as scld.c owns port 0xFF and sfx.asm owns the beeper. The beeper SFX layer is
 * untouched and mixes over the music in hardware.
 *
 * Plays Pator's "Spectrumizer.pt3" via z88dk's VortexTracker2 player, on any
 * machine with an AY (ZX 128/+2/+3, TS2068/TC2068, or a 48K + AY interface),
 * selected from the title-screen SOUND menu. On a beeper-only choice/machine,
 * music_init() returns 0 and every other AY entry is a cheap no-op.
 */
#ifndef MUSIC_H
#define MUSIC_H

#include "types.h"

#define SOUND_BEEPER    0u
#define SOUND_MUSIC_FX  1u
#define SOUND_FX        2u

/* Pick the title-screen default without enabling IM2 or starting the player:
 * TC2068/TS2068 or standard AY -> MUSIC+FX; TC2048 -> BEEPER. */
u8   music_default_sound(void);

/* Short title-screen hardware summary, e.g. "HW TC2048   AY NO". */
const char *music_status_text(void);

/* Apply the title-screen SOUND choice. Returns 1 if the AY path is enabled
 * (MUSIC+FX or FX), 0 if the beeper path remains active. */
u8   music_init(u8 mode);

/* Advance the player one 50 Hz frame. Call once per HALT. No-op without an AY. */
void music_tick(void);

/* Stop the music (silences the AY). Defined for completeness; unused for now. */
void music_stop(void);

/* ---- AY sound effects (only when an AY is present) -------------------------
 * While the tune plays, the BLOCKING beeper SFX stall the loop and make the
 * music wobble, so SFX move onto AY channel C instead -- instant register
 * writes, no busy-loop. The PT3 player keeps channels A+B; channel C is the SFX.
 * On a beeper-only machine these are unused and sfx.c keeps the beeper. */

/* 1 if the AY SFX path is active (MUSIC+FX or FX), else use the beeper. */
u8   music_is_on(void);

/* 1 only when the PT3 tune is currently playing. */
u8   music_is_playing(void);

/* Trigger sound effect `id` (an SFX_* from sfx.h) on AY channel C. */
void music_sfx(u8 id);

/* Short random-noise crackle on channel C (explosion grain). */
void music_sfx_noise(void);

#endif /* MUSIC_H */
