/*
 * globe.h -- rotating 3D dot-sphere for the title screen. Pure logic,
 * host-testable: fixed-point Y-axis rotation via fxtab. Each point's screen-y is
 * constant (Y-rotation only slides it horizontally), so per frame a point costs
 * one cos lookup + one fx_mul.
 *
 * Two kinds of points: MERIDIAN points (dense vertical N-S arcs -> drawn as blue
 * lines) and PARALLEL points (sparse latitude-ring dots -> white). The caller
 * colours meridian cells blue and leaves the rest white.
 */
#ifndef GLOBE_H
#define GLOBE_H

#include "types.h"

/* (Re)build the point tables for a globe centred at (cx,cy) with radius r.
 * Uses fx_sin. Call before drawing; cheap enough to call per title phase. */
void globe_init(u8 cx, u8 cy, u8 r);

/* Number of surface points. */
u8 globe_count(void);

/* Screen x of point i at rotation theta (0..255). In [cx-r, cx+r]. */
u8 globe_x(u8 i, u8 theta);

/* Screen y of point i (constant -- Y-axis spin). In [cy-r, cy+r]. */
u8 globe_y(u8 i);

/* 1 if point i faces the viewer at theta (front hemisphere), else 0. */
u8 globe_front(u8 i, u8 theta);

/* 1 if point i is on a meridian line (draw its cell blue), 0 if a parallel dot. */
u8 globe_is_meridian(u8 i);

#endif /* GLOBE_H */
