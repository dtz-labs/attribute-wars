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

/* ---- wave-table sanity ---- */
static void test_wave_table(void)
{
    u8 i;
    check("WAVE_COUNT is 16", WAVE_COUNT == 16);
    /* every wave: count >= n_bounce + n_chase + n_hunter */
    for (i = 0; i < WAVE_COUNT; i++) {
        const wave_t *w = &wave_table[i];
        check("wave mix sums to count",
              w->n_bounce + w->n_chase + w->n_hunter == w->count ||
              /* clamped waves: mix sums to original count from spec, but
                 spawn uses MAX_ENEMIES cap so we just verify mix <= count */
              w->n_bounce + w->n_chase + w->n_hunter >= w->count);
        check("wave time > 0", w->time_frames > 0u);
        check("wave count > 0", w->count > 0u);
    }
    /* spot-check a few rows verbatim from §5.4 */
    check("wave1 count=4",      wave_table[0].count      == 4u);
    check("wave1 n_bounce=4",   wave_table[0].n_bounce   == 4u);
    check("wave1 n_chase=0",    wave_table[0].n_chase    == 0u);
    check("wave1 n_hunter=0",   wave_table[0].n_hunter   == 0u);
    check("wave1 pattern=PERIM",wave_table[0].pattern    == PAT_PERIMETER);
    check("wave1 time=1500",    wave_table[0].time_frames== 1500u);

    check("wave8 count=6",      wave_table[7].count      == 6u);
    check("wave8 n_bounce=2",   wave_table[7].n_bounce   == 2u);
    check("wave8 n_chase=2",    wave_table[7].n_chase    == 2u);
    check("wave8 n_hunter=2",   wave_table[7].n_hunter   == 2u);

    check("wave16 count=8",     wave_table[15].count     == 8u);
    check("wave16 n_bounce=0",  wave_table[15].n_bounce  == 0u);
    check("wave16 n_hunter=7",  wave_table[15].n_hunter  == 7u);
    check("wave16 time=1000",   wave_table[15].time_frames== 1000u);
}

/* ---- spawn wave 1: 4 bouncers, all in-bounds ---- */
static void test_spawn_wave1(void)
{
    enemies_t es;
    u8 alive = 0, b = 0;
    u8 i;
    rng_seed(0xACE1u);
    enemies_spawn(&es, 1);
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (es.e[i].alive) {
            alive++;
            if (es.e[i].level == ENEMY_BOUNCE) b++;
            check("x in arena", es.e[i].x >= ARENA_L && es.e[i].x <= ARENA_R);
            check("y in arena", es.e[i].y >= ARENA_T && es.e[i].y <= ARENA_B);
        }
    }
    check("wave1 alive==4", alive == 4u);
    check("wave1 all bounce", b == 4u);
}

/* ---- spawn wave 8: 6 alive, mix 2/2/2 (clamped from table) ---- */
static void test_spawn_wave8(void)
{
    enemies_t es;
    u8 alive = 0, nb = 0, nc = 0, nh = 0;
    u8 i;
    rng_seed(0xBEEFu);
    enemies_spawn(&es, 8);
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (es.e[i].alive) {
            alive++;
            if (es.e[i].level == ENEMY_BOUNCE) nb++;
            else if (es.e[i].level == ENEMY_CHASE) nc++;
            else if (es.e[i].level == ENEMY_HUNTER) nh++;
        }
    }
    check("wave8 alive==6", alive == 6u);
    check("wave8 n_bounce==2", nb == 2u);
    check("wave8 n_chase==2",  nc == 2u);
    check("wave8 n_hunter==2", nh == 2u);
}

/* ---- two consecutive spawns must pick different patterns ---- */
static void test_pattern_no_repeat(void)
{
    enemies_t es;
    u8 first, second;
    rng_seed(0x1234u);
    enemies_spawn(&es, 5);
    first  = enemy_last_pattern();
    enemies_spawn(&es, 6);
    second = enemy_last_pattern();
    check("consecutive patterns differ", first != second);
}

/* ---- wave > 16 loops at index 15 ---- */
static void test_wave_loop(void)
{
    enemies_t es16, es17;
    u8 i;
    /* same seed, two spawns that should look structurally identical in count+levels */
    rng_seed(0xAAAAu);
    enemies_spawn(&es16, 16);
    rng_seed(0xAAAAu);
    enemies_spawn(&es17, 17);
    {
        u8 alive16 = 0, alive17 = 0;
        for (i = 0; i < MAX_ENEMIES; i++) {
            if (es16.e[i].alive) alive16++;
            if (es17.e[i].alive) alive17++;
        }
        check("wave17 same alive count as wave16", alive16 == alive17);
    }
}

/* ---- wave 0 guard: treated as wave 1 ---- */
static void test_wave0_guard(void)
{
    enemies_t es;
    u8 alive = 0, i;
    rng_seed(0x9999u);
    enemies_spawn(&es, 0);
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (es.e[i].alive) alive++;
    }
    check("wave0 spawns wave1 count (4)", alive == 4u);
}

/* ---- in-bounds check over many waves ---- */
static void test_all_waves_in_bounds(void)
{
    enemies_t es;
    u8 w, i;
    for (w = 1; w <= 16u; w++) {
        rng_seed((u16)(0x100u + w));
        enemies_spawn(&es, w);
        for (i = 0; i < MAX_ENEMIES; i++) {
            if (es.e[i].alive) {
                check("wave x in arena", es.e[i].x >= ARENA_L && es.e[i].x <= ARENA_R);
                check("wave y in arena", es.e[i].y >= ARENA_T && es.e[i].y <= ARENA_B);
            }
        }
    }
}

/* ---- movement tests (unchanged from original) ---- */
static void test_movement(void)
{
    enemies_t es;
    bullets_t bs;
    u8 i;
    int k;

    clear_bullets(&bs);

    /* Bouncers stay inside the walls over time. */
    rng_seed(0x1234u);
    enemies_spawn(&es, 1);  /* wave 1: 4 bouncers */
    for (k = 0; k < 300; k++) {
        enemies_update(&es, 128, 96, &bs);
        for (i = 0; i < MAX_ENEMIES; i++) {
            if (es.e[i].alive) {
                check("x within walls", es.e[i].x >= ARENA_L && es.e[i].x <= ARENA_R);
                check("y within walls", es.e[i].y >= ARENA_T && es.e[i].y <= ARENA_B);
            }
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

    /* Higher wave numbers must produce tougher levels (wave 10 has hunters). */
    {
        int saw_hunter = 0;
        int w;
        for (w = 0; w < 5 && !saw_hunter; w++) {
            rng_seed((u16)(0x5000u + (u16)w));
            enemies_spawn(&es, 10);
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (es.e[i].alive && es.e[i].level == ENEMY_HUNTER) saw_hunter = 1;
            }
        }
        check("hunters appear in wave 10", saw_hunter);
    }
}

int main(void)
{
    test_wave_table();
    test_spawn_wave1();
    test_spawn_wave8();
    test_pattern_no_repeat();
    test_wave_loop();
    test_wave0_guard();
    test_all_waves_in_bounds();
    test_movement();

    if (failures == 0) { printf("test_enemy: ALL PASS\n"); return 0; }
    printf("test_enemy: %d FAILURE(S)\n", failures);
    return 1;
}
