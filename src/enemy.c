/*
 * enemy.c -- enemy behaviour levels (see enemy.h).
 *   ENEMY_BOUNCE : diagonal drift, reflects off the arena walls
 *   ENEMY_CHASE  : steps toward the player each frame
 *   ENEMY_HUNTER : chases, but flees a bullet that comes within DODGE_DIST
 *
 * Hot path uses POINTER iteration and u8 sign math (no s16 casts, no arr[i]
 * multiply) -- the hunter's per-bullet scan was ~34k T with the naive form.
 */
#include "enemy.h"
#include "arena.h"
#include "rng.h"

#define DODGE_DIST 24u   /* hunter flees a bullet this close (per axis) */

#if ENEMY_SPEED != 1
#error "enemies_update uses a 1 px/frame u8 fast path; revisit it for ENEMY_SPEED != 1"
#endif

/* sign of (a - b) for u8 operands: +1 if a>b, -1 if a<b, else 0. A MACRO, not a
 * function: sdcc won't inline a static call, and enemies_update calls this 2-4x
 * per enemy -- the call overhead was a big chunk of the logic frame. */
#define SGN_U8(a, b) ((s8)((a) > (b) ? 1 : ((a) < (b) ? -1 : 0)))

/* ---- 16-wave difficulty table (spec §5.4) --------------------------------- */

const wave_t wave_table[WAVE_COUNT] = {
    /* wave  count  B  C  H  pattern        time_frames */
    /* 1  */ { 4u, 4u, 0u, 0u, PAT_PERIMETER, 1500u },
    /* 2  */ { 5u, 5u, 0u, 0u, PAT_ROWS,      1500u },
    /* 3  */ { 6u, 6u, 0u, 0u, PAT_FLANKS,    1500u },
    /* 4  */ { 5u, 4u, 1u, 0u, PAT_ROWS,      1500u },
    /* 5  */ { 6u, 4u, 2u, 0u, PAT_STAR,      1500u },
    /* 6  */ { 6u, 3u, 3u, 0u, PAT_FLANKS,    1500u },
    /* 7  */ { 7u, 3u, 4u, 0u, PAT_DIAGONALS, 1500u },
    /* 8  */ { 6u, 2u, 2u, 2u, PAT_PERIMETER, 1500u },
    /* 9  */ { 7u, 2u, 3u, 2u, PAT_STAR,      1250u },
    /* 10 */ { 7u, 1u, 3u, 3u, PAT_FLANKS,    1250u },
    /* 11 */ { 8u, 2u, 3u, 3u, PAT_DIAGONALS, 1250u },
    /* 12 */ { 8u, 1u, 3u, 4u, PAT_STAR,      1250u },
    /* 13 */ { 8u, 0u, 4u, 4u, PAT_FLANKS,    1000u },
    /* 14 */ { 8u, 0u, 3u, 5u, PAT_ROWS,      1000u },
    /* 15 */ { 8u, 0u, 2u, 6u, PAT_DIAGONALS, 1000u },
    /* 16 */ { 8u, 0u, 1u, 7u, PAT_STAR,      1000u },
};

/* ---- 5 spawn position tables (spec §5.1) ---------------------------------- */
/* All coordinates inside [ARENA_L..ARENA_R] x [ARENA_T..ARENA_B] = 8..240 x 8..176 */

/* PERIMETER: corners + edge midpoints (up to 8 positions). */
static const u8 POS_PERIMETER_X[] = {
    (u8)(ARENA_L + 8u),  /* top-left corner area   */
    (u8)(ARENA_R - 8u),  /* top-right corner area  */
    (u8)(ARENA_L + 8u),  /* bottom-left corner area*/
    (u8)(ARENA_R - 8u),  /* bottom-right corner area*/
    124u,                /* top edge midpoint      */
    124u,                /* bottom edge midpoint   */
    (u8)(ARENA_L + 8u),  /* left edge midpoint     */
    (u8)(ARENA_R - 8u),  /* right edge midpoint    */
};
static const u8 POS_PERIMETER_Y[] = {
    (u8)(ARENA_T + 8u),
    (u8)(ARENA_T + 8u),
    (u8)(ARENA_B - 8u),
    (u8)(ARENA_B - 8u),
    (u8)(ARENA_T + 8u),
    (u8)(ARENA_B - 8u),
    92u,                 /* left wall vertical centre  */
    92u,                 /* right wall vertical centre */
};

/* STAR: tight central ring around arena centre (~124, 92). */
static const u8 POS_STAR_X[] = {
    124u, 108u, 140u, 108u, 140u, 124u, 96u, 152u,
};
static const u8 POS_STAR_Y[] = {
    76u,  84u,  84u,  100u, 100u, 108u, 92u, 92u,
};

