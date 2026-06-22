/*
 * test_collision.c -- host unit test for bullet↔enemy collision (collision.c).
 */
#include "collision.h"

#include <stdio.h>

static int failures = 0;

static void check(const char *name, int cond)
{
    if (!cond) {
        printf("FAIL %s\n", name);
        failures++;
    }
}

int main(void)
{
    /* boxes_overlap: 8x8 boxes overlap iff both axis gaps < 8. */
    check("same cell overlaps",      boxes_overlap(100, 100, 100, 100));
    check("7px apart overlaps",      boxes_overlap(100, 100, 107, 100));
    check("8px apart x: no",        !boxes_overlap(100, 100, 108, 100));
    check("8px apart y: no",        !boxes_overlap(100, 100, 100, 108));
    check("diag 7,7 overlaps",       boxes_overlap(100, 100, 107, 107));
    check("far apart: no",          !boxes_overlap(10, 10, 200, 200));

    /* collide_bullets_enemies: a hit kills enemy + consumes bullet. */
    {
        bullets_t bs;
        enemies_t es;
        u8 i, kills;

        for (i = 0; i < MAX_BULLETS; i++) bs.b[i].active = 0;
        for (i = 0; i < MAX_ENEMIES; i++) es.e[i].alive = 0;

        es.e[0].x = 50; es.e[0].y = 50; es.e[0].alive = 1;
        es.e[1].x = 90; es.e[1].y = 90; es.e[1].alive = 1;

        bs.b[0].x = 52; bs.b[0].y = 51; bs.b[0].active = 1;  /* overlaps e0 */
        bs.b[1].x = 10; bs.b[1].y = 10; bs.b[1].active = 1;  /* hits nothing */

        kills = collide_bullets_enemies(&bs, &es);

        check("one kill reported",   kills == 1);
        check("enemy0 destroyed",    es.e[0].alive == 0);
        check("enemy1 survives",     es.e[1].alive == 1);
        check("hitting bullet gone", bs.b[0].active == 0);
        check("missing bullet stays",bs.b[1].active == 1);
    }

    /* One bullet cannot kill two enemies (break after first hit). */
    {
        bullets_t bs;
        enemies_t es;
        u8 i, kills;

        for (i = 0; i < MAX_BULLETS; i++) bs.b[i].active = 0;
        for (i = 0; i < MAX_ENEMIES; i++) es.e[i].alive = 0;

        es.e[0].x = 50; es.e[0].y = 50; es.e[0].alive = 1;
        es.e[1].x = 51; es.e[1].y = 50; es.e[1].alive = 1;  /* both under bullet */
        bs.b[0].x = 50; bs.b[0].y = 50; bs.b[0].active = 1;

        kills = collide_bullets_enemies(&bs, &es);
        check("one bullet -> one kill", kills == 1);
    }

    /* player_hit: caught only by a live, overlapping enemy. */
    {
        enemies_t es;
        u8 i;
        for (i = 0; i < MAX_ENEMIES; i++) es.e[i].alive = 0;
        es.e[0].x = 100; es.e[0].y = 100; es.e[0].alive = 1;

        check("player clear when far",  !player_hit(10, 10, &es));
        check("player caught on overlap", player_hit(103, 102, &es));
        es.e[0].alive = 0;
        check("dead enemy can't catch", !player_hit(103, 102, &es));
    }

    if (failures == 0) {
        printf("test_collision: ALL PASS\n");
        return 0;
    }
    printf("test_collision: %d FAILURE(S)\n", failures);
    return 1;
}
