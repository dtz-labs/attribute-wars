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

static u8 pick_level(u16 kills)
{
    if (kills < WAVE_CHASE_AT) {
        return ENEMY_BOUNCE;
    }
    if (kills < WAVE_HUNTER_AT) {
        return (u8)((rng_byte() & 1u) ? ENEMY_CHASE : ENEMY_BOUNCE);
    }
    {
        u8 r = (u8)(rng_byte() & 3u);   /* 0/3 -> bounce, 1 -> chase, 2 -> hunter */
        if (r == 1u) return ENEMY_CHASE;
        if (r == 2u) return ENEMY_HUNTER;
        return ENEMY_BOUNCE;
    }
}

void enemies_spawn(enemies_t *es, u16 total_kills)
{
    /* Spread around the arena: 4 corners + top & bottom edge midpoints. */
    static const u8 SX[MAX_ENEMIES] = {
        (u8)(ARENA_L + 16), (u8)(ARENA_R - 16), (u8)(ARENA_L + 16), (u8)(ARENA_R - 16),
        124,                124
    };
    static const u8 SY[MAX_ENEMIES] = {
        (u8)(ARENA_T + 16), (u8)(ARENA_T + 16), (u8)(ARENA_B - 16), (u8)(ARENA_B - 16),
        (u8)(ARENA_T + 16), (u8)(ARENA_B - 16)
    };
    enemy_t *e = es->e;
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        e->x = SX[i];
        e->y = SY[i];
        e->alive = 1;
        e->level = pick_level(total_kills);
        e->dx = (rng_byte() & 1u) ? (s8)1 : (s8)-1;   /* diagonal seed (bounce) */
        e->dy = (rng_byte() & 1u) ? (s8)1 : (s8)-1;
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
