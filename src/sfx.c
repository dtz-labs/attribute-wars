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
 * Timing (MEASURED against sfx.asm, TC2048 @ 3.5 MHz): each half-period costs
 * ~(60 + 16*half_period) T, so total ≈ duration * (60 + 16*hp). The earlier
 * estimate here was ~2.3x too low and made the SHOOT/EXPLODE/HIT blips block
 * for ~HALF A FRAME each -> a per-shot/per-kill stutter. The LIVE-frame SFX
 * (SHOOT/EXPLODE/HIT/EXTRA_LIFE) are now sized for <= ~3.5k T (~5% of a 69,888 T
 * frame) so they never drop a frame. DEATH and BONUS play during a frozen pause
 * (death_anim / wave-clear) so they can be longer.
 *   SHOOT      hp=18 dur=10 -> ~3.5k T   (short tick)
 *   EXPLODE    hp=10 dur=16 -> ~3.5k T
 *   HIT        hp=14 dur=12 -> ~3.4k T
 *   EXTRA_LIFE hp= 8 dur=18 -> ~3.4k T   (plays on a live frame)
 *   DEATH      hp=30 dur=90 -> frozen pause, free
 *   BONUS      hp=10 dur=22 -> ~4.8k T   (brief, before the next telegraph)
 */

#include "sfx.h"
#include "music.h"  /* route SFX to the AY when an AY tune is playing */

#ifdef __SDCC

#include "rng.h"   /* rng_byte() for the explosion crackle */

/* Globals read by sfx_blip in sfx.asm. */
u8 sfx_hp;   /* half-period delay count (smaller = higher pitch) */
u8 sfx_dur;  /* number of half-periods to generate               */

/* Declare the asm routine -- parameterless, reads sfx_hp / sfx_dur. */
void sfx_blip(void);

/* (half_period, duration) table, indexed by SFX_* id. */
typedef struct { u8 hp; u8 dur; } sfx_params_t;

static const sfx_params_t sfx_table[SFX_N] = {
    /* The "super" set the player liked, kept rich (~2/3 of the original length,
     * ~22k T each ~= 6 ms). The stutter was the score-digit BACKGROUND, not the
     * sound; with that reverted there is room. ~22k leaves ~8k margin under a
     * busy shot frame so it never overruns 50 Hz. (Full original length ~35k
     * would need a non-blocking beeper -- offered if you want it.) */
    /* SFX_SHOOT fires constantly, so it must be SHORT or every shot drops a
     * frame. ~7k T (~10% of a frame) -- a snappy pew at the same pitch. */
    /* SFX_SHOOT      */ { 16u,  22u },   /* live: ~7k T  short pew             */
    /* SFX_EXPLODE    */ {  8u, 120u },   /* (one-shot unused; per-frame noise)  */
    /* SFX_HIT        */ { 12u,  26u },   /* live: ~6.5k T  short -- fires on    */
                                          /* enemy contact, must not drop a frame */
    /* SFX_DEATH      */ { 32u, 250u },   /* frozen death_anim -> budget free   */
    /* SFX_EXTRA_LIFE */ {  6u, 140u },   /* live: ~21.8k T  rising chime       */
    /* SFX_BONUS      */ { 10u,  90u },   /* ~19.8k T, before the next telegraph */
    /* SFX_DASH       */ {  4u,  42u },   /* quick high lunge tick              */
    /* SFX_DASH_READY */ {  6u,  50u },   /* short ready chime                  */
    /* SFX_DASH_FAIL  */ { 24u,  22u },   /* low cooldown bump                  */
};

void sfx_play(u8 id)
{
    if (id >= SFX_N) return;
    if (music_is_on()) {        /* AY mode: play on channel C (no beeper stall) */
        music_sfx(id);
        return;
    }
    sfx_hp  = sfx_table[id].hp;
    sfx_dur = sfx_table[id].dur;
    sfx_blip();
}

/* A few back-to-back short bursts at random pitches -> a harsh crackle. Called
 * once per frame while an explosion animates, so the whole burst is noisy.
 * ~6k T total, safe in the live loop. */
void sfx_noise(void)
{
    u8 j;
    if (music_is_on()) {        /* AY mode: noise crackle on channel C */
        music_sfx_noise();
        return;
    }
    for (j = 0; j < 2u; j++) {
        sfx_hp  = (u8)(1u + (rng_byte() & 0x0Fu));   /* random pitch 1..16 */
        sfx_dur = 9u;
        sfx_blip();
    }
}

#else /* host build -- pure no-op */

void sfx_play(u8 id)
{
    (void)id;
}

void sfx_noise(void)
{
}

#endif /* __SDCC */
