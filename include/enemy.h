/*
 * enemy.h -- enemies with behaviour "levels" (Geometry-Wars-style types).
 * Pure logic (host-tested with a seeded RNG); drawing via sprite.h.
 *
 * Levels can be MIXED within a wave; the mix escalates as the player racks up
 * kills.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "types.h"
#include "bullet.h"      /* enemies_update needs bullet positions (dodging) */

#define MAX_ENEMIES 6      /* 8 overran the 50 Hz frame budget; 6 holds it */
#define ENEMY_SPEED 1        /* pixels per frame (<= player so they're evadable) */

/* Behaviour levels. (1 is intentionally unused for now.) */
#define ENEMY_BOUNCE 0       /* drifts diagonally, bounces off the walls       */
#define ENEMY_CHASE  2       /* steers toward the player                        */
#define ENEMY_HUNTER 3       /* chases the player AND dodges nearby bullets     */

/* Kill thresholds at which tougher types start mixing into new waves. */
#define WAVE_CHASE_AT  20u
#define WAVE_HUNTER_AT 40u

typedef struct {
    u8 x, y;      /* top-left pixel position           */
    s8 dx, dy;    /* velocity (-1/0/+1), used by bounce */
    u8 level;     /* ENEMY_BOUNCE / CHASE / HUNTER      */
    u8 alive;     /* 0 = empty slot                    */
} enemy_t;

typedef struct {
    enemy_t e[MAX_ENEMIES];
} enemies_t;

/* Spawn a fresh wave. The level mix depends on total_kills:
 *   < WAVE_CHASE_AT : all bounce
 *   < WAVE_HUNTER_AT: bounce + chase
 *   else            : bounce + chase + hunter */
void enemies_spawn(enemies_t *es, u16 total_kills);

/* Non-zero if at least one enemy is alive (else the wave is cleared). */
u8 enemies_any_alive(const enemies_t *es);

/* Advance one frame. Per level: bounce off the arena walls / chase the player /
 * chase but flee bullets within DODGE range. All movement stays inside the
 * walled arena. */
void enemies_update(enemies_t *es, u8 player_x, u8 player_y, const bullets_t *bs);

#endif /* ENEMY_H */
