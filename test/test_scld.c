/*
 * test_scld.c -- host unit test for the pure SCLD addressing math (scld.h).
 *
 * scld_scanline() is the interleaved screen-address calculation every blitter
 * relies on; a wrong offset means garbage on real hardware. These checks run
 * natively (no Z80, no emulator) for instant red/green. The hardware functions
 * (init/present/flip) are verified separately in Fuse.
 */

#include "scld.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void expect(const char *name, uint16_t got, uint16_t want)
{
    if (got != want) {
        printf("FAIL %-10s got 0x%04X want 0x%04X\n", name, got, want);
        failures++;
    }
}

/* address of scanline y in buffer base, as a 16-bit value */
static uint16_t addr(uint16_t base, uint8_t y)
{
    return (uint16_t)(uintptr_t)scld_scanline(base, y);
}

int main(void)
{
    /* Screen A: known interleaved scanline addresses. */
    expect("A y0",   addr(0x4000, 0),   0x4000);
    expect("A y1",   addr(0x4000, 1),   0x4100);  /* next scanline = +0x100   */
    expect("A y7",   addr(0x4000, 7),   0x4700);  /* last row of char 0       */
    expect("A y8",   addr(0x4000, 8),   0x4020);  /* next char row = +0x20    */
    expect("A y56",  addr(0x4000, 56),  0x40E0);  /* last char row of third 0 */
    expect("A y64",  addr(0x4000, 64),  0x4800);  /* third 1 starts           */
    expect("A y128", addr(0x4000, 128), 0x5000);  /* third 2 starts           */
    expect("A y191", addr(0x4000, 191), 0x57E0);  /* bottom-left byte         */

    /* Screen B is the same layout shifted by the 0x2000 base difference. */
    expect("B y0",   addr(0x6000, 0),   0x6000);
    expect("B y1",   addr(0x6000, 1),   0x6100);
    expect("B y191", addr(0x6000, 191), 0x77E0);

    /* scld_next_scanline must match a full recompute for every step 0..190 --
     * this is what the sprite blitter relies on to walk rows cheaply. */
    for (int y = 0; y < 191; y++) {
        uint16_t got  = scld_next_scanline(addr(0x4000, (uint8_t)y));
        uint16_t want = addr(0x4000, (uint8_t)(y + 1));
        if (got != want) {
            printf("FAIL next A y=%d -> 0x%04X want 0x%04X\n", y, got, want);
            failures++;
        }
    }
    for (int y = 0; y < 191; y++) {
        uint16_t got  = scld_next_scanline(addr(0x6000, (uint8_t)y));
        uint16_t want = addr(0x6000, (uint8_t)(y + 1));
        if (got != want) {
            printf("FAIL next B y=%d -> 0x%04X want 0x%04X\n", y, got, want);
            failures++;
        }
    }

    /* Every scanline of screen A must land inside its 6144-byte bitmap. */
    for (int y = 0; y < 192; y++) {
        uint16_t a = addr(0x4000, y);
        if (a < 0x4000 || a + SCLD_ROW_BYTES > 0x4000 + SCLD_BITMAP_LEN) {
            printf("FAIL range y=%d -> 0x%04X out of bitmap\n", y, a);
            failures++;
        }
    }

    if (failures == 0) {
        printf("test_scld: ALL PASS\n");
        return 0;
    }
    printf("test_scld: %d FAILURE(S)\n", failures);
    return 1;
}
