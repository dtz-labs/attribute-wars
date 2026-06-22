/*
 * hud.c -- HUD widgets + the score-as-big-attribute-digits background.
 *
 * Target-only drawing (touches the SCLD attribute blocks and both bitmaps); no
 * host test. See hud.h for the contract and design spec §9 for the layout.
 *
 * Layout pinned by the spec:
 *   - rows 0 and 23 are HUD bands (black paper / white ink);
 *   - cols 0 and 31 (rows 1..22) are the magenta arena wall;
 *   - the score is a 3x5-cell font, 6 digits, origin row 9 / start col 4
 *     (each digit 3 cols + 1 gap -> cols 4..26, rows 9..13). A LIT score cell
 *     gets dark-blue paper / white ink so white sprites stay readable; unlit
 *     interior cells are black.
 *
 * score_cell_attr() reads a cached snapshot of the digits (last_digits) so it
 * never dereferences a score_t in the per-frame restore path. The caller keeps
 * the cache current via hud_paint_background() (full repaint) and
 * hud_score_changed() (incremental, changed digits only).
 */
#include "hud.h"
#include "scld.h"
#include "sprite.h"
#include "sprites.h"   /* spr_heart */
#include "player.h"    /* BOOST_MAX */
#include <string.h>    /* memset (top-border bitmap clear) */

/* ---- score-digit geometry (spec §9, pinned) ---------------------------- */
#define DIGIT_W      3u    /* cells wide per glyph                          */
#define DIGIT_H      5u    /* cells tall per glyph                          */
#define DIGIT_GAP    1u    /* blank cells between glyphs                    */
#define DIGIT_PITCH  (DIGIT_W + DIGIT_GAP)        /* 4 cells per digit slot */
#define SCORE_COL0   4u    /* start col of digit 0 (cols 4..26 for 6 digits)*/
#define SCORE_ROW0   9u    /* top row of the digit band (rows 9..13)        */
#define SCORE_NDIG   6u

/* Cell colours. */
#define ATTR_HUD     ATTR(0, 0, 7)   /* HUD bands: black paper, white ink   */
#define ATTR_WALL    ATTR(1, 3, 0)   /* side rails: bright magenta          */
#define ATTR_LIT     ATTR(0, 1, 7)   /* lit digit cell: dark-blue paper     */
#define ATTR_BLACK   ATTR(0, 0, 7)   /* unlit interior cell: black          */

/* 3x5 digit glyphs: 5 rows, low 3 bits = the 3 columns (bit 2 = leftmost). */
static const u8 digitfont[10][DIGIT_H] = {
    { 0x7, 0x5, 0x5, 0x5, 0x7 },   /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x7 },   /* 1 */
    { 0x7, 0x1, 0x7, 0x4, 0x7 },   /* 2 */
    { 0x7, 0x1, 0x7, 0x1, 0x7 },   /* 3 */
    { 0x5, 0x5, 0x7, 0x1, 0x1 },   /* 4 */
    { 0x7, 0x4, 0x7, 0x1, 0x7 },   /* 5 */
    { 0x7, 0x4, 0x7, 0x5, 0x7 },   /* 6 */
    { 0x7, 0x1, 0x2, 0x2, 0x2 },   /* 7 */
    { 0x7, 0x5, 0x7, 0x5, 0x7 },   /* 8 */
    { 0x7, 0x5, 0x7, 0x1, 0x7 },   /* 9 */
};

/* Cached snapshot of the painted score digits; score_cell_attr() reads this. */
static u8 last_digits[SCORE_NDIG] = { 0u, 0u, 0u, 0u, 0u, 0u };

/* HUD's own pre-shifted heart table (built once by hud_init()). Self-contained
 * so main.c need not hand its sprite tables across the module boundary. */
static u8 ps_hud_heart[SPR_PRESHIFT_SIZE];

void hud_init(void)
{
    spr_preshift(ps_hud_heart, spr_heart);
}

/* ---- shared cell writer (declared in hud.h; used by main.c too) -------- */
void put_attr(u8 row, u8 col, u8 v)
{
    u16 i = (u16)row * 32u + (u16)col;
    ((u8 *)SCLD_ATTRS_A)[i] = v;
    ((u8 *)SCLD_ATTRS_B)[i] = v;
}

