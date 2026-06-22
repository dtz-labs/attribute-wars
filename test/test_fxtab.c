/* test_fxtab.c -- host unit test for the fixed-point sine table + multiply. */
#include "fxtab.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    int i;

    /* Quadrant anchors. */
    check("sin[0]   == 0",    fx_sin[0]   == 0);
    check("sin[64]  == 127",  fx_sin[64]  == 127);
    check("sin[128] == 0",    fx_sin[128] == 0);
    check("sin[192] == -127", fx_sin[192] == -127);

    /* Antisymmetry: sin[i] == -sin[i+128]. */
    {
        int ok = 1;
        for (i = 0; i < 256; i++) {
            if (fx_sin[i] != (s8)(-fx_sin[(i + 128) & 255])) { ok = 0; break; }
        }
        check("antisymmetric over half period", ok);
    }

    /* Amplitude bound. */
    {
        int ok = 1;
        for (i = 0; i < 256; i++) if (fx_sin[i] > 127 || fx_sin[i] < -127) { ok = 0; break; }
        check("within +/-127", ok);
    }

    /* fx_mul exact cases (no rounding ambiguity). */
    check("mul 127*128>>7 == 127", fx_mul(127, 128) == 127);
    check("mul 0*200 == 0",        fx_mul(0, 200)   == 0);
    check("mul 64*128>>7 == 64",   fx_mul(64, 128)  == 64);
    check("mul -64*128>>7 == -64", fx_mul(-64, 128) == -64);

    if (failures == 0) { printf("test_fxtab: ALL PASS\n"); return 0; }
    printf("test_fxtab: %d FAILURE(S)\n", failures);
    return 1;
}
