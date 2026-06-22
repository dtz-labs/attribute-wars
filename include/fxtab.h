/*
 * fxtab.h -- fixed-point helpers (no floating point): a signed sine table and
 * an 8-bit fractional multiply. Pure data + tiny function, host-testable.
 */
#ifndef FXTAB_H
#define FXTAB_H

#include "types.h"

/* One full period, amplitude +/-127: fx_sin[i] ~= 127 * sin(2*pi*i/256). */
extern const s8 fx_sin[256];

/* (s * mag) >> 7 : project a signed cos/sin (-127..127) onto magnitude `mag`. */
s16 fx_mul(s8 s, u8 mag);

#endif /* FXTAB_H */
