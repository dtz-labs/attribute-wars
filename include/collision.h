/*
 * collision.h -- bullet↔enemy collision (pure logic, host-tested).
 *
 * 8×8 axis-aligned box overlap. No toroidal-wrap handling: enemies sit mid-
 * screen and bullets despawn at the edges (bullet.h), so wrap-edge collisions
 * cannot arise in this slice. Add wrap-aware distance later if enemies roam to
 * the edges.
 */
#ifndef COLLISION_H
#define COLLISION_H

#include "types.h"
#include "bullet.h"
#include "enemy.h"

/* True if the two 8×8 boxes with top-left (ax,ay) and (bx,by) overlap. */
u8 boxes_overlap(u8 ax, u8 ay, u8 bx, u8 by);

/* Test every active bullet against every live enemy. On a hit, kill the enemy
 * (alive = 0) and consume the bullet (active = 0). Returns the number of
 * enemies destroyed this call (for scoring). */
u8 collide_bullets_enemies(bullets_t *bs, enemies_t *es);

/* Non-zero if the player's 8×8 box at (px,py) overlaps any live enemy
 * (i.e. the player has been caught). Read-only. */
u8 player_hit(u8 px, u8 py, const enemies_t *es);

#endif /* COLLISION_H */
