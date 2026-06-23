/* test_plasma.c -- host unit test for the game-over plasma field + palette. */
#include "plasma.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    u16 x, y, ph;

    /* Determinism: the field is static (no phase). */
    check("field deterministic", plasma_field(7u, 19u) == plasma_field(7u, 19u));

    /* Field stays within the 3*127 amplitude bound over the 32x24 cell grid. */
    {
        int ok = 1;
        for (y = 0; y < 24u; y++)
            for (x = 0; x < 32u; x++) {
                s16 v = plasma_field((u8)x, (u8)y);
                if (v > 381 || v < -381) ok = 0;
            }
        check("field within +/-381", ok);
    }

    /* Palette across the whole field range AND every phase:
     * ink white, paper non-white, and NEVER bright (the "darker colours" rule). */
    {
        int ink_ok = 1, paper_ok = 1, dark_ok = 1;
        s16 v;
        for (ph = 0; ph < 256u; ph += 7u)
            for (v = -381; v <= 381; v += 3) {
                u8 a = plasma_palette(v, (u8)ph);
                if ((a & 7u) != 7u) ink_ok = 0;             /* ink == white       */
                if (((a >> 3) & 7u) == 7u) paper_ok = 0;    /* paper != white     */
                if (((a >> 6) & 1u) != 0u) dark_ok = 0;     /* bright bit == 0    */
            }
        check("palette ink == white", ink_ok);
        check("palette paper != white", paper_ok);
        check("palette never bright (darker colours)", dark_ok);
    }

    /* Phase actually rotates the colour (shimmer). */
    {
        int varies = 0;
        s16 v;
        for (v = -381; v <= 381 && !varies; v += 1)
            if (plasma_palette(v, 0u) != plasma_palette(v, 8u)) varies = 1;
        check("palette cycles with phase", varies);
    }

    /* The renderer's field formula must match plasma_field (guards main.c). */
    {
        int ok = 1;
        for (y = 0; y < 24u; y++)
            for (x = 0; x < 32u; x++) {
                s16 f = (s16)fx_sin[(u8)(x * PLA_KX)]
                      + (s16)fx_sin[(u8)(y * PLA_KY)]
                      + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD)];
                if (f != plasma_field((u8)x, (u8)y)) ok = 0;
            }
        check("field formula == plasma_field", ok);
    }

    if (failures == 0) { printf("test_plasma: ALL PASS\n"); return 0; }
    printf("test_plasma: %d FAILURE(S)\n", failures);
    return 1;
}