/* FLANKS: two vertical columns near left and right walls. */
static const u8 POS_FLANKS_X[] = {
    (u8)(ARENA_L + 16u), (u8)(ARENA_L + 16u), (u8)(ARENA_L + 16u), (u8)(ARENA_L + 16u),
    (u8)(ARENA_R - 16u), (u8)(ARENA_R - 16u), (u8)(ARENA_R - 16u), (u8)(ARENA_R - 16u),
};
static const u8 POS_FLANKS_Y[] = {
    (u8)(ARENA_T + 16u), 68u, 108u, (u8)(ARENA_B - 16u),
    (u8)(ARENA_T + 16u), 68u, 108u, (u8)(ARENA_B - 16u),
};

/* ROWS: horizontal lines top and bottom. */
static const u8 POS_ROWS_X[] = {
    (u8)(ARENA_L + 16u), 76u, 124u, 172u, (u8)(ARENA_R - 16u),
    (u8)(ARENA_L + 16u), 76u, 124u,
};
static const u8 POS_ROWS_Y[] = {
    (u8)(ARENA_T + 16u), (u8)(ARENA_T + 16u), (u8)(ARENA_T + 16u),
    (u8)(ARENA_T + 16u), (u8)(ARENA_T + 16u),
    (u8)(ARENA_B - 16u), (u8)(ARENA_B - 16u), (u8)(ARENA_B - 16u),
};

/* DIAGONALS: an "X" pattern across the arena. */
static const u8 POS_DIAGONALS_X[] = {
    (u8)(ARENA_L + 16u), (u8)(ARENA_L + 56u), 124u, (u8)(ARENA_R - 56u), (u8)(ARENA_R - 16u),
    (u8)(ARENA_L + 16u), (u8)(ARENA_L + 56u), 124u,
};
static const u8 POS_DIAGONALS_Y[] = {
    (u8)(ARENA_T + 16u), 52u, 92u, 52u, (u8)(ARENA_T + 16u),
    (u8)(ARENA_B - 16u), 132u, 92u,
};

/* Pattern table: indexed by PAT_* enum. */
static const u8 * const PAT_X[PAT_N] = {
    POS_PERIMETER_X, POS_STAR_X, POS_FLANKS_X, POS_ROWS_X, POS_DIAGONALS_X,
};
static const u8 * const PAT_Y[PAT_N] = {
    POS_PERIMETER_Y, POS_STAR_Y, POS_FLANKS_Y, POS_ROWS_Y, POS_DIAGONALS_Y,
};

/* ---- spawn state ---------------------------------------------------------- */

static u8 s_last_pattern = (u8)PAT_N;   /* sentinel: no pattern used yet */

u8 enemy_last_pattern(void)
{
    return s_last_pattern;
}

/* ---- enemies_spawn -------------------------------------------------------- */

void enemies_spawn(enemies_t *es, u8 wave)
{
    const wave_t *w;
    u8 pat;
    u8 count, nb, nc, nh;
    u8 i;
    enemy_t *e;
    const u8 *px, *py;

    /* Index clamped to [0..15]; wave==0 → index 0 (wave 1); wave>16 → index 15 */
    {
        u8 idx;
        u8 endless;   /* non-zero if wave > 16 (endless loop, rng-pick pattern) */
        if (wave == 0u || wave == 1u) {
            idx = 0u;
            endless = 0u;
        } else if (wave >= 17u) {
            idx = 15u;
            endless = 1u;
        } else {
            idx = (u8)(wave - 1u);
            endless = 0u;
        }
        w = &wave_table[idx];

        /* (1) Pattern source: waves 1-16 use the per-wave table field.
         *     Wave > 16 (endless) rng-picks, rerolling while == last_pattern. */
        if (endless) {
            pat = (u8)(rng_byte() % (u8)PAT_N);
            while (pat == s_last_pattern) {
                pat = (u8)(rng_byte() % (u8)PAT_N);
            }
        } else {
            pat = w->pattern;
        }
        s_last_pattern = pat;
    }

    /* Clamp count to MAX_ENEMIES. */
    count = w->count;
    if (count > MAX_ENEMIES) {
        count = MAX_ENEMIES;
    }

    /* (2) Assign positions and levels.
     *     Level order: first n_bounce bouncers, then n_chase chasers,
     *     then n_hunter hunters (no rng for levels). */
    px = PAT_X[pat];
    py = PAT_Y[pat];

    /* Compute clamped mix counts (proportional when total > MAX_ENEMIES). */
    nb = w->n_bounce;
    nc = w->n_chase;
    nh = w->n_hunter;
    /* Simple clamp: drop from hunters first, then chasers, then bouncers. */
    while ((u8)(nb + nc + nh) > count) {
        if (nh > 0u) { nh--; }
        else if (nc > 0u) { nc--; }
        else if (nb > 0u) { nb--; }
    }

    e = es->e;
    for (i = 0u; i < MAX_ENEMIES; i++, e++) {
        if (i < count) {
            e->x = px[i];
            e->y = py[i];
            e->alive = 1u;
            /* Assign level: first nb bounce, then nc chase, then nh hunter. */
            if (i < nb) {
                /* Bouncer: a random variant -- ball (diagonal), vertical-only,
                 * or horizontal-only -- set by the dx/dy it spawns with. (3)
                 * rng_byte() per bouncer: [variant, sx, sy]. (Chasers/hunters
                 * ignore dx/dy, so they draw no rng.) */
                u8 var = (u8)(rng_byte() % 3u);
                s8 sx  = (rng_byte() & 1u) ? (s8)1 : (s8)-1;
                s8 sy  = (rng_byte() & 1u) ? (s8)1 : (s8)-1;
                if (var == 1u) {                 /* vertical-only  */
                    e->level = ENEMY_BOUNCE_V; e->dx = (s8)0; e->dy = sy;
                } else if (var == 2u) {          /* horizontal-only*/
                    e->level = ENEMY_BOUNCE_H; e->dx = sx; e->dy = (s8)0;
                } else {                         /* diagonal ball  */
                    e->level = ENEMY_BOUNCE;   e->dx = sx; e->dy = sy;
                }
            } else if (i < (u8)(nb + nc)) {
                e->level = ENEMY_CHASE;
                e->dx = (s8)0;
                e->dy = (s8)0;
            } else {
                e->level = ENEMY_HUNTER;
                e->dx = (s8)0;
                e->dy = (s8)0;
            }
        } else {
            e->alive = 0u;
            e->dx = (s8)0;
            e->dy = (s8)0;
        }
    }
}

