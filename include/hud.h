/*
 * hud.h -- on-screen HUD for the Timex twin-stick shooter (target-only draw).
 *
 * Minimal HUD drawn on the arena frame (the background is the generated floor + frame
 * painted by main.c's bg_attr/bg_paint -- the HUD does NOT own the background):
 *   - top border (row 0):    lives as hearts (left) + shields as dots (right),
 *                            bitmap sprites into BOTH display files;
 *   - bottom border (row 23): the SCORE is drawn as text by main.c (left); the
 *                            timer bar (middle) and the DASH gauge (right) are
 *                            attribute bars owned here.
 *
 * All HUD drawing targets BOTH display files (attrs 0x5800 + 0x7800; bitmaps via
 * the sprite path into screen A and B) so the page-flip never disturbs it. The
 * widgets cache their last-rendered state and redraw only on change.
 */
#ifndef HUD_H
#define HUD_H

#include "types.h"

/* Attribute byte: FLASH(7) BRIGHT(6) PAPER(5-3) INK(2-0). Shared with main.c. */
#define ATTR(bright, paper, ink) ((u8)(((bright) << 6) | ((paper) << 3) | (ink)))

/* Write one attribute cell to BOTH blocks (shows on whichever page is flipped
 * in). Lives in hud.c; main.c's fx/death/telegraph paths call it too. */
void put_attr(u8 row, u8 col, u8 v);

/* Build the HUD's pre-shifted heart sprite once at startup. */
void hud_init(void);

/* Force the next hud_draw_* to repaint regardless of the cache. Call after
 * scld_clear() / bg_paint() wipes the HUD area. */
void hud_invalidate(void);

/* Lives as hearts, top border left, both bitmaps. Cached. */
void hud_draw_lives(u8 lives);

/* Shields as dots, top border right, both bitmaps. Cached. */
void hud_draw_shields(u8 shields);

/* Timer bar (bottom border, right): paper-filled proportional to
 * frames_left/frames_total, both attribute blocks. Cached on bar length. */
void hud_draw_timer(u16 frames_left, u16 frames_total);

#endif /* HUD_H */
