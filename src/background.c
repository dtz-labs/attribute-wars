/*
 * background.c -- generated arena floor + frame + ULA border. See background.h.
 */
#include "background.h"
#include "scld.h"
#include "hud.h"      /* ATTR */
#include <z80.h>      /* z80_outp() for the ULA border */

#define BORDER_FLASH_FRAMES 4u

static u8 bg_pattern = BG_GRID;
static u8 border_flash;

/* Background attribute for one cell: a bright-magenta frame all the way around
 * the screen (rows 0/23 + cols 0/31), with WHITE ink so the HUD text/sprites
 * drawn on the frame stay readable, over a black/dark-blue generated floor.
 * Used both to paint the arena and to RESTORE a cell after an explosion /
 * telegraph pulse. */
u8 bg_attr(u8 row, u8 col)
{
    u8 blue;
    if (row == 0u || row == 23u || col == 0u || col == 31u) {
        return ATTR(1, 3, 7);          /* frame: bright magenta, white ink    */
    }
    if (!bg_pattern) {
        blue = (u8)((row + col) & 1u);
    } else if (bg_pattern == BG_GRID) {
        blue = (u8)(((row & 3u) == 0u) || ((col & 3u) == 0u));
    } else {
        blue = (u8)(((u8)(row + col) >> 1) & 1u);
    }
    return ATTR(0, blue, 7);           /* black/dark-blue paper, white ink    */
}

/* Paint the whole arena into BOTH attribute blocks (identical on both screens,
 * so the page-flip never disturbs colour). */
void bg_paint(void)
{
    u8 *a = (u8 *)SCLD_ATTRS_A;
    u8 *b = (u8 *)SCLD_ATTRS_B;
    u8  row, col;
    u16 i = 0;
    for (row = 0; row < 24u; row++) {
        for (col = 0; col < 32u; col++, i++) {
            u8 v = bg_attr(row, col);
            a[i] = v;
            b[i] = v;
        }
    }
}

void bg_next_pattern(void)
{
    if (++bg_pattern >= BG_PATTERN_N) {
        bg_pattern = BG_CHECKER;
    }
}

void border_flash_red(void)
{
    border_flash = BORDER_FLASH_FRAMES;
}

void border_reset(void)
{
    border_flash = 0u;
}

void border_tick(void)
{
    if (border_flash) {
        border_flash--;
        z80_outp(0xFEu, (border_flash & 1u) ? BORDER_RED : BORDER_BLACK);
    } else {
        z80_outp(0xFEu, BORDER_BLACK);
    }
}
