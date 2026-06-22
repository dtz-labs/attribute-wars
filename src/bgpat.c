/*
 * bgpat.c -- see bgpat.h. Integer-only pattern generators. Interior cells use
 * ink=7 (white) so white sprites read on any paper.
 */
#include "bgpat.h"
#include "fxtab.h"

#define ABS8(x) ((x) < 0 ? -(x) : (x))

/* Integer sqrt of a u16 (for concentric rings). */
static u8 isqrt_u16(u16 x)
{
    u16 res = 0u, bit = 1u << 14;
    while (bit > x) bit >>= 2;
    while (bit != 0u) {
        if (x >= res + bit) { x -= res + bit; res = (u16)((res >> 1) + bit); }
        else res >>= 1;
        bit >>= 2;
    }
    return (u8)res;
}

/* Attribute (ink=white) for one INTERIOR cell of pattern `id`. */
static u8 interior_attr(u8 id, u8 r, u8 c, u16 seed)
{
    s8 dr = (s8)((s8)r - 12);     /* centre approx (row 12, col 16) */
    s8 dc = (s8)((s8)c - 16);
    (void)seed;
    switch (id) {
    case BGPAT_CHECKER:
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
    case BGPAT_DIAGONAL:
        return ATTR(0, (((u8)(r + c) >> 1) & 1u) ? 1u : 0u, 7);
    case BGPAT_CIRCLES: {
        u8 ring = isqrt_u16((u16)(dr * dr + dc * dc));
        return ATTR(0, ((ring >> 1) & 1u) ? 1u : 0u, 7);
    }
    case BGPAT_LATTICE:
        return ATTR(0, ((r % 3u == 0u) || (c % 3u == 0u)) ? 1u : 0u, 7);
    default:
        /* noisy ids implemented in Task 4 */
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
    }
}

void bgpat_generate(u8 *cells, u8 id, u16 seed)
{
    u8 r, c;
    u16 i = 0u;
    for (r = 0; r < BGPAT_ROWS; r++) {
        for (c = 0; c < BGPAT_COLS; c++, i++) {
            if (r == 0u || r == 23u || c == 0u || c == 31u) {
                cells[i] = (u8)BGPAT_FRAME_ATTR;
            } else {
                cells[i] = interior_attr(id, r, c, seed);
            }
        }
    }
}
