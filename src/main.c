/*
 * Twin-Stick Shooter for Timex TC2048 / ZX Spectrum -- main.c
 *
 * The full gameplay layer wired into the 50 Hz loop (spec §3.5/§5-§9):
 *   - player ship: 8x8 sprite, scheme-driven move + energy boost; visual recoil
 *                  + muzzle flash on each shot (render-only, no physics kick)
 *   - enemies:     8x8 sprites, wave-driven mix (bounce/chase/hunter) spawned
 *                  per the 16-wave difficulty table (enemies_spawn(es, g.wave))
 *   - bullets:     fired 8-way, pooled, fire cooldown; each shot costs points
 *   - wave timer:  per-wave frame countdown; early clear pays a (seconds*10)
 *                  bonus; expiry carries no penalty (the wave just continues)
 *   - economy:     BCD score (score.c) -- +points per kill, -5 fire, -10 shield
 *                  hit, -100 death; crossing each 10,000 grants an extra life
 *   - state:       a single game_state_t g {wave, score, lives, shields}; on a
 *                  wave clear g.wave++ advances the difficulty index
 *   - game over:   lives==0 -> a GAME OVER screen with final score + wave;
 *                  FIRE/SPACE resumes from the death wave, Q starts a fresh game
 *   - sound:       1-bit beeper SFX on shoot/explode/hit/death/extra-life/bonus
 *   - HUD:         lives + shields (top), timer + boost bars (bottom), and the
 *                  SCORE rendered as big attribute-cell digits behind the action
 *   - rendering:   incremental erase+redraw into the hidden buffer, page-flip
 *
 * All hardware (port 0xFF, screen/attr addresses) lives in scld.c; the loop only
 * asks for the back buffer, blits into it, and presents. The score-digit
 * background uses dark paper on lit cells so the white-ink sprites stay readable
 * (design D1); every cell-restore path (fx/death/telegraph) asks the HUD for the
 * current background colour so explosions never punch holes in the score.
 */

#include "scld.h"
#include "sprite.h"
#include "sprites.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "collision.h"
#include "controls.h"
#include "arena.h"
#include "rng.h"
#include "score.h"
#include "sfx.h"           /* sfx_play + SFX_* ids (shoot/explode/hit/death/...) */
#include "music.h"         /* AY chiptune (auto-detected; no-op without an AY)   */
#include "hud.h"           /* ATTR macro, put_attr, score_cell_attr, HUD widgets */
#include "types.h"
#include <z80.h>          /* z80_outp() for the ULA border */
#include <string.h>       /* memset (game-over fills) */
#include <input.h>        /* in_key_pressed + IN_KEY_SCANCODE_* (title/game-over) */

#define INVULN_FRAMES 50u /* ~1s of i-frames after a hit (ship blinks)         */

/* Max objects drawn in one frame: player + enemies + bullets + muzzle flash. */
#define MAX_DRAW (1u + MAX_ENEMIES + MAX_BULLETS + 1u)

/* Frames between shots — keeps the bullet/sprite load inside the ~50 Hz budget. */
#define FIRE_COOLDOWN 8u

#define PLAYER_START_X 128u
#define PLAYER_START_Y 96u

/* Visual recoil (spec §3.5): how many frames the ship draws kicked-back. */
#define RECOIL_FRAMES 3u
#define RECOIL_PIXELS 2u

/* Largest u8 wave we let the difficulty index climb to. enemies_spawn() loops
 * at the wave-16 settings for anything >16, so this is only an anti-wrap guard
 * (a player reaching ~200 endless waves never needs the counter to overflow). */
#define WAVE_MAX 200u

#define KIND_SPRITE 0u   /* full 8x8 sprite (player, enemy) */
#define KIND_BULLET 1u   /* cheap 3x3 dot                   */
typedef struct { u8 x, y, kind; } cell_t;

/* Per-buffer record of what was drawn last time that buffer was the back one. */
static cell_t prev[2][MAX_DRAW];
static u8     prevn[2];

/* Pre-shifted sprite tables (built once at startup from the 8-byte source art).
 * Bullets/thruster are not sprites -- they use the cheap bul_draw/bul_erase. */
static u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];    /* 8 directional ship frames */
static u8 ps_enemy[SPR_PRESHIFT_SIZE];          /* level 0 bouncer (all-dir) */
static u8 ps_enemy_vbounce[SPR_PRESHIFT_SIZE];  /* level 4 vertical bouncer  */
static u8 ps_enemy_hbounce[SPR_PRESHIFT_SIZE];  /* level 5 horizontal bouncer*/
static u8 ps_enemy_chase[SPR_PRESHIFT_SIZE];    /* level 2 chaser  */
static u8 ps_enemy_hunter[SPR_PRESHIFT_SIZE];   /* level 3 hunter  */
/* (the HUD life-heart pre-shift table now lives in hud.c) */

