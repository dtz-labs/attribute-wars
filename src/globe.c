/*
 * globe.c -- see globe.h. A wireframe globe: NMER meridian arcs (vertical) and
 * NPAR parallel rings (horizontal), projected with a fixed-point Y-axis
 * rotation. cos(a) = fx_sin[(a+64)&255]. All dots identical (the caller draws
 * them white).
 */
#include "globe.h"
#include "fxtab.h"

#define NMER 8u     /* meridian lines (longitudes)                    */
#define MARC 7u     /* dots per meridian                              */
#define MARC_LO  (-72)  /* meridian latitude range -72..72            */
#define MARC_STEP 24    /* 144 / (MARC-1) deg between meridian dots    */
#define NPAR 5u     /* parallel rings (latitudes)                     */
#define PARC 10u    /* dots per parallel                              */
#define PAR_LO  (-60)   /* parallel latitudes -60..60 (skip the poles)*/
#define PAR_STEP 30     /* 120 / (NPAR-1) deg between parallels        */
#define NPTS (NMER * MARC + NPAR * PARC)   /* 56 + 50 = 106 */

static u8 g_cx, g_cy;
static u8 g_rpix[NPTS];   /* xz-plane radius in pixels (0..r)   */
static u8 g_sy[NPTS];     /* screen y (constant per point)      */
static u8 g_lon[NPTS];    /* base longitude index (0..255)      */

/* latitude angle (deg, -90..90) -> 0..255 phase index. */
static u8 lat_idx(s16 deg)
{
    return (u8)((s16)(deg * 256) / 360);
}

void globe_init(u8 cx, u8 cy, u8 r)
{
    u8 m, k, p, j;
    u8 i = 0u;

    g_cx = cx;
    g_cy = cy;

    /* meridian arcs: NMER longitudes, each a dense -78..78 latitude line */
    for (m = 0; m < NMER; m++) {
        u8 lon = (u8)(m * (256u / NMER));
        for (k = 0; k < MARC; k++) {
            s16 deg    = (s16)(MARC_LO + MARC_STEP * (s16)k);
            u8  a      = lat_idx(deg);
            s8  sinphi = fx_sin[a];
            s8  cosphi = fx_sin[(u8)(a + 64u)];
            g_rpix[i] = (u8)fx_mul(cosphi, r);
            g_sy[i]   = (u8)((s16)cy - fx_mul(sinphi, r));
            g_lon[i]  = lon;
            i++;
        }
    }

    /* parallel rings: NPAR latitudes, each a dense ring of PARC longitudes */
    for (p = 0; p < NPAR; p++) {
        s16 deg    = (s16)(PAR_LO + PAR_STEP * (s16)p);
        u8  a      = lat_idx(deg);
        s8  sinphi = fx_sin[a];
        s8  cosphi = fx_sin[(u8)(a + 64u)];
        u8  rp     = (u8)fx_mul(cosphi, r);
        u8  sy     = (u8)((s16)cy - fx_mul(sinphi, r));
        for (j = 0; j < PARC; j++) {
            g_rpix[i] = rp;
            g_sy[i]   = sy;
            g_lon[i]  = (u8)(j * (256u / PARC));
            i++;
        }
    }
}

u8 globe_count(void) { return NPTS; }

u8 globe_x(u8 i, u8 theta)
{
    u8 a    = (u8)(g_lon[i] + theta);
    s8 cosA = fx_sin[(u8)(a + 64u)];
    return (u8)((s16)g_cx + fx_mul(cosA, g_rpix[i]));
}

u8 globe_y(u8 i) { return g_sy[i]; }

u8 globe_front(u8 i, u8 theta)
{
    u8 a = (u8)(g_lon[i] + theta);
    return (fx_sin[a] >= 0) ? 1u : 0u;
}
