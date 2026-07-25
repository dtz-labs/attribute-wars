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
 *   - sound:       title-selected beeper, AY music+FX, or AY FX-only
 *   - HUD:         lives + shields (top), timer + boost bars (bottom), and the
 *                  SCORE rendered as big attribute-cell digits behind the action
 *   - rendering:   incremental erase+redraw into the hidden buffer, page-flip
 *
 * All hardware (port 0xFF, screen/attr addresses) lives in scld.c; the loop only
 * asks for the back buffer, blits into it, and presents. The score-digit
 * background uses only black/dark-blue paper with white ink so sprites stay
 * readable; every cell-restore path (fx/death/telegraph) asks bg_attr() for the
 * current background colour so explosions never punch holes in the floor.
 */

#include "scld.h"
#include "sprite.h"
#include "sprites.h"
#include "sprite_art.h"    /* pre-shifted tables: PS_SHIP_DIR / ENEMY_SPRITE   */
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "collision.h"
#include "controls.h"
#include "arena.h"
#include "background.h"    /* generated floor + frame + ULA border             */
#include "fx.h"            /* hit pops, death fireball, spawn telegraph        */
#include "rng.h"
#include "score.h"
#include "sfx.h"           /* sfx_play + SFX_* ids (shoot/explode/hit/death/...) */
#include "music.h"         /* title-selected beeper / AY music+FX / AY FX       */
#include "hud.h"           /* HUD widgets: lives, shields, timer, score         */
#include "title.h"         /* title screen + control/sound menus                */
#include "gameover.h"      /* GAME OVER screen + GAME_OVER_* choices            */
#include "types.h"
#include <z80.h>          /* z80_outp() for the ULA border */

#ifdef ZX128_PAGE_FLIP
extern void zx128_load_tune(void);  /* pull the PT3 tune into bank 4 (zx128_page.asm) */
#endif

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

/* (the pre-shifted sprite tables now live in sprite_art.c/h; the HUD
 * life-heart pre-shift table lives in hud.c) */

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

/* The top-border lives/shields HUD now lives in hud.c
 * (hud_draw_lives / hud_draw_shields). */

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
    static u8 menu_sound = 0xFFu;
    u8        i;
    u8        cooldown = 0;
    u8        boost_was_down = 0u;
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
    z80_outp(0xFEu, BORDER_BLACK);    /* black ULA border (title + arena)       */
    rng_seed(0xACE1u);
    sprite_art_init();                           /* build pre-shifted tables once */
    hud_init();                                  /* build the HUD heart sprite */
#ifdef ZX128_PAGE_FLIP
    zx128_load_tune();                /* tune -> bank 4 before any music_init */
#endif