/* Pick the pre-shifted table for an enemy's behaviour level. Level 1 is unused,
 * so it deliberately aliases the default bouncer sprite. */
#if ENEMY_BOUNCE_H != 5
#error "enemy_sprite_by_level assumes enemy levels 0..5; update the table"
#endif
static const u8 * const enemy_sprite_by_level[] = {
    ps_enemy, ps_enemy, ps_enemy_chase, ps_enemy_hunter,
    ps_enemy_vbounce, ps_enemy_hbounce
};

#define ENEMY_SPRITE(level_) \
    (((level_) <= ENEMY_BOUNCE_H) ? enemy_sprite_by_level[(level_)] : ps_enemy)

/* Wave time budget in frames for the active wave. Mirrors enemies_spawn()'s
 * index clamp (1-based wave; wave==0 -> wave 1; >16 loops at index 15) so the
 * timer length always matches the wave that was actually spawned. */
static u16 wave_time_frames(u8 wave)
{
    u8 idx;
    if (wave <= 1u)       idx = 0u;
    else if (wave >= 17u) idx = 15u;
    else                  idx = (u8)(wave - 1u);
    return wave_table[idx].time_frames;
}

/* sign of a -1/0/+1 step (already normalised, returned as-is). */
static s8 step_sign(s8 v)
{
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}

/* Background attribute for one cell: a bright-magenta frame all the way around
 * the screen (rows 0/23 + cols 0/31), with WHITE ink so the HUD text/sprites
 * drawn on the frame stay readable, over a blue/black checker floor. Used both
 * to paint the arena and to RESTORE a cell after an explosion / telegraph pulse.
 * (put_attr() + the ATTR macro are shared from hud.h.) */
static u8 bg_attr(u8 row, u8 col)
{
    if (row == 0u || row == 23u || col == 0u || col == 31u) {
        return ATTR(1, 3, 7);          /* frame: bright magenta, white ink    */
    }
    if ((u8)((row + col) & 1u)) {
        return ATTR(0, 1, 7);          /* dark-blue checker                   */
    }
    return ATTR(0, 0, 7);              /* black checker                       */
}

/* Paint the whole arena into BOTH attribute blocks (identical on both screens,
 * so the page-flip never disturbs colour). */
static void bg_paint(void)
{
    u8 *a = (u8 *)SCLD_ATTRS_A;
    u8 *b = (u8 *)SCLD_ATTRS_B;
    u8  row, col;
    u16 i = 0;
    for (row = 0; row < 24u; row++) {
        for (col = 0; col < 32u; col++, i++) {
            u8 v = bg_attr(row, col);
            a[i] = v;
            b[i] = v;
        }
    }
}

/* ---- enemy-hit explosions: brief colour pops, NO game freeze ----
 * A small pool of timed attribute bursts. An enemy death spawns one at its cell;
 * each frame fx_render() repaints the 3x3 burst (white->yellow->red) and, when it
 * expires, restores those cells to the background. Cheap (a handful of cells). */
#define MAX_FX     6u
#define FX_FRAMES  6u
typedef struct { u8 cx, cy, t, shape; } fx_t;   /* shape: 0 full / 1 plus / 2 X */
static fx_t fx[MAX_FX];

static void fx_clear(void)
{
    u8 i;
    for (i = 0; i < MAX_FX; i++) fx[i].t = 0u;
}

static void fx_spawn(u8 x, u8 y)
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

static u8 fx_render(void)
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

static u8 enemies_alive_count(const enemies_t *es)
{
    const enemy_t *e = es->e;
    u8 i, n = 0u;
    for (i = MAX_ENEMIES; i != 0u; i--, e++) {
        if (e->alive) {
            n++;
        }
    }
    return n;
}

/*
 * Death animation: a fast attribute KABOOM. The scene freezes on the displayed
 * buffer; one of three random styles plays, painting only the cells it needs
 * (cheap/snappy). Caller restores the arena afterwards (hud_paint_background).
 */
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
static void death_anim(u8 px, u8 py)
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
        scld_wait();
        music_tick();                                           /* keep music alive */
        if (f >= (u8)(maxR - 1u)) {
            scld_wait();                                        /* brief hold full */
            music_tick();
        }
    }
}

