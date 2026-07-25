/*
 * text.h -- 8x8 text blitting from the ROM character set.
 *
 * Glyphs come straight out of the ROM font (CHARS = 0x3C00 + code*8) and are
 * written into a display file's bitmap, so there is no font of our own to ship.
 * These are COLD-PATH routines -- title screen, GAME OVER, the HUD score --
 * never the per-frame render loop, so clarity beats micro-optimisation here.
 *
 * The `_both` / SCLD_SCREEN_A+B variants write the same glyph into both display
 * files, which is what keeps text stable across a page-flip.
 */
#ifndef TEXT_H
#define TEXT_H

#include "types.h"
#include "score.h"

/* One ROM glyph at character cell (col,row) of the bitmap based at `base`. */
void put_char(u16 base, u8 col, u8 row, u8 ch);

/* NUL-terminated string from (col,row) rightwards, into one bitmap. */
void put_text(u16 base, u8 col, u8 row, const char *s);

/* Same string into BOTH bitmaps, so the page-flip never hides it. */
void put_text_both(u8 col, u8 row, const char *s);

/* The 6 BCD score digits left-to-right at (col,row) of BOTH bitmaps. */
void put_score_digits(u8 col, u8 row, const score_t *s);

/* A u8 (0..255) as up to 3 right-aligned decimal chars, into BOTH bitmaps. */
void put_u8(u8 col, u8 row, u8 v);

#endif /* TEXT_H */
