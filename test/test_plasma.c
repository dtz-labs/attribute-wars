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
    u16 x, y, p;

    /* Determinism: same (x,y,phase) -> same field value. */
    check("field deterministic",
          plasma_field(7u, 19u, 100u) == plasma_field(7u, 19u, 100u));

    /* Field stays within the 3*127 amplitude bound. */
    {
        int ok = 1;
        for (p = 0; p < 256u; p += 17u)
            for (y = 0; y < 192u; y += 13u)
                for (x = 0; x < 32u; x++) {
                    s16 v = plasma_field((u8)x, (u8)y, (u8)p);
                    if (v > 381 || v < -381) ok = 0;
                }
        check("field within +/-381", ok);
    }

    /* Palette: text-readability invariant across the whole field range. */
    {
        int ink_ok = 1, paper_ok = 1;
        s16 v;
        for (v = -381; v <= 381; v++) {
            u8 a = plasma_palette(v);
            if ((a & 7u) != 7u) ink_ok = 0;                 /* ink must be white  */
            if (((a >> 3) & 7u) == 7u) paper_ok = 0;        /* paper never white  */
        }
        check("palette ink == white", ink_ok);
        check("palette paper != white", paper_ok);
    }

    /* Palette actually varies (not one flat colour). */
    check("palette varies", plasma_palette(-300) != plasma_palette(300));

    /* The renderer's separable decomposition must equal plasma_field (guards the
     * fast path in main.c against drift). */
    {
        int ok = 1;
        u8 ph = 77u, ph2 = (u8)(77u + 64u);
        for (y = 0; y < 192u; y += 11u)
            for (x = 0; x < 32u; x++) {
                s16 sep = (s16)fx_sin[(u8)(x * PLA_KX + ph)]
                        + (s16)fx_sin[(u8)(y * PLA_KY + ph)]
                        + (s16)fx_sin[(u8)((u8)(x + y) * PLA_KD + ph2)];
                if (sep != plasma_field((u8)x, (u8)y, ph)) ok = 0;
            }
        check("separable decomposition == plasma_field", ok);
    }

    if (failures == 0) { printf("test_plasma: ALL PASS\n"); return 0; }
    printf("test_plasma: %d FAILURE(S)\n", failures);
    return 1;
}
