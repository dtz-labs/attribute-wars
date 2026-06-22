/*
 * score.h -- BCD score and game-state types.
 *
 * Score is stored as 6 decimal digits (digits[0] = most significant).
 * All arithmetic is done via decimal carry/borrow loops -- no u32, no division.
 * Pure logic: host-testable, Z80-friendly.
 *
 * extra_tt = highest "ten-thousands milestone" (score/10000, range 0..99) for
 * which an extra life has been granted.  MONOTONIC -- never decreases, so a
 * later score drop cannot re-earn a passed threshold.  Using a raw points value
 * for the next threshold would overflow u16 above 65535.
 */
#ifndef SCORE_H
#define SCORE_H

#include "types.h"

typedef struct {
    u8 digits[6];   /* BCD digits, digits[0] = most significant (hundred-thousands) */
    u8 extra_tt;    /* monotonic ten-thousands milestone counter, 0..99 */
} score_t;

typedef struct {
    u8      wave;
    score_t score;
    u8      lives;
    u8      shields;
} game_state_t;

#define START_LIVES   2u
#define START_SHIELDS 3u

/* Zero all digits and extra_tt. */
void score_reset(score_t *s);

/* BCD add pts to s; returns number of new 10,000-point milestones crossed (0 or 1+). */
u8   score_add(score_t *s, u16 pts);

/* BCD subtract pts from s; clamps at zero (no wrap), leaves extra_tt unchanged. */
void score_sub(score_t *s, u16 pts);

/* Points awarded for killing an enemy at the given level (200 / 0 / 400 / 600). */
u16  score_enemy_points(u8 level);

/* Initialise a fresh game: wave=1, score reset, lives=START_LIVES, shields=START_SHIELDS. */
void game_new(game_state_t *g);

/* Resume at a given wave: wave=wave, score reset, lives and shields reset. */
void game_resume_from_wave(game_state_t *g, u8 wave);

#endif /* SCORE_H */
