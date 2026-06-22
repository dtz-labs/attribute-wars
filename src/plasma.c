/*
 * plasma.c -- see plasma.h. Integer-only plasma field + palette.
 */
#include "plasma.h"

s16 plasma_field(u8 x, u8 y, u8 phase)
{
    u8 ph2 = (u8)(phase + 64u);   /* quarter-turn offset for the diagonal term */
    return (s16)fx_sin[(u8)(x * PLA_KX + phase)]
         + (s16)fx_sin[(u8)(y * PLA_KY + phase)]
         + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD + ph2)];
}

u8 plasma_palette(s16 v)
{
    /* v in ~[-381,381] -> 0..11 band -> a non-white paper colour, ink white.
     * Bright on the upper bands for vividness. Papers chosen from {1..6} (blue,
     * red, magenta, green, cyan, yellow) -- never 7 (white) so text reads. */
    static const u8 paper[12] = { 1u, 2u, 3u, 4u, 5u, 6u, 5u, 4u, 3u, 2u, 1u, 6u };
    u16 band = (u16)((v + 384) >> 6);          /* 0..11 */
    if (band > 11u) band = 11u;
    return ATTR((band >= 6u) ? 1u : 0u, paper[band], 7u);
}
