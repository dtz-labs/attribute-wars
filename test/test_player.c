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

static void test_init_boost_energy(void)
{
    player_t p;
    player_init(&p, 100, 80);
    CHECK(p.boost_energy == BOOST_MAX);
}

static void test_boost_faster_and_drains(void)
{
    player_t p;
    player_init(&p, 100, 80);
    intent_t in = mvb(1, 0, 1);
    u8 e0 = p.boost_energy;
    u8 k;
    for (k = 0; k < 10; k++) player_update(&p, &in);
    CHECK(p.boost_energy < e0);                  /* energy drained */
    CHECK(p.vx > (s16)(PLAYER_MAXV));            /* exceeds base top speed (fixed-point) */
}

static void test_boost_empty_caps_at_base(void)
{
    player_t p;
    player_init(&p, 100, 80);
    p.boost_energy = 0;
    intent_t in = mvb(1, 0, 1);
    u8 k;
    for (k = 0; k < 30; k++) player_update(&p, &in);
    CHECK(p.vx <= (s16)PLAYER_MAXV);             /* no boost without energy */
}

static void test_boost_recharge(void)
{
    player_t p;
    player_init(&p, 100, 80);
    /* Drain some energy by boosting. */
    intent_t in = mvb(1, 0, 1);
    u8 k;
    for (k = 0; k < 10; k++) player_update(&p, &in);
    u8 e_drained = p.boost_energy;
    CHECK(e_drained < BOOST_MAX);
    /* Release boost: energy should recharge. */
    in = mvb(1, 0, 0);
    for (k = 0; k < 10; k++) player_update(&p, &in);
    CHECK(p.boost_energy > e_drained);
}

static void test_boost_recharge_cap(void)
{
    player_t p;
    player_init(&p, 100, 80);
    /* Start at full energy, hold no boost -- energy must not exceed BOOST_MAX. */
    intent_t in = mvb(1, 0, 0);
    u8 k;
    for (k = 0; k < 20; k++) player_update(&p, &in);
    CHECK(p.boost_energy <= BOOST_MAX);
}

int main(void)
{
    test_init();
    test_init_boost_energy();
    test_accel_and_coast();
    test_diagonal_drift();
    test_walls();
    test_boost_faster_and_drains();
    test_boost_empty_caps_at_base();
    test_boost_recharge();
    test_boost_recharge_cap();
    printf("player: %d checks passed\n", checks);
    return 0;
}
