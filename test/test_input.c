/*
 * test_input.c -- host unit tests for the pure input decode logic.
 * No hardware: validates joystick decode, QWEADZXC aim mapping, facing,
 * and the combined intent (incl. boost/fire re-sourcing per scheme).
 */
#include <assert.h>
#include <stdio.h>
#include "controls.h"
#include "geometry.h"

static int checks = 0;
#define CHECK(cond) do { assert(cond); ++checks; } while (0)

static void test_decode_move(void)
{
    s8 dx, dy;
    decode_move(0, &dx, &dy);                 CHECK(dx == 0 && dy == 0);
    decode_move(JOY_UP, &dx, &dy);            CHECK(dx == 0 && dy == -1);
    decode_move(JOY_DOWN, &dx, &dy);          CHECK(dx == 0 && dy == 1);
    decode_move(JOY_LEFT, &dx, &dy);          CHECK(dx == -1 && dy == 0);
    decode_move(JOY_RIGHT, &dx, &dy);         CHECK(dx == 1 && dy == 0);
    decode_move(JOY_UP | JOY_RIGHT, &dx, &dy);CHECK(dx == 1 && dy == -1);
    decode_move(JOY_DOWN | JOY_LEFT, &dx, &dy);CHECK(dx == -1 && dy == 1);
    /* FIRE bit must not affect movement. */
    decode_move(JOY_FIRE | JOY_LEFT, &dx, &dy);CHECK(dx == -1 && dy == 0);
    /* Opposing bits cancel (shouldn't happen on real stick, but be safe). */
    decode_move(JOY_LEFT | JOY_RIGHT, &dx, &dy);CHECK(dx == 0 && dy == 0);
}

static void test_decode_aim_keys(void)
{
    CHECK(decode_aim_keys(0)      == DIR_NONE);
    CHECK(decode_aim_keys(KEY_W)  == DIR_N);
    CHECK(decode_aim_keys(KEY_E)  == DIR_NE);
    CHECK(decode_aim_keys(KEY_D)  == DIR_E);
    CHECK(decode_aim_keys(KEY_C)  == DIR_SE);
    CHECK(decode_aim_keys(KEY_X)  == DIR_S);
    CHECK(decode_aim_keys(KEY_Z)  == DIR_SW);
    CHECK(decode_aim_keys(KEY_A)  == DIR_W);
    CHECK(decode_aim_keys(KEY_Q)  == DIR_NW);
    /* Multiple held: lowest bit (Q before D) wins, deterministically. */
    CHECK(decode_aim_keys(KEY_Q | KEY_D) == DIR_NW);
}

static void test_decode_move_keys(void)
{
    s8 dx, dy;
    /* CTRL_KEMPSTON_FIRE drives MOVEMENT from QWEADZXC -> -1/0/+1 steps. */
    decode_move_keys(0, &dx, &dy);       CHECK(dx == 0 && dy == 0);
    decode_move_keys(KEY_W, &dx, &dy);   CHECK(dx == 0 && dy == -1);  /* N  */
    decode_move_keys(KEY_X, &dx, &dy);   CHECK(dx == 0 && dy == 1);   /* S  */
    decode_move_keys(KEY_A, &dx, &dy);   CHECK(dx == -1 && dy == 0);  /* W  */
    decode_move_keys(KEY_D, &dx, &dy);   CHECK(dx == 1 && dy == 0);   /* E  */
    decode_move_keys(KEY_E, &dx, &dy);   CHECK(dx == 1 && dy == -1);  /* NE */
    decode_move_keys(KEY_Z, &dx, &dy);   CHECK(dx == -1 && dy == 1);  /* SW */
}

static void test_update_facing(void)
{
    /* Idle keeps previous facing. */
    CHECK(update_facing(DIR_E, 0, 0) == DIR_E);
    /* Moving updates facing to the movement direction. */
    CHECK(update_facing(DIR_E, 0, -1) == DIR_N);
    CHECK(update_facing(DIR_N, 1, 1)  == DIR_SE);
    /* From the initial DIR_NONE, idle stays NONE. */
    CHECK(update_facing(DIR_NONE, 0, 0) == DIR_NONE);
}

