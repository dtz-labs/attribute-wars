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
    /* every wave: mix must sum exactly to count (spec §5.4, all 16 rows satisfy this) */
    for (i = 0; i < WAVE_COUNT; i++) {
        const wave_t *w = &wave_table[i];
        check("wave mix sums to count",
              w->n_bounce + w->n_chase + w->n_hunter == w->count);
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

    /* per-wave pattern field is deterministic (not rng) for waves 1-16 */
    check("wave5 pattern=STAR",      wave_table[4].pattern  == PAT_STAR);
    check("wave6 pattern=FLANKS",    wave_table[5].pattern  == PAT_FLANKS);
    check("wave7 pattern=DIAGONALS", wave_table[6].pattern  == PAT_DIAGONALS);
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
            u8 lv = es.e[i].level;
            alive++;
            if (lv == ENEMY_BOUNCE || lv == ENEMY_BOUNCE_V || lv == ENEMY_BOUNCE_H) b++;
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
            u8 lv = es.e[i].level;
            alive++;
            if (lv == ENEMY_BOUNCE || lv == ENEMY_BOUNCE_V || lv == ENEMY_BOUNCE_H) nb++;
            else if (lv == ENEMY_CHASE) nc++;
            else if (lv == ENEMY_HUNTER) nh++;
        }
    }
    check("wave8 alive==6", alive == 6u);
    check("wave8 n_bounce==2", nb == 2u);
    check("wave8 n_chase==2",  nc == 2u);
    check("wave8 n_hunter==2", nh == 2u);
}

/* ---- waves 1-16: pattern comes from table (deterministic, not rng) ---- */
static void test_pattern_from_table(void)
{
    enemies_t es;
    /* Regardless of rng seed, wave 5 must use PAT_STAR (table field). */
    rng_seed(0x1234u);
    enemies_spawn(&es, 5);
    check("wave5 uses PAT_STAR",      enemy_last_pattern() == PAT_STAR);
    rng_seed(0xABCDu);
    enemies_spawn(&es, 6);
    check("wave6 uses PAT_FLANKS",    enemy_last_pattern() == PAT_FLANKS);
    rng_seed(0x5678u);
    enemies_spawn(&es, 7);
    check("wave7 uses PAT_DIAGONALS", enemy_last_pattern() == PAT_DIAGONALS);
}

/* ---- wave > 16: rng-picked pattern must not repeat on consecutive spawns ---- */
static void test_pattern_no_repeat(void)
{
    enemies_t es;
    u8 first, second;
    rng_seed(0x1234u);
    enemies_spawn(&es, 17);
    first  = enemy_last_pattern();
    enemies_spawn(&es, 18);
    second = enemy_last_pattern();
    check("endless-loop consecutive patterns differ", first != second);
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

/* ---- cap: late waves now use all 8 enemy slots ---- */
static void test_spawn_wave16_cap8(void)
{
    enemies_t es;
    u8 alive = 0u, nc = 0u, nh = 0u, i;
    rng_seed(0x4444u);
    enemies_spawn(&es, 16);
    for (i = 0u; i < MAX_ENEMIES; i++) {
        if (es.e[i].alive) {
            alive++;
            if (es.e[i].level == ENEMY_CHASE) nc++;
            else if (es.e[i].level == ENEMY_HUNTER) nh++;
        }
    }
    check("wave16 alive==8", alive == 8u);
    check("wave16 n_chase==1", nc == 1u);
    check("wave16 n_hunter==7", nh == 7u);
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

/* ---- axis-only bouncers stay rare; the rest bounce diagonally ---- */
static void test_axis_bouncer_cap(void)
{
    enemies_t es;
    u8 seed, w, i;
    for (seed = 0u; seed < 32u; seed++) {
        for (w = 1u; w <= 16u; w++) {
            u8 axis = 0u;
            rng_seed((u16)(0x7000u + (u16)seed * 17u + w));
            enemies_spawn(&es, w);
            for (i = 0u; i < MAX_ENEMIES; i++) {
                if (es.e[i].alive &&
                    (es.e[i].level == ENEMY_BOUNCE_V || es.e[i].level == ENEMY_BOUNCE_H)) {
                    axis++;
                }
            }
            check("axis-only bouncers capped", axis <= 2u);
        }
    }
}

/* ---- killed chasers split into diagonal bouncers when there is room ---- */
static void test_chaser_split(void)
{
    enemies_t es;
    u8 i, spawned, alive = 0u, bouncers = 0u;
    for (i = 0u; i < MAX_ENEMIES; i++) {
        es.e[i].alive = 0u;
        es.e[i].level = ENEMY_CHASE;
    }
    spawned = enemies_spawn_chaser_splits(&es, 50u, 50u);
    for (i = 0u; i < MAX_ENEMIES; i++) {
        if (es.e[i].alive) {
            alive++;
            if (es.e[i].level == ENEMY_BOUNCE && es.e[i].dx && es.e[i].dy) {
                bouncers++;
            }
            check("split x in arena", es.e[i].x >= ARENA_L && es.e[i].x <= ARENA_R);
            check("split y in arena", es.e[i].y >= ARENA_T && es.e[i].y <= ARENA_B);
        }
    }
    check("chaser split spawned 2", spawned == 2u);
    check("chaser split alive 2", alive == 2u);
    check("chaser split diagonal bouncers", bouncers == 2u);

    for (i = 0u; i < MAX_ENEMIES; i++) {
        es.e[i].alive = 1u;
        es.e[i].level = ENEMY_BOUNCE;
    }
    es.e[3].alive = 0u;
    spawned = enemies_spawn_chaser_splits(&es, 80u, 80u);
    check("chaser split respects cap", spawned == 1u);
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
    check("hunter flees the bullet faster", es.e[0].x == 48);

    /* Wider dodge: a hunter should already react before the bullet is on top
     * of it, making the dodge visibly more effective without extra AI state. */
    es.e[0].x = 50; es.e[0].y = 50;
    bs.b[0].x = 90; bs.b[0].y = 50;
    enemies_update(&es, 100, 100, &bs);
    check("hunter flees bullet within wider range", es.e[0].x == 48);

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
    test_pattern_from_table();
    test_pattern_no_repeat();
    test_wave_loop();
    test_wave0_guard();
    test_spawn_wave16_cap8();
    test_all_waves_in_bounds();
    test_axis_bouncer_cap();
    test_chaser_split();
    test_movement();

    if (failures == 0) { printf("test_enemy: ALL PASS\n"); return 0; }
    printf("test_enemy: %d FAILURE(S)\n", failures);
    return 1;
}