u8 enemies_any_alive(const enemies_t *es)
{
    const enemy_t *e = es->e;
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        if (e->alive) {
            return 1;
        }
    }
    return 0;
}

#ifdef __SDCC
/* Z80 target: the C below compiled to ~1600 T per enemy -- sdcc spilled the
 * per-enemy skeleton (alive/integrate/wall-bounce/write-back) to the IX frame
 * (~351 instructions, 158 ld). enemies_update is hand-written in
 * enemy_update.asm; this thin once-per-frame wrapper hands it the inputs via
 * globals. The C version below stays compiled on the host as the unit-tested
 * reference and is what the asm was verified byte-identical against. */
enemies_t       *eu_es;
const bullets_t *eu_bs;
u8               eu_px, eu_py;
extern void enemies_update_asm(void);

void enemies_update(enemies_t *es, u8 px, u8 py, const bullets_t *bs)
{
    eu_es = es;
    eu_bs = bs;
    eu_px = px;
    eu_py = py;
    enemies_update_asm();
}
#else
void enemies_update(enemies_t *es, u8 px, u8 py, const bullets_t *bs)
{
    enemy_t *e = es->e;
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        u8 ex, ey, lvl, nx, ny;
        s8 dx, dy;

        if (!e->alive) {
            continue;
        }

        /* Cache members in locals: sdcc reloads e->field on every access, and
         * this body touches them a lot. Operate on locals, write back once. */
        ex = e->x; ey = e->y; lvl = e->level;
        dx = e->dx; dy = e->dy;

        if (lvl == ENEMY_CHASE) {
            dx = SGN_U8(px, ex);
            dy = SGN_U8(py, ey);
        } else if (lvl == ENEMY_HUNTER) {
            const bullet_t *b = bs->b;
            u8 j;
            dx = SGN_U8(px, ex);         /* chase by default */
            dy = SGN_U8(py, ey);
            for (j = 0; j < MAX_BULLETS; j++, b++) {
                u8 bx, by, adx, ady;
                if (!b->active) {
                    continue;
                }
                /* inline absdiff (no call) + short-circuit on x first */
                bx = b->x;
                adx = (u8)(bx > ex ? bx - ex : ex - bx);
                if (adx >= DODGE_DIST) continue;
                by = b->y;
                ady = (u8)(by > ey ? by - ey : ey - by);
                if (ady >= DODGE_DIST) continue;
                dx = SGN_U8(ex, bx);     /* flee the bullet */
                dy = SGN_U8(ey, by);
                break;
            }
        }
        /* else ENEMY_BOUNCE: keep current dx/dy */

        /* Integrate in U8 (no s16). dx/dy are -1/0/+1; as u8 that's 0xFF/0/1,
         * so (x + (u8)dx) is exactly x-1 / x / x+1 mod 256. ex>=ARENA_L>0 and
         * ey>=ARENA_T>0, so a -1 step can't underflow the byte. */
        nx = (u8)(ex + (u8)dx);
        ny = (u8)(ey + (u8)dy);

        /* Walls: reflect velocity (so bouncers bounce) and clamp inside. */
        if (nx < ARENA_L)      { nx = ARENA_L; dx = (s8)-dx; }
        else if (nx > ARENA_R) { nx = ARENA_R; dx = (s8)-dx; }
        if (ny < ARENA_T)      { ny = ARENA_T; dy = (s8)-dy; }
        else if (ny > ARENA_B) { ny = ARENA_B; dy = (s8)-dy; }

        e->x = nx; e->y = ny;
        e->dx = dx; e->dy = dy;
    }
}
#endif /* __SDCC */
