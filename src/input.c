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

void decode_move_keys(u8 keys, s8 *dx, s8 *dy)
{
    u8 dir = decode_aim_keys(keys);
    if (dir == DIR_NONE) {
        *dx = 0;
        *dy = 0;
        return;
    }
    *dx = dir_dx[dir];
    *dy = dir_dy[dir];
}

u8 joy_sanitize(u8 joy)
{
    /* Opposing directions can't both be held on a real stick -> floating bus. */
    if ((u8)(joy & (JOY_UP | JOY_DOWN)) == (JOY_UP | JOY_DOWN)) {
        return 0;
    }
    if ((u8)(joy & (JOY_LEFT | JOY_RIGHT)) == (JOY_LEFT | JOY_RIGHT)) {
        return 0;
    }
    return joy;
}

u8 update_facing(u8 prev_facing, s8 move_dx, s8 move_dy)
{
    if (move_dx == 0 && move_dy == 0) {
        return prev_facing;          /* idle: keep pointing where we last moved */
    }
    return dir_from_steps(move_dx, move_dy);
}

u8 decode_aim_joy(u8 joy, s8 *adx, s8 *ady)
{
    /* Extract directional tilt (ignore the FIRE bit for aim computation). */
    u8 dirs = (u8)(joy & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT));
    decode_move(dirs, adx, ady);
    /* Any of: directional tilt or FIRE bit alone -> fire is set. */
    if (joy != 0) {
        return 1;
    }
    return 0;
}