main_menu:
    /* SOUND is applied after START on a cold title. Returning from a game keeps
     * a running tune alive until the player explicitly selects BEEPER or FX. */
    {
        u8 ctrl_choice;
        u8 sound_choice;
        if (menu_sound == 0xFFu) {
            menu_sound = music_default_sound();
        }
        title_screen(&ctrl_choice, &sound_choice, menu_sound);
        menu_sound = sound_choice;
        input_set_scheme(ctrl_choice);
        music_init(sound_choice);
    }

    scld_clear(SCLD_SCREEN_A);        /* wipe the title text off both buffers   */
    scld_clear(SCLD_SCREEN_B);
    game_new(&g);                     /* wave 1, score 0, START_LIVES/SHIELDS   */
    bg_next_pattern();
    bg_paint();                       /* floor + frame into both attr blocks    */
    hud_draw_score(&g.score, 1u);          /* full score draw after the clear        */

    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
    bullets_init(&bullets);
    bullet_count = 0u;
    enemies_spawn(&enemies, g.wave);
    enemy_count = enemies_alive_count(&enemies);
    wave_total = wave_time_frames(g.wave);   /* full bar; clock starts after the
                                              * telegraph (when enemies go live) */
    wave_timer = wave_total;
    wave_secs  = 0xFFFFu;
    cooldown = 0u;
    tick = 0u;
    invuln = 0u;
    spawn_timer = TELEGRAPH_FRAMES;
    recoil_timer = 0u;
    recoil_dx = 0;
    recoil_dy = 0;
    boost_was_down = 0u;
    border_reset();
    hud_invalidate();                 /* force first widget paint               */
    hud_draw_lives(g.lives);
    hud_draw_shields(g.shields);
    prevn[0] = 0;
    prevn[1] = 0;

    for (;;) {
        u16 back;
        u8  bi, n;

        tick++;

        /* ---- input + player ---- */
        {
            u8 dash_t0;
            u8 dash_cd0;
            u8 boost_pressed;
            u8 dash_fail = 0u;
            input_read(player.facing, &in);
            boost_pressed = (u8)(in.boost && !boost_was_down);
            dash_t0 = player.dash_t;
            dash_cd0 = player.dash_cd;
            player_update(&player, &in);

            if (in.boost && dash_t0 == 0u && dash_cd0 == 0u &&
                    (in.move_dx || in.move_dy) && player.dash_t > 0u) {
                sfx_play(SFX_DASH);
            } else if (boost_pressed && dash_t0 == 0u && dash_cd0 > 0u) {
                sfx_play(SFX_DASH_FAIL);
                dash_fail = 1u;
            }
            if (!dash_fail && dash_cd0 > 0u && player.dash_cd == 0u &&
                    player.dash_t == 0u) {
                sfx_play(SFX_DASH_READY);
            }
            boost_was_down = in.boost;
        }

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
                hud_draw_score(&g.score, 0u);
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
            telegraph_blink(&enemies, spawn_timer);
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
             * skip it. When bullets are live, collide returns a kill mask so we
             * score only the killed slots; wounded chasers are displaced but
             * stay alive and do not score until the second hit. */
            {
                if (bullet_count) {
                    u8 kill_mask = 0u;
                    u8 wound_mask = 0u;
                    u8 kills;
                    kills = collide_bullets_enemies_masks(&bullets, &enemies,
                                                          &kill_mask, &wound_mask);
                    bullet_count = bullets_count(&bullets);
                    if (wound_mask) {
                        /* Spawn hit effects at the wounded positions BEFORE they jump */
                        enemy_t *ew = enemies.e;
                        u8 wbit = 1u;
                        for (i = MAX_ENEMIES; i != 0u; i--, ew++) {
                            if (wound_mask & wbit && ew->alive) {
                                fx_spawn(ew->x, ew->y);
                            }
                            wbit = (u8)(wbit << 1);
                        }
                        enemies_jump_wounded_chasers(&enemies, wound_mask);
                        sfx_play(SFX_HIT);
                        border_flash_red();
                    }
                    if (kills) {
                        enemy_t *e = enemies.e;
                        u8 bit = 1u;
                        u8 scored = 0u;
                        u8 killed_n = 0u;
                        u8 killed_level[MAX_BULLETS];
                        u8 killed_x[MAX_BULLETS];
                        u8 killed_y[MAX_BULLETS];
                        enemy_count = (u8)(enemy_count - kills);
                        for (i = MAX_ENEMIES; i != 0u; i--, e++) {
                            if (kill_mask & bit) {
                                u8 level = e->level;
                                u8 xl = score_add(&g.score, score_enemy_points(level));
                                g.lives = (u8)(g.lives + xl);  /* extra life(s)  */
                                if (xl) {
                                    sfx_play(SFX_EXTRA_LIFE);
                                    hud_draw_lives(g.lives);
                                }
                                fx_spawn(e->x, e->y);
                                if (killed_n < MAX_BULLETS) {
                                    killed_level[killed_n] = level;
                                    killed_x[killed_n] = e->x;
                                    killed_y[killed_n] = e->y;
                                    killed_n++;
                                }
                                scored = 1u;
                            }
                            bit = (u8)(bit << 1);
                        }
                        for (i = 0u; i < killed_n; i++) {
                            if (killed_level[i] == ENEMY_HUNTER &&
                                enemy_count <= (u8)(MAX_ENEMIES - 2u) &&
                                (rng_byte() & 1u)) {
                                /* Hunter splits into 2 chasers */
                                enemy_count = (u8)(enemy_count +
                                    enemies_spawn_chaser_clones(&enemies,
                                                               killed_x[i], killed_y[i]));
                            }
                            if (killed_level[i] == ENEMY_CHASE &&
                                enemy_count <= (u8)(MAX_ENEMIES - 2u) &&
                                (rng_byte() & 1u)) {
                                enemy_count = (u8)(enemy_count +
                                    enemies_spawn_bouncer_clones(&enemies,
                                                               killed_x[i], killed_y[i]));
                            }
                        }
                        if (scored) {
                            hud_draw_score(&g.score, 0u);           /* points landed   */
                            border_flash_red();
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
                    hud_draw_score(&g.score, 0u);
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
                border_flash_red();
                if (g.shields > 0u) {
                    /* ---- a shield absorbs the hit (spec §4/§8): -10 + click ---- */
                    g.shields--;
                    score_sub(&g.score, 10u);
                    sfx_play(SFX_HIT);               /* short, in-loop            */
                    hud_draw_score(&g.score, 0u);
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
                         * FIRE/SPACE resume, Q fresh game, ENTER title menu. ---- */
                        u8 death_wave = g.wave;
                        u8 over = game_over_screen(&g, death_wave);
                        if (over == GAME_OVER_RESUME) {
                            game_resume_from_wave(&g, death_wave);  /* score 0, keep wave */
                        } else if (over == GAME_OVER_NEW_GAME) {
                            game_new(&g);                            /* wave 1     */
                        } else {
                            goto main_menu;
                        }
                    } else {
                        g.shields = START_SHIELDS;    /* new life, full shields    */
                    }
                    scld_clear(SCLD_SCREEN_A);        /* wipe the death-anim AND the */
                    scld_clear(SCLD_SCREEN_B);        /* GAME OVER text off both pages */
                    bg_paint();                       /* repaint floor + frame      */
                    hud_draw_score(&g.score, 1u);          /* full score after the clear */
                    fx_clear();
                    prevn[0] = 0; prevn[1] = 0;
                    player_init(&player, PLAYER_START_X, PLAYER_START_Y);
                    bullets_init(&bullets);
                    bullet_count = 0u;
                    enemies_spawn(&enemies, g.wave);  /* re-init the current wave  */
                    enemy_count = enemies_alive_count(&enemies);
                    wave_total = wave_time_frames(g.wave);
                    wave_timer = wave_total;          /* fresh clock for the wave  */
                    wave_secs  = 0xFFFFu;
                    spawn_timer = TELEGRAPH_FRAMES;   /* telegraph the respawn     */
                    hud_invalidate();                 /* bitmaps + bars were wiped  */
                    hud_draw_lives(g.lives);
                    hud_draw_shields(g.shields);
                    cooldown = 0;
                    recoil_timer = 0u;
                    boost_was_down = 0u;
                    invuln = INVULN_FRAMES;
                }
            }
        }

        /* ---- render into the hidden buffer ----
         * ZX48 has no hidden buffer, so wait for the frame interrupt just before
         * erase+draw. That puts visible writes as early in the frame as this
         * single-buffer path can manage, reducing the erased-sprite blink window.
         * Timex/ZX128 keep the normal draw-then-HALT page flip below. */
#ifdef ZX48_SINGLE_BUFFER
        scld_wait();
#endif
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
                    SPR_DRAW(back, dx, dy, PS_SHIP_DIR(fdir));
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
            if (bullet_count) {                                     /* bullets (cheap) */
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
        prevn[bi] = n;

        /* Explosion sound SYNCED with the animation: while any hit-pop is on
         * screen, emit a short noisy crackle each frame so the whole burst is
         * audible together with the graphics. Cheap (~2.5k T, only during fx). */
        if (fx_render()) {              /* animate enemy-hit colour pops (attrs) */
            sfx_noise();
        }

        border_tick();
#ifndef ZX48_SINGLE_BUFFER
        scld_present();                 /* HALT to 50 Hz, then page-flip */
#endif
        /* music is driven by the 50 Hz IM2 ISR -- no per-frame tick here */
    }
    /* never reached */
}
