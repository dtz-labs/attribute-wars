/*
 * sfx.h -- 1-bit beeper sound effects API.
 *
 * Target (Z80 / sdcc): sfx_play sets globals and calls sfx_blip (in sfx.asm)
 * which toggles only bit 4 of port 0xFE, keeping the border black.
 * Host (macOS unit-test build): sfx_play is an empty no-op.
 *
 * sfx_play is synchronous and blocking (it busy-loops for the tone duration).
 * Short SFX (SHOOT/EXPLODE/HIT) are safe in the game loop; long SFX
 * (DEATH/BONUS/EXTRA_LIFE) should only be called during frozen pauses.
 */
#ifndef SFX_H
#define SFX_H

#include "types.h"

/* Sound effect identifiers -- keep in sync with the table in sfx.c. */
enum {
    SFX_SHOOT,
    SFX_EXPLODE,
    SFX_HIT,
    SFX_DEATH,
    SFX_EXTRA_LIFE,
    SFX_BONUS,
    SFX_DASH,
    SFX_DASH_READY,
    SFX_DASH_FAIL,
    SFX_N           /* sentinel / count */
};

/*
 * sfx_play(id) -- play a beeper sound effect.
 * Target: synchronous square wave via sfx_blip asm routine.
 * Host:   no-op.
 */
void sfx_play(u8 id);

/*
 * sfx_noise() -- a short, harsh random-pitch crackle (~6k T). Call once PER
 * FRAME while an explosion is on screen so the whole burst stays noisy.
 * Host: no-op.
 */
void sfx_noise(void);

#endif /* SFX_H */
