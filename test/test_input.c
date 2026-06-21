/*
 * test_input.c -- host unit tests for the pure input decode logic.
 * No hardware: validates joystick decode, QWEADZXC aim mapping, facing,
 * and the combined intent (incl. Kempston-fire-in-facing-direction).
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

    /* No input at all: no movement, no fire. */
    make_intent(0, 0, DIR_E, &in);
    CHECK(in.move_dx == 0 && in.move_dy == 0);
    CHECK(in.fire == 0 && in.aim_dx == 0 && in.aim_dy == 0);

    /* Move only (no fire). */
    make_intent(JOY_RIGHT, 0, DIR_E, &in);
    CHECK(in.move_dx == 1 && in.move_dy == 0 && in.fire == 0);

    /* Kempston FIRE shoots in the facing direction (here: West). */
    make_intent(JOY_FIRE, 0, DIR_W, &in);
    CHECK(in.fire == 1 && in.aim_dx == -1 && in.aim_dy == 0);

    /* Kempston FIRE with facing == NONE: no shot (nothing to aim at). */
    make_intent(JOY_FIRE, 0, DIR_NONE, &in);
    CHECK(in.fire == 0);

    /* QWEADZXC overrides facing: hold E key -> shoot East regardless of facing W. */
    make_intent(JOY_FIRE, KEY_D, DIR_W, &in);
    CHECK(in.fire == 1 && in.aim_dx == 1 && in.aim_dy == 0);

    /* Move and shoot independently (true twin-stick): move up, shoot down-left. */
    make_intent(JOY_UP, KEY_Z, DIR_N, &in);
    CHECK(in.move_dx == 0 && in.move_dy == -1);
    CHECK(in.fire == 1 && in.aim_dx == -1 && in.aim_dy == 1);
}

int main(void)
{
    test_decode_move();
    test_decode_aim_keys();
    test_update_facing();
    test_make_intent();
    printf("input: %d checks passed\n", checks);
    return 0;
}
