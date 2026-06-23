/*
 * music.c -- AY music module (see music.h). Target-only; the host build is a
 * pure no-op so host unit tests never link any Z80/AY code.
 *
 * Uses z88dk's VortexTracker2 PT3 player (ay_vt2_*). Two local asm pieces make
 * it work here (music_ay.asm): ay_detect() probes the chip + latches the port
 * scheme, and our override of the player's asm_vt_hardware_out routes register
 * writes through those latched ports (so both 128K and TS2068 play). The player
 * is driven from the main loop (one pt3_play_safe() per HALT) -- no interrupt
 * mode change; the IY-preserving shim makes that call safe under sdcc_iy.
 */
#include "music.h"
#include "sfx.h"        /* SFX_* ids + SFX_N (AY SFX reuse the same ids)        */
#include "rng.h"        /* rng_byte() for the explosion crackle                */

#ifdef __SDCC

/* The vendored PT3 player + our asm glue (music_ay.asm / pt3prom.asm). z88dk's
 * <psg/vt2.h> player ships only for the classic clib, so we vendor the player
 * and drive it through these parameterless, IY-safe wrappers instead. */
extern u8   ay_detect(void);            /* probe AY + latch the port scheme    */
extern void pt3_init(void);             /* load the tune + reset to the start  */
extern void pt3_play_safe(void);        /* play one frame (IY-safe)            */
extern void pt3_mute(void);             /* silence all channels                */
extern void music_im2_init(void);       /* IM1 -> IM2: the ISR drives the player */

static u8 music_on;     /* 1 once an AY is detected */

/* ---- AY channel-C sound effects --------------------------------------------
 * State read by the asm_vt_hardware_out merge in music_ay.asm: while asfx_vol>0
 * it overlays channel C (amp + tone/noise) onto the player's register output.
 * The volume decays by asfx_step each frame -> a natural percussive envelope. */
u8  asfx_vol;     /* 0..15, 0 = no SFX active */
u8  asfx_kind;    /* 0 = tone (R4/R5), 1 = noise (R6) */
u16 asfx_tper;    /* tone period (channel C) */
u8  asfx_nper;    /* noise period (shared R6) */
static u8 asfx_step;   /* volume decay per frame */

/* Per-SFX voice: (kind, tone period, noise period, decay step). Tuned by ear;
 * AY clock ~1.77 MHz so tone freq ~= 110800 / period. */
typedef struct { u8 kind; u16 tper; u8 nper; u8 step; } asfx_voice_t;
static const asfx_voice_t asfx_tbl[SFX_N] = {
    /* SFX_SHOOT      */ { 0u, 126u,  0u, 5u },  /* ~880 Hz pew, fast decay   */
    /* SFX_EXPLODE    */ { 1u,   0u, 10u, 2u },  /* bright noise burst        */
    /* SFX_HIT        */ { 1u,   0u, 16u, 3u },  /* shorter noise tick        */
    /* SFX_DEATH      */ { 1u,   0u, 24u, 1u },  /* long low noise boom       */
    /* SFX_EXTRA_LIFE */ { 0u,  84u,  0u, 1u },  /* ~1320 Hz chime, slow fade */
    /* SFX_BONUS      */ { 0u, 106u,  0u, 2u },  /* ~1045 Hz blip             */
};

u8 music_init(void)
{
    music_on = ay_detect();
    if (!music_on) {
        return 0;                       /* beeper-only machine -> stay silent */
    }
    pt3_init();                         /* hand the player the module, reset */
    music_im2_init();                   /* drive the player from the 50 Hz ISR */
    return 1;
}

void music_tick(void)
{
    if (!music_on) {
        return;
    }
    pt3_play_safe();                    /* play one frame (override merges SFX) */
    if (asfx_vol) {                     /* decay the channel-C SFX envelope     */
        asfx_vol = (u8)((asfx_vol > asfx_step) ? (asfx_vol - asfx_step) : 0u);
    }
}

void music_stop(void)
{
    if (music_on) {
        pt3_mute();
    }
}

u8 music_is_on(void)
{
    return music_on;
}

void music_sfx(u8 id)
{
    if (!music_on || id >= SFX_N) {
        return;
    }
    asfx_kind = asfx_tbl[id].kind;
    asfx_tper = asfx_tbl[id].tper;
    asfx_nper = asfx_tbl[id].nper;
    asfx_step = asfx_tbl[id].step;
    asfx_vol  = 15u;                    /* full, then decays each frame */
}

void music_sfx_noise(void)
{
    if (!music_on) {
        return;
    }
    asfx_kind = 1u;                     /* noise */
    asfx_nper = (u8)(1u + (rng_byte() & 0x1Fu));   /* random hiss 1..32 */
    asfx_step = 5u;                                /* very short crackle */
    asfx_vol  = 15u;
}

#else /* host build -- pure no-op (no Z80/AY dependency) */

u8   music_init(void)       { return 0; }
void music_tick(void)       { }
void music_stop(void)       { }
u8   music_is_on(void)      { return 0; }
void music_sfx(u8 id)       { (void)id; }
void music_sfx_noise(void)  { }

#endif /* __SDCC */
