/*
 * globe.h -- rotating 3D dot-sphere for the title screen. Pure logic,
 * host-testable: fixed-point Y-axis rotation via fxtab. Each point's screen-y is
 * constant (Y-rotation only slides it horizontally), so per frame a point costs
 * one cos lookup + one fx_mul.
 *
 * A sparse lat/long grid of white dots; a couple of longitudes are tagged "blue"
 * so they trace two faint blue meridians of dots (the rest stay white). Colour
 * is per 8x8 cell, so each dot's cell is set blue/white accordingly.
 */
#ifndef GLOBE_H
#define GLOBE_H

#include "types.h"

#define GLOBE_MAXPTS 256u   /* upper bound on globe_count(), for caller arrays */

/* (Re)build the point grid for a globe centred at (cx,cy) with radius r. */
void globe_init(u8 cx, u8 cy, u8 r);

/* Number of surface points (<= GLOBE_MAXPTS). */
u8 globe_count(void);

/* Screen x of point i at rotation theta (0..255). In [cx-r, cx+r]. */
u8 globe_x(u8 i, u8 theta);

/* Screen y of point i (constant -- Y-axis spin). In [cy-r, cy+r]. */
u8 globe_y(u8 i);

/* 1 if point i faces the viewer at theta (front hemisphere), else 0. */
u8 globe_front(u8 i, u8 theta);

/* 1 if point i is a blue dot (on a tagged meridian), 0 if a white dot. */
u8 globe_is_blue(u8 i);

#endif /* GLOBE_H */
