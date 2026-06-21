/*
 * test_geometry.c -- host unit tests for the direction/wrap core.
 *
 * Builds with the native macOS compiler (no emulator): see test/run.sh.
 * This is where the spec's "host unit tests (TDD lives here)" strategy lands.
 */
#include <assert.h>
#include <stdio.h>
#include "geometry.h"

static int checks = 0;
#define CHECK(cond) do { assert(cond); ++checks; } while (0)

static void test_dir_vectors_are_unit_steps(void)
{
    /* Every direction step is in {-1,0,+1} and no direction is (0,0). */
    for (int d = 0; d < 8; ++d) {
        CHECK(dir_dx[d] >= -1 && dir_dx[d] <= 1);
        CHECK(dir_dy[d] >= -1 && dir_dy[d] <= 1);
        CHECK(!(dir_dx[d] == 0 && dir_dy[d] == 0));
    }
    /* Cardinals point the expected way (y grows DOWN). */
    CHECK(dir_dx[DIR_N] == 0  && dir_dy[DIR_N] == -1);
    CHECK(dir_dx[DIR_E] == 1  && dir_dy[DIR_E] == 0);
    CHECK(dir_dx[DIR_S] == 0  && dir_dy[DIR_S] == 1);
    CHECK(dir_dx[DIR_W] == -1 && dir_dy[DIR_W] == 0);
    /* A diagonal. */
    CHECK(dir_dx[DIR_NE] == 1 && dir_dy[DIR_NE] == -1);
}

static void test_dir_from_steps(void)
{
    CHECK(dir_from_steps(0, 0)   == DIR_NONE);
    CHECK(dir_from_steps(0, -1)  == DIR_N);
    CHECK(dir_from_steps(1, -1)  == DIR_NE);
    CHECK(dir_from_steps(1, 0)   == DIR_E);
    CHECK(dir_from_steps(1, 1)   == DIR_SE);
    CHECK(dir_from_steps(0, 1)   == DIR_S);
    CHECK(dir_from_steps(-1, 1)  == DIR_SW);
    CHECK(dir_from_steps(-1, 0)  == DIR_W);
    CHECK(dir_from_steps(-1, -1) == DIR_NW);
    /* Round trip: steps -> dir -> steps. */
    for (int d = 0; d < 8; ++d) {
        CHECK(dir_from_steps(dir_dx[d], dir_dy[d]) == d);
    }
}

static void test_wrap_x_natural_u8(void)
{
    CHECK(wrap_x(0)   == 0);
    CHECK(wrap_x(255) == 255);
    CHECK(wrap_x(256) == 0);     /* off the right edge -> left edge   */
    CHECK(wrap_x(-1)  == 255);   /* off the left edge  -> right edge  */
    CHECK(wrap_x(-10) == 246);
    CHECK(wrap_x(300) == 44);
}

static void test_wrap_y_mod_192(void)
{
    CHECK(wrap_y(0)   == 0);
    CHECK(wrap_y(191) == 191);
    CHECK(wrap_y(192) == 0);     /* off the bottom -> top    */
    CHECK(wrap_y(-1)  == 191);   /* off the top    -> bottom */
    CHECK(wrap_y(-5)  == 187);
    CHECK(wrap_y(200) == 8);
    CHECK(wrap_y(383) == 191);   /* 383 - 192 = 191          */
    CHECK(wrap_y(384) == 0);
}

int main(void)
{
    test_dir_vectors_are_unit_steps();
    test_dir_from_steps();
    test_wrap_x_natural_u8();
    test_wrap_y_mod_192();
    printf("geometry: %d checks passed\n", checks);
    return 0;
}
