/*
 * plasma.h -- game-over plasma (8x8 attributes). Pure logic, host-testable:
 * a STATIC interference field (sum of three sines from fxtab) whose colours are
 * CYCLED by a phase (palette rotation, so it shimmers in place -- no scrolling).
 * Dark papers only + white ink, so the GAME OVER text stays readable.
 */
#ifndef PLASMA_H
#define PLASMA_H

#include "types.h"
#include "fxtab.h"

#define PLA_KX 6u    /* x frequency (per cell column) */
#define PLA_KY 5u    /* y frequency (per cell row)    */
#define PLA_KD 4u    /* diagonal frequency            */

/* Static interference value at cell (x,y). Range approx [-381,381]. */
s16 plasma_field(u8 x, u8 y);

/* Map a field value to an attribute byte at animation `phase`: ink=white(7),
 * paper a DARK non-bright colour, the colour index rotated by `phase` so the
 * pattern shimmers without translating. */
u8 plasma_palette(s16 v, u8 phase);

#endif /* PLASMA_H */
