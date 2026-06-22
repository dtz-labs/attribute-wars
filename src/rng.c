/*
 * rng.c -- 16-bit Galois LFSR pseudo-random generator (see rng.h).
 * Taps 0xB400 give a maximal-length (period 65535) sequence.
 */
#include "rng.h"

static u16 state = 0xACE1u;   /* arbitrary non-zero seed */

void rng_seed(u16 s)
{
    state = s ? s : 0xACE1u;  /* never 0 (the LFSR would lock) */
}

u8 rng_byte(void)
{
    u16 lsb = (u16)(state & 1u);
    state = (u16)(state >> 1);
    if (lsb) {
        state ^= 0xB400u;
    }
    return (u8)state;
}
