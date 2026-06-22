/*
 * test_enemy.c -- host unit tests for enemy levels (enemy.c).
 */
#include "enemy.h"
#include "arena.h"
#include "rng.h"
#include "bullet.h"

#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

static void clear_bullets(bullets_t *bs)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) bs->b[i].active = 0;
}

int main(void)
{
    enemies_t es;
    bullets_t bs;
    u8 i;
    int k;

    clear_bullets(&bs);
    rng_seed(0x1234);

    /* Wave 0 (no kills) -> all bouncers, all alive, inside the arena. */
    enemies_spawn(&es, 0);
    check("4 enemies spawned", es.e[0].alive && es.e[1].alive &&
                               es.e[2].alive && es.e[3].alive);
    check("wave is alive", enemies_any_alive(&es));
    check("all bounce at wave 0",
          es.e[0].level == ENEMY_BOUNCE && es.e[3].level == ENEMY_BOUNCE);

    /* Bouncers stay inside the walls over time. */
    for (k = 0; k < 300; k++) {
        enemies_update(&es, 128, 96, &bs);
        for (i = 0; i < MAX_ENEMIES; i++) {
            check("x within walls", es.e[i].x >= ARENA_L && es.e[i].x <= ARENA_R);
            check("y within walls", es.e[i].y >= ARENA_T && es.e[i].y <= ARENA_B);
        }
    }

    /* CHASE: a chaser steps toward the player. */
    clear_bullets(&bs);
    for (i = 0; i < MAX_ENEMIES; i++) es.e[i].alive = 0;
    es.e[0].alive = 1; es.e[0].level = ENEMY_CHASE; es.e[0].x = 50; es.e[0].y = 50;
    enemies_update(&es, 100, 100, &bs);     /* player down-right of enemy */
    check("chaser moved toward player", es.e[0].x == 51 && es.e[0].y == 51);

    /* HUNTER: chases, but flees a nearby bullet (overrides chase). */
    es.e[0].level = ENEMY_HUNTER; es.e[0].x = 50; es.e[0].y = 50;
    bs.b[0].active = 1; bs.b[0].x = 55; bs.b[0].y = 50;   /* bullet to the right, close */
    enemies_update(&es, 100, 100, &bs);     /* player right, but flee left from bullet */
    check("hunter flees the bullet (moves left)", es.e[0].x == 49);

    /* HUNTER with no bullet near -> behaves like a chaser. */
    clear_bullets(&bs);
    es.e[0].x = 50; es.e[0].y = 50;
    enemies_update(&es, 100, 100, &bs);
    check("hunter chases when safe", es.e[0].x == 51 && es.e[0].y == 51);

    /* Higher kill counts must be able to produce tougher levels. */
    {
        int saw_chase = 0, w;
        for (w = 0; w < 40 && !saw_chase; w++) {
            enemies_spawn(&es, 25);          /* in the chase band */
            for (i = 0; i < MAX_ENEMIES; i++)
                if (es.e[i].level == ENEMY_CHASE) saw_chase = 1;
        }
        check("chasers appear past WAVE_CHASE_AT", saw_chase);
    }

    if (failures == 0) { printf("test_enemy: ALL PASS\n"); return 0; }
    printf("test_enemy: %d FAILURE(S)\n", failures);
    return 1;
}
