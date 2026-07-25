/*
 * fx.c -- attribute-only effects: hit pops, death fireball, spawn telegraph.
 * See fx.h. Every restore path asks background.c's bg_attr() for the current
 * floor colour, so an effect never punches a hole in the generated background.
 */
#include "fx.h"
#include "background.h"    /* bg_attr + BORDER_* */
#include "scld.h"
#include "hud.h"           /* ATTR, put_attr */
#include "rng.h"
#include "sfx.h"           /* sfx_noise: the death crackle */
#include <z80.h>           /* z80_outp() for the ULA border */

/* ---- enemy-hit explosions: brief colour pops, NO game freeze ----
 * A small pool of timed attribute bursts. An enemy death spawns one at its cell;
 * each frame fx_render() repaints the 3x3 burst (white->yellow->red) and, when it
 * expires, restores those cells to the background. Cheap (a handful of cells). */
#define MAX_FX     6u
#define FX_FRAMES  6u
typedef struct { u8 cx, cy, t, shape; } fx_t;   /* shape: 0 full / 1 plus / 2 X */
static fx_t fx[MAX_FX];

void fx_clear(void)
{
    u8 i;
    for (i = 0; i < MAX_FX; i++) fx[i].t = 0u;
}

void fx_spawn(u8 x, u8 y)
{
    u8 i;
    for (i = 0; i < MAX_FX; i++) {
        if (fx[i].t == 0u) {
            u8 s = (u8)(rng_byte() & 3u);     /* random shape (3->0) */
            fx[i].cx    = (u8)(x >> 3);
            fx[i].cy    = (u8)(y >> 3);
            fx[i].t     = FX_FRAMES;
            fx[i].shape = (u8)((s == 3u) ? 0u : s);
            return;
        }
    }
}

static u8 fx_colour(u8 t)
{
    if (t >= 5u) return ATTR(1, 7, 0);   /* white  */
    if (t >= 3u) return ATTR(1, 6, 0);   /* yellow */
    return ATTR(1, 2, 0);                /* red    */
}

u8 fx_render(void)
{
    fx_t *f = fx;
    u8 any = 0u;
    u8 i;
    for (i = MAX_FX; i != 0u; i--, f++) {
        u8 colr, restore, shape, cx, cy, t;
        s8 dr, dc;
        t = f->t;
        if (t == 0u) {
            continue;
        }
        any = 1u;
        t--;
        f->t = t;
        restore = (u8)(t == 0u);                /* last frame -> restore bg */
        colr    = fx_colour((u8)(t + 1u));
        shape   = f->shape;
        cx      = f->cx;
        cy      = f->cy;
        for (dr = -1; dr <= 1; dr++) {
            s8 row = (s8)cy + dr;
            if (row < 0 || row > 23) continue;
            for (dc = -1; dc <= 1; dc++) {
                s8 c = (s8)cx + dc;
                u8 keep;
                if (c < 0 || c > 31) continue;
                if (shape == 1u) {
                    keep = (u8)(dr == 0 || dc == 0);                 /* plus  */
                } else if (shape == 2u) {
                    keep = (u8)((dr == 0 && dc == 0) || (dr != 0 && dc != 0)); /* X */
                } else {
                    keep = 1u;                                       /* full  */
                }
                if (!keep) {
                    continue;
                }
                put_attr((u8)row, (u8)c,
                         restore ? bg_attr((u8)row, (u8)c) : colr);
            }
        }
    }
    return any;
}

/* Paint one attribute cell of ONE attribute block (clipped). The death explosion
 * is a frozen scene (no page-flip), so it draws into only the displayed block --
 * half the writes of put_attr(), which touches both. Caller passes the shown
 * attribute base (scld_shown_attrs()). */
static void put_cell(u16 atbase, s8 col, s8 row, u8 v)
{
    if (col >= 0 && col < 32 && row >= 0 && row < 24) {
        ((u8 *)(uintptr_t)atbase)[(u16)row * 32u + (u16)col] = v;
    }
}

