/*
 * bgpat.h -- generate a 32x24 attribute "background shape" into a RAM table.
 * Pure logic, host-testable. Interior keeps ink=white so white sprites stay
 * readable on any paper; the frame ring is the HUD colour.
 */
#ifndef BGPAT_H
#define BGPAT_H

#include "types.h"

#define BGPAT_COLS   32u
#define BGPAT_ROWS   24u
#define BGPAT_CELLS  768u                 /* COLS*ROWS */
#define BGPAT_FRAME_ATTR ATTR(1, 3, 7)    /* bright magenta paper, white ink */

enum {
    BGPAT_CHECKER = 0, BGPAT_DIAGONAL, BGPAT_CIRCLES, BGPAT_LATTICE, /* low-noise */
    BGPAT_DIAMONDS, BGPAT_VBANDS, BGPAT_PLASMA, BGPAT_STARFIELD,     /* noisy     */
    BGPAT_COUNT
};

#define BGPAT_LOWNOISE_FIRST 0u
#define BGPAT_LOWNOISE_COUNT 4u
#define BGPAT_NOISY_FIRST    4u
#define BGPAT_NOISY_COUNT    4u

/* Fill cells[BGPAT_CELLS]: frame ring = BGPAT_FRAME_ATTR; interior = pattern
 * `id` (ink=7, paper from the safe palette). `seed` varies plasma/starfield
 * detail. Deterministic for a given (id, seed). */
void bgpat_generate(u8 *cells, u8 id, u16 seed);

/* Pick a pattern id from a tier [first, first+count), avoiding `prev`. `rnd` is
 * a fresh random byte. Pass prev=0xFF to allow any. */
u8 bgpat_pick(u8 first, u8 count, u8 prev, u8 rnd);

#endif /* BGPAT_H */