/* ---- spawn telegraph: gently pulse the cells where the next wave appears ----
 * For ~TELEGRAPH_FRAMES the enemies are inert and invisible; their spawn cells
 * blink, then they pop in. Gives the player a moment + a warning. */
#define TELEGRAPH_FRAMES 80u

static void telegraph_blink(const enemies_t *es, u8 tk)
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

static void telegraph_clear(const enemies_t *es)
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

/* The top-border lives/shields HUD now lives in hud.c
 * (hud_draw_lives / hud_draw_shields). */

/* Whole-screen red/white flash on GAME OVER (attributes only). */
static void game_over_flash(void)
{
    u8 k, d;
    for (k = 0; k < 8u; k++) {
        u8 v = (k & 1u) ? ATTR(1, 2, 0) : ATTR(1, 7, 0);
        memset((u8 *)SCLD_ATTRS_A, v, SCLD_ATTRS_LEN);
        memset((u8 *)SCLD_ATTRS_B, v, SCLD_ATTRS_LEN);
        d = 6;
        while (d--) { scld_wait(); music_tick(); }
    }
}

/* ---- title screen: game name, control-scheme menu, copyright ----
 * Text is blitted from the 8x8 ROM character set (CHARS = 0x3C00 + code*8) into
 * screen A's bitmap. Only screen A is shown here (no page-flip), so one static
 * text draw plus a few per-frame attribute-row highlights is the whole cost.
 */
static void put_char(u16 base, u8 col, u8 row, u8 ch)
{
    const u8 *g = (const u8 *)(uintptr_t)(0x3C00u + (u16)ch * 8u);
    u8 r;
    for (r = 0; r < 8u; r++) {
        scld_scanline(base, (u8)((u16)row * 8u + r))[col] = g[r];
    }
}

static void put_text(u16 base, u8 col, u8 row, const char *s)
{
    while (*s != '\0') {
        put_char(base, col, row, (u8)*s);
        col++;
        s++;
    }
}

/* Render the 6 BCD score digits left-to-right at (col,row) of BOTH bitmaps. */
static void put_score_digits(u8 col, u8 row, const score_t *s)
{
    u8 i;
    for (i = 0; i < 6u; i++) {
        u8 ch = (u8)('0' + s->digits[i]);
        put_char(SCLD_SCREEN_A, (u8)(col + i), row, ch);
        put_char(SCLD_SCREEN_B, (u8)(col + i), row, ch);
    }
}

/* HUD score at (col 1, row 23), redrawing ONLY the digits that changed since the
 * last call (cache) -- so a per-shot -5 doesn't repaint all six glyphs every
 * shot. Pass force=1 after a scld_clear wiped the score bitmap (init/respawn). */
static u8 g_score_cache[6] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };

static void hud_score(const score_t *s, u8 force)
{
    u8 i;
    for (i = 0; i < 6u; i++) {
        if (force || s->digits[i] != g_score_cache[i]) {
            g_score_cache[i] = s->digits[i];
            put_char(SCLD_SCREEN_A, (u8)(1u + i), 23u, (u8)('0' + s->digits[i]));
            put_char(SCLD_SCREEN_B, (u8)(1u + i), 23u, (u8)('0' + s->digits[i]));
        }
    }
}

/* Render a u8 (0..255) as up to 3 right-aligned decimal chars into BOTH bitmaps.
 * Only /10 and /100 on a small u8 — cheap, no runtime-value division. */
static void put_u8(u8 col, u8 row, u8 v)
{
    u8 h = (u8)(v / 100u);
    u8 t = (u8)((v / 10u) % 10u);
    u8 o = (u8)(v % 10u);
    if (h) {
        put_char(SCLD_SCREEN_A, col, row, (u8)('0' + h));
        put_char(SCLD_SCREEN_B, col, row, (u8)('0' + h));
    }
    if (h || t) {
        put_char(SCLD_SCREEN_A, (u8)(col + 1), row, (u8)('0' + t));
        put_char(SCLD_SCREEN_B, (u8)(col + 1), row, (u8)('0' + t));
    }
    put_char(SCLD_SCREEN_A, (u8)(col + 2), row, (u8)('0' + o));
    put_char(SCLD_SCREEN_B, (u8)(col + 2), row, (u8)('0' + o));
}

/* put_text into BOTH bitmaps (so the page-flip's current page shows it). */
static void put_text_both(u8 col, u8 row, const char *s)
{
    put_text(SCLD_SCREEN_A, col, row, s);
    put_text(SCLD_SCREEN_B, col, row, s);
}

