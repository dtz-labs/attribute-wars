/*
 * test_rng.c -- host unit test for the LFSR PRNG (rng.c).
 */
#include "rng.h"

#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    int seen[256];
    int i, distinct = 0, stuck = 0;
    u8 prev;

    rng_seed(0xBEEF);

    for (i = 0; i < 256; i++) seen[i] = 0;
    prev = rng_byte();
    seen[prev] = 1;
    for (i = 0; i < 1000; i++) {
        u8 v = rng_byte();
        if (v == prev) stuck++;
        prev = v;
        seen[v] = 1;
    }
    for (i = 0; i < 256; i++) distinct += seen[i];

    /* A healthy byte stream covers most values and rarely repeats back-to-back. */
    check("good spread (>200 distinct of 256)", distinct > 200);
    check("not stuck (few repeats in 1000)",    stuck < 50);

    /* Seeding with 0 must not lock the generator at 0. */
    rng_seed(0);
    check("zero seed not locked", rng_byte() != 0 || rng_byte() != 0);

    /* Determinism: same seed -> same sequence. */
    {
        u8 a, b;
        rng_seed(1234); a = rng_byte();
        rng_seed(1234); b = rng_byte();
        check("deterministic for a seed", a == b);
    }

    if (failures == 0) { printf("test_rng: ALL PASS\n"); return 0; }
    printf("test_rng: %d FAILURE(S)\n", failures);
    return 1;
}
