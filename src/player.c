/*
 * player.c -- player ship: inertial movement, clamped to the arena. Pure integer
 * (fixed-point) logic. See player.h.
 */
#include "player.h"
#include "arena.h"
#include "controls.h"

void player_init(player_t *p, u8 x, u8 y)
{
    p->x            = x;
    p->y            = y;
    p->px           = (s16)((s16)x << PLAYER_SUB);
    p->py           = (s16)((s16)y << PLAYER_SUB);
    p->vx           = 0;
    p->vy           = 0;
    p->facing       = DIR_NONE;
    p->boost_energy = BOOST_MAX;
}

/* Ease v toward target using the supplied accel step (while target!=0) and
 * fric step (while target==0 / coasting). Caller passes the appropriate accel
 * and fric for the current boost state. */
static s16 ease(s16 v, s16 target, s16 accel, s16 fric)
{
    s16 step = (target != 0) ? accel : fric;
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
    s16 maxv, accel;
    s16 tvx, tvy;
    s8  sx, sy;
    s16 lo, hi;

    /* Determine boost state: active only when requested AND energy remains. */
    if (in->boost && p->boost_energy > 0) {
        /* Drain energy, floored at 0. */
        if (p->boost_energy > BOOST_DRAIN)
            p->boost_energy = (u8)(p->boost_energy - BOOST_DRAIN);
        else
            p->boost_energy = 0;
        maxv  = PLAYER_MAXV_BOOST;
        accel = PLAYER_ACCEL_BOOST;
    } else {
        /* Recharge energy only when boost is NOT held (or held with 0 energy).
         * We do NOT recharge while the player is actively requesting boost --
         * that would cause a 1-frame recharge that immediately re-enables boost
         * on the next frame when starting from 0. */
        if (!in->boost && p->boost_energy < BOOST_MAX) {
            u8 recharged = (u8)(p->boost_energy + BOOST_RECHARGE);
            p->boost_energy = (recharged > BOOST_MAX) ? BOOST_MAX : recharged;
        }
        maxv  = PLAYER_MAXV;
        accel = PLAYER_ACCEL;
    }

    tvx = (s16)((s16)in->move_dx * maxv);   /* target velocity */
    tvy = (s16)((s16)in->move_dy * maxv);

    /* Both axes ease independently -> diagonal moves drift on both.
     * Friction is always PLAYER_FRICTION regardless of boost state. */
    p->vx = ease(p->vx, tvx, accel, PLAYER_FRICTION);
    p->vy = ease(p->vy, tvy, accel, PLAYER_FRICTION);

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
