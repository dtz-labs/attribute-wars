/*
 * plasma.c -- see plasma.h. Integer-only plasma field + palette.
 */
#include "plasma.h"

s16 plasma_field(u8 x, u8 y)
{
    return (s16)fx_sin[(u8)(x * PLA_KX)]
         + (s16)fx_sin[(u8)(y * PLA_KY)]
         + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD)];
}

u8 plasma_palette(s16 v, u8 phase)
{
    /* Static spatial band (0..11) + a phase-rotated colour index, mapped to a
     * cycle of DARK non-bright papers (never bright, never white) so white text
     * always reads. phase>>1 slows the cycle to a smooth shimmer. */
    static const u8 paper[8] = { 0u, 1u, 3u, 2u, 4u, 2u, 3u, 1u };  /* black/blue/magenta/red/green */
    u8 band = (u8)((u8)((v + 384) >> 6) + (u8)(phase >> 1));
    return ATTR(0u, paper[band & 7u], 7u);     /* bright=0 (dark), ink=white */
}
