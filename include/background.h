/*
 * background.h -- the generated arena floor, its frame, and the ULA border.
 *
 * The background is pure attribute work: a bright-magenta frame around the
 * screen (rows 0/23, cols 0/31) over a black/dark-blue floor whose pattern is
 * picked per run. It is painted once into BOTH attribute blocks so the
 * page-flip never disturbs colour.
 *
 * bg_attr() is also the RESTORE oracle: every path that temporarily recolours a
 * cell (explosion pops, the death fireball, the spawn telegraph) asks it what
 * the cell should look like again, so effects never punch holes in the floor.
 *
 * The ULA border lives here too -- it is screen dressing on the same frame
 * clock, and border_tick() must be called once per frame from the game loop.
 */
#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "types.h"

/* Floor patterns, cycled per run by bg_next_pattern(). */
#define BG_CHECKER  0u
#define BG_DIAGONAL 1u
#define BG_GRID     2u
#define BG_PATTERN_N 3u

/* ULA border colours. Port 0xFE, written only by border_tick()/death effects. */
#define BORDER_BLACK 0u
#define BORDER_RED   2u

/* Attribute the cell at (row,col) should have when nothing is drawn over it. */
u8 bg_attr(u8 row, u8 col);

/* Paint the whole arena into both attribute blocks. */
void bg_paint(void);

/* Advance to the next floor pattern (called once per new game). */
void bg_next_pattern(void);

/* Flash the border red for a few frames (enemy hit, player hit, kill). */
void border_flash_red(void);

/* Clear a pending border flash. Call when (re)starting a run. */
void border_reset(void);

/* Drive the border for one frame. Call once per game-loop iteration. */
void border_tick(void);

#endif /* BACKGROUND_H */
