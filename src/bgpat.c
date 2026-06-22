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

/* Cheap deterministic hash -> 0..255, for the static starfield. */
static u8 hash3(u8 r, u8 c, u16 seed)
{
    u16 h = (u16)((u16)(r * 61u) + (u16)(c * 113u) + seed);
    h ^= (u16)(h << 7);
    h ^= (u16)(h >> 9);
    h = (u16)(h * 0x2545u);
    return (u8)(h >> 8);
}

/* Attribute (ink=white) for one INTERIOR cell of pattern `id`. */
static u8 interior_attr(u8 id, u8 r, u8 c, u16 seed)
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
    case BGPAT_DIAMONDS: {
        u8 dm = (u8)(ABS8(dr) + ABS8(dc));
        u8 band = (u8)((dm >> 1) & 1u);
        u8 accent = (u8)(((dm >> 1) % 4u) == 0u);
        return ATTR(0, accent ? 3u : (band ? 1u : 0u), 7);   /* magenta accent rings */
    }
    case BGPAT_VBANDS: {
        u8 k = (u8)((c / 2u) % 3u);
        u8 p = (k == 0u) ? 0u : (k == 1u) ? 1u : 3u;          /* black/blue/magenta cols */
        return ATTR(0, p, 7);
    }
    case BGPAT_PLASMA: {
        s16 v = (s16)fx_sin[(u8)((u8)(c * 10u) + (u8)seed)]
              + (s16)fx_sin[(u8)(r * 12u)]
              + (s16)fx_sin[(u8)((u8)((r + c) * 7u) + (u8)(seed >> 3))];
        u8 q = (u8)((v + 384) / 192);                          /* 0..3 */
        static const u8 pp[4] = { 0u, 1u, 3u, 2u };            /* black/blue/magenta/red */
        if (q > 3u) q = 3u;
        return ATTR(0, pp[q], 7);
    }
    case BGPAT_STARFIELD: {
        u8 h = hash3(r, c, seed);
        if (h < 8u)  return ATTR(1, 5u, 7);                    /* bright cyan star  */
        if (h < 16u) return ATTR(1, 1u, 7);                    /* bright blue star  */
        if (h < 22u) return ATTR(1, 3u, 7);                    /* bright magenta    */
        return ATTR(0, 0u, 7);                                 /* black space       */
    }
    default:
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
