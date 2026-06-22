/*
 * types.h -- shared fixed-width integer types and direction constants.
 *
 * Pure definitions, no hardware dependency: this header is included both by
 * the Z80 target build and by the host (macOS) unit-test build.
 */
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;

/*
 * 8-way compass directions, clockwise from North.
 * Screen coordinates: x grows right, y grows DOWN (standard framebuffer).
 * DIR_NONE means "no direction / no shot".
 */
enum {
    DIR_N = 0,
    DIR_NE,
    DIR_E,
    DIR_SE,
    DIR_S,
    DIR_SW,
    DIR_W,
    DIR_NW,
    DIR_NONE
};

/* Playfield dimensions (Spectrum standard screen). */
#define SCREEN_W 256u   /* x is a u8: wraps 0..255 automatically            */
#define SCREEN_H 192u   /* y must be wrapped manually (not a power of two)   */

#endif /* TYPES_H */
