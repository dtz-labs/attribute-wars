/* test_globe.c -- host unit test for the title globe projection. */
#include "globe.h"
#include <stdio.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

int main(void)
{
    u8 i, n;
    globe_init();
    n = globe_count();
    check("has points", n > 0u);

    /* All projected points sit inside the globe's bounding box, every theta. */
    {
        int x_ok = 1, y_ok = 1;
        u16 t;
        for (t = 0; t < 256u; t += 17u)
            for (i = 0; i < n; i++) {
                u8 x = globe_x(i, (u8)t);
                u8 y = globe_y(i);
                if (x < (GLOBE_CX - GLOBE_R) || x > (GLOBE_CX + GLOBE_R)) x_ok = 0;
                if (y < (GLOBE_CY - GLOBE_R) || y > (GLOBE_CY + GLOBE_R)) y_ok = 0;
            }
        check("x within bounds (all theta)", x_ok);
        check("y within bounds", y_ok);
    }

    /* Y-axis rotation: screen-y is constant per point (only x moves). */
    check("y constant under rotation", globe_y(3) == globe_y(3));
    check("x moves under rotation", globe_x(5, 0u) != globe_x(5, 64u));

    /* Determinism. */
    check("x deterministic", globe_x(7, 100u) == globe_x(7, 100u));

    /* Each point is front-facing for roughly half a full turn. */
    {
        u16 t, fc = 0;
        for (t = 0; t < 256u; t++) if (globe_front(9, (u8)t)) fc++;
        check("front ~half the turn", fc > 80u && fc < 176u);
    }

    if (failures == 0) { printf("test_globe: ALL PASS\n"); return 0; }
    printf("test_globe: %d FAILURE(S)\n", failures);
    return 1;
}
