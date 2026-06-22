/*
 * bullet.c -- fixed-size bullet pool. Pure integer logic; drawing is done
 * elsewhere (render.c, target). See bullet.h.
 */
#include "bullet.h"
#include "arena.h"

void bullets_init(bullets_t *bs)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; ++i) {
        bs->b[i].active = 0;
    }
}

u8 bullets_count(const bullets_t *bs)
{
    u8 i, n = 0;
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (bs->b[i].active) {
            ++n;
        }
    }
    return n;
}

int bullet_spawn(bullets_t *bs, u8 x, u8 y, s8 aim_dx, s8 aim_dy)
{
    u8 i;
    if (aim_dx == 0 && aim_dy == 0) {
        return -1;                  /* no direction -> no shot */
    }
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bs->b[i].active) {
            bs->b[i].x = x;
            bs->b[i].y = y;
            bs->b[i].dx = (s8)(aim_dx * BULLET_SPEED);
            bs->b[i].dy = (s8)(aim_dy * BULLET_SPEED);
            bs->b[i].active = 1;
            return (int)i;
        }
    }
    return -1;                       /* pool full */
}

void bullets_update(bullets_t *bs)
{
    bullet_t *b = bs->b;     /* pointer-step the pool (no arr[i] multiply) */
    u8 i;
    for (i = 0; i < MAX_BULLETS; ++i, ++b) {
        s16 nx, ny;
        if (!b->active) {
            continue;
        }
        /* Widen so an out-of-range step is detected before it wraps. */
        nx = (s16)b->x + b->dx;
        ny = (s16)b->y + b->dy;
        /* Despawn at the ARENA WALL, not the screen edge -- keeps shots off the
         * magenta border frame (and the HUD in the top border row). */
        if (nx < (s16)ARENA_L || nx > (s16)ARENA_R ||
            ny < (s16)ARENA_T || ny > (s16)ARENA_B) {
            b->active = 0;          /* hit the wall -> despawn */
        } else {
            b->x = (u8)nx;
            b->y = (u8)ny;
        }
    }
}
