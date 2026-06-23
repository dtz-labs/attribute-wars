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
    const u8 CX = 128u, CY = 60u, R = 36u;
    u8 i, n;

    globe_init(CX, CY, R);
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
                if (x < (u8)(CX - R) || x > (u8)(CX + R)) x_ok = 0;
                if (y < (u8)(CY - R) || y > (u8)(CY + R)) y_ok = 0;
            }
        check("x within bounds (all theta)", x_ok);
        check("y within bounds", y_ok);
    }

    check("x moves under rotation", globe_x(5, 0u) != globe_x(5, 64u));
    check("x deterministic", globe_x(7, 100u) == globe_x(7, 100u));

    /* Each point is front-facing for roughly half a full turn. */
    {
        u16 t, fc = 0;
        for (t = 0; t < 256u; t++) if (globe_front(9, (u8)t)) fc++;
        check("front ~half the turn", fc > 80u && fc < 176u);
    }

    /* Both meridian and parallel points exist. */
    {
        int merid = 0, par = 0;
        for (i = 0; i < n; i++) {
            if (globe_is_meridian(i)) merid++; else par++;
        }
        check("has meridian points", merid > 0);
        check("has parallel points", par > 0);
    }

    /* globe_init is reusable with a different size (no out-of-bounds). */
    {
        int ok = 1;
        globe_init(128u, 96u, 64u);
        for (i = 0; i < globe_count(); i++) {
            u8 y = globe_y(i);
            if (y < 32u || y > 160u) ok = 0;
        }
        check("re-init resizes cleanly", ok);
    }

    if (failures == 0) { printf("test_globe: ALL PASS\n"); return 0; }
    printf("test_globe: %d FAILURE(S)\n", failures);
    return 1;
}
