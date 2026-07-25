/*
 * gameover.c -- the GAME OVER screen. See gameover.h.
 */
#include "gameover.h"
#include "background.h"    /* BORDER_* */
#include "scld.h"
#include "text.h"
#include "hud.h"           /* ATTR */
#include "controls.h"
#include <z80.h>           /* z80_outp() for the ULA border */
#include <string.h>        /* memset (attribute fills) */
#include <input.h>         /* in_key_pressed + IN_KEY_SCANCODE_* */

/* Whole-screen red/white flash on GAME OVER, with the ULA border in sync. */
static void game_over_flash(void)
{
    u8 k, d;
    for (k = 0; k < 8u; k++) {
        u8 v = (k & 1u) ? ATTR(1, 2, 0) : ATTR(1, 7, 0);
        z80_outp(0xFEu, (k & 1u) ? BORDER_RED : 7u);
        memset((u8 *)SCLD_ATTRS_A, v, SCLD_ATTRS_LEN);
        memset((u8 *)SCLD_ATTRS_B, v, SCLD_ATTRS_LEN);
        d = 6;
        while (d--) scld_wait();        /* music ticks from the IM2 ISR */
    }
    z80_outp(0xFEu, BORDER_BLACK);
}

u8 game_over_screen(const game_state_t *g, u8 death_wave)
{
    game_over_flash();

    scld_clear(SCLD_SCREEN_A);
    scld_clear(SCLD_SCREEN_B);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);   /* white on black */
    memset((u8 *)SCLD_ATTRS_B, ATTR(0, 0, 7), SCLD_ATTRS_LEN);

    put_text_both(11,  6, "GAME OVER");
    put_text_both( 7, 10, "SCORE");
    put_score_digits(13, 10, &g->score);
    put_text_both( 7, 12, "WAVE");
    put_u8(13, 12, death_wave);
    put_text_both( 3, 17, "FIRE/SPACE  RESUME WAVE");
    put_u8(27, 17, death_wave);
    put_text_both( 3, 19, "Q           NEW GAME");
    put_text_both( 3, 21, "ENTER       MAIN MENU");

    for (;;) {
        intent_t in;
        input_read(DIR_NONE, &in);    /* scheme-agnostic read */
        /* CONTINUE on the FIRE button or SPACE. In the twin-stick schemes the
         * FIRE button maps to BOOST (scheme A/C) or fire-in-heading (scheme B),
         * so accept either intent here, plus SPACE directly. */
        if (in.boost || in.fire || in_key_pressed(IN_KEY_SCANCODE_SPACE)) {
            return GAME_OVER_RESUME;   /* resume from the death wave, score 0 */
        }
        if (in_key_pressed(IN_KEY_SCANCODE_q)) {
            return GAME_OVER_NEW_GAME;
        }
        if (in_key_pressed(IN_KEY_SCANCODE_ENTER)) {
            return GAME_OVER_MENU;
        }
        scld_wait();                   /* music continues from the IM2 ISR */
    }
}