void make_intent(u8 joy, u8 keys, u8 facing, intent_t *out)
{
    u8 aim;
    (void)facing;   /* Scheme A: JOY_FIRE now boosts, not fires-in-heading. */

    decode_move(joy, &out->move_dx, &out->move_dy);
    out->aim_dx = 0;
    out->aim_dy = 0;
    out->fire   = 0;
    /* JOY_FIRE (including cursor 0 merged in by read_cursor_joy) -> boost. */
    out->boost  = (joy & JOY_FIRE) ? 1u : 0u;

    aim = decode_aim_keys(keys);
    if (aim != DIR_NONE) {
        /* QWEADZXC gives an absolute 8-way aim, overriding facing. */
        out->aim_dx = dir_dx[aim];
        out->aim_dy = dir_dy[aim];
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
#include <z80.h>          /* z80_inp/z80_outp -- TS2068 AY joystick ports */

/* ---- TS2068 built-in joysticks (CTRL_DUAL_STICK) -------------------------
 * Verified against the TS2068 Technical Manual (port map) AND the Fuse
 * emulator's tc2068.c bus model:
 *   - The two sticks hang off the AY-3-8912 I/O port A (AY register 14).
 *   - Port $F5 selects an AY register; port $F6 reads it.
 *   - Reg 7 bit 6 must be 0 (port A = input) or reg 14 reads back its latch.
 *   - A8 high (port $01F6) strobes joystick 1 (LEFT); A9 high (port $02F6)
 *     strobes joystick 2 (RIGHT).  z80_inp() puts the 16-bit port on the bus,
 *     so the high byte supplies A8/A9.
 *   - Data bits are ACTIVE LOW: 0=up 1=down 2=left 3=right 7=fire -- the same
 *     positions as our JOY_* masks, so one inversion yields a JOY_* byte.
 * On a TC2048 (no AY) these ports float to 0xFF -> inverted to 0 -> inert, so
 * the scheme harmlessly falls through to the Sinclair-keyboard read below. */
#define AY_PORT_SEL  0xF5u
#define AY_PORT_DAT  0xF6u
#define AY_REG_MIXER 7u
#define AY_REG_IOA   14u
#define TS_JOY1_PORT 0x01F6u   /* A8 high -> joystick 1 (left)  */
#define TS_JOY2_PORT 0x02F6u   /* A9 high -> joystick 2 (right) */
#define JOY_DIRS     (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_FIRE)

/* Spectrum keyboard matrix rows (active low, data bits D0..D4). Direct matrix
 * reads avoid repeated z88dk in_key_pressed() calls in the 50 Hz game loop. */
#define KB_ROW_SHIFT_ZXCV 0xFEFEu
#define KB_ROW_ASDFG      0xFDFEu
#define KB_ROW_QWERT      0xFBFEu
#define KB_ROW_12345      0xF7FEu
#define KB_ROW_09876      0xEFFEu
#define KB_ROW_SPACE      0x7FFEu

#define KB_BIT_0 0x01u
#define KB_BIT_1 0x02u
#define KB_BIT_2 0x04u
#define KB_BIT_3 0x08u
#define KB_BIT_4 0x10u

/* Set AY register 7 bit 6 = 0 (I/O port A = input) so the joystick lines read.
 * Read-modify-write to preserve the sound channel-enable bits. Harmless no-op
 * on a TC2048 (the AY ports are unmapped there). */
static void ts2068_joy_init(void)
{
    u8 r7;
    z80_outp(AY_PORT_SEL, AY_REG_MIXER);
    r7 = z80_inp(AY_PORT_DAT);
    z80_outp(AY_PORT_SEL, AY_REG_MIXER);
    z80_outp(AY_PORT_DAT, (u8)(r7 & (u8)~0x40u));
}

/* Read one TS2068 built-in joystick (TS_JOY1_PORT / TS_JOY2_PORT) as a JOY_*
 * byte (active high). */
static u8 ts2068_read_joy(u16 port)
{
    u8 raw;
    z80_outp(AY_PORT_SEL, AY_REG_IOA);   /* select I/O port A */
    raw = z80_inp(port);                 /* A8/A9 (high byte) pick the stick */
    return (u8)(~raw & JOY_DIRS);
}

static u8 read_aim_keys(void)
{
    u8 k = 0;
    u8 row_qwe = (u8)~z80_inp(KB_ROW_QWERT);
    u8 row_ad  = (u8)~z80_inp(KB_ROW_ASDFG);
    u8 row_zxc = (u8)~z80_inp(KB_ROW_SHIFT_ZXCV);

    if (row_qwe & KB_BIT_0) k |= KEY_Q;
    if (row_qwe & KB_BIT_1) k |= KEY_W;
    if (row_qwe & KB_BIT_2) k |= KEY_E;
    if (row_ad  & KB_BIT_0) k |= KEY_A;
    if (row_ad  & KB_BIT_2) k |= KEY_D;
    if (row_zxc & KB_BIT_1) k |= KEY_Z;
    if (row_zxc & KB_BIT_2) k |= KEY_X;
    if (row_zxc & KB_BIT_3) k |= KEY_C;
    return k;
}

/* Cursor / Protek joystick: keys 5/6/7/8 move, 0 fires -- returned as a
 * normalised JOY_* byte so it merges (OR) with the Kempston read and flows
 * through make_intent(). 0 sets JOY_FIRE -> boosts (scheme A: JOY_FIRE=boost).
 * This makes the game playable with no joystick hardware (handy when macOS
 * Fuse's HID/gamepad path fails). Classic Spectrum cursor: 5=left 6=down
 * 7=up 8=right. */
static u8 read_cursor_joy(void)
{
    u8 j = 0;
    u8 row_12345 = (u8)~z80_inp(KB_ROW_12345);
    u8 row_09876 = (u8)~z80_inp(KB_ROW_09876);

    if (row_12345 & KB_BIT_4) j |= JOY_LEFT;   /* 5 */
    if (row_09876 & KB_BIT_4) j |= JOY_DOWN;   /* 6 */
    if (row_09876 & KB_BIT_3) j |= JOY_UP;     /* 7 */
    if (row_09876 & KB_BIT_2) j |= JOY_RIGHT;  /* 8 */
    if (row_09876 & KB_BIT_0) j |= JOY_FIRE;   /* 0 */
    return j;
}

static u8 read_key_s(void)
{
    return (u8)(((u8)~z80_inp(KB_ROW_ASDFG) & KB_BIT_1) != 0u);
}

static u8 read_key_space(void)
{
    return (u8)(((u8)~z80_inp(KB_ROW_SPACE) & KB_BIT_0) != 0u);
}

/* Active control scheme (CTRL_*), set from the title screen. The pure decode
 * logic is identical across schemes; only the input SOURCES differ here. */
static u8 g_scheme = CTRL_KEMPSTON_MOVE;

void input_set_scheme(u8 scheme)
{
    g_scheme = scheme;
#ifndef ZX_SINCLAIR_DUAL_STICK
    if (scheme == CTRL_DUAL_STICK) {
        ts2068_joy_init();   /* arm the AY port-A-input read on a TS2068 */
    }
#else
    (void)scheme;
#endif
}

/* Aim in `facing` and fire -- shared by the fire-button schemes. */
static void fire_facing(u8 facing, intent_t *out)
{
    out->aim_dx = 0;
    out->aim_dy = 0;
    out->fire   = 0;
    if (facing != DIR_NONE) {
        out->aim_dx = dir_dx[facing];
        out->aim_dy = dir_dy[facing];
        out->fire   = 1;
    }
}

void input_read(u8 facing, intent_t *out)
{
    if (g_scheme == CTRL_KEMPSTON_FIRE) {
        /* Scheme B: QWEADZXC moves; Kempston tilt -> aim+fire; Kempston FIRE
         * button -> fire in heading; S key -> boost.
         *
         * Read the raw Kempston byte first, then split it:
         *   - Directional bits (tilt) -> aim+fire via decode_aim_joy.
         *   - JOY_FIRE with no tilt -> fire_facing (fire-in-heading).
         *   - JOY_FIRE is NOT boost in this scheme (that is S key).
         */
        u8 keys = read_aim_keys();
        u8 joy  = joy_sanitize((u8)in_stick_kempston());
        u8 dirs = (u8)(joy & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT));
        s8 adx, ady;
        u8 joy_fires;

        decode_move_keys(keys, &out->move_dx, &out->move_dy);
        out->boost = read_key_s();

        joy_fires = (u8)(joy & JOY_FIRE);

        if (dirs != 0) {
            /* Kempston tilt: aim in the deflection direction and fire. */
            decode_aim_joy(joy, &adx, &ady);
            out->aim_dx = adx;
            out->aim_dy = ady;
            out->fire   = 1;
        } else if (joy_fires) {
            /* Fire button, no tilt: fire in the current heading. */
            fire_facing(facing, out);
        } else {
            out->aim_dx = 0; out->aim_dy = 0; out->fire = 0;
        }
        return;
    }

    if (g_scheme == CTRL_DUAL_STICK) {
#ifdef ZX_SINCLAIR_DUAL_STICK
        /* Sinclair twin-stick build:
         *   Sinclair 1 -> move + boost on FIRE.
         *   Sinclair 2 -> aim + fire on tilt/FIRE.
         * This path never touches the TS2068 AY joystick ports. */
        u8 mv  = joy_sanitize((u8)in_stick_sinclair1());
        u8 aim = joy_sanitize((u8)in_stick_sinclair2());
        s8 adx, ady;
#else
        /* Scheme C: TS2068 twin-stick.
         *   Left stick  -> move; its FIRE button -> boost.
         *   Right stick -> aim+fire (tilt = aim+fire).
         * Each stick is the TS2068 built-in joystick OR-merged with the
         * matching Sinclair-keyboard read, so it is authentic on a real TS2068
         * yet still playable/testable from the keyboard on a TC2048:
         *   move : TS2068 joy 1  |  Sinclair 1 (keys 6/7/8/9/0)
         *   aim  : TS2068 joy 2  |  Sinclair 2 (keys 1/2/3/4/5) */
        u8 mv  = (u8)(joy_sanitize(ts2068_read_joy(TS_JOY1_PORT)) |
                      joy_sanitize((u8)in_stick_sinclair1()));
        u8 aim = (u8)(joy_sanitize(ts2068_read_joy(TS_JOY2_PORT)) |
                      joy_sanitize((u8)in_stick_sinclair2()));
        s8 adx, ady;
#endif

        decode_move(mv,  &out->move_dx, &out->move_dy);
        /* Left stick FIRE -> boost. */
        out->boost = (mv & JOY_FIRE) ? 1u : 0u;

        /* Right stick tilt -> aim+fire. */
        if (decode_aim_joy(aim, &adx, &ady)) {
            out->aim_dx = adx; out->aim_dy = ady; out->fire = 1;
        } else {
            out->aim_dx = 0; out->aim_dy = 0; out->fire = 0;
        }
        return;
    }

    /* Scheme A (CTRL_KEMPSTON_MOVE, default): Kempston + cursor keys move;
     * QWEADZXC aims and fires; JOY_FIRE (incl. cursor 0) + SPACE -> boost.
     * Sanitise the Kempston read (drop a floating port) BEFORE merging the
     * cursor keys, so a floating Kempston can't mask keyboard movement. */
    {
        u8 joy  = (u8)(joy_sanitize((u8)in_stick_kempston()) | read_cursor_joy());
        u8 keys = read_aim_keys();
        make_intent(joy, keys, facing, out);
        /* SPACE also boosts (make_intent already sets boost from JOY_FIRE). */
        if (read_key_space()) {
            out->boost = 1;
        }
    }
}

#endif /* __SPECTRUM */
