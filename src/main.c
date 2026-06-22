/*
 * Twin-Stick Shooter for Timex TC2048 / ZX Spectrum -- main.c
 *
 * Milestone 1, step 5: a playing slice with stakes.
 *   - player ship: 8x8 sprite, Kempston/cursor movement, wraps
 *   - enemies:     8x8 sprites that WANDER randomly (no player tracking yet)
 *   - bullets:     fired (facing or QWEADZXC 8-way), pooled, fire cooldown
 *   - collision:   bullets destroy enemies; cleared wave respawns
 *   - death:       touching an enemy resets the player to centre + new wave
 *   - background:  static 8x8 attribute colour (blue arena frame + checker),
 *                  set once on both buffers so the page-flip never disturbs it
 *   - rendering:   incremental erase+redraw into the hidden buffer, page-flip
 *
 * All hardware (port 0xFF, screen/attr addresses) lives in scld.c; the loop only
 * asks for the back buffer, blits into it, and presents. Attributes are painted
 * once (design D1) -- white ink everywhere so sprites stay readable on any cell.
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
#include "hud.h"           /* ATTR macro, put_attr, score_cell_attr, HUD widgets */
#include "types.h"
#include <z80.h>          /* z80_outp() for the ULA border */
#include <string.h>       /* memset (game-over fills) */
#include <input.h>        /* in_key_pressed + IN_KEY_SCANCODE_* (title menu) */

#define INVULN_FRAMES 50u /* ~1s of i-frames after a hit (ship blinks)         */

/* Max objects drawn in one frame: player + enemies + bullets. */
#define MAX_DRAW (1u + MAX_ENEMIES + MAX_BULLETS)

/* Frames between shots — keeps the bullet/sprite load inside the ~50 Hz budget. */
#define FIRE_COOLDOWN 8u

/* Placeholder full timer bar until Task 8 wires the real wave clock. */
#define HUD_TIMER_PLACEHOLDER 1500u

#define PLAYER_START_X 128u
#define PLAYER_START_Y 96u

#define KIND_SPRITE 0u   /* full 8x8 sprite (player, enemy) */
#define KIND_BULLET 1u   /* cheap 3x3 dot                   */
typedef struct { u8 x, y, kind; } cell_t;

/* Per-buffer record of what was drawn last time that buffer was the back one. */
static cell_t prev[2][MAX_DRAW];
static u8     prevn[2];

/* Pre-shifted sprite tables (built once at startup from the 8-byte source art).
 * Bullets/thruster are not sprites -- they use the cheap bul_draw/bul_erase. */
static u8 ps_ship_dir[8][SPR_PRESHIFT_SIZE];    /* 8 directional ship frames */
static u8 ps_enemy[SPR_PRESHIFT_SIZE];          /* level 0 bouncer */
static u8 ps_enemy_chase[SPR_PRESHIFT_SIZE];    /* level 2 chaser  */
static u8 ps_enemy_hunter[SPR_PRESHIFT_SIZE];   /* level 3 hunter  */
/* (the HUD life-heart pre-shift table now lives in hud.c) */

/* Pick the pre-shifted table for an enemy's behaviour level. */
static const u8 *enemy_sprite(u8 level)
{
    if (level == ENEMY_HUNTER) return ps_enemy_hunter;
    if (level == ENEMY_CHASE)  return ps_enemy_chase;
    return ps_enemy;
}

/* Background colour, page-flip helper, and the ATTR macro now live in hud.c /
 * hud.h: score_cell_attr() replaces bg_attr() (the score is rendered as big
 * attribute digits behind the action) and hud_paint_background() replaces
 * bg_paint(). put_attr() is shared from hud.c so the fx/death/telegraph paths
 * still write both attribute blocks. */

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

