/*
 * score.c -- BCD score arithmetic and game-state initialisation.
 *
 * Stores 6 decimal digits as individual bytes (digits[0] = most significant).
 * Carry and borrow propagate through the digit array; no 32-bit arithmetic,
 * no division -- cheap on the Z80 and trivially testable on the host.
 */
#include "score.h"
#include "enemy.h"   /* ENEMY_BOUNCE / CHASE / HUNTER constants */

/* Points table indexed by enemy level (size 4; level 1 is intentionally unused). */
static const u16 enemy_pts[4] = { 200u, 0u, 400u, 600u };

void score_reset(score_t *s)
{
    u8 i;
    for (i = 0; i < 6u; i++) {
        s->digits[i] = 0u;
    }
    s->extra_tt = 0u;
}

u8 score_add(score_t *s, u16 pts)
{
    /* Decompose pts into up to 6 decimal digits and add digit-by-digit
     * from the least-significant end, propagating carry leftward. */
    u8 carry = 0u;
    u8 i;

    /* Unpack pts into a temporary 6-digit BCD representation.
     * pts fits in 5 decimal digits (max 65535), so digits[0] stays 0. */
    u8 add[6];
    u16 tmp = pts;
    for (i = 5u; ; i--) {
        add[i] = (u8)(tmp % 10u);
        tmp    = (u16)(tmp / 10u);
        if (i == 0u) break;
    }

    /* Add digit-by-digit, LSB first (index 5 -> 0). */
    for (i = 5u; ; i--) {
        u8 sum = s->digits[i] + add[i] + carry;
        if (sum >= 10u) {
            s->digits[i] = (u8)(sum - 10u);
            carry = 1u;
        } else {
            s->digits[i] = sum;
            carry = 0u;
        }
        if (i == 0u) break;
    }
    /* Overflow beyond 999999 is silently saturated (carry dropped) -- the
     * game score fits in 6 digits in practice. */

    /* Update monotonic ten-thousands milestone counter. */
    {
        u8 tt = (u8)(s->digits[0] * 10u + s->digits[1]);
        if (tt > s->extra_tt) {
            u8 new_lives = (u8)(tt - s->extra_tt);
            s->extra_tt  = tt;
            return new_lives;
        }
    }
    return 0u;
}

void score_sub(score_t *s, u16 pts)
{
    u8 borrow = 0u;
    u8 i;

    /* Unpack pts into a 6-digit array (same layout as score). */
    u8 sub[6];
    u16 tmp = pts;
    for (i = 5u; ; i--) {
        sub[i] = (u8)(tmp % 10u);
        tmp    = (u16)(tmp / 10u);
        if (i == 0u) break;
    }

    /* Subtract digit-by-digit, LSB first; propagate borrow leftward. */
    for (i = 5u; ; i--) {
        s8 diff = (s8)((s8)s->digits[i] - (s8)sub[i] - (s8)borrow);
        if (diff < 0) {
            s->digits[i] = (u8)(diff + 10);
            borrow = 1u;
        } else {
            s->digits[i] = (u8)diff;
            borrow = 0u;
        }
        if (i == 0u) break;
    }

    /* If borrow is still set the result underflowed -- clamp to zero. */
    if (borrow) {
        for (i = 0u; i < 6u; i++) {
            s->digits[i] = 0u;
        }
    }
    /* extra_tt is intentionally left unchanged on subtraction. */
}

u16 score_enemy_points(u8 level)
{
    /* level is ENEMY_BOUNCE(0), ENEMY_CHASE(2), or ENEMY_HUNTER(3).
     * The table has a hole at index 1 (intentionally unused enemy level). */
    return enemy_pts[level];
}

void game_new(game_state_t *g)
{
    g->wave    = 1u;
    g->lives   = START_LIVES;
    g->shields = START_SHIELDS;
    score_reset(&g->score);
}

void game_resume_from_wave(game_state_t *g, u8 wave)
{
    g->wave    = wave;
    g->lives   = START_LIVES;
    g->shields = START_SHIELDS;
    score_reset(&g->score);
}
