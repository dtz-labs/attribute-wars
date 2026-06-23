/*
 * globe.h -- rotating 3D dot-sphere for the title screen. Pure logic,
 * host-testable: fixed-point Y-axis rotation via fxtab. Each point's screen-y is
 * constant (Y-rotation only slides it horizontally), so per frame a point costs
 * one cos lookup + one fx_mul.
 */
#ifndef GLOBE_H
#define GLOBE_H

#include "types.h"

#define GLOBE_CX 128u    /* centre x */
#define GLOBE_CY 60u     /* centre y */
#define GLOBE_R  36u     /* radius (pixels) */

/* Build the point tables (uses fx_sin). Call once before drawing. */
void globe_init(void);

/* Number of surface points. */
u8 globe_count(void);

/* Screen x of point i at rotation theta (0..255). In [CX-R, CX+R]. */
u8 globe_x(u8 i, u8 theta);

/* Screen y of point i (constant -- Y-axis spin). In [CY-R, CY+R]. */
u8 globe_y(u8 i);

/* 1 if point i faces the viewer at theta (front hemisphere), else 0. */
u8 globe_front(u8 i, u8 theta);

#endif /* GLOBE_H */
