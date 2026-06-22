/*
 * bgpat.c -- see bgpat.h. Integer-only pattern generators. Interior cells use
 * ink=7 (white) so white sprites read on any paper.
 */
#include "bgpat.h"

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

/* Attribute (ink=white) for one INTERIOR cell of pattern `id`. All four shapes
 * use only black/blue paper (dark-blue mono look). */
static u8 interior_attr(u8 id, u8 r, u8 c)
{
    s8 dr = (s8)((s8)r - 12);     /* centre approx (row 12, col 16) */
    s8 dc = (s8)((s8)c - 16);
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
        return ATTR(0, ((r + c) & 1u) ? 1u : 0u, 7);
    }
}

void bgpat_generate(u8 *cells, u8 id)
{
    u8 r, c;
    u16 i = 0u;
    for (r = 0; r < BGPAT_ROWS; r++) {
        for (c = 0; c < BGPAT_COLS; c++, i++) {
            if (r == 0u || r == 23u || c == 0u || c == 31u) {
                cells[i] = (u8)BGPAT_FRAME_ATTR;
            } else {
                cells[i] = interior_attr(id, r, c);
            }
        }
    }
}

u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd)
{
    u8 id = (u8)(first + (rnd % count));
    if (count > 1u && id == prev) {
        id = (u8)(first + (u8)(((u8)(id - first) + 1u) % count));
    }
    return id;
}
