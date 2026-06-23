/*
 * globe.c -- see globe.h. A sparse lat/long grid on a unit sphere, projected
 * with a fixed-point Y-axis rotation. cos(a) = fx_sin[(a+64)&255]. Two opposite
 * longitudes are tagged blue so they read as faint blue meridians of dots.
 */
#include "globe.h"
#include "fxtab.h"

#define NLAT 17u   /* latitudes -80..80 (dense -> each meridian is a LINE) */
#define NLON 8u    /* few longitudes -> distinct meridian lines that sweep */
#define LAT_STEP 10   /* 160 / (NLAT-1) degrees between latitudes          */
#define NPTS (NLAT * NLON)   /* 136 */

static u8 g_cx, g_cy;
static u8 g_rpix[NPTS];   /* xz-plane radius in pixels (0..r)   */
static u8 g_sy[NPTS];     /* screen y (constant per point)      */
static u8 g_lon[NPTS];    /* base longitude index (0..255)      */
static u8 g_blue[NPTS];   /* 1 = blue dot, 0 = white            */

/* latitude angle (deg, -90..90) -> 0..255 phase index. */
static u8 lat_idx(s16 deg)
{
    return (u8)((s16)(deg * 256) / 360);
}

void globe_init(u8 cx, u8 cy, u8 r)
{
    u8 li, lo;
    u8 i = 0u;

    g_cx = cx;
    g_cy = cy;

    for (li = 0; li < NLAT; li++) {
        s16 deg    = (s16)(-80 + LAT_STEP * (s16)li);
        u8  a      = lat_idx(deg);
        s8  sinphi = fx_sin[a];
        s8  cosphi = fx_sin[(u8)(a + 64u)];
        u8  rp     = (u8)fx_mul(cosphi, r);
        u8  sy     = (u8)((s16)cy - fx_mul(sinphi, r));
        for (lo = 0; lo < NLON; lo++, i++) {
            u8 h = (u8)(i * 37u + 11u);      /* cheap pseudo-random hash */
            g_rpix[i] = rp;
            g_sy[i]   = sy;
            g_lon[i]  = (u8)(lo * (256u / NLON));
            /* ~20% of dots scattered blue (the rest white) */
            g_blue[i] = (h < 51u) ? 1u : 0u;
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

u8 globe_is_blue(u8 i) { return g_blue[i]; }
