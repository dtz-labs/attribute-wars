/*
 * input.c -- control input decode.
 *
 * The decode logic is PURE (no hardware) and host-tested. The actual
 * joystick/keyboard reads live in input_read(), compiled only for the
 * Spectrum/Timex target (__SPECTRUM) where the z88dk library exists.
 */
#include "controls.h"
#include "geometry.h"

/* Key bit (Q,W,E,A,D,Z,X,C) -> direction. Index = bit position 0..7. */
static const u8 key_dir[8] = {
    DIR_NW, DIR_N, DIR_NE,   /* Q W E */
    DIR_W,         DIR_E,    /* A . D */
    DIR_SW, DIR_S, DIR_SE    /* Z X C */
};

void decode_move(u8 joy, s8 *dx, s8 *dy)
{
    s8 x = 0, y = 0;
    if (joy & JOY_RIGHT) ++x;
    if (joy & JOY_LEFT)  --x;
    if (joy & JOY_DOWN)  ++y;
    if (joy & JOY_UP)    --y;   /* y grows DOWN, so UP is negative */
    *dx = x;
    *dy = y;
}

u8 decode_aim_keys(u8 keys)
{
    u8 i;
    for (i = 0; i < 8; ++i) {
        if (keys & (u8)(1u << i)) {
            return key_dir[i];
        }
    }
    return DIR_NONE;
}

u8 update_facing(u8 prev_facing, s8 move_dx, s8 move_dy)
{
    if (move_dx == 0 && move_dy == 0) {
        return prev_facing;          /* idle: keep pointing where we last moved */
    }
    return dir_from_steps(move_dx, move_dy);
}

void make_intent(u8 joy, u8 keys, u8 facing, intent_t *out)
{
    u8 aim;

    decode_move(joy, &out->move_dx, &out->move_dy);
    out->aim_dx = 0;
    out->aim_dy = 0;
    out->fire   = 0;

    aim = decode_aim_keys(keys);
    if (aim != DIR_NONE) {
        /* QWEADZXC gives an absolute 8-way aim, overriding facing. */
        out->aim_dx = dir_dx[aim];
        out->aim_dy = dir_dy[aim];
        out->fire   = 1;
    } else if ((joy & JOY_FIRE) && facing != DIR_NONE) {
        /* Kempston fire shoots in the direction the ship faces. */
        out->aim_dx = dir_dx[facing];
        out->aim_dy = dir_dy[facing];
        out->fire   = 1;
    }
}

/* --------------------------------------------------------------------------
 * Target-only hardware read. Compiled only for the Spectrum/Timex build,
 * where the z88dk input library is available. The host test build excludes
 * this and tests make_intent() directly.
 * -------------------------------------------------------------------------- */
#ifdef __SPECTRUM

/* z88dk newlib (sdcc_iy clib) input API. <input.h> pulls <input/input_zx.h>
 * under __SPECTRUM, giving in_stick_kempston(), in_key_pressed() and the
 * IN_KEY_SCANCODE_* / IN_STICK_* constants. The IN_STICK_* bit values match
 * our JOY_* masks (UP=0x01, DOWN=0x02, LEFT=0x04, RIGHT=0x08, FIRE=0x80). */
#include <input.h>

static u8 read_aim_keys(void)
{
    u8 k = 0;
    if (in_key_pressed(IN_KEY_SCANCODE_q)) k |= KEY_Q;
    if (in_key_pressed(IN_KEY_SCANCODE_w)) k |= KEY_W;
    if (in_key_pressed(IN_KEY_SCANCODE_e)) k |= KEY_E;
    if (in_key_pressed(IN_KEY_SCANCODE_a)) k |= KEY_A;
    if (in_key_pressed(IN_KEY_SCANCODE_d)) k |= KEY_D;
    if (in_key_pressed(IN_KEY_SCANCODE_z)) k |= KEY_Z;
    if (in_key_pressed(IN_KEY_SCANCODE_x)) k |= KEY_X;
    if (in_key_pressed(IN_KEY_SCANCODE_c)) k |= KEY_C;
    return k;
}

void input_read(u8 facing, intent_t *out)
{
    u8 joy  = (u8)in_stick_kempston();  /* IN_STICK_* bits == our JOY_* masks */
    u8 keys = read_aim_keys();
    make_intent(joy, keys, facing, out);
}

#endif /* __SPECTRUM */
