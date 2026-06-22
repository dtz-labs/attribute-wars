/*
 * test_bullet.c -- host unit tests for the bullet pool (pure logic).
 */
#include <assert.h>
#include <stdio.h>
#include "bullet.h"
#include "arena.h"

static int checks = 0;
#define CHECK(cond) do { assert(cond); ++checks; } while (0)

static void test_init_empty(void)
{
    bullets_t bs;
    bullets_init(&bs);
    CHECK(bullets_count(&bs) == 0);
}

static void test_spawn(void)
{
    bullets_t bs;
    int i;
    bullets_init(&bs);

    /* Spawn East: velocity = (+SPEED, 0). */
    i = bullet_spawn(&bs, 100, 80, 1, 0);
    CHECK(i >= 0);
    CHECK(bullets_count(&bs) == 1);
    CHECK(bs.b[i].active && bs.b[i].x == 100 && bs.b[i].y == 80);
    CHECK(bs.b[i].dx == BULLET_SPEED && bs.b[i].dy == 0);

    /* Diagonal NE: (+SPEED, -SPEED). */
    i = bullet_spawn(&bs, 50, 50, 1, -1);
    CHECK(i >= 0 && bs.b[i].dx == BULLET_SPEED && bs.b[i].dy == -BULLET_SPEED);
    CHECK(bullets_count(&bs) == 2);

    /* No aim -> no bullet. */
    CHECK(bullet_spawn(&bs, 10, 10, 0, 0) == -1);
    CHECK(bullets_count(&bs) == 2);
}

static void test_pool_full(void)
{
    bullets_t bs;
    int i;
    bullets_init(&bs);
    for (i = 0; i < MAX_BULLETS; ++i) {
        CHECK(bullet_spawn(&bs, 100, 100, 1, 0) >= 0);
    }
    CHECK(bullets_count(&bs) == MAX_BULLETS);
    /* One past capacity fails. */
    CHECK(bullet_spawn(&bs, 100, 100, 1, 0) == -1);
}

static void test_update_moves_and_despawns(void)
{
    bullets_t bs;
    int i;
    bullets_init(&bs);

    /* Travels East and advances by BULLET_SPEED each frame. */
    i = bullet_spawn(&bs, 100, 80, 1, 0);
    bullets_update(&bs);
    CHECK(bs.b[i].x == 100 + BULLET_SPEED && bs.b[i].y == 80);

    /* Despawns at the arena WALL, not the screen edge: x past ARENA_R but still
     * on-screen must still die (so it never paints over the magenta border). */
    bullets_init(&bs);
    i = bullet_spawn(&bs, (u8)(ARENA_R - 2), 80, 1, 0);  /* +4 -> past ARENA_R */
    CHECK(bullets_count(&bs) == 1);
    bullets_update(&bs);
    CHECK(bullets_count(&bs) == 0);         /* hit the wall, gone */

    /* Off the top wall despawns too. */
    bullets_init(&bs);
    i = bullet_spawn(&bs, 80, (u8)(ARENA_T + 1), 0, -1);  /* steps above ARENA_T */
    bullets_update(&bs);
    CHECK(bullets_count(&bs) == 0);
    (void)i;
}

int main(void)
{
    test_init_empty();
    test_spawn();
    test_pool_full();
    test_update_moves_and_despawns();
    printf("bullet: %d checks passed\n", checks);
    return 0;
}