/* True when attribute cell (row,col) lies inside digit slot `d`'s 3x5 box AND
 * the cached glyph lights that (sub-row, sub-col). Returns 0 outside the box. */
static u8 score_cell_lit(u8 row, u8 col)
{
    u8 d, slot_col, sub_col, sub_row, bit;

    if (row < SCORE_ROW0 || row >= (u8)(SCORE_ROW0 + DIGIT_H)) {
        return 0u;
    }
    if (col < SCORE_COL0) {
        return 0u;
    }
    {
        u8 rel = (u8)(col - SCORE_COL0);
        d       = (u8)(rel / DIGIT_PITCH);     /* which digit slot           */
        slot_col = (u8)(rel % DIGIT_PITCH);    /* column within the slot     */
    }
    if (d >= SCORE_NDIG || slot_col >= DIGIT_W) {
        return 0u;                              /* past last digit or in gap  */
    }
    sub_col = slot_col;                         /* 0 = leftmost (bit 2)       */
    sub_row = (u8)(row - SCORE_ROW0);           /* 0..4                       */
    /* bit 2 = leftmost column, bit 0 = rightmost. */
    bit = (u8)(digitfont[last_digits[d]][sub_row] & (u8)(1u << (2u - sub_col)));
    return (u8)(bit != 0u);
}

u8 score_cell_attr(u8 row, u8 col)
{
    if (row == 0u || row == 23u) {
        return ATTR_HUD;                        /* HUD bands                  */
    }
    if (col == 0u || col == 31u) {
        return ATTR_WALL;                       /* side rails (rows 1..22)    */
    }
    if (score_cell_lit(row, col)) {
        return ATTR_LIT;                        /* lit score cell: dark paper */
    }
    return ATTR_BLACK;                          /* interior / unlit           */
}

void hud_paint_background(const score_t *s)
{
    u8 *a = (u8 *)SCLD_ATTRS_A;
    u8 *b = (u8 *)SCLD_ATTRS_B;
    u8  row, col, i;
    u16 k = 0;

    for (i = 0; i < SCORE_NDIG; i++) {          /* refresh the cache first    */
        last_digits[i] = s->digits[i];
    }
    for (row = 0; row < 24u; row++) {
        for (col = 0; col < 32u; col++, k++) {
            u8 v = score_cell_attr(row, col);
            a[k] = v;
            b[k] = v;
        }
    }
}

/* Repaint one digit slot's 3x5 box (both blocks) from the cache. */
static void paint_digit_slot(u8 d)
{
    u8 base_col = (u8)(SCORE_COL0 + d * DIGIT_PITCH);
    u8 dr, dc;
    for (dr = 0; dr < DIGIT_H; dr++) {
        u8 row = (u8)(SCORE_ROW0 + dr);
        for (dc = 0; dc < DIGIT_W; dc++) {
            u8 col = (u8)(base_col + dc);
            put_attr(row, col, score_cell_attr(row, col));
        }
    }
}

void hud_score_changed(const score_t *s)
{
    u8 d;
    for (d = 0; d < SCORE_NDIG; d++) {
        if (s->digits[d] != last_digits[d]) {
            last_digits[d] = s->digits[d];      /* update cache BEFORE repaint*/
            paint_digit_slot(d);                /* score_cell_attr now lit ok */
        }
    }
}

/* ---- top-border bitmap widgets (hearts left, shield dots right) -------- */
/* Cached so the per-event redraw is skipped when nothing changed. */
static u8 last_lives   = 0xFFu;
static u8 last_shields = 0xFFu;

/* Clear the 8-scanline top-border bitmap strip on both screens. */
static void hud_clear_top_strip(void)
{
    u8 r;
    for (r = 0; r < 8u; r++) {
        memset(scld_scanline(SCLD_SCREEN_A, r), 0, SCLD_ROW_BYTES);
        memset(scld_scanline(SCLD_SCREEN_B, r), 0, SCLD_ROW_BYTES);
    }
}

