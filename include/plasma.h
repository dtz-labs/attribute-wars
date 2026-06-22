/*
 * plasma.h -- game-over plasma field. Pure logic, host-testable: a sum of three
 * sines (fxtab) mapped to a non-white-paper rainbow attribute (ink stays white
 * so text reads). The renderer (main.c) computes the same field separably.
 */
#ifndef PLASMA_H
#define PLASMA_H

#include "types.h"
#include "fxtab.h"

#define PLA_KX 6u    /* x frequency */
#define PLA_KY 5u    /* y frequency */
#define PLA_KD 4u    /* diagonal frequency */

/* Sum of three sines at (x,y) with animation phase. Range approx [-381,381]. */
s16 plasma_field(u8 x, u8 y, u8 phase);

/* Map a field value to an attribute byte: ink=white(7), paper a cycling
 * non-white colour. */
u8 plasma_palette(s16 v);

#endif /* PLASMA_H */
