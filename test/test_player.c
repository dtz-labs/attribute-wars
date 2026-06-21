/*
 * test_player.c -- host unit tests for player movement/facing (pure logic).
 */
#include <assert.h>
#include <stdio.h>
#include "player.h"
#include "geometry.h"
#include "controls.h"

static int checks = 0;
#define CHECK(cond) do { assert(cond); ++checks; } while (0)

/* Build an intent with just a movement step (no fire). */
static intent_t mv(s8 dx, s8 dy)
{
    intent_t in = {0, 0, 0, 0, 0};
    in.move_dx = dx;
    in.move_dy = dy;
    return in;
}

static void test_init(void)
{
    player_t p;
    player_init(&p, 100, 80);
    CHECK(p.x == 100 && p.y == 80);
    CHECK(p.facing == DIR_NONE);
}

static void test_move_axes(void)
{
    player_t p;
    intent_t in;

    player_init(&p, 100, 80);
    in = mv(1, 0);
    player_update(&p, &in);
    CHECK(p.x == 100 + PLAYER_SPEED && p.y == 80);
    CHECK(p.facing == DIR_E);

    in = mv(0, -1);            /* up = -y */
    player_update(&p, &in);
    CHECK(p.y == 80 - PLAYER_SPEED);
    CHECK(p.facing == DIR_N);

    /* Idle keeps facing, position unchanged. */
    in = mv(0, 0);
    player_update(&p, &in);
    CHECK(p.facing == DIR_N);
    CHECK(p.x == 100 + PLAYER_SPEED && p.y == 80 - PLAYER_SPEED);
}

static void test_wrap(void)
{
    player_t p;
    intent_t in;

    /* Walk off the left edge -> wraps to the right (x is u8). */
    player_init(&p, 1, 50);
    in = mv(-1, 0);
    player_update(&p, &in);                 /* 1 - 2 = -1 -> 255 */
    CHECK(p.x == 255);

    /* Walk off the top -> wraps to the bottom (y mod 192). */
    player_init(&p, 50, 1);
    in = mv(0, -1);
    player_update(&p, &in);                 /* 1 - 2 = -1 -> 191 */
    CHECK(p.y == 191);

    /* Walk off the bottom -> wraps to the top. */
    player_init(&p, 50, 190);
    in = mv(0, 1);
    player_update(&p, &in);                 /* 190 + 2 = 192 -> 0 */
    CHECK(p.y == 0);
}

int main(void)
{
    test_init();
    test_move_axes();
    test_wrap();
    printf("player: %d checks passed\n", checks);
    return 0;
}