void hud_draw_lives(u8 lives)
{
    u8 i;
    if (lives == last_lives) {
        return;
    }
    last_lives = lives;
    /* Hearts share the top strip with the shield dots; clearing the strip
     * would wipe the dots, so erase only the hearts region (left half) and
     * leave the right (dots) untouched. Repaint hearts. */
    {
        u8 r;
        for (r = 0; r < 8u; r++) {
            memset(scld_scanline(SCLD_SCREEN_A, r), 0, 16u);   /* cols 0..15 */
            memset(scld_scanline(SCLD_SCREEN_B, r), 0, 16u);
        }
    }
    for (i = 0; i < lives && i < 8u; i++) {     /* hearts left, capped at 8   */
        u8 x = (u8)(8u + i * 8u);
        spr_draw(SCLD_SCREEN_A, x, 0, ps_hud_heart);
        spr_draw(SCLD_SCREEN_B, x, 0, ps_hud_heart);
    }
}

void hud_draw_shields(u8 shields)
{
    u8 i;
    if (shields == last_shields) {
        return;
    }
    last_shields = shields;
    /* Erase only the dots region (right half, cols 16..31) so the hearts on
     * the left survive. */
    {
        u8 r;
        for (r = 0; r < 8u; r++) {
            memset(scld_scanline(SCLD_SCREEN_A, r) + 16u, 0, 16u);  /* cols16..31*/
            memset(scld_scanline(SCLD_SCREEN_B, r) + 16u, 0, 16u);
        }
    }
    for (i = 0; i < shields && i < 8u; i++) {   /* shield dots, right edge    */
        u8 x = (u8)(240u - i * 8u);
        bul_draw(SCLD_SCREEN_A, x, 2);
        bul_draw(SCLD_SCREEN_B, x, 2);
    }
}

/* ---- bottom-border bars (attribute paper-fill, proportional) ----------- */
#define TIMER_COL0   1u    /* timer bar cols 1..15  (15 cells)              */
#define TIMER_CELLS  15u
#define BOOST_COL0   17u   /* boost bar cols 17..30 (14 cells)              */
#define BOOST_CELLS  14u
#define ATTR_TIMER   ATTR(1, 6, 0)   /* yellow paper                        */
#define ATTR_BOOST   ATTR(1, 5, 0)   /* cyan paper                          */
#define ATTR_EMPTY   ATTR_HUD        /* unfilled bar cell: black HUD band   */

/* Scale a 0..total value to a 0..cells bar length (rounds to nearest, no
 * float; clamps to cells). num==0 -> 0, num>=total -> cells. */
static u8 bar_len(u16 num, u16 total, u8 cells)
{
    u16 r;
    if (total == 0u || num == 0u) {
        return 0u;
    }
    if (num >= total) {
        return cells;
    }
    /* (num * cells + total/2) / total -- products fit in u16 here:
     * num <= 1500, cells <= 15 -> <= 22500 < 65536. */
    r = (u16)(((u16)(num * (u16)cells) + (u16)(total >> 1)) / total);
    if (r > (u16)cells) {
        r = (u16)cells;
    }
    return (u8)r;
}

static u8 last_timer_len = 0xFFu;
static u8 last_boost_len = 0xFFu;

static void draw_bar(u8 col0, u8 cells, u8 fill, u8 fill_attr)
{
    u8 c;
    for (c = 0; c < cells; c++) {
        put_attr(23u, (u8)(col0 + c), (u8)(c < fill ? fill_attr : ATTR_EMPTY));
    }
}

void hud_draw_timer(u16 frames_left, u16 frames_total)
{
    u8 len = bar_len(frames_left, frames_total, TIMER_CELLS);
    if (len == last_timer_len) {
        return;
    }
    last_timer_len = len;
    draw_bar(TIMER_COL0, TIMER_CELLS, len, ATTR_TIMER);
}

void hud_draw_boost(u8 energy)
{
    u8 len = bar_len((u16)energy, (u16)BOOST_MAX, BOOST_CELLS);
    if (len == last_boost_len) {
        return;
    }
    last_boost_len = len;
    draw_bar(BOOST_COL0, BOOST_CELLS, len, ATTR_BOOST);
}

void hud_invalidate(void)
{
    last_lives     = 0xFFu;
    last_shields   = 0xFFu;
    last_timer_len = 0xFFu;
    last_boost_len = 0xFFu;
}
