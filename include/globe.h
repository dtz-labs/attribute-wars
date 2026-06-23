/*
 * globe.h -- rotating 3D wireframe globe for the title screen. Pure logic,
 * host-testable: fixed-point Y-axis rotation via fxtab. Points are the dots of
 * a wireframe -- a set of meridian arcs (vertical) and parallel rings
 * (horizontal). Each point's screen-y is constant under Y-rotation, so per frame
 * a point costs one cos lookup + one fx_mul. All dots are identical (white).
 */
#ifndef GLOBE_H
#define GLOBE_H

#include "types.h"

/* (Re)build the wireframe for a globe centred at (cx,cy) with radius r. */
void globe_init(u8 cx, u8 cy, u8 r);

/* Number of wireframe dots. */
u8 globe_count(void);

/* Screen x of dot i at rotation theta (0..255). In [cx-r, cx+r]. */
u8 globe_x(u8 i, u8 theta);

/* Screen y of dot i (constant -- Y-axis spin). In [cy-r, cy+r]. */
u8 globe_y(u8 i);

/* 1 if dot i faces the viewer at theta (front hemisphere), else 0. */
u8 globe_front(u8 i, u8 theta);

#endif /* GLOBE_H */
