/*
 * player.h -- player ship state and movement (pure logic, host-tested).
 *
 * The ship has a little INERTIA: velocity ramps up toward the input direction
 * and coasts back down when input stops (it keeps drifting a few frames). Done
 * in fixed point (1/16 px) -- no floating point. Drawn as an 8x8 directional
 * sprite that turns to face the way it is flying.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"
#include "controls.h"   /* intent_t */

/* Fixed-point at 1/32 px (SUB=5) instead of 1/16 -- the finer grid lets us tune
 * the coast in smaller steps. Top speed and ramp-up time are unchanged from the
 * 1/16 tuning (MAXV/ACCEL scaled x2); only FRICTION is set for a LONGER drift:
 * at 3/32 px/frame the ship coasts ~21 px (~0.4 s) vs ~16 px before. */
#define PLAYER_SUB      5            /* fixed-point fraction bits (1/32 px)      */
#define PLAYER_MAXV     64           /* top speed: 64/32 = 2 px/frame            */
#define PLAYER_ACCEL    12           /* ramp-up per frame while a key is held    */
#define PLAYER_FRICTION 3            /* coast-down per frame when released;
                                      * lower = LONGER drift (applies to both x,y)*/

typedef struct {
    u8  x, y;           /* pixel position (integer; for render/collision)  */
    s16 px, py;         /* fixed-point position (pixel << PLAYER_SUB)       */
    s16 vx, vy;         /* velocity, fixed-point per frame                  */
    u8  facing;         /* DIR_* the ship points at (DIR_NONE at start)     */
} player_t;

void player_init(player_t *p, u8 x, u8 y);

/* Advance one frame from an input intent: ease velocity toward the input,
 * integrate position (clamped to the arena), face the flight direction. */
void player_update(player_t *p, const intent_t *in);

#endif /* PLAYER_H */
