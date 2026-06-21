/*
 * bullet.c -- fixed-size bullet pool. Pure integer logic; drawing is done
 * elsewhere (render.c, target). See bullet.h.
 */
#include "bullet.h"

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
    u8 i;
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (bs->b[i].active) {
            /* Widen so an out-of-range step is detected before it wraps. */
            s16 nx = (s16)bs->b[i].x + bs->b[i].dx;
            s16 ny = (s16)bs->b[i].y + bs->b[i].dy;
            if (nx < 0 || nx > 255 || ny < 0 || ny >= (s16)SCREEN_H) {
                bs->b[i].active = 0;        /* left the screen -> despawn */
            } else {
                bs->b[i].x = (u8)nx;
                bs->b[i].y = (u8)ny;
            }
        }
    }
}
