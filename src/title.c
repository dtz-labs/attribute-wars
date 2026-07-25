/*
 * title.c -- the title screen and its menus. See title.h.
 *
 * Text is blitted from the 8x8 ROM character set into screen A's bitmap. Only
 * screen A is shown here (no page-flip), so one static text draw plus a few
 * per-frame attribute-row highlights is the whole cost.
 */
#include "title.h"
#include "scld.h"
#include "text.h"
#include "hud.h"           /* ATTR */
#include "controls.h"      /* CTRL_* + input_read */
#include "music.h"         /* SOUND_* + music_status_text/is_playing/init */
#include <string.h>        /* memset (attribute fills) */
#include <input.h>         /* in_key_pressed + IN_KEY_SCANCODE_* */

#define AW_STR_1(x) #x
#define AW_STR(x) AW_STR_1(x)

#if !defined(APP_VERSION_STR) && defined(APP_VERSION_MAJOR) && defined(APP_VERSION_MINOR) && defined(APP_VERSION_PATCH)
#define APP_VERSION_STR AW_STR(APP_VERSION_MAJOR) "." AW_STR(APP_VERSION_MINOR) "." AW_STR(APP_VERSION_PATCH)
#elif !defined(APP_VERSION_STR) && defined(APP_VERSION_MAJOR) && defined(APP_VERSION_MINOR)
#define APP_VERSION_STR AW_STR(APP_VERSION_MAJOR) "." AW_STR(APP_VERSION_MINOR)
#elif !defined(APP_VERSION_STR)
#define APP_VERSION_STR "dev"
#endif

#ifdef ZX_SINCLAIR_DUAL_STICK
#define CTRL_DUAL_LABEL "3 SIN JOY"
#else
#define CTRL_DUAL_LABEL "3 TWO JOYSTICKS (TS2068)"
#endif

/* Fill one 32-cell attribute row of screen A (title highlights). */
static void title_attr_row(u8 row, u8 v)
{
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)row * 32u;
    u8  c;
    for (c = 0; c < 32u; c++) {
        a[c] = v;
    }
}

/* ---- title shine-sweep ----------------------------------------------------
 * A one-cell glint walks across each title/menu/credit text row in sequence,
 * then pauses briefly before restarting at "ATTRIBUTE WARS". Attribute writes
 * are title-screen-only, so clarity beats micro-optimising the small row table.
 */
#ifndef ZX128_PAGE_FLIP
#define SHINE_ROWS   15u
#define SHINE_PAUSE  40u

static const u8 shine_row[SHINE_ROWS] = {
     3u,  4u,  7u,  8u,  9u, 10u, 12u, 13u, 14u, 15u, 16u, 18u, 20u, 22u, 23u
};
static const u8 shine_col0[SHINE_ROWS] = {
     9u,  5u,  2u,  2u,  2u,  2u,  2u,  2u,  2u,  2u,  2u,  2u,  5u,  0u,  8u
};
static const u8 shine_col1[SHINE_ROWS] = {
    22u, 26u,  9u, 27u, 27u, 25u,  6u,  9u, 11u,  5u, 21u, 13u, 25u, 31u, 22u
};
#endif

