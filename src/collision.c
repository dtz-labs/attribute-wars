/*
 * collision.c -- bullet<->enemy collision (see collision.h). Pure integer logic.
 *
 * Hot path: iterate the pools with POINTERS (b++/e++), not arr[i] indexing --
 * sdcc turns arr[i] into an i*sizeof multiply on every access, which dominated
 * the frame (collide was ~22k T). Pointer stepping is a constant add.
 */
#include "collision.h"

#define SPR_SIZE 8u   /* sprites are 8x8 */

u8 boxes_overlap(u8 ax, u8 ay, u8 bx, u8 by)
{
    u8 dx = (u8)((ax > bx) ? (ax - bx) : (bx - ax));
    u8 dy = (u8)((ay > by) ? (ay - by) : (by - ay));
    return (u8)(dx < SPR_SIZE && dy < SPR_SIZE);
}

#ifdef __SDCC
/* Z80 target: the pointer-walk C below compiled to ~6500 T worst-case (sdcc
 * spent ~540 T per u8 box test). collide_bullets_enemies is hand-written in
 * collide.asm; this thin wrapper passes the pools via globals and returns the
 * hit count the asm leaves in cbe_kills. The C version stays compiled on the
 * host as the unit-tested reference (and what the asm was diffed against). */
#if MAX_BULLETS != 2 || MAX_ENEMIES != 8
#error "collide.asm hardcodes MAX_BULLETS==2 and MAX_ENEMIES==8; update both"
#endif
bullets_t *cbe_bs;
enemies_t *cbe_es;
u8         cbe_kills;
u8         cbe_kill_mask;
extern void collide_asm(void);

u8 collide_bullets_enemies_mask(bullets_t *bs, enemies_t *es, u8 *kill_mask)
{
    cbe_bs = bs;
    cbe_es = es;
    collide_asm();
    *kill_mask = cbe_kill_mask;
    return cbe_kills;
}

u8 collide_bullets_enemies(bullets_t *bs, enemies_t *es)
{
    cbe_bs = bs;
    cbe_es = es;
    collide_asm();
    return cbe_kills;
}
#else
u8 collide_bullets_enemies_mask(bullets_t *bs, enemies_t *es, u8 *kill_mask)
{
    u8 kills = 0;
    u8 mask = 0;
    u8 bit = 1u;
    u8 i, j;
    bullet_t *b = bs->b;

    for (i = 0; i < MAX_BULLETS; i++, b++) {
        enemy_t *e;
        u8 bx, by;
        if (!b->active) {
            continue;
        }
        bx = b->x;
        by = b->y;                  /* cache: no repeated pointer derefs */
        e = es->e;
        bit = 1u;
        for (j = 0; j < MAX_ENEMIES; j++, e++) {
            u8 dx, dy;
            if (!e->alive) {
                bit = (u8)(bit << 1);
                continue;
            }
            /* inline boxes_overlap (no call) + short-circuit on x first */
            dx = (u8)(bx > e->x ? bx - e->x : e->x - bx);
            if (dx >= SPR_SIZE) {
                bit = (u8)(bit << 1);
                continue;
            }
            dy = (u8)(by > e->y ? by - e->y : e->y - by);
            if (dy >= SPR_SIZE) {
                bit = (u8)(bit << 1);
                continue;
            }
            e->alive = 0;           /* enemy destroyed */
            b->active = 0;          /* bullet consumed */
            kills++;
            mask = (u8)(mask | bit);
            break;                  /* this bullet is spent */
        }
    }
    *kill_mask = mask;
    return kills;
}

u8 collide_bullets_enemies(bullets_t *bs, enemies_t *es)
{
    u8 kill_mask;
    return collide_bullets_enemies_mask(bs, es, &kill_mask);
}
#endif /* __SDCC */

u8 player_hit(u8 px, u8 py, const enemies_t *es)
{
    const enemy_t *e = es->e;
    u8 j;
    for (j = 0; j < MAX_ENEMIES; j++, e++) {
        u8 dx, dy;
        if (!e->alive) {
            continue;
        }
        dx = (u8)(px > e->x ? px - e->x : e->x - px);
        if (dx >= SPR_SIZE) continue;
        dy = (u8)(py > e->y ? py - e->y : e->y - py);
        if (dy >= SPR_SIZE) continue;
        return 1;
    }
    return 0;
}
