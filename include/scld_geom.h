/*
 * scld_geom.h -- the two PURE display-file geometry helpers.
 *
 * These live apart from scld.h on purpose. They are `static inline`, and sdcc
 * emits an out-of-line copy of each into EVERY translation unit that sees them,
 * used or not -- roughly 95 dead bytes per unit. With scld.h included by a
 * dozen modules that adds up to real RAM on the 48K, where the stack-gap check
 * (tools/check_zx_stack_layout.py) has only a few hundred bytes of margin.
 *
 * So: include "scld.h" for the screen backend, and include THIS header only in
 * the handful of modules that actually compute scanline addresses in C
 * (scld.c, hud.c, text.c, the measurement harness and the host tests). The hot
 * blitter does not -- it uses scld_row_off[] or hand asm.
 *
 * Pure integer math, no hardware: host-unit-testable (test/test_scld.c).
 */
#ifndef SCLD_GEOM_H
#define SCLD_GEOM_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * scld_scanline -- leftmost byte address of pixel scanline `y` (0..191) in the
 * display file based at `base` (SCLD_SCREEN_A or SCLD_SCREEN_B). The ZX/Timex
 * bitmap is interleaved; the SCLD_ROW_BYTES bytes of one scanline are then
 * contiguous from the returned address. This is the bridge a sprite/line
 * blitter builds on (compute once, step rows) -- the cheap way to draw.
 *
 * Pure function (no hardware) -> inlined here so it is host-unit-testable and
 * costs nothing at the call site.
 * ------------------------------------------------------------------------- */
static inline uint8_t *scld_scanline(uint16_t base, uint8_t y)
{
    uint16_t off = (uint16_t)(base
            + ((uint16_t)(y & 0xC0u) << 5)    /* third (0/1/2)               */
            + ((uint16_t)(y & 0x07u) << 8)    /* pixel row within char (0-7) */
            + ((uint16_t)(y & 0x38u) << 2));  /* char row within third (0-7) */
    /* via uintptr_t so this header compiles on the 64-bit host too (target
     * pointers are 16-bit; the (uint16_t) above gives the real screen wrap). */
    return (uint8_t *)(uintptr_t)off;
}

/* ---------------------------------------------------------------------------
 * scld_next_scanline -- given the byte address of one pixel scanline, return
 * the address of the scanline directly below it, WITHOUT recomputing from y.
 * This is the cheap way to walk an 8-pixel-tall sprite down the interleaved
 * screen (compute scld_scanline once, then step). It implements the classic
 * Z80 "down a line" carry across the pixel-row / char-row / third boundaries.
 *
 * Valid for scanlines 0..190 (stepping from y=191 would leave the bitmap);
 * blitters clip at the bottom edge instead of stepping past it.
 *
 * Pure -> inlined here, host-testable against scld_scanline.
 * ------------------------------------------------------------------------- */
static inline uint16_t scld_next_scanline(uint16_t a)
{
    uint8_t h = (uint8_t)(a >> 8);
    uint8_t l = (uint8_t)(a & 0xFFu);

    h++;                                  /* ++pixel row (ripples into third)   */
    if ((h & 0x07u) != 0u) {              /* still inside the char cell         */
        return (uint16_t)(((uint16_t)h << 8) | l);
    }
    {
        uint8_t nl = (uint8_t)(l + 0x20u);   /* ++char row                      */
        if (nl >= l) {                       /* no carry -> undo the third ripple */
            h = (uint8_t)(h - 0x08u);
        }
        return (uint16_t)(((uint16_t)h << 8) | nl);
    }
}

#endif /* SCLD_GEOM_H */
