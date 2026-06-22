/*
 * sfx.c -- 1-bit beeper SFX dispatch.
 *
 * Under __SDCC (Z80 target): a static table maps each SFX id to a
 * (half_period, duration) pair.  sfx_play() stores the pair into the globals
 * sfx_hp / sfx_dur that sfx.asm's sfx_blip routine reads, then calls sfx_blip.
 *
 * Under the host build (#else): sfx_play is a pure no-op so the host unit
 * tests compile without any Z80/hardware dependency.
 *
 * Timing reference (TC2048, Z80A @ 3.5 MHz):
 *   1 ms  ≈  3500 T-states
 *   SFX_SHOOT : half_period=16, duration=110 -> ~110 half-periods * 16 T ≈ 1760 T  ≈ 0.5 ms click
 *   SFX_EXPLODE: half_period=8,  duration=200 -> short low-pitch burst
 *   SFX_HIT    : half_period=12, duration=150 -> brief mid-pitch blip
 *   SFX_DEATH  : half_period=32, duration=250 -> longer low rumble (frozen pause)
 *   SFX_EXTRA_LIFE: half_period=6, duration=220 -> short rising blip
 *   SFX_BONUS  : half_period=10, duration=230 -> mid-length blip (frozen pause)
 *
 * sfx_blip inner loop (sfx.asm) is ~26 T per half-period iteration plus the
 * half_period delay count, so actual T ≈ duration * (26 + half_period * N)
 * where N is the inner nop-pad count.  The values above are conservative
 * upper bounds for the short SFX; the long SFX run during frozen pauses so
 * their budget is free.
 */

#include "sfx.h"

#ifdef __SDCC

/* Globals read by sfx_blip in sfx.asm. */
u8 sfx_hp;   /* half-period delay count (smaller = higher pitch) */
u8 sfx_dur;  /* number of half-periods to generate               */

/* Declare the asm routine -- parameterless, reads sfx_hp / sfx_dur. */
void sfx_blip(void);

/* (half_period, duration) table, indexed by SFX_* id. */
typedef struct { u8 hp; u8 dur; } sfx_params_t;

static const sfx_params_t sfx_table[SFX_N] = {
    /* SFX_SHOOT      */ { 16u, 110u },
    /* SFX_EXPLODE    */ {  8u, 200u },
    /* SFX_HIT        */ { 12u, 150u },
    /* SFX_DEATH      */ { 32u, 250u },
    /* SFX_EXTRA_LIFE */ {  6u, 220u },
    /* SFX_BONUS      */ { 10u, 230u },
};

void sfx_play(u8 id)
{
    if (id >= SFX_N) return;
    sfx_hp  = sfx_table[id].hp;
    sfx_dur = sfx_table[id].dur;
    sfx_blip();
}

#else /* host build -- pure no-op */

void sfx_play(u8 id)
{
    (void)id;
}

#endif /* __SDCC */
