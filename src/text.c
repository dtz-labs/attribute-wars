/*
 * text.c -- ROM-font text blitting. See text.h.
 *
 * Text is blitted from the 8x8 ROM character set (CHARS = 0x3C00 + code*8) into
 * a display file's bitmap.
 */
#include "text.h"
#include "scld.h"
#include "scld_geom.h"  /* scld_scanline / scld_next_scanline */

void put_char(u16 base, u8 col, u8 row, u8 ch)
{
    const u8 *g = (const u8 *)(uintptr_t)(0x3C00u + (u16)ch * 8u);
    u8 r;
    for (r = 0; r < 8u; r++) {
        scld_scanline(base, (u8)((u16)row * 8u + r))[col] = g[r];
    }
}

void put_text(u16 base, u8 col, u8 row, const char *s)
{
    while (*s != '\0') {
        put_char(base, col, row, (u8)*s);
        col++;
        s++;
    }
}

/* put_text into BOTH bitmaps (so the page-flip's current page shows it). */
void put_text_both(u8 col, u8 row, const char *s)
{
    put_text(SCLD_SCREEN_A, col, row, s);
    put_text(SCLD_SCREEN_B, col, row, s);
}

/* Render the 6 BCD score digits left-to-right at (col,row) of BOTH bitmaps. */
void put_score_digits(u8 col, u8 row, const score_t *s)
{
    u8 i;
    for (i = 0; i < 6u; i++) {
        u8 ch = (u8)('0' + s->digits[i]);
        put_char(SCLD_SCREEN_A, (u8)(col + i), row, ch);
        put_char(SCLD_SCREEN_B, (u8)(col + i), row, ch);
    }
}

/* Render a u8 (0..255) as up to 3 right-aligned decimal chars into BOTH bitmaps.
 * Only /10 and /100 on a small u8 — cheap, no runtime-value division. */
void put_u8(u8 col, u8 row, u8 v)
{
    u8 h = (u8)(v / 100u);
    u8 t = (u8)((v / 10u) % 10u);
    u8 o = (u8)(v % 10u);
    if (h) {
        put_char(SCLD_SCREEN_A, col, row, (u8)('0' + h));
        put_char(SCLD_SCREEN_B, col, row, (u8)('0' + h));
    }
    if (h || t) {
        put_char(SCLD_SCREEN_A, (u8)(col + 1), row, (u8)('0' + t));
        put_char(SCLD_SCREEN_B, (u8)(col + 1), row, (u8)('0' + t));
    }
    put_char(SCLD_SCREEN_A, (u8)(col + 2), row, (u8)('0' + o));
    put_char(SCLD_SCREEN_B, (u8)(col + 2), row, (u8)('0' + o));
}
