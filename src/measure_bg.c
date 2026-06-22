/*
 * measure_bg.c -- THROWAWAY T-state spike (NOT shipped): cost of painting the
 * background attribute plane. Answers "how many T-states is one full-screen
 * attribute write?" for three realistic methods. Same marker/z88dk-ticks
 * pattern as measure_main.c.
 *
 *   seg A = memset ONE attr block (768 B, constant)      -> floor cost
 *   seg B = memcpy a RAM table -> BOTH blocks (1536 B)    -> "generate once, copy both"
 *   seg C = compute checker per-cell -> BOTH blocks       -> naive bg_paint() in C
 */
#include "scld.h"
#include "types.h"
#include <z80.h>
#include <string.h>

#define ATTR(bright, paper, ink) ((u8)(((bright) << 6) | ((paper) << 3) | (ink)))
#define ITERS 100u

static u8 cells[768];

void mark0(void) { z80_outp(0xFEu, 1u); }   /* after setup        */
void markA(void) { z80_outp(0xFEu, 2u); }   /* + memset x N       */
void markB(void) { z80_outp(0xFEu, 3u); }   /* + memcpy both x N  */
void markC(void) { z80_outp(0xFEu, 4u); }   /* + compute both x N */

int main(void)
{
    u8  k, row, col;
    u16 i, j;

    for (i = 0; i < 768u; i++) {                 /* build a checker source table */
        row = (u8)(i >> 5);
        col = (u8)(i & 31u);
        cells[i] = (u8)(((row + col) & 1u) ? ATTR(0, 1, 7) : ATTR(0, 0, 7));
    }

    mark0();
    for (k = 0; k < ITERS; k++) {                /* (A) memset ONE block */
        memset((u8 *)SCLD_ATTRS_A, 0x07u, 768u);
    }
    markA();
    for (k = 0; k < ITERS; k++) {                /* (B) memcpy table -> BOTH */
        memcpy((u8 *)SCLD_ATTRS_A, cells, 768u);
        memcpy((u8 *)SCLD_ATTRS_B, cells, 768u);
    }
    markB();
    for (k = 0; k < ITERS; k++) {                /* (C) compute checker -> BOTH */
        u8 *a = (u8 *)SCLD_ATTRS_A, *b = (u8 *)SCLD_ATTRS_B;
        j = 0;
        for (row = 0; row < 24u; row++) {
            for (col = 0; col < 32u; col++, j++) {
                u8 v = (u8)(((row + col) & 1u) ? ATTR(0, 1, 7) : ATTR(0, 0, 7));
                a[j] = v;
                b[j] = v;
            }
        }
    }
    markC();

    z80_outp(0xFEu, 0u);
    for (;;) { }
}
