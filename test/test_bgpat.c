/* test_bgpat.c -- host unit test for the background pattern generator. */
#include "bgpat.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(const char *name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); failures++; }
}

#define IMPL 4   /* number of pattern ids implemented so far (grows per task) */

static int paper_safe(u8 p) { return p <= 5u; }   /* {0..5}; never 6 or 7 */

static void check_invariants(u8 id)
{
    static u8 cells[BGPAT_CELLS];
    u8 r, c;
    char nm[48];
    int ink_ok = 1, paper_ok = 1, frame_ok = 1, filled_ok = 1;

    memset(cells, 0xAAu, sizeof cells);            /* sentinel: detect unwritten */
    bgpat_generate(cells, id, 0x1234u);

    for (r = 0; r < BGPAT_ROWS; r++) {
        for (c = 0; c < BGPAT_COLS; c++) {
            u8 v = cells[(u16)r * BGPAT_COLS + c];
            if (v == 0xAAu) filled_ok = 0;
            if (r == 0u || r == 23u || c == 0u || c == 31u) {
                if (v != (u8)BGPAT_FRAME_ATTR) frame_ok = 0;
            } else {
                if ((v & 7u) != 7u) ink_ok = 0;             /* ink must be white */
                if (!paper_safe((u8)((v >> 3) & 7u))) paper_ok = 0;
            }
        }
    }
    sprintf(nm, "id %u: all 768 cells written", id);      check(nm, filled_ok);
    sprintf(nm, "id %u: frame ring is FRAME_ATTR", id);   check(nm, frame_ok);
    sprintf(nm, "id %u: interior ink == white", id);      check(nm, ink_ok);
    sprintf(nm, "id %u: interior paper in safe set", id); check(nm, paper_ok);
}

int main(void)
{
    u8 id;
    for (id = 0; id < IMPL; id++) check_invariants(id);

    /* Determinism: same (id, seed) -> identical buffer. */
    {
        static u8 a[BGPAT_CELLS], b[BGPAT_CELLS];
        bgpat_generate(a, BGPAT_CHECKER, 0x9999u);
        bgpat_generate(b, BGPAT_CHECKER, 0x9999u);
        check("deterministic for (id,seed)", memcmp(a, b, sizeof a) == 0);
    }

    if (failures == 0) { printf("test_bgpat: ALL PASS\n"); return 0; }
    printf("test_bgpat: %d FAILURE(S)\n", failures);
    return 1;
}