static void title_base_attrs(u8 sel, u8 snd)
{
    title_attr_row( 3, ATTR(1, 0, 5));      /* title: bright cyan */
    title_attr_row( 4, ATTR(1, 0, 5));      /* version */
    title_attr_row( 7, ATTR(1, 0, 5));      /* CONTROLS heading */
    title_attr_row( 8, (sel == CTRL_KEMPSTON_MOVE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row( 9, (sel == CTRL_KEMPSTON_FIRE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row(10, (sel == CTRL_DUAL_STICK)    ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row(12, ATTR(1, 0, 5));      /* SOUND heading */
    title_attr_row(13, (snd == SOUND_BEEPER)       ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row(14, (snd == SOUND_MUSIC_FX)     ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row(15, (snd == SOUND_FX)           ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
    title_attr_row(16, ATTR(0, 0, 5));      /* detected machine / AY */
    title_attr_row(18, ATTR(1, 0, 4));      /* START */
    title_attr_row(20, ATTR(0, 0, 7));
    title_attr_row(22, ATTR(0, 0, 7));
    title_attr_row(23, ATTR(0, 0, 7));
}

#ifndef ZX128_PAGE_FLIP
static void title_shine(u8 row_idx, u8 col)
{
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)shine_row[row_idx] * 32u;
    if (col >= shine_col0[row_idx] && col <= shine_col1[row_idx]) {
        a[col] = ATTR(1, 0, 7);            /* bright white glint */
    }
    if (col > shine_col0[row_idx]) {
        a[(u8)(col - 1u)] = ATTR(1, 0, 6); /* yellow trail */
    }
}
#endif

/* Draw the menu, poll keys 1/2/3 (controls), 4/5/6 (sound), and 0 (start).
 * AY setup is still deferred on the first title screen; if music is already
 * playing, selecting BEEPER or FX stops just the tune immediately. */
void title_screen(u8 *ctrl_out, u8 *sound_out, u8 initial_sound)
{
    u8 sel = CTRL_KEMPSTON_MOVE;
    u8 snd = initial_sound;
#ifndef ZX128_PAGE_FLIP
    u8 shine_i = 0u;
    u8 shine_c = shine_col0[0];
    u8 pause = 0u;
#endif

    scld_show_a();
    scld_clear(SCLD_SCREEN_A);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);   /* white on black */

    put_text(SCLD_SCREEN_A,  9,  3, "ATTRIBUTE WARS");
    put_text(SCLD_SCREEN_A,  5,  4, "version " APP_VERSION_STR);
    put_text(SCLD_SCREEN_A,  2,  7, "CONTROLS");
    put_text(SCLD_SCREEN_A,  2,  8, "1 KEMPSTON MOVE  KEYS FIRE");
    put_text(SCLD_SCREEN_A,  2,  9, "2 KEYS MOVE  KEMPSTON FIRE");
    put_text(SCLD_SCREEN_A,  2, 10, CTRL_DUAL_LABEL);
    put_text(SCLD_SCREEN_A,  2, 12, "SOUND");
    put_text(SCLD_SCREEN_A,  2, 13, "4 BEEPER");
    put_text(SCLD_SCREEN_A,  2, 14, "5 MUSIC+FX");
    put_text(SCLD_SCREEN_A,  2, 15, "6 FX");
    put_text(SCLD_SCREEN_A,  2, 16, music_status_text());
    put_text(SCLD_SCREEN_A,  2, 18, "0 START GAME");
    put_text(SCLD_SCREEN_A,  5, 20, "\x7F 2026 Claude & Codex");
    put_text(SCLD_SCREEN_A,  0, 22, "human in the loop: @mpasternak79");
    put_text(SCLD_SCREEN_A,  8, 23, "music: @paatorr");

    title_base_attrs(sel, snd);

    for (;;) {
        title_base_attrs(sel, snd);
#ifndef ZX128_PAGE_FLIP
        title_shine(shine_i, shine_c);

        if (pause > 0u) {
            pause--;
            if (pause == 0u) {
                shine_i = 0u;
                shine_c = shine_col0[0];
            }
        } else {
            if (shine_c < shine_col1[shine_i]) {
                shine_c++;
            } else if (shine_i < (SHINE_ROWS - 1u)) {
                shine_i++;
                shine_c = shine_col0[shine_i];
            } else {
                pause = SHINE_PAUSE;
            }
        }
#endif

        if      (in_key_pressed(IN_KEY_SCANCODE_1)) sel = CTRL_KEMPSTON_MOVE;
        else if (in_key_pressed(IN_KEY_SCANCODE_2)) sel = CTRL_KEMPSTON_FIRE;
        else if (in_key_pressed(IN_KEY_SCANCODE_3)) sel = CTRL_DUAL_STICK;
        else if (in_key_pressed(IN_KEY_SCANCODE_4)) {
            snd = SOUND_BEEPER;
            if (music_is_playing()) {
                music_init(SOUND_BEEPER);
            }
        }
        else if (in_key_pressed(IN_KEY_SCANCODE_5)) snd = SOUND_MUSIC_FX;
        else if (in_key_pressed(IN_KEY_SCANCODE_6)) {
            snd = SOUND_FX;
            if (music_is_playing()) {
                music_init(SOUND_FX);
            }
        }
        else if (in_key_pressed(IN_KEY_SCANCODE_0)) break;

        scld_wait();
    }
    *ctrl_out = sel;
    *sound_out = snd;
}
