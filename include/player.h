/*
 * player.h -- player ship state and movement (pure logic, host-tested).
 *
 * The ship has a little INERTIA: velocity ramps up toward the input direction
 * and coasts back down when input stops (it keeps drifting a few frames). Done
 * in fixed point (1/32 px) -- no floating point. Drawn as an 8x8 directional
 * sprite that turns to face the way it is flying.
 *
 * DASH: tapping the boost input launches a short, high-speed BURST (a "throw")
 * in the current move direction -- DASH_FRAMES at DASH_MAXV (5.5 px/frame) with
 * a snappy ramp -- then a DASH_COOLDOWN before it can fire again. No held-energy
 * meter; it is a once-in-a-while lunge. After the burst the ship coasts back via
 * the normal friction (the drift), giving the throw a satisfying follow-through.
 *
 * FRICTION: coast-down step is PLAYER_FRICTION (3/32 px/frame) in both dash and
 * normal mode. At 3/32 the ship coasts ~27 frames (~0.5 s) from full normal
 * speed before stopping.
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

/* Dash parameters (the "throw" burst + cooldown). A sharp, SHORT lunge: instant
 * snap to a high speed for a few frames (~34 px), then the residual velocity is
 * capped back to PLAYER_MAXV so control returns at once (no long coast tail). */
#define DASH_MAXV     272     /* burst top speed: 272/32 = 8.5 px/frame         */
#define DASH_ACCEL    272     /* instant snap to dash speed (1 frame)           */
#define DASH_FRAMES   4u      /* burst duration (~0.08 s, ~34 px lunge)         */
#define DASH_COOLDOWN 75u     /* cooldown before the next dash (~1.5 s)         */

typedef struct {
    u8  x, y;           /* pixel position (integer; for render/collision)  */
    s16 px, py;         /* fixed-point position (pixel << PLAYER_SUB)       */
    s16 vx, vy;         /* velocity, fixed-point per frame                  */
    u8  facing;         /* DIR_* the ship points at (DIR_NONE at start)     */
    u8  dash_t;         /* frames left in the active dash (0 = not dashing) */
    u8  dash_cd;        /* cooldown frames left (0 = dash ready)            */
} player_t;

void player_init(player_t *p, u8 x, u8 y);

/* Advance one frame from an input intent: ease velocity toward the input
 * (speed limit depends on boost state), integrate position (clamped to the
 * arena), face the flight direction, update boost energy. */
void player_update(player_t *p, const intent_t *in);

#endif /* PLAYER_H */
