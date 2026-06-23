/*
 * globe.c -- see globe.h. A lat/long grid of points on a unit sphere, projected
 * with a fixed-point Y-axis rotation. cos(a) = fx_sin[(a+64)&255].
 */
#include "globe.h"
#include "fxtab.h"

#define NLAT 7u
#define NLON 16u
#define NPTS (NLAT * NLON)   /* 112 */

/* Latitude angles -72..72 as 0..255 phase indices (angle*256/360), stored u8. */
static const u8 lat_idx[NLAT] = {
    205u, 222u, 239u, 0u, 17u, 34u, 51u   /* -72,-48,-24,0,24,48,72 deg */
};

static u8 g_rpix[NPTS];   /* xz-plane radius in pixels (0..R)      */
static u8 g_sy[NPTS];     /* screen y (constant per point)         */
static u8 g_lon[NPTS];    /* base longitude index (0..255)         */

void globe_init(void)
{
    u8 li, lo;
    u8 i = 0u;
    for (li = 0; li < NLAT; li++) {
        u8  a       = lat_idx[li];
        s8  sinphi  = fx_sin[a];
        s8  cosphi  = fx_sin[(u8)(a + 64u)];          /* >= 0 for |lat|<90 */
        u8  rpix    = (u8)fx_mul(cosphi, GLOBE_R);    /* 0..R              */
        s16 yoff    = fx_mul(sinphi, GLOBE_R);        /* -R..R            */
        u8  sy      = (u8)((s16)GLOBE_CY - yoff);
        for (lo = 0; lo < NLON; lo++, i++) {
            g_rpix[i] = rpix;
            g_sy[i]   = sy;
            g_lon[i]  = (u8)(lo * (256u / NLON));     /* step 16 */
        }
    }
}

u8 globe_count(void) { return NPTS; }

u8 globe_x(u8 i, u8 theta)
{
    u8 a    = (u8)(g_lon[i] + theta);
    s8 cosA = fx_sin[(u8)(a + 64u)];
    return (u8)((s16)GLOBE_CX + fx_mul(cosA, g_rpix[i]));
}

u8 globe_y(u8 i) { return g_sy[i]; }

u8 globe_front(u8 i, u8 theta)
{
    u8 a = (u8)(g_lon[i] + theta);
    return (fx_sin[a] >= 0) ? 1u : 0u;    /* sin(lon+theta) >= 0 -> front */
}