/*
 * Death animation: a single GROWING FIREBALL. A lumpy ball of fire expands from
 * the player's cell -- white-hot core, yellow glow, red shock edge -- with a
 * per-cell random jitter on the boundary, so it builds a different shape every
 * death (plus a random max size). Scene freezes; caller restores the arena.
 */
void death_anim(u8 px, u8 py)
{
    s8  cx   = (s8)(px >> 3);
    s8  cy   = (s8)(py >> 3);
    u8  seed = rng_byte();
    u8  maxR = (u8)(7u + (rng_byte() & 3u));        /* random size 7..10 */
    u16 sat  = scld_shown_attrs();                  /* draw into the shown block only */
    u8  f;

    for (f = 1u; f <= maxR; f++) {
        s8 lim = (s8)(f + 1);
        s8 dy, dx;
        for (dy = (s8)-lim; dy <= lim; dy++) {
            s8 row = (s8)(cy + dy);
            u8 ady;
            if (row < 0 || row > 23) continue;
            ady = (u8)(dy < 0 ? -dy : dy);
            for (dx = (s8)-lim; dx <= lim; dx++) {
                s8 col = (s8)(cx + dx);
                u8 adx, d, jit, fd, v;
                if (col < 0 || col > 31) continue;
                adx = (u8)(dx < 0 ? -dx : dx);
                d   = (u8)(adx > ady ? adx : ady);              /* blocky radius   */
                /* per-cell stable noise pushes the cell "outward" -> the WHOLE
                 * ball is lumpy (core + bands + edge), not a clean square.
                 * (adx*7 + ady*13) done with shifts+adds -- no sdcc multiply in
                 * this per-cell hot loop. */
                jit = (u8)(((u8)(((adx << 3) - adx)
                                 + ((ady << 3) + (ady << 2) + ady)) ^ seed) & 3u);
                fd  = (u8)(d + jit);
                if (fd > f) {
                    continue;                                   /* outside the ball */
                }
                if      (fd <= (u8)(f >> 1)) v = ATTR(1, 7, 0); /* white-hot core  */
                else if (fd <= (u8)(f - 1u)) v = ATTR(1, 6, 0); /* yellow glow     */
                else                         v = ATTR(1, 2, 0); /* red shock edge  */
                put_cell(sat, col, row, v);
            }
        }
        /* Death explosion sound, SYNCED with the growing fireball: the scene is
         * frozen here (only the fireball attrs draw) so the frame budget is free
         * -- a loud crackle every expansion frame. */
        sfx_noise();
        sfx_noise();
        z80_outp(0xFEu, (f & 1u) ? BORDER_RED : BORDER_BLACK);
        scld_wait();                                            /* music ticks from the IM2 ISR */
        if (f >= (u8)(maxR - 1u)) {
            z80_outp(0xFEu, (f & 1u) ? BORDER_BLACK : BORDER_RED);
            scld_wait();                                        /* brief hold full */
        }
    }
    z80_outp(0xFEu, BORDER_BLACK);
}

/* ---- spawn telegraph: one quick warning blink where the next wave appears ----
 * Enemies are inert/invisible for this short warning, then pop in. */
void telegraph_blink(const enemies_t *es, u8 tk)
{
    const enemy_t *e = es->e;
    u8 i, on = (u8)((tk & 8u) != 0u);    /* toggle every 8 frames */
    for (i = MAX_ENEMIES; i != 0u; i--, e++) {
        if (e->alive) {
            u8 row = (u8)(e->y >> 3), col = (u8)(e->x >> 3);
            put_attr(row, col, on ? ATTR(0, 2, 7) : bg_attr(row, col));  /* soft red */
        }
    }
}

void telegraph_clear(const enemies_t *es)
{
    const enemy_t *e = es->e;
    u8 i;
    for (i = MAX_ENEMIES; i != 0u; i--, e++) {
        if (e->alive) {
            u8 row = (u8)(e->y >> 3), col = (u8)(e->x >> 3);
            put_attr(row, col, bg_attr(row, col));
        }
    }
}
