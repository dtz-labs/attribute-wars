/*
 * scld.c -- Timex SCLD standard-resolution double-buffering (see scld.h).
 *
 * The ONLY translation unit that knows port 0xFF and the 0x4000/0x6000 screen
 * addresses (design boundary rule). Everything above this draws into a
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
#define SCLD_MODE_HICOLOR 0x02u  /* bit 1: 8x1 colour, bitmap 0x4000 + attrs 0x6000 */

/* Currently displayed page: 0 = screen A, 1 = screen B. */
static uint8_t scld_front;

/* Precomputed scanline offsets (see scld.h); filled by scld_init. */
uint16_t scld_row_off[SCLD_H];

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
    for (y = 0; y < SCLD_H; y++) {
        scld_row_off[y] = (uint16_t)(uintptr_t)scld_scanline(0, y);
    }

    /* Attributes set once on both screens; never touched per frame -> no colour
     * clash work and a clean look. */
    memset((uint8_t *)SCLD_ATTRS_A, attr, SCLD_ATTRS_LEN);
    memset((uint8_t *)SCLD_ATTRS_B, attr, SCLD_ATTRS_LEN);

    scld_clear(SCLD_SCREEN_A);
    scld_clear(SCLD_SCREEN_B);

    scld_front = 0;
    z80_outp(SCLD_PORT, SCLD_PAGE_A);   /* display screen A */
}

uint16_t scld_back(void)
{
    /* Draw into whichever file is NOT on screen. */
    return scld_front ? SCLD_SCREEN_A : SCLD_SCREEN_B;
}

uint8_t scld_back_page(void)
{
    return (uint8_t)(scld_front ^ 1u);
}

void scld_present(void)
{
    intrinsic_halt();                   /* sync to the 50 Hz frame interrupt   */
    scld_front ^= 1u;                   /* the buffer we just drew is now front */
    /* Only ever 0x00 / 0x01 -- bits 6-7 must stay 0 (see scld.h). The flip
     * happens during vblank (right after HALT) so it is tear-free. */
    z80_outp(SCLD_PORT, scld_front);
}

void scld_wait(void)
{
    intrinsic_halt();                   /* one frame, no flip */
}

void scld_hicolor_on(void)
{
    /* bitmap 0x4000 + 8x1 attribute map at 0x6000; bits 6-7 stay 0. */
    z80_outp(SCLD_PORT, SCLD_MODE_HICOLOR);
}

void scld_hicolor_off(void)
{
    /* back to standard mode showing the current double-buffer page. */
    z80_outp(SCLD_PORT, scld_front);
}
