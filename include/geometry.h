/*
 * geometry.h -- direction vectors and toroidal (Pac-Man) wrap arithmetic.
 *
 * Pure integer logic, host-testable. No floating point (project constraint):
 * the 8 directions are unit steps of -1/0/+1, so movement is exact integer.
 */
#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "types.h"

/* Unit step for each DIR_* (index 0..7). dir_dx[DIR_NONE] is not defined. */
extern const s8 dir_dx[8];
extern const s8 dir_dy[8];

/*
 * Combine a +/-1 horizontal and +/-1 vertical step into a DIR_* value.
 * (sx, sy) each in {-1, 0, +1}; (0,0) -> DIR_NONE.
 */
u8 dir_from_steps(s8 sx, s8 sy);

/*
 * Toroidal wrap. x uses the natural u8 wrap (width 256). y wraps modulo 192.
 * The y input is signed/wide so callers can pass (pos +/- velocity) directly.
 */
u8 wrap_x(s16 x);
u8 wrap_y(s16 y);

#endif /* GEOMETRY_H */
