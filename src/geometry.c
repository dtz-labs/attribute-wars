/*
 * geometry.c -- direction vectors and toroidal wrap arithmetic.
 * See geometry.h. Pure integer logic, shared by target and host-test builds.
 */
#include "geometry.h"

/* Indexed by DIR_N..DIR_NW (clockwise from North). y grows DOWN. */
const s8 dir_dx[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
const s8 dir_dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

u8 dir_from_steps(s8 sx, s8 sy)
{
    if (sx == 0 && sy == 0) {
        return DIR_NONE;
    }
    /* Small fixed lookup over the 8 directions; no trig, no branch ladder. */
    for (u8 d = 0; d < 8; ++d) {
        if (dir_dx[d] == sx && dir_dy[d] == sy) {
            return d;
        }
    }
    return DIR_NONE; /* unreachable for in-range inputs */
}

u8 wrap_x(s16 x)
{
    /* Width is 256, so a u8 cast performs the wrap for free. */
    return (u8)x;
}

u8 wrap_y(s16 y)
{
    /* Height 192 is not a power of two: wrap by adding/subtracting 192.
     * Velocities are small, so a couple of conditionals suffice (no modulo,
     * which is comparatively slow on the Z80). */
    while (y < 0) {
        y += (s16)SCREEN_H;
    }
    while (y >= (s16)SCREEN_H) {
        y -= (s16)SCREEN_H;
    }
    return (u8)y;
}
