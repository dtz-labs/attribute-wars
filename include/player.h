/*
 * player.h -- player ship state and movement (pure logic, host-tested).
 *
 * The ship has a little INERTIA: velocity ramps up toward the input direction
 * and coasts back down when input stops (it keeps drifting a few frames). Done
 * in fixed point (1/32 px) -- no floating point. Drawn as an 8x8 directional
 * sprite that turns to face the way it is flying.
 *
 * BOOST: holding the boost input (intent_t.boost) while boost_energy > 0
 * raises the top speed to PLAYER_MAXV_BOOST (4.5 px/frame) and drains energy
 * by BOOST_DRAIN per frame. Releasing boost recharges at BOOST_RECHARGE per
 * frame (capped at BOOST_MAX). With no energy left boost is silently ignored
 * and the ship tops out at the normal PLAYER_MAXV (2.5 px/frame).
 *
 * FRICTION: coast-down step is PLAYER_FRICTION (3/32 px/frame) in both boost
 * and normal mode -- drift distance is identical either way. At 3/32 the ship
 * coasts roughly 27 frames (~0.5 s) from full normal speed before stopping.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"
#include "controls.h"   /* intent_t */

/* Fixed-point at 1/32 px (SUB=5) instead of 1/16 -- the finer grid lets us tune
 * the coast in smaller steps. */
#define PLAYER_SUB      5            /* fixed-point fraction bits (1/32 px)      */
#define PLAYER_MAXV     80           /* top speed (no boost): 80/32 = 2.5 px/frame */
#define PLAYER_ACCEL    12           /* ramp-up per frame while a key is held    */
#define PLAYER_FRICTION 3            /* coast-down per frame when released;
                                      * lower = LONGER drift (applies to both x,y)*/

/* Boost parameters. */
#define PLAYER_MAXV_BOOST  144       /* boost top speed: 144/32 = 4.5 px/frame  */
#define PLAYER_ACCEL_BOOST 24        /* ramp-up per frame while boosting         */
#define BOOST_MAX          100       /* full energy tank                         */
#define BOOST_DRAIN        2         /* energy consumed per frame while boosting */
#define BOOST_RECHARGE     1         /* energy regained per frame when not boost */

typedef struct {
    u8  x, y;           /* pixel position (integer; for render/collision)  */
    s16 px, py;         /* fixed-point position (pixel << PLAYER_SUB)       */
    s16 vx, vy;         /* velocity, fixed-point per frame                  */
    u8  facing;         /* DIR_* the ship points at (DIR_NONE at start)     */
    u8  boost_energy;   /* current boost energy [0, BOOST_MAX]              */
} player_t;

void player_init(player_t *p, u8 x, u8 y);

/* Advance one frame from an input intent: ease velocity toward the input
 * (speed limit depends on boost state), integrate position (clamped to the
 * arena), face the flight direction, update boost energy. */
void player_update(player_t *p, const intent_t *in);

#endif /* PLAYER_H */
