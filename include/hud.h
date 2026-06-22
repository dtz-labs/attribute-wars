/*
 * hud.h -- on-screen HUD for the Timex twin-stick shooter (target-only draw).
 *
 * Four widgets in three places (design spec §9):
 *   - top border (row 0):    lives as hearts (left) + shields as dots (right),
 *                            drawn into BOTH bitmaps (the only bitmap HUD);
 *   - bottom border (row 23): two proportional attribute bars -- timer (left)
 *                            and boost energy (right) -- both attribute blocks;
 *   - the play field:        the SCORE rendered as BIG attribute-cell digits in
 *                            the background, replacing the old checker.
 *
 * The score "background" is a 3x5-cell font, 6 digits, origin row 9 / start
 * col 4 (cols 4..26, rows 9..13). A lit score cell sets DARK paper so the
 * white-ink sprites flying over it still read (design D1). Every cell-restore
 * caller (fx_render / death_anim / telegraph_*) asks score_cell_attr() for the
 * cell colour, so explosions and the spawn telegraph never punch holes in the
 * score.
 *
 * score_cell_attr() reads a cached copy of the last-painted digits, so it is a
 * pure table lookup with no score_t deref in the hot restore path. The caller
 * keeps that cache current via hud_paint_background() / hud_score_changed().
 *
 * All HUD drawing targets BOTH display files (attrs: 0x5800 + 0x7800; bitmaps
 * via the sprite path into screen A and B), so the page-flip never disturbs it.
 * The bars and bitmap widgets cache their last-rendered state and redraw only
 * on change -- cheap enough to call every frame.
 */
#ifndef HUD_H
#define HUD_H

#include "types.h"
#include "score.h"

/* Attribute byte: FLASH(7) BRIGHT(6) PAPER(5-3) INK(2-0). Shared with main.c. */
#define ATTR(bright, paper, ink) ((u8)(((bright) << 6) | ((paper) << 3) | (ink)))

/* Write one attribute cell to BOTH blocks (shows on whichever page is flipped
 * in). Lives in hud.c; main.c's fx/death/telegraph paths call it too. */
void put_attr(u8 row, u8 col, u8 v);

/* Build the HUD's pre-shifted heart sprite once at startup (before any
 * hud_draw_lives call). Cheap; mirrors the other spr_preshift() calls. */
void hud_init(void);

/* Background attribute for one attribute cell (row 0..23, col 0..31). Replaces
 * the old bg_attr(): HUD rows black, side rails magenta, lit score-digit cells
 * dark-paper, everything else black. Reads the cached digit snapshot. */
u8   score_cell_attr(u8 row, u8 col);

/* Snapshot s->digits into the cache, then paint score_cell_attr() across the
 * whole 24x32 attribute grid into BOTH attribute blocks. Replaces bg_paint(). */
void hud_paint_background(const score_t *s);

/* Repaint only the digit cells whose digit changed vs the cache (both blocks),
 * then update the cache. Cheap: score changes are infrequent. */
void hud_score_changed(const score_t *s);

/* Force the next hud_draw_lives / _shields / _timer / _boost to repaint
 * regardless of the cache. Call after scld_clear() wipes the HUD bitmaps or
 * after hud_paint_background() repaints the bar cells. */
void hud_invalidate(void);

/* Lives as hearts along the top border (left), into both bitmaps. Cached. */
void hud_draw_lives(u8 lives);

/* Shields as dots along the top border (right), into both bitmaps. Cached. */
void hud_draw_shields(u8 shields);

/* Timer bar in the bottom-left: paper-filled proportional to
 * frames_left/frames_total, both attribute blocks. Cached on bar length. */
void hud_draw_timer(u16 frames_left, u16 frames_total);

/* Boost bar in the bottom-right: paper-filled proportional to energy/BOOST_MAX,
 * both attribute blocks. Cached on bar length. */
void hud_draw_boost(u8 energy);

#endif /* HUD_H */
