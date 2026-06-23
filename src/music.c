/*
 * music.c -- AY music module (see music.h). Target-only; the host build is a
 * pure no-op so host unit tests never link any Z80/AY code.
 *
 * Uses z88dk's VortexTracker2 PT3 player (ay_vt2_*). The asm glue in
 * music_ay.asm latches either the TC2068 AY ports or the standard 128K ports,
 * and our override of asm_vt_hardware_out routes register writes through them.
 * MUSIC+FX and FX modes are driven by a 50 Hz IM2 ISR; BEEPER mode leaves the
 * ROM IM1 handler alone.
 */
#include "music.h"
#include "sfx.h"        /* SFX_* ids + SFX_N (AY SFX reuse the same ids)        */
#include "rng.h"        /* rng_byte() for the explosion crackle                */

#if defined(__SDCC) && defined(ZX128_NO_MUSIC)

u8   music_default_sound(void) { return SOUND_BEEPER; }
const char *music_status_text(void) { return "HW ZX128    AY OFF"; }
u8   music_init(u8 mode)    { (void)mode; return 0; }
void music_tick(void)       { }
void music_stop(void)       { }
u8   music_is_on(void)      { return 0; }
u8   music_is_playing(void) { return 0; }
void music_sfx(u8 id)       { (void)id; }
void music_sfx_noise(void)  { }

#elif defined(__SDCC)

/* The vendored PT3 player + our asm glue (music_ay.asm / pt3prom.asm). z88dk's
 * <psg/vt2.h> player ships only for the classic clib, so we vendor the player
 * and drive it through these parameterless, IY-safe wrappers instead. */
extern u8   ay_detect(void);            /* probe AY + latch the port scheme    */
extern u8   ay_default_sound(void);     /* choose title SOUND default          */
extern u8   ay_machine_status(void);    /* packed machine/AY title status      */
extern void ay_set_ports_std(void);     /* latch standard 128K AY ports        */
extern void ay_sfx_out(void);           /* FX-only: emit channel-C state       */
extern void ay_sfx_mute(void);          /* FX-only: silence channel C          */
extern void pt3_init(void);             /* load the tune + reset to the start  */
extern void pt3_play_safe(void);        /* play one frame (IY-safe)            */
extern void pt3_mute(void);             /* silence all channels                */
extern void music_im2_init(void);       /* IM1 -> IM2: the ISR drives the player */
extern void music_im1_init(void);       /* IM2 -> IM1 for BEEPER/menu reset    */

static u8 ay_on;        /* 1 when AY output is enabled for SFX */
static u8 music_on;     /* 1 when the PT3 tune is playing      */
static u8 sound_mode = SOUND_BEEPER;

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
    /* SFX_DASH       */ { 1u,   0u,  5u, 4u },  /* bright noise whoosh       */
    /* SFX_DASH_READY */ { 0u,  66u,  0u, 2u },  /* high ready chime          */
    /* SFX_DASH_FAIL  */ { 0u, 260u,  0u, 4u },  /* low rejected thud         */
};

u8 music_default_sound(void)
{
    return ay_default_sound();
}

const char *music_status_text(void)
{
    switch (ay_machine_status()) {
        case 0x00u: return "HW ZX48     AY NO";
        case 0x11u: return "HW ZX128    AY ZX";
        case 0x02u: return "HW TC2048   AY NO";
        case 0x23u: return "HW TC2068   AY TIMEX";
        default:    return "HW UNKNOWN  AY ?";
    }
}

u8 music_init(u8 mode)
{
    if (mode == sound_mode) {
        if (mode == SOUND_BEEPER && !ay_on) {
            return 0u;
        }
        if (mode == SOUND_FX && ay_on && !music_on) {
            return 1u;
        }
        if (mode == SOUND_MUSIC_FX && ay_on && music_on) {
            return 1u;
        }
    }

    if (ay_on) {
        if (music_on) {
            pt3_mute();
        } else {
            ay_sfx_mute();
        }
    }
    ay_on = 0u;
    music_on = 0u;
    asfx_vol = 0u;
    sound_mode = mode;

    if (mode == SOUND_BEEPER) {
        music_im1_init();
        return 0u;
    }

    /* Prefer TC2068 ports only when ROM-confirmed. Otherwise use the standard
     * odd AY ports; on a forced TC2048-without-AY choice this is safely silent,
     * never the ULA/beeper. */
    if (!ay_detect()) {
        ay_set_ports_std();
    }

    ay_on = 1u;
    if (mode == SOUND_MUSIC_FX) {
        music_on = 1u;
        pt3_init();                     /* hand the player the module, reset */
    }
    music_im2_init();                   /* 50 Hz music/SFX envelope driver */
    return 1u;
}

void music_tick(void)
{
    if (!ay_on) {
        return;
    }
    if (music_on) {
        pt3_play_safe();                /* play one frame (override merges SFX) */
    } else if (asfx_vol) {
        ay_sfx_out();                   /* FX-only: output channel C directly */
    }
    if (asfx_vol) {                     /* decay the channel-C SFX envelope     */
        asfx_vol = (u8)((asfx_vol > asfx_step) ? (asfx_vol - asfx_step) : 0u);
        if (!music_on && !asfx_vol) {
            ay_sfx_mute();              /* no PT3 player will overwrite amp C */
        }
    }
}

void music_stop(void)
{
    if (music_on) {
        pt3_mute();
    } else if (ay_on) {
        ay_sfx_mute();
    }
}

u8 music_is_on(void)
{
    return ay_on;
}

u8 music_is_playing(void)
{
    return music_on;
}

void music_sfx(u8 id)
{
    if (!ay_on || id >= SFX_N) {
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
    if (!ay_on) {
        return;
    }
    asfx_kind = 1u;                     /* noise */
    asfx_nper = (u8)(1u + (rng_byte() & 0x1Fu));   /* random hiss 1..32 */
    asfx_step = 5u;                                /* very short crackle */
    asfx_vol  = 15u;
}

#else /* host build -- pure no-op (no Z80/AY dependency) */

u8   music_default_sound(void) { return SOUND_BEEPER; }
const char *music_status_text(void) { return "HW HOST     AY NO"; }
u8   music_init(u8 mode)    { (void)mode; return 0; }
void music_tick(void)       { }
void music_stop(void)       { }
u8   music_is_on(void)      { return 0; }
u8   music_is_playing(void) { return 0; }
void music_sfx(u8 id)       { (void)id; }
void music_sfx_noise(void)  { }

#endif /* __SDCC */