/* Dash readiness: a single dot on the top frame (col 15) -- bright GREEN when
 * the dash is ready (dash_cd==0), plain magenta frame while charging. Attribute
 * only + cached (one put_attr on a state change), so the common frame pays
 * nothing. (The earlier ">" arrows via put_char dragged the whole game.) */
static u8 g_dash_dot = 0xFFu;     /* reset (=0xFF) wherever the HUD is invalidated */

static void hud_dash_dot(u8 dash_cd)
{
    u8 rdy = (u8)(dash_cd == 0u);
    if (rdy == g_dash_dot) {
        return;                                       /* no state change */
    }
    g_dash_dot = rdy;
    put_attr(0u, 15u, (u8)(rdy ? ATTR(1, 4, 0)        /* green dot: ready */
                               : ATTR(1, 3, 7)));     /* frame: charging  */
}

/*
 * GAME OVER screen (spec §7). Flashes, then shows the final score + the wave the
 * player died on, and waits for a choice:
 *   FIRE / SPACE -> resume from the death wave  (returns 0)
 *   Q            -> fresh game from wave 1       (returns 1)
 * Drawn into BOTH bitmaps + both attribute blocks, so it reads regardless of
 * which page the last page-flip left visible (we don't flip while waiting).
 * Caller does the game_state reset + re-init.
 */
static u8 game_over_screen(const game_state_t *g, u8 death_wave)
{
    game_over_flash();

    scld_clear(SCLD_SCREEN_A);
    scld_clear(SCLD_SCREEN_B);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);   /* white on black */
    memset((u8 *)SCLD_ATTRS_B, ATTR(0, 0, 7), SCLD_ATTRS_LEN);

    put_text_both(11,  6, "GAME OVER");
    put_text_both( 7, 10, "SCORE");
    put_score_digits(13, 10, &g->score);
    put_text_both( 7, 12, "WAVE");
    put_u8(13, 12, death_wave);
    put_text_both( 3, 17, "FIRE/SPACE  RESUME WAVE");
    put_u8(27, 17, death_wave);
    put_text_both( 3, 19, "Q           NEW GAME");

    for (;;) {
        intent_t in;
        input_read(DIR_NONE, &in);    /* scheme-agnostic read */
        /* CONTINUE on the FIRE button or SPACE. In the twin-stick schemes the
         * FIRE button maps to BOOST (scheme A/C) or fire-in-heading (scheme B),
         * so accept either intent here, plus SPACE directly. */
        if (in.boost || in.fire || in_key_pressed(IN_KEY_SCANCODE_SPACE)) {
            return 0u;                 /* resume from the death wave, score 0 */
        }
        if (in_key_pressed(IN_KEY_SCANCODE_q)) {
            return 1u;                 /* fresh game */
        }
        scld_wait();
        music_tick();                  /* music continues on the game-over screen */
    }
}

/* Fill one 32-cell attribute row of screen A (title highlights). */
static void title_attr_row(u8 row, u8 v)
{
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)row * 32u;
    u8  c;
    for (c = 0; c < 32u; c++) {
        a[c] = v;
    }
}

/* ---- title shine-sweep: diagonal glint across "ATTRIBUTE WARS" ----
 *
 * The title text occupies row 3, cols 9..22 (14 characters: "ATTRIBUTE WARS").
 * The sweep diagonal value for cell (col, row) is (col + row).  With a single
 * title row (row=3) the range is (9+3)=12 .. (22+3)=25.
 *
 * sweep position `s` advances each frame over that range, then pauses for
 * SHINE_PAUSE frames before wrapping back to the start.
 *
 * For each title cell the brightness relative to `s` is:
 *   d = (col + 3) - s
 *   d == 0  -> BRIGHT WHITE ink (on the sweep line)
 *   d == 1  -> mid shade (cyan, bright) — the trailing cell
 *   else    -> base colour (bright cyan)
 */
#define SHINE_COL_START  9u   /* first col of "ATTRIBUTE WARS"                 */
#define SHINE_COL_END   22u   /* last  col of "ATTRIBUTE WARS" (14 chars)      */
#define SHINE_ROW        3u   /* the title character row                        */
/* diagonal range: (col+row) for col in [9..22], row=3 → [12..25] */
#define SHINE_S_MIN     12u   /* (SHINE_COL_START + SHINE_ROW)                 */
#define SHINE_S_MAX     25u   /* (SHINE_COL_END   + SHINE_ROW)                 */
#define SHINE_PAUSE     60u   /* frames to hold before restarting the sweep    */

