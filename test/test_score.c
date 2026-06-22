/*
 * test_score.c -- host unit tests for the BCD score + game_state module.
 */
#include <assert.h>
#include <stdio.h>
#include "score.h"
#include "enemy.h"
static int checks = 0;
#define CHECK(c) do { assert(c); ++checks; } while (0)

static u32 val(const score_t *s){ u32 v=0; for(int i=0;i<6;i++) v=v*10+s->digits[i]; return v; }

static void test_add_carry(void){
    score_t s; score_reset(&s);
    score_add(&s, 600); CHECK(val(&s)==600);
    score_reset(&s); for(int i=0;i<6;i++) s.digits[i]=9; s.digits[0]=0; /* 099999 */
    score_add(&s, 5); CHECK(val(&s)==100004);          /* carry across the 9-run */
}
static void test_sub_borrow_and_clamp(void){
    score_t s; score_reset(&s);
    score_add(&s, 0); s.digits[0]=1;s.digits[1]=0;s.digits[2]=0;s.digits[3]=0;s.digits[4]=0;s.digits[5]=3; /* 100003 */
    score_sub(&s, 100); CHECK(val(&s)==99903);         /* borrow through the zero-run */
    score_reset(&s); score_add(&s, 50);
    score_sub(&s, 100); CHECK(val(&s)==0);             /* underflow clamps at 0, not wrap */
}
static void test_extra_life(void){
    score_t s; score_reset(&s);
    CHECK(score_add(&s, 600)==0);
    /* climb to 9600 (under 10000), no life yet */
    for(int i=0;i<15;i++) score_add(&s, 600);          /* 9600 */
    CHECK(s.extra_tt==0);
    u8 lives = score_add(&s, 600);                      /* 10200 -> crosses 10000 once */
    CHECK(lives==1 && s.extra_tt==1 && val(&s)==10200);
    /* losing points must NOT let you re-earn the passed milestone */
    score_sub(&s, 5000); CHECK(s.extra_tt==1);          /* 5200 */
    CHECK(score_add(&s, 6000)==0);                      /* 11200, back over 10000, but milestone 1 already given */
    CHECK(s.extra_tt==1);
}
static void test_enemy_points(void){
    CHECK(score_enemy_points(ENEMY_BOUNCE)==200);
    CHECK(score_enemy_points(ENEMY_CHASE)==400);
    CHECK(score_enemy_points(ENEMY_HUNTER)==600);
}
static void test_game_state(void){
    game_state_t g; game_new(&g);
    CHECK(g.wave==1 && g.lives==START_LIVES && g.shields==START_SHIELDS && val(&g.score)==0);
    g.score.digits[5]=9; g.score.extra_tt=5; g.wave=14; g.lives=0;
    game_resume_from_wave(&g, 14);
    CHECK(g.wave==14 && val(&g.score)==0 && g.score.extra_tt==0 && g.lives==START_LIVES && g.shields==START_SHIELDS);
}
int main(void){ test_add_carry(); test_sub_borrow_and_clamp(); test_extra_life();
                test_enemy_points(); test_game_state();
                printf("score: %d checks passed\n", checks); return 0; }
