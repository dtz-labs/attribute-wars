/*
 * test_player.c -- host unit tests for inertial player movement (pure logic).
 */
#include <assert.h>
#include <stdio.h>
#include "player.h"
#include "geometry.h"
#include "controls.h"
#include "arena.h"

static int checks = 0;
#define CHECK(cond) do { assert(cond); ++checks; } while (0)

/* Build an intent with just a movement step (no fire). */
static intent_t mv(s8 dx, s8 dy)
{
    intent_t in = {0, 0, 0, 0, 0, 0};
    in.move_dx = dx;
    in.move_dy = dy;
    return in;
}

/* Build an intent with movement + boost. */
static intent_t mvb(s8 dx, s8 dy, u8 b)
{
    intent_t in = {0, 0, 0, 0, 0, 0};
    in.move_dx = dx;
    in.move_dy = dy;
    in.boost = b;
    return in;
}

static void test_init(void)
{
    player_t p;
    player_init(&p, 100, 80);
    CHECK(p.x == 100 && p.y == 80);
    CHECK(p.facing == DIR_NONE);
    CHECK(p.vx == 0 && p.vy == 0);
}

static void test_accel_and_coast(void)
{
    player_t p;
    intent_t in;
    u8 k, x0;

    player_init(&p, 100, 80);

    /* Hold right: velocity ramps up, ship moves right and faces East. */
    in = mv(1, 0);
    for (k = 0; k < 12; k++) player_update(&p, &in);
    CHECK(p.x > 100);
    CHECK(p.vx > 0);
    CHECK(p.facing == DIR_E);
    x0 = p.x;

    /* Release: it keeps drifting (inertia), then brakes to a stop. */
    in = mv(0, 0);
    player_update(&p, &in);
    CHECK(p.x >= x0);                 /* still coasting */
    for (k = 0; k < 30; k++) player_update(&p, &in);  /* longer drift now (~22 frames) */
    CHECK(p.vx == 0 && p.vy == 0);    /* came to rest */
    CHECK(p.facing == DIR_E);         /* keeps last facing while idle */
}

static void test_walls(void)
{
    player_t p;
    intent_t in;
    u8 k;

    /* Drive into the right wall -> clamps at ARENA_R, velocity killed. */
    player_init(&p, (u8)(ARENA_R - 4), 80);
    in = mv(1, 0);
    for (k = 0; k < 30; k++) player_update(&p, &in);
    CHECK(p.x == ARENA_R);
    CHECK(p.vx == 0);

    /* Top wall too. */
    player_init(&p, 80, (u8)(ARENA_T + 4));
    in = mv(0, -1);
    for (k = 0; k < 30; k++) player_update(&p, &in);
    CHECK(p.y == ARENA_T);
}

static void test_diagonal_drift(void)
{
    player_t p;
    intent_t in;
    u8 k, x0, y0;

    player_init(&p, 100, 80);
    /* Hold down-right: both axes ramp up. */
    in = mv(1, 1);
    for (k = 0; k < 12; k++) player_update(&p, &in);
    CHECK(p.x > 100 && p.y > 80);
    CHECK(p.facing == DIR_SE);
    x0 = p.x; y0 = p.y;

    /* Release: BOTH axes keep drifting, then both stop. */
    in = mv(0, 0);
    player_update(&p, &in);
    CHECK(p.x >= x0 && p.y >= y0);          /* both still coasting */
    for (k = 0; k < 30; k++) player_update(&p, &in);
    CHECK(p.vx == 0 && p.vy == 0);
}

static void test_init_dash(void)
{
    player_t p;
    player_init(&p, 100, 80);
    CHECK(p.dash_t == 0u && p.dash_cd == 0u);   /* ready, not dashing */
}

static void test_dash_launches_and_is_faster(void)
{
    player_t p;
    player_init(&p, 100, 80);
    intent_t in = mvb(1, 0, 1);
    u8 k;
    player_update(&p, &in);                       /* frame 1: launch + instant snap */
    CHECK(p.dash_t > 0u);                          /* mid-burst */
    CHECK(p.vx > (s16)PLAYER_MAXV);                /* burst exceeds base top speed */
    for (k = 1; k < DASH_FRAMES; k++) player_update(&p, &in);  /* finish the burst */
    CHECK(p.dash_t == 0u && p.dash_cd == DASH_COOLDOWN);   /* cooldown started */
    CHECK(p.vx <= (s16)PLAYER_MAXV);              /* residual velocity capped to normal */
}

static void test_dash_cooldown_blocks_relaunch(void)
{
    player_t p;
    player_init(&p, 100, 80);
    intent_t in = mvb(1, 0, 1);
    u8 k;
    for (k = 0; k < DASH_FRAMES; k++) player_update(&p, &in);  /* full burst */
    CHECK(p.dash_t == 0u && p.dash_cd > 0u);
    for (k = 0; k < 10; k++) player_update(&p, &in);   /* still holding boost */
    CHECK(p.dash_t == 0u);                       /* did NOT relaunch during cooldown */
    CHECK(p.dash_cd < DASH_COOLDOWN);            /* cooldown ticking down */
}

static void test_dash_needs_movement(void)
{
    player_t p;
    player_init(&p, 100, 80);
    intent_t in = mvb(0, 0, 1);                  /* boost held but not moving */
    u8 k;
    for (k = 0; k < 5; k++) player_update(&p, &in);
    CHECK(p.dash_t == 0u && p.dash_cd == 0u);    /* no dash without movement */
}

static void test_dash_ready_after_cooldown(void)
{
    player_t p;
    player_init(&p, 100, 80);
    intent_t in = mvb(1, 0, 1);
    u8 k, relaunched = 0u;
    for (k = 0; k < DASH_FRAMES; k++) player_update(&p, &in);  /* dash 1 */
    /* Hold boost+move; once the cooldown expires a 2nd dash must fire. */
    for (k = 0; k < (u8)(DASH_COOLDOWN + 5u); k++) {
        u8 before = p.dash_t;
        player_update(&p, &in);
        if (before == 0u && p.dash_t == (u8)(DASH_FRAMES - 1u)) {
            relaunched = 1u;                     /* a fresh burst just launched */
        }
    }
    CHECK(relaunched);
}

int main(void)
{
    test_init();
    test_init_dash();
    test_accel_and_coast();
    test_diagonal_drift();
    test_walls();
    test_dash_launches_and_is_faster();
    test_dash_cooldown_blocks_relaunch();
    test_dash_needs_movement();
    test_dash_ready_after_cooldown();
    printf("player: %d checks passed\n", checks);
    return 0;
}
