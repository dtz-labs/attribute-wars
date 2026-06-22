/*
 * sprites.h -- 8x8 pixel-art for the slice (one byte per row, MSB = leftmost).
 * Data only; drawn via sprite.h. Directional ship frames (one per DIR_*) are an
 * easy later addition -- index spr_ship_dir[facing] -- since player_t carries
 * facing already.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include "types.h"

extern const u8 spr_ship[8];          /* player ship (legacy, points up)  */
extern const u8 spr_ship_dir[8][8];   /* 8 directional frames, by DIR_*    */
extern const u8 spr_enemy[8];         /* level 0 bouncer (all-dir)        */
extern const u8 spr_enemy_vbounce[8]; /* level 4 vertical-only bouncer    */
extern const u8 spr_enemy_hbounce[8]; /* level 5 horizontal-only bouncer  */
extern const u8 spr_enemy_chase[8];   /* level 2 chaser  (X)              */
extern const u8 spr_enemy_hunter[8];  /* level 3 hunter  (spiky)          */
extern const u8 spr_bullet[8];        /* small bullet dot                 */
extern const u8 spr_heart[8];         /* small heart for the lives HUD    */

#endif /* SPRITES_H */
