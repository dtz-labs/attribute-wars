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

#ifdef __SDCC

/* The vendored PT3 player + our asm glue (music_ay.asm / pt3prom.asm). z88dk's
 * <psg/vt2.h> player ships only for the classic clib, so we vendor the player
 * and drive it through these parameterless, IY-safe wrappers instead. */
extern u8   ay_detect(void);            /* probe AY + latch the port scheme    */
extern void pt3_init(void);             /* load the tune + reset to the start  */
extern void pt3_play_safe(void);        /* play one frame (IY-safe)            */
extern void pt3_mute(void);             /* silence all channels                */

static u8 music_on;     /* 1 once an AY is detected */

u8 music_init(void)
{
    music_on = ay_detect();
    if (!music_on) {
        return 0;                       /* beeper-only machine -> stay silent */
    }
    pt3_init();                         /* hand the player the module, reset */
    return 1;
}

void music_tick(void)
{
    if (music_on) {
        pt3_play_safe();                /* one frame; writes AY via latched ports */
    }
}

void music_stop(void)
{
    if (music_on) {
        pt3_mute();
    }
}

#else /* host build -- pure no-op (no Z80/AY dependency) */

u8   music_init(void) { return 0; }
void music_tick(void) { }
void music_stop(void) { }

#endif /* __SDCC */
