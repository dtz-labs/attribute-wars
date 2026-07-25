/*
 * gameover.h -- the GAME OVER screen (spec §7).
 *
 * Flashes the whole screen, then shows the final score and the wave the player
 * died on, and waits for a choice. Everything is drawn into BOTH bitmaps and
 * both attribute blocks, because the page-flip is not running while the screen
 * waits -- whichever page the last flip left visible has to read correctly.
 *
 * The caller owns the game_state reset that follows the returned choice.
 */
#ifndef GAMEOVER_H
#define GAMEOVER_H

#include "types.h"
#include "score.h"     /* game_state_t */

/* game_over_screen() results. */
#define GAME_OVER_RESUME   0u   /* FIRE/SPACE: resume from the death wave  */
#define GAME_OVER_NEW_GAME 1u   /* Q:          fresh game from wave 1      */
#define GAME_OVER_MENU     2u   /* ENTER:      back to the title menu      */

/* Show the screen and block until the player picks one of the three options. */
u8 game_over_screen(const game_state_t *g, u8 death_wave);

#endif /* GAMEOVER_H */