static void fx_render(void)
{
    u8 i;
    for (i = 0; i < MAX_FX; i++) {
        u8 colr, restore;
        s8 dr, dc;
        if (fx[i].t == 0u) {
            continue;
        }
        fx[i].t--;
        restore = (u8)(fx[i].t == 0u);          /* last frame -> restore bg */
        colr    = fx_colour((u8)(fx[i].t + 1u));
        for (dr = -1; dr <= 1; dr++) {
            s8 row = (s8)fx[i].cy + dr;
            if (row < 0 || row > 23) continue;
            for (dc = -1; dc <= 1; dc++) {
                s8 c = (s8)fx[i].cx + dc;
                u8 keep;
                if (c < 0 || c > 31) continue;
                if (fx[i].shape == 1u) {
                    keep = (u8)(dr == 0 || dc == 0);                 /* plus  */
                } else if (fx[i].shape == 2u) {
                    keep = (u8)((dr == 0 && dc == 0) || (dr != 0 && dc != 0)); /* X */
                } else {
                    keep = 1u;                                       /* full  */
                }
                if (!keep) {
                    continue;
                }
                put_attr((u8)row, (u8)c,
                         restore ? score_cell_attr((u8)row, (u8)c) : colr);
            }
        }
    }
}

/*
 * Death animation: a fast attribute KABOOM. The scene freezes on the displayed
 * buffer; one of three random styles plays, painting only the cells it needs
 * (cheap/snappy). Caller restores the arena afterwards (hud_paint_background).
 */
/* Paint one attribute cell, clipped to the grid. */
static void put_cell(s8 col, s8 row, u8 v)
{
    if (col >= 0 && col < 32 && row >= 0 && row < 24) {
        put_attr((u8)row, (u8)col, v);
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
    s8 cx   = (s8)(px >> 3);
    s8 cy   = (s8)(py >> 3);
    u8 seed = rng_byte();
    u8 maxR = (u8)(7u + (rng_byte() & 3u));        /* random size 7..10 */
    u8 f;

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
                 * ball is lumpy (core + bands + edge), not a clean square. */
                jit = (u8)(((u8)(adx * 7u + ady * 13u) ^ seed) & 3u);
                fd  = (u8)(d + jit);
                if (fd > f) {
                    continue;                                   /* outside the ball */
                }
                if      (fd <= (u8)(f >> 1)) v = ATTR(1, 7, 0); /* white-hot core  */
                else if (fd <= (u8)(f - 1u)) v = ATTR(1, 6, 0); /* yellow glow     */
                else                         v = ATTR(1, 2, 0); /* red shock edge  */
                put_cell(col, row, v);
            }
        }
        scld_wait();
        if (f >= (u8)(maxR - 1u)) {
            scld_wait();                                        /* brief hold full */
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
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        u8 row = (u8)(e->y >> 3), col = (u8)(e->x >> 3);
        put_attr(row, col, on ? ATTR(0, 2, 7) : score_cell_attr(row, col));  /* soft red */
    }
}