/* Paint the title row attribute cells for the current sweep position. */
static void title_shine(u8 s)
{
    u8 col;
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)SHINE_ROW * 32u;
    for (col = SHINE_COL_START; col <= SHINE_COL_END; col++) {
        u8 diag = (u8)(col + SHINE_ROW);   /* == col + 3 */
        u8 attr;
        if (diag == s) {
            attr = ATTR(1, 0, 7);          /* BRIGHT WHITE ink — on the glint  */
        } else if (diag == (u8)(s + 1u)) {
            attr = ATTR(1, 0, 6);          /* BRIGHT YELLOW — trailing cell     */
        } else {
            attr = ATTR(1, 0, 5);          /* bright cyan — base title colour   */
        }
        a[col] = attr;
    }
}

/* Draw the menu, poll keys 1/2/3 (pick a control scheme) and 0 (start).
 * Returns the chosen CTRL_* scheme. */
static u8 title_screen(void)
{
    u8 sel = CTRL_KEMPSTON_MOVE;
    /* Sweep state: s is the current diagonal position; pause counts down between
     * passes. Start s at SHINE_S_MIN so the glint enters from the left edge. */
    u8 s     = SHINE_S_MIN;
    u8 pause = 0u;

    scld_clear(SCLD_SCREEN_A);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);   /* white on black */

    put_text(SCLD_SCREEN_A,  9,  3, "ATTRIBUTE WARS");
    put_text(SCLD_SCREEN_A,  2,  8, "1 KEMPSTON MOVE  KEYS FIRE");
    put_text(SCLD_SCREEN_A,  2, 10, "2 KEYS MOVE  KEMPSTON FIRE");
    put_text(SCLD_SCREEN_A,  2, 12, "3 TWO JOYSTICKS (TS2068)");
    put_text(SCLD_SCREEN_A,  2, 15, "0 START GAME");
    /* Music credit. The AY tune "Spectrumizer" is by Pator (@paatorr on X).
     * (Plays only on AY machines; the credit shows on every machine.) */
    put_text(SCLD_SCREEN_A,  3, 18, "MUSIC: PATOR  @PAATORR ON X");
    put_text(SCLD_SCREEN_A,  3, 22, "(C) 2026 ANTHROPIC, INC.");
    put_text(SCLD_SCREEN_A,  3, 23, "(C) 2026 MICHAL PASTERNAK");

    title_attr_row(3, ATTR(1, 0, 5));      /* bright-cyan title (base colour) */

    for (;;) {
        /* highlight the selected scheme bright yellow; START is bright green */
        title_attr_row( 8, (sel == CTRL_KEMPSTON_MOVE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(10, (sel == CTRL_KEMPSTON_FIRE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(12, (sel == CTRL_DUAL_STICK)    ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(15, ATTR(1, 0, 4));

        /* ---- shine sweep: paint title-letter row (row 3) only --------------- */
        title_shine(s);

        /* Advance (or pause) the sweep position */
        if (pause > 0u) {
            pause--;
            if (pause == 0u) {
                s = SHINE_S_MIN;           /* restart the sweep */
            }
        } else {
            if (s < SHINE_S_MAX) {
                s++;
            } else {
                pause = SHINE_PAUSE;       /* glint exited right — begin pause  */
            }
        }

        if      (in_key_pressed(IN_KEY_SCANCODE_1)) sel = CTRL_KEMPSTON_MOVE;
        else if (in_key_pressed(IN_KEY_SCANCODE_2)) sel = CTRL_KEMPSTON_FIRE;
        else if (in_key_pressed(IN_KEY_SCANCODE_3)) sel = CTRL_DUAL_STICK;
        else if (in_key_pressed(IN_KEY_SCANCODE_0)) break;

        scld_wait();
        music_tick();                  /* keep the menu tune running */
    }
    return sel;
}

int main(void)
{
    player_t  player;
    bullets_t bullets;
    enemies_t enemies;
    intent_t  in;
    /* Single source of truth for the run: wave (difficulty index), BCD score,
     * lives, shields. game_new() seeds it; the HUD reads g.score for its
     * big-attribute-digit background. */
    static game_state_t g;
    u8        i;
    u8        cooldown = 0;
    u8        tick     = 0;            /* frame counter (thruster flicker etc.)  */
    u8        invuln   = 0;            /* i-frames after a hit                    */
    u8        bullet_count = 0;        /* active bullet slots, avoids empty scans */
    u8        enemy_count = 0;         /* live enemies in the current wave        */
    u8        spawn_timer = TELEGRAPH_FRAMES;  /* telegraph the opening wave      */

    /* ---- per-wave clock (spec §5.3): counts down once enemies are active. ----
     * wave_total is the wave's full budget (for the proportional HUD bar);
     * wave_secs is the last second shown on the bar, so we redraw the timer only
     * when the displayed second actually changes (not every frame). */
    u16       wave_timer = 0u;
    u16       wave_total = 0u;
    u16       wave_secs  = 0xFFFFu;    /* force the first timer draw             */

    /* ---- visual recoil (spec §3.5): render-only, no physics. On a shot we set
     * recoil_timer and remember the aim dir; the player-draw offsets the ship
     * 1px opposite the aim and flashes a muzzle dot one cell ahead. ---- */
    static u8 recoil_timer = 0u;
    static s8 recoil_dx    = 0;
    static s8 recoil_dy    = 0;

    scld_init(0x07u);                 /* clears both buffers, IM1+EI, shows A   */
    z80_outp(0xFEu, 0x00u);           /* black ULA border (title + arena)       */
    rng_seed(0xACE1u);
    music_init();                     /* probe AY; load+start the tune (silent  */
                                      /* no-op on a beeper-only machine)        */

    { u8 d; for (d = 0; d < 8u; d++) spr_preshift(ps_ship_dir[d], spr_ship_dir[d]); }
    spr_preshift(ps_enemy,         spr_enemy);  /* build pre-shifted tables once */
    spr_preshift(ps_enemy_vbounce, spr_enemy_vbounce);
    spr_preshift(ps_enemy_hbounce, spr_enemy_hbounce);
    spr_preshift(ps_enemy_chase,   spr_enemy_chase);
    spr_preshift(ps_enemy_hunter,  spr_enemy_hunter);
    hud_init();                                  /* build the HUD heart sprite */

    /* Title + control-scheme menu; the choice drives input_read() all game. */
    input_set_scheme(title_screen());

    scld_clear(SCLD_SCREEN_A);        /* wipe the title text off both buffers   */
    scld_clear(SCLD_SCREEN_B);
    game_new(&g);                     /* wave 1, score 0, START_LIVES/SHIELDS   */
    bg_paint();                       /* checker + frame into both attr blocks  */
    hud_score(&g.score, 1u);          /* full score draw after the clear        */

    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
    bullets_init(&bullets);
    enemies_spawn(&enemies, g.wave);
    enemy_count = enemies_alive_count(&enemies);
    wave_total = wave_time_frames(g.wave);   /* full bar; clock starts after the
                                              * telegraph (when enemies go live) */
    wave_timer = wave_total;
    hud_invalidate();                 /* force first widget paint               */
                    g_dash_dot = 0xFFu;
    hud_draw_lives(g.lives);
    hud_draw_shields(g.shields);
    prevn[0] = 0;
    prevn[1] = 0;

    for (;;) {
        u16 back;
        u8  bi, n;

        tick++;

        /* ---- input + player ---- */
        input_read(player.facing, &in);
        player_update(&player, &in);

        if (cooldown) {
            cooldown--;
        }
        if (recoil_timer) {
            recoil_timer--;
        }
        if (in.fire && cooldown == 0) {
            if (bullet_spawn(&bullets, player.x, player.y,
                             in.aim_dx, in.aim_dy) >= 0) {
                bullet_count++;
                cooldown = FIRE_COOLDOWN;
                /* economy + sound + recoil — only on a real shot (spec §3.5/§4/§8) */
                score_sub(&g.score, 5u);
                hud_score(&g.score, 0u);
                sfx_play(SFX_SHOOT);             /* ~1ms click, safe in-loop      */
                recoil_timer = RECOIL_FRAMES;    /* kick the ship back ~2 frames  */
                recoil_dx    = in.aim_dx;        /* store aim for the draw offset */
                recoil_dy    = in.aim_dy;
            }
        }
        if (bullet_count) {
            bullets_update(&bullets);
            bullet_count = bullets_count(&bullets);
        }

        if (invuln) {
            invuln--;
        }

        if (spawn_timer > 0u) {
            /* ---- spawn telegraph: enemies inert+invisible; cells pulse ---- */
            spawn_timer--;
            telegraph_blink(&enemies, tick);
            if (spawn_timer == 0u) {
                telegraph_clear(&enemies);      /* restore before they appear */
            }
        } else {
            /* ---- wave clock: counts down once enemies are active (spec §5.3).
             * Expiry is a non-event (no penalty, no bonus) -- the wave just runs
             * on until cleared, so we only stop the counter at 0. ---- */
            if (wave_timer > 0u) {
                wave_timer--;
            }

            /* ---- enemies act; bullets destroy them (hit-pop + points per kill) ---- */
            enemies_update(&enemies, player.x, player.y, &bullets);
            /* A bullet is the ONLY way collide kills an enemy, so on the common
             * bulletless frame the whole snapshot/collide/rescan is a no-op --
             * skip it. When bullets are live, snapshot the alive flags, run
             * collide, and only rescan for kills if it actually reported any. */
            {
                if (bullet_count) {
                    u8 kill_mask = 0u;
                    u8 bit = 1u;
                    u8 kills;
                    kills = collide_bullets_enemies_mask(&bullets, &enemies, &kill_mask);
                    if (kills) {
                        enemy_t *e = enemies.e;
                        u8 scored = 0u;
                        bullet_count = (u8)(bullet_count - kills);
                        enemy_count = (u8)(enemy_count - kills);
                        bit = 1u;
                        for (i = MAX_ENEMIES; i != 0u; i--, e++) {
                            if (kill_mask & bit) {
                                u8 xl = score_add(&g.score,
                                          score_enemy_points(e->level));
                                g.lives = (u8)(g.lives + xl);  /* extra life(s)  */
                                if (xl) {
                                    sfx_play(SFX_EXTRA_LIFE);
                                    hud_draw_lives(g.lives);
                                }
                                fx_spawn(e->x, e->y);
                                scored = 1u;
                            }
                            bit = (u8)(bit << 1);
                        }
                        if (scored) {
                            hud_score(&g.score, 0u);           /* points landed   */
                        }
                    }
                }
            }

            if (enemy_count == 0u) {
                /* ---- WAVE CLEARED: early-clear bonus = (seconds left)*10 (§5.3) */
                u16 bonus = (u16)((wave_timer / 50u) * 10u);
                if (bonus) {
                    u8 xl = score_add(&g.score, bonus);
                    g.lives = (u8)(g.lives + xl);
                    if (xl) {
                        sfx_play(SFX_EXTRA_LIFE);
                        hud_draw_lives(g.lives);
                    }
                    sfx_play(SFX_BONUS);          /* longer tone; we're between
                                                   * waves so blocking is fine    */
                    hud_score(&g.score, 0u);
                }
                /* advance the difficulty index (capped so the u8 never wraps) */
                if (g.wave < (u8)WAVE_MAX) {
                    g.wave++;
                }
                enemies_spawn(&enemies, g.wave);    /* next wave (telegraphed)    */
                enemy_count = enemies_alive_count(&enemies);
                wave_total = wave_time_frames(g.wave);
                wave_timer = wave_total;
                wave_secs  = 0xFFFFu;               /* force a timer redraw       */
                spawn_timer = TELEGRAPH_FRAMES;
            } else if (invuln == 0u && player_hit(player.x, player.y, &enemies)) {
                fx_spawn(player.x, player.y);        /* hit pop at the ship */
                if (g.shields > 0u) {
                    /* ---- a shield absorbs the hit (spec §4/§8): -10 + click ---- */
                    g.shields--;
                    score_sub(&g.score, 10u);
                    sfx_play(SFX_HIT);               /* short, in-loop            */
                    hud_score(&g.score, 0u);
                    invuln = INVULN_FRAMES;
                    hud_draw_shields(g.shields);
                } else {
                    /* ---- shields gone -> DEATH: -100, KABOOM with the explosion
                     * crackle playing DURING the death animation (inside
                     * death_anim, synced with the growing fireball). ---- */
                    score_sub(&g.score, 100u);
                    death_anim(player.x, player.y);
                    if (g.lives > 0u) {
                        g.lives--;
                    }
                    if (g.lives == 0u) {
                        /* ---- GAME OVER (spec §7): show score + wave, then offer
                         * FIRE/SPACE resume-from-death-wave or Q fresh game. ---- */
                        u8 death_wave = g.wave;
                        if (game_over_screen(&g, death_wave) == 0u) {
                            game_resume_from_wave(&g, death_wave);  /* score 0, keep wave */
                        } else {
                            game_new(&g);                            /* wave 1     */
                        }
                    } else {
                        g.shields = START_SHIELDS;    /* new life, full shields    */
                    }
                    scld_clear(SCLD_SCREEN_A);        /* wipe the death-anim AND the */
                    scld_clear(SCLD_SCREEN_B);        /* GAME OVER text off both pages */
                    bg_paint();                       /* repaint checker + frame    */
                    hud_score(&g.score, 1u);          /* full score after the clear */
                    fx_clear();
                    prevn[0] = 0; prevn[1] = 0;
                    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
                    enemies_spawn(&enemies, g.wave);  /* re-init the current wave  */
                    enemy_count = enemies_alive_count(&enemies);
                    wave_total = wave_time_frames(g.wave);
                    wave_timer = wave_total;          /* fresh clock for the wave  */
                    wave_secs  = 0xFFFFu;
                    spawn_timer = TELEGRAPH_FRAMES;   /* telegraph the respawn     */
                    hud_invalidate();                 /* bitmaps + bars were wiped  */
                    g_dash_dot = 0xFFu;
                    hud_draw_lives(g.lives);
                    hud_draw_shields(g.shields);
                    cooldown = 0;
                    recoil_timer = 0u;
                    invuln = INVULN_FRAMES;
                }
            }
        }

        /* ---- render into the hidden buffer ---- */
        back = scld_back();
        bi   = scld_back_page();

        {                                        /* erase this buffer's last frame */
            cell_t *p = prev[bi];
            for (i = prevn[bi]; i != 0u; i--, p++) {
                if (p->kind == KIND_BULLET) {
                    BUL_ERASE(back, p->x, p->y);
                } else {
                    SPR_ERASE(back, p->x, p->y);
                }
            }
        }

        n = 0;
        {
            cell_t *out = prev[bi];

            {                                                    /* player ship  */
                u8 fdir = (u8)((player.facing < 8u) ? player.facing : 0u); /* NONE->N */
                if (!invuln || (tick & 2u)) {            /* blink while invuln */
                    u8 dx = player.x;
                    u8 dy = player.y;
                    /* ---- visual recoil (spec §3.5): on a recent shot, draw the ship
                     * 1px opposite the aim (and flash a muzzle dot ahead). Player
                     * state is untouched; we just record the drawn position so the
                     * incremental erase still matches what landed. ---- */
                    if (recoil_timer) {
                        s8 sx = step_sign(recoil_dx);
                        s8 sy = step_sign(recoil_dy);
                        /* opposite aim; y stays 0..184, x has enough arena margin. */
                        if (sx > 0)       dx = (u8)(player.x - RECOIL_PIXELS);
                        else if (sx < 0)  dx = (u8)(player.x + RECOIL_PIXELS);
                        if (sy > 0 && player.y >= RECOIL_PIXELS) {
                            dy = (u8)(player.y - RECOIL_PIXELS);
                        } else if (sy < 0 && player.y <= (u8)(184u - RECOIL_PIXELS)) {
                            dy = (u8)(player.y + RECOIL_PIXELS);
                        }
                    }
                    SPR_DRAW(back, dx, dy, ps_ship_dir[fdir]);
                    out->x = dx; out->y = dy;
                    out->kind = KIND_SPRITE; out++; n++;

                    /* muzzle flash: one bright dot a cell ahead in the aim dir. */
                    if (recoil_timer && (recoil_dx || recoil_dy)) {
                        u8 mx = (u8)(player.x + (u8)(step_sign(recoil_dx) * 8));
                        s16 my = (s16)player.y + (s16)(step_sign(recoil_dy) * 8);
                        if (my >= 0 && my <= 184) {
                            BUL_DRAW(back, mx, (u8)my);
                            out->x = mx; out->y = (u8)my;
                            out->kind = KIND_BULLET; out++; n++;
                        }
                    }
                }
            }
            if (spawn_timer == 0u) {                               /* enemies */
                const enemy_t *e = enemies.e;
                for (i = MAX_ENEMIES; i != 0u; i--, e++) {
                    if (e->alive) {
                        SPR_DRAW(back, e->x, e->y, ENEMY_SPRITE(e->level));
                        out->x = e->x; out->y = e->y;
                        out->kind = KIND_SPRITE; out++; n++;
                    }
                }
            }
            {                                                       /* bullets (cheap) */
                if (bullet_count) {
                    const bullet_t *b = bullets.b;
                    for (i = MAX_BULLETS; i != 0u; i--, b++) {
                        if (b->active) {
                            BUL_DRAW(back, b->x, b->y);
                            out->x = b->x; out->y = b->y;
                            out->kind = KIND_BULLET; out++; n++;
                        }
                    }
                }
            }
        }
        prevn[bi] = n;

        hud_dash_dot(player.dash_cd);   /* green dot on the top frame = dash ready */

        /* Explosion sound SYNCED with the animation: while any hit-pop is on
         * screen, emit a short noisy crackle each frame so the whole burst is
         * audible together with the graphics. Cheap (~2.5k T, only during fx). */
        if (fx_render()) {              /* animate enemy-hit colour pops (attrs) */
            sfx_noise();
        }

        scld_present();                 /* HALT to 50 Hz, then page-flip */
        music_tick();                   /* advance the AY tune one frame */
    }
    /* never reached */
}
