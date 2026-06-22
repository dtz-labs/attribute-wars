/*
 * hud.c -- minimal HUD widgets (target-only draw). See hud.h.
 *
 * The background (checker + frame) is owned by main.c (bg_attr/bg_paint); this
 * module only draws the overlay widgets: hearts + shield dots on the top border,
 * and the timer + dash bars on the bottom border. The score itself is text,
 * drawn by main.c (it has the put_char text routine and the game state).
 */
#include "hud.h"
#include "scld.h"
#include "sprite.h"
#include "sprites.h"   /* spr_heart */
#include <string.h>    /* memset (top-border bitmap clears) */

/* HUD's own pre-shifted heart table (built once by hud_init()). */
static u8 ps_hud_heart[SPR_PRESHIFT_SIZE];

void hud_init(void)
{
    spr_preshift(ps_hud_heart, spr_heart);
}

void put_attr(u8 row, u8 col, u8 v)
{
    u16 i = (u16)row * 32u + (u16)col;
    ((u8 *)SCLD_ATTRS_A)[i] = v;
    ((u8 *)SCLD_ATTRS_B)[i] = v;
}

/* ---- top-border bitmap widgets (hearts left, shield dots right) -------- */
static u8 last_lives   = 0xFFu;
static u8 last_shields = 0xFFu;

void hud_draw_lives(u8 lives)
{
    u8 i, r;
    if (lives == last_lives) {
        return;
    }
    last_lives = lives;
    /* Erase only the hearts region (left half cols 0..15) so the shield dots on
     * the right survive, then repaint hearts. */
    for (r = 0; r < 8u; r++) {
        memset(scld_scanline(SCLD_SCREEN_A, r), 0, 16u);
        memset(scld_scanline(SCLD_SCREEN_B, r), 0, 16u);
    }
    for (i = 0; i < lives && i < 8u; i++) {
        u8 x = (u8)(8u + i * 8u);
        spr_draw(SCLD_SCREEN_A, x, 0, ps_hud_heart);
        spr_draw(SCLD_SCREEN_B, x, 0, ps_hud_heart);
    }
}

void hud_draw_shields(u8 shields)
{
    u8 i, r;
    if (shields == last_shields) {
        return;
    }
    last_shields = shields;
    /* Erase only the dots region (right half cols 16..31) so the hearts survive. */
    for (r = 0; r < 8u; r++) {
        memset(scld_scanline(SCLD_SCREEN_A, r) + 16u, 0, 16u);
        memset(scld_scanline(SCLD_SCREEN_B, r) + 16u, 0, 16u);
    }
    for (i = 0; i < shields && i < 8u; i++) {
        u8 x = (u8)(240u - i * 8u);
        bul_draw(SCLD_SCREEN_A, x, 2);
        bul_draw(SCLD_SCREEN_B, x, 2);
    }
}

/* ---- bottom-border timer bar ------------------------------------------- */
#define TIMER_COL0   9u    /* timer bar cols 9..18 (10 cells)               */
#define TIMER_CELLS  10u
#define ATTR_TIMER   ATTR(1, 6, 0)   /* yellow paper (filled)               */
#define ATTR_EMPTY   ATTR(1, 3, 7)   /* unfilled: the magenta frame colour  */

/* Scale a 0..total value to a 0..cells bar length (rounds to nearest, no float;
 * clamps). num==0 -> 0, num>=total -> cells. */
static u8 bar_len(u16 num, u16 total, u8 cells)
{
    u16 r;
    if (total == 0u || num == 0u) {
        return 0u;
    }
    if (num >= total) {
        return cells;
    }
    r = (u16)(((u16)(num * (u16)cells) + (u16)(total >> 1)) / total);
    if (r > (u16)cells) {
        r = (u16)cells;
    }
    return (u8)r;
}

static void draw_bar(u8 col0, u8 cells, u8 fill, u8 fill_attr)
{
    u8 c;
    for (c = 0; c < cells; c++) {
        put_attr(23u, (u8)(col0 + c), (u8)(c < fill ? fill_attr : ATTR_EMPTY));
    }
}

static u8 last_timer_len = 0xFFu;

void hud_draw_timer(u16 frames_left, u16 frames_total)
{
    u8 len = bar_len(frames_left, frames_total, TIMER_CELLS);
    if (len == last_timer_len) {
        return;
    }
    last_timer_len = len;
    draw_bar(TIMER_COL0, TIMER_CELLS, len, ATTR_TIMER);
}

void hud_invalidate(void)
{
    last_lives     = 0xFFu;
    last_shields   = 0xFFu;
    last_timer_len = 0xFFu;
}
