/*
 * fx.h -- attribute-only visual effects: hit pops, the death fireball, and the
 *         spawn telegraph.
 *
 * Everything here paints ATTRIBUTE cells, never bitmap, which is what makes it
 * cheap enough to run inside the frame budget: a handful of cells per effect
 * instead of a sprite blit. Every cell that stops being an effect is restored
 * from bg_attr(), so effects never leave holes in the generated floor.
 *
 * Two of these deliberately BLOCK: death_anim() freezes the scene and paces
 * itself on scld_wait(), and so does the caller-side game-over flash. That is
 * safe because the music keeps ticking from the 50 Hz IM2 ISR.
 */
#ifndef FX_H
#define FX_H

#include "types.h"
#include "enemy.h"

/* ---- enemy/player hit pops ------------------------------------------------
 * A small pool of timed attribute bursts. A death or a wound spawns one at the
 * object's cell; fx_render() repaints the 3x3 burst (white->yellow->red) each
 * frame and restores the background when it expires. */

/* Drop every live effect without restoring cells. Use when the caller is about
 * to repaint the whole arena anyway (post-death re-init). */
void fx_clear(void);

/* Start a burst at pixel (x,y). Silently ignored when the pool is full. */
void fx_spawn(u8 x, u8 y);

/* Advance and repaint every live burst. Returns 1 if any effect is on screen
 * (the caller uses that to drive the synced crackle). Call once per frame. */
u8 fx_render(void);

/* ---- death animation ------------------------------------------------------
 * A single growing fireball at the player's cell -- white-hot core, yellow
 * glow, red shock edge, with per-cell jitter so every death looks different.
 * The scene FREEZES (no page-flip) and the explosion crackle plays in sync.
 * The caller must repaint the arena afterwards (bg_paint + HUD). */
void death_anim(u8 px, u8 py);

/* ---- spawn telegraph ------------------------------------------------------
 * One quick warning blink where the next wave will appear; the enemies are
 * inert and invisible for this long, then pop in. */
#define TELEGRAPH_FRAMES 16u

/* Blink the cells under the (inert) wave. `tk` is the countdown value. */
void telegraph_blink(const enemies_t *es, u8 tk);

/* Restore those cells to the background, just before the enemies appear. */
void telegraph_clear(const enemies_t *es);

#endif /* FX_H */
