/*
 * player.c -- player ship state and movement. Pure integer logic; the
 * vector drawing of the ship lives in render.c (target). See player.h.
 */
#include "player.h"
#include "geometry.h"
#include "controls.h"

void player_init(player_t *p, u8 x, u8 y)
{
    p->x = x;
    p->y = y;
    p->facing = DIR_NONE;
}

void player_update(player_t *p, const intent_t *in)
{
    /* Point where we move; keep last facing while idle. */
    p->facing = update_facing(p->facing, in->move_dx, in->move_dy);

    /* Toroidal move. Widen to s16 first so the +/- step can go out of range
     * before wrap_x/wrap_y fold it back in. */
    p->x = wrap_x((s16)p->x + (s16)in->move_dx * PLAYER_SPEED);
    p->y = wrap_y((s16)p->y + (s16)in->move_dy * PLAYER_SPEED);
}
