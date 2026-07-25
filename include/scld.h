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
#define SCLD_ATTRS_A    0x5800u   /* screen A attributes                      */

#ifdef ZX128_PAGE_FLIP
/* ZX Spectrum 128K build: normal screen is RAM page 5 at 0x4000; shadow screen
 * is RAM page 7, kept banked into the 0xC000 window. */
#define SCLD_SCREEN_B   0xC000u
#define SCLD_ATTRS_B    0xD800u
#elif defined(ZX48_SINGLE_BUFFER)
/* ZX Spectrum 48K build: no SCLD, no shadow screen. Alias page B to page A so
 * existing "draw both pages" cold paths remain correct and harmless. */
#define SCLD_SCREEN_B   SCLD_SCREEN_A
#define SCLD_ATTRS_B    SCLD_ATTRS_A
#else
#define SCLD_SCREEN_B   0x6000u   /* screen B bitmap base (page 1)            */
#define SCLD_ATTRS_B    0x7800u   /* screen B attributes                      */
#endif

#define SCLD_BITMAP_LEN 6144u     /* bytes in one bitmap                      */
#define SCLD_ATTRS_LEN  768u      /* bytes in one attribute block            */
#define SCLD_W          256u      /* visible width  (pixels)                 */
#define SCLD_H          192u      /* visible height (scanlines)              */
#define SCLD_ROW_BYTES  32u       /* bytes per pixel scanline (256/8)        */

/* ---------------------------------------------------------------------------
 * scld_scanline() / scld_next_scanline() -- the pure geometry helpers -- now
 * live in "scld_geom.h". They are `static inline`, so sdcc plants a dead
 * out-of-line copy in every unit that sees them; including them only where
 * they are used keeps that cost off the modules that just need the addresses
 * and the backend calls below.
 * ------------------------------------------------------------------------- */

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
#ifndef ZX128_PAGE_FLIP
extern uint16_t scld_row_off[SCLD_H];
#endif

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

/* Bitmap / attribute base of the buffer CURRENTLY ON SCREEN. For frozen effects
 * that don't page-flip (death explosion): draw into only this one (half the
 * writes of touching both buffers; the hidden one is never displayed). */
extern uint16_t scld_shown(void);
extern uint16_t scld_shown_attrs(void);

/* Force screen A visible. Used by static single-page screens such as the title
 * menu, which draw only into screen A. */
extern void     scld_show_a(void);

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

#endif /* SCLD_DOUBLE_BUFFER_H */
