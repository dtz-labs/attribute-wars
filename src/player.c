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
    p->dash_t       = 0u;
    p->dash_cd      = 0u;
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

    /* DASH state machine: a boost request launches a short high-speed BURST in
     * the move direction -- only when not already dashing, off cooldown, and
     * actually moving. The burst raises the speed cap + accel; friction stays
     * PLAYER_FRICTION (the coast/drift is unchanged). When the burst ends, the
     * cooldown starts. No held-energy meter. */
    if (in->boost && p->dash_t == 0u && p->dash_cd == 0u
            && (in->move_dx || in->move_dy)) {
        p->dash_t = DASH_FRAMES;                 /* launch the throw */
    }
    if (p->dash_t > 0u) {
        maxv  = DASH_MAXV;
        accel = DASH_ACCEL;
        p->dash_t--;
        if (p->dash_t == 0u) {
            p->dash_cd = DASH_COOLDOWN;          /* burst over -> cool down */
        }
    } else {
        if (p->dash_cd > 0u) {
            p->dash_cd--;
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

    /* Dash just ended this frame (cd was set to DASH_COOLDOWN above): cap the
     * big residual dash velocity back to the normal max so the ship doesn't
     * coast forever -- the dash is a punch, not a launch. */
    if (p->dash_t == 0u && p->dash_cd == DASH_COOLDOWN) {
        if (p->vx >  (s16)PLAYER_MAXV)      p->vx =  (s16)PLAYER_MAXV;
        else if (p->vx < -(s16)PLAYER_MAXV) p->vx = -(s16)PLAYER_MAXV;
        if (p->vy >  (s16)PLAYER_MAXV)      p->vy =  (s16)PLAYER_MAXV;
        else if (p->vy < -(s16)PLAYER_MAXV) p->vy = -(s16)PLAYER_MAXV;
    }

    p->x = (u8)(p->px >> PLAYER_SUB);
    p->y = (u8)(p->py >> PLAYER_SUB);
}
