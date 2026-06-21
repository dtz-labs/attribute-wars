/*
 * player.h -- player ship state and movement (pure logic, host-tested).
 *
 * The ship is drawn as a live vector shape (render.c, target), but its
 * position/facing update is pure integer math and lives here.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"
#include "controls.h"   /* intent_t */

/* Movement speed in pixels per frame (per axis). Diagonals move on both
 * axes, so they are slightly faster -- acceptable for this arcade feel. */
#define PLAYER_SPEED 2

typedef struct {
    u8 x, y;        /* position, screen pixels (toroidal)          */
    u8 facing;      /* DIR_* the ship points at (DIR_NONE at start) */
} player_t;

void player_init(player_t *p, u8 x, u8 y);

/* Advance one frame from an input intent: update facing then wrap-move. */
void player_update(player_t *p, const intent_t *in);

#endif /* PLAYER_H */
