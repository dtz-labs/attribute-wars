/*
 * sprite_art.h -- pre-shifted sprite tables for the game loop.
 *
 * The 8-byte source art in sprites.c is expanded once at startup into
 * SPR_PRESHIFT_SIZE-byte pre-shifted tables (sprite_art_init), so the per-row
 * bit-shift is off the render hot path. This module owns WHERE those tables
 * live, which differs per target:
 *
 *   default (ZX48):  plain BSS arrays.
 *   Timex:           screen B ends at 0x7AFF and the AY IM2 table uses
 *                    0x7B00/0x7C7C, so 0x7D00..0x7FFF is safe scratch RAM --
 *                    the five enemy tables live there to free BSS.
 *   ZX128 page-flip: RAM page 7 is mapped at 0xC000 and its display file uses
 *                    0xC000..0xDAFF, so 0xDB00.. is free -- all 13 tables live
 *                    there, keeping the 0x8000..0xBFFF resident area free for
 *                    the stack. Clear of the IM2 vector table at 0xF000.
 *
 * Consumers only ever use PS_SHIP_DIR() and ENEMY_SPRITE(); both resolve to a
 * plain address, so the render loop pays nothing for this indirection.
 */
#ifndef SPRITE_ART_H
#define SPRITE_ART_H

#include <stdint.h>
#include "types.h"
#include "sprite.h"
#include "enemy.h"

#ifdef ZX128_PAGE_FLIP
#define ZX128_SCRATCH_BASE 0xDB00u
/* 8 ship frames (slots 0..7) + 5 enemy frames (slots 8..12) = 13 pre-shift
 * tables = 1664 B, 0xDB00..0xE180 -- clear of the IM2 vector table at 0xF000. */
#define PS_AT(slot_) ((u8 *)(uintptr_t)(ZX128_SCRATCH_BASE + (slot_) * SPR_PRESHIFT_SIZE))
#define ps_ship_dir      PS_AT(0)
#define ps_enemy         PS_AT(8)
#define ps_enemy_vbounce PS_AT(9)
#define ps_enemy_hbounce PS_AT(10)
#define ps_enemy_chase   PS_AT(11)
#define ps_enemy_hunter  PS_AT(12)
#define PS_SHIP_DIR(d_) (ps_ship_dir + (u16)(d_) * SPR_PRESHIFT_SIZE)

#elif defined(AW_TIMEX_SPRITE_SCRATCH)
/* Timex standard display uses screen B through 0x7AFF. AY IM2 uses 0x7B00
 * and vector 0x7C7C, leaving 0x7D00..0x7FFF as safe scratch RAM. Park the
 * five enemy pre-shift tables there to keep all enemy art while freeing BSS. */
#define TIMEX_SPRITE_SCRATCH_BASE 0x7D00u
#define ps_enemy         ((u8 *)(uintptr_t)TIMEX_SPRITE_SCRATCH_BASE)
#define ps_enemy_vbounce ((u8 *)(uintptr_t)(TIMEX_SPRITE_SCRATCH_BASE + SPR_PRESHIFT_SIZE))
#define ps_enemy_hbounce ((u8 *)(uintptr_t)(TIMEX_SPRITE_SCRATCH_BASE + 2u * SPR_PRESHIFT_SIZE))
#define ps_enemy_chase   ((u8 *)(uintptr_t)(TIMEX_SPRITE_SCRATCH_BASE + 3u * SPR_PRESHIFT_SIZE))
#define ps_enemy_hunter  ((u8 *)(uintptr_t)(TIMEX_SPRITE_SCRATCH_BASE + 4u * SPR_PRESHIFT_SIZE))
extern u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];    /* 8 directional ship frames */
#define PS_SHIP_DIR(d_) ps_ship_dir[(d_)]

#else
extern u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];    /* 8 directional ship frames */
extern u8 ps_enemy[SPR_PRESHIFT_SIZE];          /* level 0 bouncer (all-dir) */
extern u8 ps_enemy_vbounce[SPR_PRESHIFT_SIZE];  /* level 4 vertical bouncer  */
extern u8 ps_enemy_hbounce[SPR_PRESHIFT_SIZE];  /* level 5 horizontal bouncer*/
extern u8 ps_enemy_chase[SPR_PRESHIFT_SIZE];    /* level 2 chaser  */
extern u8 ps_enemy_hunter[SPR_PRESHIFT_SIZE];   /* level 3 hunter  */
#define PS_SHIP_DIR(d_) ps_ship_dir[(d_)]
#endif

/* Pre-shifted table per enemy behaviour level. Level 1 is unused, so it
 * deliberately aliases the default bouncer sprite. */
extern const u8 * const enemy_sprite_by_level[];

#define ENEMY_SPRITE(level_) \
    (((level_) <= ENEMY_BOUNCE_H) ? enemy_sprite_by_level[(level_)] : ps_enemy)

/* Expand every source sprite into its pre-shifted table. Call once at startup,
 * before the first frame is drawn. */
void sprite_art_init(void);

#endif /* SPRITE_ART_H */
