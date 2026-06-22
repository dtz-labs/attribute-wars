/*
 * rng.h -- tiny deterministic pseudo-random generator (16-bit Galois LFSR).
 *
 * No floating point, no division, ~cheap on Z80. Deterministic given a seed, so
 * it is host-unit-testable. Used for enemy wander direction etc.
 */
#ifndef RNG_H
#define RNG_H

#include "types.h"

/* Seed the generator. A zero seed is replaced (an LFSR locks up at 0). */
void rng_seed(u16 s);

/* Next pseudo-random byte (0..255). */
u8 rng_byte(void);

#endif /* RNG_H */
