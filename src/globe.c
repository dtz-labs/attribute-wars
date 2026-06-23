/*
 * globe.c -- see globe.h. Meridian arcs (dense, N-S) + parallel rings (sparse
 * dots) on a unit sphere, projected with a fixed-point Y-axis rotation.
 * cos(a) = fx_sin[(a+64)&255].
 */
#include "globe.h"
#include "fxtab.h"

#define NMERID 8u    /* meridian lines (longitudes)                 */
#define MLAT   25u   /* latitude samples per meridian (dense arc)   */
#define NPAR   3u    /* parallel rings                              */
#define PLON   12u   /* longitude dots per parallel                 */
#define NPTS   (NMERID * MLAT + NPAR * PLON)   /* 200 + 36 = 236 */

static u8 g_cx, g_cy;
static u8 g_rpix[NPTS];   /* xz-plane radius in pixels (0..r)   */
static u8 g_sy[NPTS];     /* screen y (constant per point)      */
static u8 g_lon[NPTS];    /* base longitude index (0..255)      */
static u8 g_mer[NPTS];    /* 1 = meridian point, 0 = parallel   */

/* latitude angle (deg, -90..90) -> 0..255 phase index. */
static u8 lat_idx(s16 deg)
{
    return (u8)((s16)(deg * 256) / 360);
}

void globe_init(u8 cx, u8 cy, u8 r)
{
    u8  m, k, p, j;
    u8  i = 0u;
    static const s16 par_lat[NPAR] = { -45, 0, 45 };

    g_cx = cx;
    g_cy = cy;

    /* meridian arcs: NMERID longitudes, each a dense -80..80 latitude line */
    for (m = 0; m < NMERID; m++) {
        u8 lon = (u8)(m * (256u / NMERID));
        for (k = 0; k < MLAT; k++) {
            s16 deg    = (s16)(-80 + (160 * (s16)k) / (s16)(MLAT - 1u));
            u8  a      = lat_idx(deg);
            s8  sinphi = fx_sin[a];
            s8  cosphi = fx_sin[(u8)(a + 64u)];
            g_rpix[i] = (u8)fx_mul(cosphi, r);
            g_sy[i]   = (u8)((s16)cy - fx_mul(sinphi, r));
            g_lon[i]  = lon;
            g_mer[i]  = 1u;
            i++;
        }
    }

    /* parallel rings: NPAR latitudes, PLON longitude dots each */
    for (p = 0; p < NPAR; p++) {
        u8 a      = lat_idx(par_lat[p]);
        s8 sinphi = fx_sin[a];
        s8 cosphi = fx_sin[(u8)(a + 64u)];
        u8 rp     = (u8)fx_mul(cosphi, r);
        u8 sy     = (u8)((s16)cy - fx_mul(sinphi, r));
        for (j = 0; j < PLON; j++) {
            g_rpix[i] = rp;
            g_sy[i]   = sy;
            g_lon[i]  = (u8)(j * (256u / PLON));
            g_mer[i]  = 0u;
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

u8 globe_is_meridian(u8 i) { return g_mer[i]; }
