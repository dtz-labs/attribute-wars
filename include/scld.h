/*
 * scld.h -- Timex Sinclair Custom Logic Device (SCLD) standard-resolution
 *           DOUBLE-BUFFERING for the Timex TC2048 / TC2068 / TS2068.
 *
 * ===========================================================================
 *  A small, reusable, z88dk-friendly library for flicker-free page-flipping
 *  on Timex machines -- the one trick a stock 48K ZX Spectrum cannot do.
 *  Built and verified for the TC2048 with z88dk (sdcc_iy) + Fuse.
 * ===========================================================================
 *
 * WHY THIS EXISTS
 *   The Timex SCLD adds a second display file at 0x6000 and a display-mode
 *   register at port 0xFF that selects which file the video hardware shows.
 *   By drawing into the hidden file and then flipping, you get tear-free,
 *   flicker-free animation. z88dk ships no helper for this (its Timex support
 *   is TS2068 hi-res; its screen helpers are hardcoded to 0x4000), so this
 *   module provides it.
 *
 * THE TWO DISPLAY FILES (standard 256x192 mode)
 *   Screen A : bitmap 0x4000 (6144 B) + attrs 0x5800 (768 B)   <- shown when page=0
 *   Screen B : bitmap 0x6000 (6144 B) + attrs 0x7800 (768 B)   <- shown when page=1
 *   The back buffer (0x6000-0x7AFF) sits inside the normal program load area,
 *   so your program MUST keep code/data out of it (ORG 0x8000 does this; the
 *   default z88dk +zx ORG is already 0x8000).
 *
 * THE DISPLAY REGISTER  (port 0xFF)  -- two legal bytes ONLY:
 *   OUT (0xFF), 0x00  -> show screen A      OUT (0xFF), 0x01  -> show screen B
 *   Bits 6-7 MUST stay 0. SCLD bit 6 is a hardware interrupt kill-switch that
 *   software EI cannot override -- setting it freezes any HALT-paced loop.
 *
 * INTERRUPTS
 *   The z88dk newlib crt boots with interrupts DISABLED, so a HALT would never
 *   wake. scld_init() runs `im 1; ei` for you; pace your loop with scld_present()
 *   (which HALTs on the 50 Hz frame interrupt).
 *
 * TYPICAL USE
 *   scld_init(0x07);                       // white-on-black; both buffers cleared
 *   for (;;) {
 *       uint16_t back = scld_back();       // address of the hidden buffer
 *       // ... draw this frame into `back` (e.g. via scld_scanline) ...
 *       scld_present();                    // HALT to 50 Hz, then flip to reveal it
 *   }
 *
 * COMPATIBILITY
 *   TC2048 / TC2068 / TS2068 (all have the SCLD). A stock ZX Spectrum 48K has
 *   no second display file: scld_present() degrades to a plain HALT and only
 *   screen A is ever visible -- single-buffered, but it still runs.
 */

#ifndef SCLD_DOUBLE_BUFFER_H
#define SCLD_DOUBLE_BUFFER_H

#include <stdint.h>

/* --- Screen geometry & memory map (standard-resolution double buffer) ----- */
#define SCLD_SCREEN_A   0x4000u   /* screen A bitmap base (page 0)            */
#define SCLD_SCREEN_B   0x6000u   /* screen B bitmap base (page 1)            */
#define SCLD_ATTRS_A    0x5800u   /* screen A attributes                      */
#define SCLD_ATTRS_B    0x7800u   /* screen B attributes                      */
#define SCLD_BITMAP_LEN 6144u     /* bytes in one bitmap                      */
#define SCLD_ATTRS_LEN  768u      /* bytes in one attribute block            */
#define SCLD_W          256u      /* visible width  (pixels)                 */
#define SCLD_H          192u      /* visible height (scanlines)              */
#define SCLD_ROW_BYTES  32u       /* bytes per pixel scanline (256/8)        */

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

/* ---------------------------------------------------------------------------
 * scld_row_off[y] -- precomputed interleaved byte offset of pixel scanline y
 * from a buffer base. Filled once by scld_init.
 *
 * THE fast path for blitters: the address of scanline y in buffer `base` is
 * just  base + scld_row_off[y]  -- a single table lookup, correct for ANY y,
 * avoiding both the per-row recompute (scld_scanline) and the carry-stepping
 * (scld_next_scanline), which are far more expensive in C. 384 bytes of RAM
 * that turn a ~13k-T/sprite blit into a ~2k-T/sprite one. (Same idea as the
 * famous Spectrum line-address tables.)
 * ------------------------------------------------------------------------- */
extern uint16_t scld_row_off[SCLD_H];

/* ---------------------------------------------------------------------------
 * Hardware functions (Timex target only; implemented in scld.c).
 * ------------------------------------------------------------------------- */

/* Initialise double-buffering. Enables IM1 + interrupts (for HALT pacing),
 * fills BOTH attribute blocks with `attr`, clears BOTH bitmaps to black, and
 * displays screen A. Call once before the game loop. */
extern void     scld_init(uint8_t attr);

/* Address of the BACK buffer -- the hidden display file you draw into now. */
extern uint16_t scld_back(void);

/* Page index of the back buffer: 0 = screen A, 1 = screen B. Handy for keeping
 * per-buffer state (e.g. last-drawn positions for incremental erase). */
extern uint8_t  scld_back_page(void);

/* Wait for the next 50 Hz frame interrupt (HALT), then page-flip so the buffer
 * you just drew becomes visible. One call per frame, after drawing. */
extern void     scld_present(void);

/* Wait one frame (HALT) WITHOUT flipping -- for effects that animate on the
 * currently-shown buffer (e.g. a death explosion painted into attributes). */
extern void     scld_wait(void);

/* Full clear of one 6144-byte bitmap (`base`) to black. Cheap enough for the
 * one-time clears in scld_init; for per-frame use prefer incremental erase
 * (clear only what moved). The designated hand-asm optimisation target. */
extern void     scld_clear(uint16_t base);

/* ---------------------------------------------------------------------------
 * Timex 8x1 HI-COLOUR mode (per-scanline attributes). OUT (0xFF), 0x02 keeps
 * the bitmap at 0x4000 but takes attributes from an 8x1 map at 0x6000 (one byte
 * per char-column per scanline, addressed with scld_scanline(0x6000, y)[x]).
 * This consumes the second screen file, so it CANNOT coexist with the page-flip
 * double buffer -- use it only on a single-buffered screen (e.g. game-over).
 * scld_hicolor_off() restores the standard double-buffer display.
 * Only bit 1 is set; bits 6-7 stay 0 (never the interrupt kill-switch).
 * ------------------------------------------------------------------------- */
extern void     scld_hicolor_on(void);
extern void     scld_hicolor_off(void);

#endif /* SCLD_DOUBLE_BUFFER_H */