static void telegraph_clear(const enemies_t *es)
{
    const enemy_t *e = es->e;
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        u8 row = (u8)(e->y >> 3), col = (u8)(e->x >> 3);
        put_attr(row, col, score_cell_attr(row, col));
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
        while (d--) scld_wait();
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

/* Fill one 32-cell attribute row of screen A (title highlights). */
static void title_attr_row(u8 row, u8 v)
{
    u8 *a = (u8 *)SCLD_ATTRS_A + (u16)row * 32u;
    u8  c;
    for (c = 0; c < 32u; c++) {
        a[c] = v;
    }
}

/* Draw the menu, poll keys 1/2/3 (pick a control scheme) and 0 (start).
 * Returns the chosen CTRL_* scheme. */
static u8 title_screen(void)
{
    u8 sel = CTRL_KEMPSTON_MOVE;

    scld_clear(SCLD_SCREEN_A);
    memset((u8 *)SCLD_ATTRS_A, ATTR(0, 0, 7), SCLD_ATTRS_LEN);   /* white on black */

    put_text(SCLD_SCREEN_A,  9,  3, "ATTRIBUTE WARS");
    put_text(SCLD_SCREEN_A,  2,  8, "1 KEMPSTON MOVE  KEYS FIRE");
    put_text(SCLD_SCREEN_A,  2, 10, "2 KEYS MOVE  KEMPSTON FIRE");
    put_text(SCLD_SCREEN_A,  2, 12, "3 TWO JOYSTICKS (TS2068)");
    put_text(SCLD_SCREEN_A,  2, 15, "0 START GAME");
    put_text(SCLD_SCREEN_A,  3, 22, "(C) 2026 ANTHROPIC, INC.");
    put_text(SCLD_SCREEN_A,  3, 23, "(C) 2026 MICHAL PASTERNAK");

    title_attr_row(3, ATTR(1, 0, 5));      /* bright-cyan title */

    for (;;) {
        /* highlight the selected scheme bright yellow; START is bright green */
        title_attr_row( 8, (sel == CTRL_KEMPSTON_MOVE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(10, (sel == CTRL_KEMPSTON_FIRE) ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(12, (sel == CTRL_DUAL_STICK)    ? ATTR(1, 0, 6) : ATTR(0, 0, 7));
        title_attr_row(15, ATTR(1, 0, 4));

        if      (in_key_pressed(IN_KEY_SCANCODE_1)) sel = CTRL_KEMPSTON_MOVE;
        else if (in_key_pressed(IN_KEY_SCANCODE_2)) sel = CTRL_KEMPSTON_FIRE;
        else if (in_key_pressed(IN_KEY_SCANCODE_3)) sel = CTRL_DUAL_STICK;
        else if (in_key_pressed(IN_KEY_SCANCODE_0)) break;

        scld_wait();
    }
    return sel;
}

int main(void)
{
    player_t  player;
    bullets_t bullets;
    enemies_t enemies;
    intent_t  in;
    /* Placeholder score: this milestone has no scoring yet (Task 8 wires the
     * real economy). It exists so the HUD has digits to render as the
     * big-attribute-digit background. */
    static score_t hud_score;
    u8        i;
    u8        cooldown = 0;
    u16       kills    = 0;            /* total enemies destroyed (drives waves) */
    u8        tick     = 0;            /* frame counter (thruster flicker etc.)  */
    u8        lives    = START_LIVES;
    u8        shields  = START_SHIELDS;
    u8        invuln   = 0;            /* i-frames after a hit                    */
    u8        spawn_timer = TELEGRAPH_FRAMES;  /* telegraph the opening wave      */

    scld_init(0x07u);                 /* clears both buffers, IM1+EI, shows A   */
    z80_outp(0xFEu, 0x00u);           /* black ULA border (title + arena)       */
    rng_seed(0xACE1u);

    { u8 d; for (d = 0; d < 8u; d++) spr_preshift(ps_ship_dir[d], spr_ship_dir[d]); }
    spr_preshift(ps_enemy,        spr_enemy);   /* build pre-shifted tables once */
    spr_preshift(ps_enemy_chase,  spr_enemy_chase);
    spr_preshift(ps_enemy_hunter, spr_enemy_hunter);
    hud_init();                                  /* build the HUD heart sprite */

    /* Title + control-scheme menu; the choice drives input_read() all game. */
    input_set_scheme(title_screen());

    scld_clear(SCLD_SCREEN_A);        /* wipe the title text off both buffers   */
    scld_clear(SCLD_SCREEN_B);
    score_reset(&hud_score);          /* zero the placeholder score             */
    hud_paint_background(&hud_score); /* paint score-digit bg into both blocks  */

    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
    bullets_init(&bullets);
    enemies_spawn(&enemies, 1u);  /* TODO Task 8: wire real wave number */
    hud_invalidate();                 /* force first widget paint               */
    hud_draw_lives(lives);
    hud_draw_shields(shields);
    hud_draw_timer(HUD_TIMER_PLACEHOLDER, HUD_TIMER_PLACEHOLDER);  /* full bar  */
    hud_draw_boost(player.boost_energy);
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
        if (in.fire && cooldown == 0) {
            if (bullet_spawn(&bullets, player.x, player.y,
                             in.aim_dx, in.aim_dy) >= 0) {
                cooldown = FIRE_COOLDOWN;
            }
        }
        bullets_update(&bullets);

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
            /* ---- enemies act; bullets destroy them (hit-pop per kill) ---- */
            enemies_update(&enemies, player.x, player.y, &bullets);
            {
                u8 alive_before[MAX_ENEMIES];
                for (i = 0; i < MAX_ENEMIES; i++) {
                    alive_before[i] = enemies.e[i].alive;
                }
                kills = (u16)(kills + collide_bullets_enemies(&bullets, &enemies));
                for (i = 0; i < MAX_ENEMIES; i++) {
                    if (alive_before[i] && !enemies.e[i].alive) {
                        fx_spawn(enemies.e[i].x, enemies.e[i].y);
                    }
                }
            }

            if (!enemies_any_alive(&enemies)) {
                enemies_spawn(&enemies, 1u);        /* next wave (telegraphed); TODO Task 8: wire real wave */
                spawn_timer = TELEGRAPH_FRAMES;
            } else if (invuln == 0u && player_hit(player.x, player.y, &enemies)) {
                fx_spawn(player.x, player.y);        /* hit pop at the ship */
                if (shields > 0u) {
                    shields--;                       /* a shield absorbs it */
                    invuln = INVULN_FRAMES;
                    hud_draw_shields(shields);
                } else {
                    death_anim(player.x, player.y);  /* shields gone -> KABOOM */
                    scld_clear(SCLD_SCREEN_A);
                    scld_clear(SCLD_SCREEN_B);
                    if (lives > 0u) {
                        lives--;
                    }
                    if (lives == 0u) {               /* GAME OVER -> fresh game */
                        game_over_flash();
                        lives = START_LIVES;
                        shields = START_SHIELDS;
                        kills = 0;
                    } else {
                        shields = START_SHIELDS;      /* new life, full shields */
                    }
                    hud_paint_background(&hud_score);  /* repaint score-digit bg */
                    fx_clear();
                    prevn[0] = 0; prevn[1] = 0;
                    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
                    enemies_spawn(&enemies, 1u);      /* TODO Task 8: wire real wave */
                    spawn_timer = TELEGRAPH_FRAMES;   /* telegraph the respawn */
                    hud_invalidate();                 /* bitmaps + bars were wiped */
                    hud_draw_lives(lives);
                    hud_draw_shields(shields);
                    hud_draw_timer(HUD_TIMER_PLACEHOLDER, HUD_TIMER_PLACEHOLDER);
                    hud_draw_boost(player.boost_energy);
                    cooldown = 0;
                    invuln = INVULN_FRAMES;
                }
            }
        }

        /* ---- render into the hidden buffer ---- */
        back = scld_back();
        bi   = scld_back_page();

        for (i = 0; i < prevn[bi]; i++) {        /* erase this buffer's last frame */
            if (prev[bi][i].kind == KIND_BULLET) {
                BUL_ERASE(back, prev[bi][i].x, prev[bi][i].y);
            } else {
                SPR_ERASE(back, prev[bi][i].x, prev[bi][i].y);
            }
        }

        n = 0;
        {                                                        /* player ship  */
            u8 fdir = (u8)((player.facing < 8u) ? player.facing : 0u); /* NONE->N */
            if (!invuln || (tick & 2u)) {                /* blink while invuln */
                SPR_DRAW(back, player.x, player.y, ps_ship_dir[fdir]);
                prev[bi][n].x = player.x; prev[bi][n].y = player.y;
                prev[bi][n].kind = KIND_SPRITE; n++;
            }
        }
        if (spawn_timer == 0u) {                                   /* enemies */
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (enemies.e[i].alive) {
                    SPR_DRAW(back, enemies.e[i].x, enemies.e[i].y,
                             enemy_sprite(enemies.e[i].level));
                    prev[bi][n].x = enemies.e[i].x; prev[bi][n].y = enemies.e[i].y;
                    prev[bi][n].kind = KIND_SPRITE; n++;
                }
            }
        }
        for (i = 0; i < MAX_BULLETS; i++) {                        /* bullets (cheap) */
            if (bullets.b[i].active) {
                BUL_DRAW(back, bullets.b[i].x, bullets.b[i].y);
                prev[bi][n].x = bullets.b[i].x; prev[bi][n].y = bullets.b[i].y;
                prev[bi][n].kind = KIND_BULLET; n++;
            }
        }
        prevn[bi] = n;

        hud_draw_boost(player.boost_energy);  /* cheap: cached, redraw on change */
        fx_render();                  /* animate enemy-hit colour pops (attrs) */
        scld_present();               /* HALT to 50 Hz, then page-flip */
    }
    /* never reached */
}
