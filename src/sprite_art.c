/*
 * sprite_art.c -- storage for the pre-shifted sprite tables + one-shot build.
 * See sprite_art.h for the per-target placement rationale.
 */
#include "sprite_art.h"
#include "sprites.h"

#ifdef ZX128_PAGE_FLIP
/* All 13 tables live at fixed addresses in page-7 scratch RAM -- no storage. */
#elif defined(AW_TIMEX_SPRITE_SCRATCH)
u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];    /* enemy tables are at 0x7D00 */
#else
u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];
u8 ps_enemy[SPR_PRESHIFT_SIZE];
u8 ps_enemy_vbounce[SPR_PRESHIFT_SIZE];
u8 ps_enemy_hbounce[SPR_PRESHIFT_SIZE];
u8 ps_enemy_chase[SPR_PRESHIFT_SIZE];
u8 ps_enemy_hunter[SPR_PRESHIFT_SIZE];
#endif

#if ENEMY_BOUNCE_H != 5
#error "enemy_sprite_by_level assumes enemy levels 0..5; update the table"
#endif
const u8 * const enemy_sprite_by_level[] = {
    ps_enemy, ps_enemy, ps_enemy_chase, ps_enemy_hunter,
    ps_enemy_vbounce, ps_enemy_hbounce
};

void sprite_art_init(void)
{
    u8 d;
    for (d = 0; d < 8u; d++) {
        spr_preshift(PS_SHIP_DIR(d), spr_ship_dir[d]);
    }
    spr_preshift(ps_enemy,         spr_enemy);
    spr_preshift(ps_enemy_vbounce, spr_enemy_vbounce);
    spr_preshift(ps_enemy_hbounce, spr_enemy_hbounce);
    spr_preshift(ps_enemy_chase,   spr_enemy_chase);
    spr_preshift(ps_enemy_hunter,  spr_enemy_hunter);
}
