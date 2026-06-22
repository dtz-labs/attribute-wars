/*
 * arena.h -- the walled play area (replaces toroidal wrap with solid walls).
 *
 * The colour background frames the field with a 1-cell (8px) border ring, so the
 * play area is attribute cells (1..30, 1..22) = pixels (8..247, 8..183). An 8x8
 * sprite's top-left must stay in [ARENA_L..ARENA_R] x [ARENA_T..ARENA_B] to keep
 * the whole sprite inside that ring. The player clamps to these bounds; level-0
 * enemies bounce off them.
 */
#ifndef ARENA_H
#define ARENA_H

#define ARENA_L 8u      /* min sprite x */
#define ARENA_T 8u      /* min sprite y */
#define ARENA_R 240u    /* max sprite x (x+7 = 247, last interior column) */
#define ARENA_B 176u    /* max sprite y (y+7 = 183, last interior row)    */

#endif /* ARENA_H */
