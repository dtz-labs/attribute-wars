/*
 * sprite.h -- pixel-positioned 8x8 sprite blitter for the SCLD double buffer.
 *
 * Draw uses PRE-SHIFTED sprites: call spr_preshift() once per sprite to build a
 * SPR_PRESHIFT_SIZE-byte table, then pass that table to spr_draw(). This drops
 * the per-row shift from the hot path. Erase clears the 8x8 box. Clipped at the
 * right and bottom edges; background assumed black (draw = OR, erase = clear).
 */
#ifndef SPRITE_H
#define SPRITE_H

#include "types.h"

/* Pre-shifted table size: 8 shifts x 8 rows x 2 bytes. */
#define SPR_PRESHIFT_SIZE 128u

/* Expand an 8-byte source sprite into a pre-shifted table at `dst`
 * (SPR_PRESHIFT_SIZE bytes). Call once at startup per sprite. */
void spr_preshift(u8 *dst, const u8 *src);

/* Draw a pre-shifted sprite at pixel (x,y) into the buffer based at `base`. */
void spr_draw(u16 base, u8 x, u8 y, const u8 *preshift);

/* Clear the 8x8 box at pixel (x,y) in `base`. */
void spr_erase(u16 base, u8 x, u8 y);

/* Cheap bullet rendering: a small 3x3 dot at (x,y), far cheaper than a full
 * sprite. draw = OR, erase = clear. (Bullets multiply, so they must be cheap.) */
void bul_draw(u16 base, u8 x, u8 y);
void bul_erase(u16 base, u8 x, u8 y);

/* ---- HOT-PATH fast path: skip the C wrapper -------------------------------
 * The functions above marshal 4 args + a full sdcc_iy call frame (push/restore
 * IY, reload args) on EVERY blit. In the per-frame render loop that ran ~18x,
 * the call plumbing rivalled the actual byte-pushing. These macros set the
 * blitter's parameter globals INLINE and call the asm directly -- identical
 * work, no function call. Use them in the game loop; keep the functions for
 * cold paths (HUD, title) where readability beats a few T-states. */
extern u16       spr_base;   /* back-buffer base (0x4000 / 0x6000)           */
extern u8        spr_x;      /* pixel x                                      */
extern u8        spr_y;      /* pixel y (0..191)                             */
extern const u8 *spr_ptr;    /* pre-shifted sprite (SPR_PRESHIFT_SIZE bytes) */
void blit8_asm(void);
void erase8_asm(void);
void bul_draw_asm(void);
void bul_erase_asm(void);

#define SPR_DRAW(base_, x_, y_, ptr_) \
    (spr_base = (base_), spr_x = (x_), spr_y = (y_), spr_ptr = (ptr_), blit8_asm())
#define SPR_ERASE(base_, x_, y_) \
    (spr_base = (base_), spr_x = (x_), spr_y = (y_), erase8_asm())
#define BUL_DRAW(base_, x_, y_) \
    (spr_base = (base_), spr_x = (x_), spr_y = (y_), bul_draw_asm())
#define BUL_ERASE(base_, x_, y_) \
    (spr_base = (base_), spr_x = (x_), spr_y = (y_), bul_erase_asm())

#endif /* SPRITE_H */
