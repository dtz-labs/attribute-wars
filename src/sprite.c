/*
 * sprite.c -- 8x8 sprite blitter, C side.
 *
 * Draw uses PRE-SHIFTED sprites: spr_preshift() expands an 8-byte source sprite
 * into a 128-byte table (8 shift slices x 8 rows x {left,right}) ONCE; spr_draw
 * then just stashes globals and calls the asm, which ORs the right slice in with
 * no per-row shift. This is the big sprite-cost win (the runtime shift was the
 * hot path). Convention-safe via globals (spec 15.5).
 */
#include "sprite.h"

/* ---- parameter block shared with blit.asm (must stay in sync) ---- */
u16       spr_base;    /* back-buffer base (0x4000 / 0x6000)          */
u8        spr_x;       /* pixel x                                     */
u8        spr_y;       /* pixel y (0..191)                            */
const u8 *spr_ptr;     /* pre-shifted sprite (SPR_PRESHIFT_SIZE bytes) */

extern void blit8_asm(void);
extern void erase8_asm(void);
extern void bul_draw_asm(void);
extern void bul_erase_asm(void);

void spr_preshift(u8 *dst, const u8 *src)
{
    u8 sh, row;
    for (sh = 0; sh < 8; sh++) {
        for (row = 0; row < 8; row++) {
            /* left = src>>sh, right = src<<(8-sh), via 16-bit (src<<8)>>sh. */
            u16 w = (u16)(((u16)src[row] << 8) >> sh);
            dst[(u8)(sh * 16u + row * 2u)]      = (u8)(w >> 8);
            dst[(u8)(sh * 16u + row * 2u + 1u)] = (u8)w;
        }
    }
}

void spr_draw(u16 base, u8 x, u8 y, const u8 *preshift)
{
    spr_base = base;
    spr_x    = x;
    spr_y    = y;
    spr_ptr  = preshift;
    blit8_asm();
}

void spr_erase(u16 base, u8 x, u8 y)
{
    spr_base = base;
    spr_x    = x;
    spr_y    = y;
    erase8_asm();
}

/* Cheap bullet (3x3 dot): the work is in asm (bul_draw_asm/bul_erase_asm) for
 * the same reason as the sprite blit -- C per-row screen addressing is too slow.
 * Here we only stash the globals and call. */
void bul_draw(u16 base, u8 x, u8 y)
{
    spr_base = base;
    spr_x    = x;
    spr_y    = y;
    bul_draw_asm();
}

void bul_erase(u16 base, u8 x, u8 y)
{
    spr_base = base;
    spr_x    = x;
    spr_y    = y;
    bul_erase_asm();
}