static void test_make_intent(void)
{
    intent_t in;

    /* No input at all: no movement, no fire, no boost. */
    make_intent(0, 0, DIR_E, &in);
    CHECK(in.move_dx == 0 && in.move_dy == 0);
    CHECK(in.fire == 0 && in.aim_dx == 0 && in.aim_dy == 0);
    CHECK(in.boost == 0);

    /* Move only (no fire, no boost). */
    make_intent(JOY_RIGHT, 0, DIR_E, &in);
    CHECK(in.move_dx == 1 && in.move_dy == 0 && in.fire == 0);
    CHECK(in.boost == 0);

    /* Scheme A: JOY_FIRE now sets boost, NOT fire. */
    make_intent(JOY_FIRE, 0, DIR_W, &in);
    CHECK(in.fire == 0);
    CHECK(in.boost != 0);
    CHECK(in.aim_dx == 0 && in.aim_dy == 0);

    /* Scheme A: JOY_FIRE with NONE facing still boosts, not fires. */
    make_intent(JOY_FIRE, 0, DIR_NONE, &in);
    CHECK(in.fire == 0);
    CHECK(in.boost != 0);

    /* Scheme A: QWEADZXC keys still set aim+fire; JOY_FIRE alongside also boosts. */
    make_intent(JOY_FIRE, KEY_D, DIR_W, &in);
    CHECK(in.fire == 1 && in.aim_dx == 1 && in.aim_dy == 0);
    CHECK(in.boost != 0);

    /* Move and shoot independently (true twin-stick): move up, shoot down-left. */
    make_intent(JOY_UP, KEY_Z, DIR_N, &in);
    CHECK(in.move_dx == 0 && in.move_dy == -1);
    CHECK(in.fire == 1 && in.aim_dx == -1 && in.aim_dy == 1);
    CHECK(in.boost == 0);

    /* Scheme A: QWEADZXC fires without boost when JOY_FIRE not held. */
    make_intent(0, KEY_W, DIR_E, &in);
    CHECK(in.fire == 1 && in.aim_dx == 0 && in.aim_dy == -1);
    CHECK(in.boost == 0);
}

/* decode_aim_joy: Scheme-B helper -- maps Kempston tilt byte to aim+fire.
 * Fire bit on the stick (JOY_FIRE) fires in heading; directional deflection
 * sets aim_dx/aim_dy and fires. This is the pure decode side. */
static void test_decode_aim_joy(void)
{
    s8 adx, ady;
    u8 fire;

    /* No stick input -> no aim, no fire. */
    fire = decode_aim_joy(0, &adx, &ady);
    CHECK(fire == 0 && adx == 0 && ady == 0);

    /* Tilt right -> aim East, fire. */
    fire = decode_aim_joy(JOY_RIGHT, &adx, &ady);
    CHECK(fire == 1 && adx == 1 && ady == 0);

    /* Tilt up -> aim North, fire. */
    fire = decode_aim_joy(JOY_UP, &adx, &ady);
    CHECK(fire == 1 && adx == 0 && ady == -1);

    /* Diagonal tilt -> aim NE, fire. */
    fire = decode_aim_joy(JOY_UP | JOY_RIGHT, &adx, &ady);
    CHECK(fire == 1 && adx == 1 && ady == -1);

    /* FIRE only (no tilt) -> no aim change (returns 0,0), fire still set. */
    fire = decode_aim_joy(JOY_FIRE, &adx, &ady);
    CHECK(fire == 1 && adx == 0 && ady == 0);

    /* Tilt with FIRE bit -> aim from tilt, still fires. */
    fire = decode_aim_joy(JOY_LEFT | JOY_FIRE, &adx, &ady);
    CHECK(fire == 1 && adx == -1 && ady == 0);
}

static void test_joy_sanitize(void)
{
    /* Valid readings pass through unchanged. */
    CHECK(joy_sanitize(0)            == 0);
    CHECK(joy_sanitize(JOY_LEFT)     == JOY_LEFT);
    CHECK(joy_sanitize(JOY_UP | JOY_RIGHT) == (JOY_UP | JOY_RIGHT));
    CHECK(joy_sanitize(JOY_FIRE | JOY_DOWN) == (JOY_FIRE | JOY_DOWN));
    /* Floating bus (all bits set) and any opposing pair are dropped to 0. */
    CHECK(joy_sanitize(0x8F)         == 0);   /* UP+DOWN+LEFT+RIGHT+FIRE */
    CHECK(joy_sanitize(JOY_UP | JOY_DOWN)   == 0);
    CHECK(joy_sanitize(JOY_LEFT | JOY_RIGHT) == 0);
}

int main(void)
{
    test_decode_move();
    test_decode_aim_keys();
    test_decode_move_keys();
    test_update_facing();
    test_make_intent();
    test_joy_sanitize();
    test_decode_aim_joy();
    printf("input: %d checks passed\n", checks);
    return 0;
}
