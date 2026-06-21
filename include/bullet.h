/*
 * bullet.h -- fixed-size bullet pool (pure logic, host-tested).
 *
 * No collision in this slice. Bullets travel in their aim direction and
 * DESPAWN when they leave the screen (open item #1: despawn, not wrap --
 * gives bullets a finite range, which reads better than wrapping shots).
 */
#ifndef BULLET_H
#define BULLET_H

#include "types.h"

#define MAX_BULLETS  16
#define BULLET_SPEED 4      /* pixels per frame, per axis */

typedef struct {
    u8 x, y;            /* position, screen pixels        */
    s8 dx, dy;          /* velocity (aim * BULLET_SPEED)   */
    u8 active;          /* 0 = free slot                   */
} bullet_t;

typedef struct {
    bullet_t b[MAX_BULLETS];
} bullets_t;

void bullets_init(bullets_t *bs);

/* Number of live bullets (for tests / HUD later). */
u8 bullets_count(const bullets_t *bs);

/* Spawn one bullet at (x,y) travelling in the -1/0/+1 aim direction.
 * Returns the slot index, or -1 if aim is (0,0) or the pool is full. */
int bullet_spawn(bullets_t *bs, u8 x, u8 y, s8 aim_dx, s8 aim_dy);

/* Advance all live bullets one frame; despawn any that leave the screen. */
void bullets_update(bullets_t *bs);

#endif /* BULLET_H */
