/*
 * measure_main.c -- T-state measurement harness (NOT shipped in the game).
 *
 * Isolates the per-frame cost of LOGIC (enemies_update + collide + player_hit)
 * vs RENDER (erase 7 + draw 7 sprites + 2 bullets) at the 6-enemy steady state,
 * so we optimise the real bottleneck instead of guessing.
 *
 * Marker functions (mark0/1/2) have a side effect (border OUT) so the optimiser
 * can't delete them; we read their addresses from the .map and run z88dk-ticks
 * three times with -end on each:
 *   logic/iter  = (T@mark1 - T@mark0) / ITERS
 *   render/iter = (T@mark2 - T@mark1) / ITERS
 * No HALT/EI here -- pure compute, measured directly.
 */
#include "scld.h"
#include "sprite.h"
#include "sprites.h"
#include "enemy.h"
#include "bullet.h"
#include "collision.h"
#include "rng.h"
#include "types.h"
#include <z80.h>

#define ITERS 200u

static u8 ps_ship[8][SPR_PRESHIFT_SIZE];
static u8 ps_en[SPR_PRESHIFT_SIZE];

void mark0(void) { z80_outp(0xFEu, 1u); }   /* after setup            */
void markA(void) { z80_outp(0xFEu, 2u); }   /* + enemies_update x N    */
void markB(void) { z80_outp(0xFEu, 3u); }   /* + collide x N           */
void markC(void) { z80_outp(0xFEu, 4u); }   /* + player_hit x N        */
void mark2(void) { z80_outp(0xFEu, 5u); }   /* + render x N            */

/* Keep the pools full so no function triggers a respawn (which would pull in
 * rng and skew the numbers). 6 alive, 2 active -- the steady state. */
static void revive(enemies_t *es, bullets_t *bs)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) es->e[i].alive = 1;
    for (i = 0; i < MAX_BULLETS; i++) bs->b[i].active = 1;
}

int main(void)
{
    enemies_t enemies;
    bullets_t bullets;
    u16 back = SCLD_SCREEN_B;
    u16 y;
    u8  d, k, i;

    /* row-offset table (normally built by scld_init; avoid IM1/EI/HALT here) */
    for (y = 0; y < SCLD_H; y++) {
        scld_row_off[y] = (u16)(uintptr_t)scld_scanline(0, (u8)y);
    }
    for (d = 0; d < 8u; d++) spr_preshift(ps_ship[d], spr_ship_dir[d]);
    spr_preshift(ps_en, spr_enemy);

    rng_seed(0xACE1u);
    bullets_init(&bullets);
    enemies_spawn(&enemies, 16u);            /* wave 16: worst-case hunter mix */
    bullet_spawn(&bullets, 120, 96, 1, 0);
    bullet_spawn(&bullets, 80, 70, 0, -1);

    mark0();
    for (k = 0; k < ITERS; k++) {            /* ---- enemies_update ---- */
        enemies_update(&enemies, 120, 96, &bullets);
    }
    markA();
    for (k = 0; k < ITERS; k++) {            /* ---- collide ---- */
        (void)collide_bullets_enemies(&bullets, &enemies);
        revive(&enemies, &bullets);
    }
    markB();
    for (k = 0; k < ITERS; k++) {            /* ---- player_hit ---- */
        (void)player_hit(120, 96, &enemies);
    }
    markC();
    for (k = 0; k < ITERS; k++) {            /* ---- RENDER ---- */
        for (i = 0; i < 7u; i++) {           /* erase last frame (7 sprites) */
            SPR_ERASE(back, (u8)(20u + i * 24u), (u8)(40u + (i & 3u) * 30u));
        }
        SPR_DRAW(back, 120, 96, ps_ship[2]); /* player */
        for (i = 0; i < 6u; i++) {           /* 6 enemies */
            SPR_DRAW(back, (u8)(24u + i * 32u), (u8)(48u + (i & 3u) * 28u), ps_en);
        }
        BUL_ERASE(back, 122, 92);
        BUL_ERASE(back, 62, 72);
        BUL_DRAW(back, 120, 96);
        BUL_DRAW(back, 60, 70);
    }

    mark2();
    z80_outp(0xFEu, 0u);
    for (;;) { }                             /* trap */
}
