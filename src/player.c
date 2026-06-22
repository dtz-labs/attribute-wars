/*
 * player.c -- player ship: inertial movement, clamped to the arena. Pure integer
 * (fixed-point) logic. See player.h.
 */
#include "player.h"
#include "arena.h"
#include "controls.h"

void player_init(player_t *p, u8 x, u8 y)
{
    p->x  = x;
    p->y  = y;
    p->px = (s16)((s16)x << PLAYER_SUB);
    p->py = (s16)((s16)y << PLAYER_SUB);
    p->vx = 0;
    p->vy = 0;
    p->facing = DIR_NONE;
}

/* Ease v toward target: fast ACCEL while a direction is held on this axis,
 * slow FRICTION while it is released (so the ship keeps drifting). */
static s16 ease(s16 v, s16 target)
{
    s16 step = (target != 0) ? PLAYER_ACCEL : PLAYER_FRICTION;
    if (v < target) {
        v = (s16)(v + step);
        if (v > target) v = target;
    } else if (v > target) {
        v = (s16)(v - step);
        if (v < target) v = target;
    }
    return v;
}

void player_update(player_t *p, const intent_t *in)
{
    s16 tvx = (s16)((s16)in->move_dx * PLAYER_MAXV);   /* target velocity */
    s16 tvy = (s16)((s16)in->move_dy * PLAYER_MAXV);
    s8  sx, sy;
    s16 lo, hi;

    /* Both axes ease independently -> diagonal moves drift on both. */
    p->vx = ease(p->vx, tvx);
    p->vy = ease(p->vy, tvy);

    /* Face the direction we are actually flying (keeps facing while coasting). */
    sx = (s8)((p->vx > 0) ? 1 : ((p->vx < 0) ? -1 : 0));
    sy = (s8)((p->vy > 0) ? 1 : ((p->vy < 0) ? -1 : 0));
    p->facing = update_facing(p->facing, sx, sy);

    /* Integrate and clamp to the arena walls (kill velocity at the wall). */
    p->px = (s16)(p->px + p->vx);
    lo = (s16)((s16)ARENA_L << PLAYER_SUB);
    hi = (s16)((s16)ARENA_R << PLAYER_SUB);
    if (p->px < lo) { p->px = lo; p->vx = 0; }
    else if (p->px > hi) { p->px = hi; p->vx = 0; }

    p->py = (s16)(p->py + p->vy);
    lo = (s16)((s16)ARENA_T << PLAYER_SUB);
    hi = (s16)((s16)ARENA_B << PLAYER_SUB);
    if (p->py < lo) { p->py = lo; p->vy = 0; }
    else if (p->py > hi) { p->py = hi; p->vy = 0; }

    p->x = (u8)(p->px >> PLAYER_SUB);
    p->y = (u8)(p->py >> PLAYER_SUB);
}
