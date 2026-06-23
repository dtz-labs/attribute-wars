/*
 * scld.c -- platform screen-buffer backend (see scld.h).
 *
 * The ONLY translation unit that knows the platform flip hardware and the
 * screen addresses (design boundary rule). Everything above this draws into a
 * buffer-base it is handed and never touches the hardware directly.
 *
 * Target-only: pulls in z88dk's <z80.h> / <intrinsic.h>. The pure address math
 * (scld_scanline) lives in scld.h so it stays host-testable without this file.
 */

#include "scld.h"

#include <string.h>      /* memset                                          */
#include <z80.h>         /* z80_outp() -- OUT (port), byte                   */
#include <intrinsic.h>   /* intrinsic_im_1 / intrinsic_ei / intrinsic_halt  */

#define SCLD_PORT   0xFFu     /* SCLD display-mode register                  */
#define SCLD_PAGE_A 0x00u     /* show screen A (bits 6-7 = 0)                */
#define SCLD_PAGE_B 0x01u     /* show screen B (bits 6-7 = 0)                */

#ifdef ZX128_PAGE_FLIP
extern void zx128_page_show_a(void);
extern void zx128_page_show_b(void);
#endif

/* Currently displayed page: 0 = screen A, 1 = screen B. */
static uint8_t scld_front;

#ifndef ZX128_PAGE_FLIP
/* Precomputed scanline offsets (see scld.h); filled by scld_init. */
uint16_t scld_row_off[SCLD_H];
#endif

void scld_clear(uint16_t base)
{
    /* memset is ~21 T/byte (~129k T for a full 6144-byte buffer = ~1.85 frames
     * @ 50 Hz) -- fine for the one-shot init clears, too slow to do every frame.
     * Per-frame rendering should erase only what moved; a stack-PUSH asm fill
     * (~5.5 T/byte) is the optimisation if a full per-frame clear is ever
     * actually needed. */
    memset((uint8_t *)base, 0x00, SCLD_BITMAP_LEN);
}

void scld_init(uint8_t attr)
{
    uint8_t y;

    /* CRITICAL: the newlib crt boots with interrupts DISABLED. Arm IM 1 and
     * enable interrupts BEFORE any HALT, or scld_present() would deadlock. */
    intrinsic_im_1();
    intrinsic_ei();

    /* Build the scanline-offset table once (base 0 -> the offset itself). */
#ifndef ZX128_PAGE_FLIP
    for (y = 0; y < SCLD_H; y++) {
        scld_row_off[y] = (uint16_t)(uintptr_t)scld_scanline(0, y);
    }
#else
    (void)y;
#endif

    /* Attributes set once on both screens; never touched per frame -> no colour
     * clash work and a clean look. In the ZX48 build screen B aliases screen A,
     * so these duplicate writes are intentional and harmless. */
#ifdef ZX128_PAGE_FLIP
    zx128_page_show_a();                /* page 7 at 0xC000, show page 5 */
#endif
    memset((uint8_t *)SCLD_ATTRS_A, attr, SCLD_ATTRS_LEN);
    memset((uint8_t *)SCLD_ATTRS_B, attr, SCLD_ATTRS_LEN);

    scld_clear(SCLD_SCREEN_A);
    scld_clear(SCLD_SCREEN_B);

    scld_front = 0;
#if !defined(ZX48_SINGLE_BUFFER) && !defined(ZX128_PAGE_FLIP)
    z80_outp(SCLD_PORT, SCLD_PAGE_A);   /* display screen A */
#endif
}

uint16_t scld_back(void)
{
#if defined(ZX48_SINGLE_BUFFER)
    return SCLD_SCREEN_A;
#elif defined(ZX128_PAGE_FLIP)
    return scld_front ? SCLD_SCREEN_A : SCLD_SCREEN_B;
#else
    /* Draw into whichever file is NOT on screen. */
    return scld_front ? SCLD_SCREEN_A : SCLD_SCREEN_B;
#endif
}

uint8_t scld_back_page(void)
{
#ifdef ZX48_SINGLE_BUFFER
    return 0u;
#else
    return (uint8_t)(scld_front ^ 1u);
#endif
}

/* Bases of the CURRENTLY-DISPLAYED buffer. For frozen effects that do not
 * page-flip (e.g. the death explosion), drawing into only the shown buffer is
 * half the memory traffic of touching both -- the hidden one is never seen. */
uint16_t scld_shown(void)
{
#ifdef ZX48_SINGLE_BUFFER
    return SCLD_SCREEN_A;
#else
    return scld_front ? SCLD_SCREEN_B : SCLD_SCREEN_A;
#endif
}

uint16_t scld_shown_attrs(void)
{
#ifdef ZX48_SINGLE_BUFFER
    return SCLD_ATTRS_A;
#else
    return scld_front ? SCLD_ATTRS_B : SCLD_ATTRS_A;
#endif
}

void scld_show_a(void)
{
    scld_front = 0;
#ifdef ZX128_PAGE_FLIP
    zx128_page_show_a();
#elif !defined(ZX48_SINGLE_BUFFER)
    z80_outp(SCLD_PORT, SCLD_PAGE_A);
#endif
}

void scld_present(void)
{
    intrinsic_halt();                   /* sync to the 50 Hz frame interrupt   */
#ifdef ZX128_PAGE_FLIP
    scld_front ^= 1u;
    if (scld_front) zx128_page_show_b();
    else            zx128_page_show_a();
#elif !defined(ZX48_SINGLE_BUFFER)
    scld_front ^= 1u;                   /* the buffer we just drew is now front */
    /* Only ever 0x00 / 0x01 -- bits 6-7 must stay 0 (see scld.h). The flip
     * happens during vblank (right after HALT) so it is tear-free. */
    z80_outp(SCLD_PORT, scld_front);
#endif
}

void scld_wait(void)
{
    intrinsic_halt();                   /* one frame, no flip */
}
