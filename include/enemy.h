/*
 * enemy.h -- enemies with behaviour "levels" (Geometry-Wars-style types).
 * Pure logic (host-tested with a seeded RNG); drawing via sprite.h.
 *
 * Levels are MIXED within a wave; the mix is driven by the 16-wave table.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "types.h"
#include "bullet.h"      /* enemies_update needs bullet positions (dodging) */

#define MAX_ENEMIES 6      /* 8 overran the 50 Hz frame budget; 6 holds it */
#define ENEMY_SPEED 1        /* pixels per frame (<= player so they're evadable) */

/* Behaviour levels. (1 is intentionally unused for now.) The three BOUNCE
 * variants share the SAME update path (enemies_update keeps their stored dx/dy
 * and reflects off the walls); they differ only in the dx/dy they SPAWN with:
 *   BOUNCE   = (dx!=0, dy!=0) -> diagonal, bounces off all 4 walls (like a ball)
 *   BOUNCE_V = (dx==0)        -> moves up/down only, bounces off top/bottom
 *   BOUNCE_H = (dy==0)        -> moves left/right only, bounces off the sides
 * No new asm: enemies_update treats any non-CHASE/HUNTER level as a bouncer. */
#define ENEMY_BOUNCE   0     /* diagonal, bounces off all walls                */
#define ENEMY_CHASE    2     /* steers toward the player                       */
#define ENEMY_HUNTER   3     /* chases the player AND dodges nearby bullets    */
#define ENEMY_BOUNCE_V 4     /* vertical-only bouncer (up/down)                */
#define ENEMY_BOUNCE_H 5     /* horizontal-only bouncer (left/right)           */

/* Spawn patterns — RNG-picked each wave (no immediate repeat). */
enum { PAT_PERIMETER, PAT_STAR, PAT_FLANKS, PAT_ROWS, PAT_DIAGONALS, PAT_N };

/* Per-wave difficulty descriptor. count is clamped to MAX_ENEMIES at use. */
#define WAVE_COUNT 16
typedef struct {
    u8  count;         /* total enemies (clamped to MAX_ENEMIES at spawn)   */
    u8  n_bounce;      /* how many get ENEMY_BOUNCE level                   */
    u8  n_chase;       /* how many get ENEMY_CHASE level                    */
    u8  n_hunter;      /* how many get ENEMY_HUNTER level                   */
    u8  pattern;       /* preferred spawn pattern (PAT_* — overridden by rng)*/
    u16 time_frames;   /* wave time budget in frames (seconds × 50)         */
} wave_t;

extern const wave_t wave_table[WAVE_COUNT];   /* §5.4 of the spec, verbatim */

typedef struct {
    u8 x, y;      /* top-left pixel position           */
    s8 dx, dy;    /* velocity (-1/0/+1), used by bounce */
    u8 level;     /* ENEMY_BOUNCE / CHASE / HUNTER      */
    u8 alive;     /* 0 = empty slot                    */
} enemy_t;

typedef struct {
    enemy_t e[MAX_ENEMIES];
} enemies_t;

/* Spawn a fresh wave.
 * wave is 1-based; >16 loops at wave-16 settings with a random pattern.
 * wave==0 is treated as wave 1. */
void enemies_spawn(enemies_t *es, u8 wave);

/* Returns the pattern used in the last enemies_spawn call (for tests). */
u8 enemy_last_pattern(void);

/* Non-zero if at least one enemy is alive (else the wave is cleared). */
u8 enemies_any_alive(const enemies_t *es);

/* Advance one frame. Per level: bounce off the arena walls / chase the player /
 * chase but flee bullets within DODGE range. All movement stays inside the
 * walled arena. */
void enemies_update(enemies_t *es, u8 player_x, u8 player_y, const bullets_t *bs);

#endif /* ENEMY_H */
