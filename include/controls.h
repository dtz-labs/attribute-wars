/*
 * controls.h -- three boost-aware control schemes for the twin-stick shooter.
 *
 * NOTE on the name: this is the project's input module interface. It is NOT
 * called "input.h" on purpose -- z88dk ships a system header <input.h>
 * (in_KeyPressed etc.) and any file of ours named input.h on the include
 * path shadows it, breaking the hardware read in input.c. So the header is
 * "controls.h"; the implementation file stays src/input.c (matches the
 * prompt's module layout).
 *
 * Split into PURE decode logic (host-testable, no hardware) and a thin
 * target-only hardware read (input_read, compiled only under __SPECTRUM).
 *
 * Joystick masks match z88dk's NORMALISED layout from <input.h>
 * (in_JoyKempston remaps the raw port to these), NOT the raw 000FUDLR bits.
 */
#ifndef CONTROLS_H
#define CONTROLS_H

#include "types.h"

/* Normalised joystick bits (as returned by z88dk in_Joy* functions). */
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x80

/*
 * QWEADZXC key bitmask -- one bit per key, laid out as the 3x3 grid
 * around S (which is unused as a shoot key in scheme A; S boosts in scheme B):
 *   Q W E      bit0 bit1 bit2
 *   A . D  ->  bit3  -   bit4
 *   Z X C      bit5 bit6 bit7
 *
 * X (bit6) maps to DIR_S (shoots South), not "no shot" -- decode_aim_keys(KEY_X)
 * returns DIR_S.
 */
#define KEY_Q 0x01  /* NW */
#define KEY_W 0x02  /* N  */
#define KEY_E 0x04  /* NE */
#define KEY_A 0x08  /* W  */
#define KEY_D 0x10  /* E  */
#define KEY_Z 0x20  /* SW */
#define KEY_X 0x40  /* S  */
#define KEY_C 0x80  /* SE */

typedef struct {
    s8  move_dx, move_dy;   /* -1/0/+1 movement axis (Kempston)              */
    s8  aim_dx,  aim_dy;    /* -1/0/+1 shot direction (keys, or facing)      */
    u8  fire;               /* non-zero if a shot is requested this frame    */
    u8  boost;              /* non-zero while a boost input is held           */
} intent_t;

/*
 * Control schemes, chosen on the title screen. The hardware read (input_read)
 * maps each to a different pair of input sources; the resulting intent_t is
 * identical so the game loop is scheme-agnostic.
 *
 * Scheme A (CTRL_KEMPSTON_MOVE): Kempston (+cursor 5/6/7/8) moves; QWEADZXC
 *   aims and fires; JOY_FIRE (incl. cursor 0) + SPACE boost.
 * Scheme B (CTRL_KEMPSTON_FIRE): QWEADZXC moves; Kempston tilt aims+fires;
 *   Kempston FIRE button fires in heading; S key boosts.
 * Scheme C (CTRL_DUAL_STICK): target-dependent twin-stick mode. Timex build:
 *   TS2068 left/right built-in sticks. Sinclair build: Sinclair 1/2 sticks.
 */
#define CTRL_KEMPSTON_MOVE 0u  /* Scheme A: Kempston move, QWEADZXC fire, JOY_FIRE+SPACE boost */
#define CTRL_KEMPSTON_FIRE 1u  /* Scheme B: QWEADZXC move, Kempston tilt aim/fire, S boost     */
#define CTRL_DUAL_STICK    2u  /* Scheme C: target twin-stick move+boost / aim+fire             */

/* Select the active control scheme (CTRL_*). Persists until changed. */
void input_set_scheme(u8 scheme);

/* ---- pure decode (host-tested) ---- */

/* Decode normalised joystick byte into a -1/0/+1 movement step. */
void decode_move(u8 joy, s8 *dx, s8 *dy);

/* Scheme-B tilt-aim helper: decode a Kempston byte as an aim direction.
 * Directional bits (UP/DOWN/LEFT/RIGHT) set out_adx/out_ady and return 1.
 * If only JOY_FIRE is set (no tilt), out_adx/out_ady are zeroed; fire=1
 * (fire-in-heading; the caller fills in the actual direction from facing).
 * Returns 0 (no fire, no aim) when joy == 0. */
u8 decode_aim_joy(u8 joy, s8 *out_adx, s8 *out_ady);

/* Map a QWEADZXC key bitmask to a DIR_* (DIR_NONE if no aim key held).
 * If several keys are held, the lowest bit (scan order Q,W,E,A,D,Z,X,C) wins. */
u8 decode_aim_keys(u8 keys);

/* Map a QWEADZXC key bitmask to a -1/0/+1 movement step (the CTRL_KEMPSTON_FIRE
 * scheme drives MOVEMENT from the keys). Diagonals come for free from the 8-way
 * key grid. No key held -> (0,0). */
void decode_move_keys(u8 keys, s8 *dx, s8 *dy);

/* Update facing: keep the last non-zero movement direction when idle.
 * prev_facing is a DIR_* (or DIR_NONE before first move). */
u8 update_facing(u8 prev_facing, s8 move_dx, s8 move_dy);

/* Combine Scheme-A inputs into *out.
 *   joy    -- normalised Kempston byte (JOY_* bits); JOY_FIRE sets boost.
 *   keys   -- QWEADZXC bitmask; any key sets aim+fire.
 *   facing -- DIR_* (or DIR_NONE) for fire-in-heading fallback (unused in A
 *             since JOY_FIRE now boosts -- kept for API stability).
 * Uses an out-param (not struct return) because SDCC's z80 codegen is
 * unreliable returning structs by value. */
void make_intent(u8 joy, u8 keys, u8 facing, intent_t *out);

/* Reject a *floating* Kempston read. A real stick can never hold two opposing
 * directions; a port with no Kempston interface present floats to all-bits-set
 * (UP+DOWN+LEFT+RIGHT+FIRE). Such a reading must be dropped, or it both jams
 * movement (opposing axes cancel) and masks any cursor-key input OR-ed in.
 * Returns the joystick byte unchanged, or 0 if it looks like a floating bus. */
u8 joy_sanitize(u8 joy);

/* ---- target-only hardware read (defined under __SPECTRUM in input.c) ---- */
void input_read(u8 facing, intent_t *out);

#endif /* CONTROLS_H */
