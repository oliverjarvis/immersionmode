#include <stdio.h>
#include <string.h>

#include "core/mix.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int near(float a, float b) {
    return a - b < 0.0005f && b - a < 0.0005f;
}

static void test_levels_and_sound_txt(void) {
    struct jpr_mix_levels l;
    char out[JPR_MIX_TEXT + 1];
    jpr_mix_defaults(&l);
    CHECK(l.music == 6 && l.effects == 10 && l.voice == 10 && l.duck == 1);
    jpr_mix_parse(&l, "music=3\neffects=7\r\nvoice=9\nduck=0\n", 34);
    CHECK(l.music == 3 && l.effects == 7 && l.voice == 9 && l.duck == 0);
    jpr_mix_parse(&l, "music=11\nother=1\nvoice=x\n", 26);   /* out of range or not a number: the default */
    CHECK(l.music == 6 && l.effects == 10 && l.voice == 10 && l.duck == 1);
    l.music = 0;
    l.effects = 10;
    l.voice = 4;
    l.duck = 0;
    jpr_mix_format(&l, out);
    out[JPR_MIX_TEXT] = 0;
    CHECK(strlen(out) == JPR_MIX_TEXT);                          /* always as long: rewritten in place */
    {
        struct jpr_mix_levels back;
        jpr_mix_parse(&back, out, JPR_MIX_TEXT);
        CHECK(back.music == 0 && back.effects == 10 && back.voice == 4 && back.duck == 0);
    }
}

static void test_curve(void) {
    CHECK(jpr_mix_curve(0) == 0 && jpr_mix_curve(10) == 1000);
    CHECK(jpr_mix_curve(6) == 360 && jpr_mix_curve(5) == 250 && jpr_mix_curve(1) == 10);
    CHECK(jpr_mix_curve(12) == 1000);
}

static void test_duck_ramps_down_fast_and_back_slowly(void) {
    u32 d = 1000, frames = 0;
    while (d > JPR_MIX_DUCK) {
        d = jpr_mix_duck_step(d, 1, 1);
        frames++;
    }
    CHECK(d == JPR_MIX_DUCK && frames == JPR_MIX_DUCK_DOWN);
    CHECK(jpr_mix_duck_step(d, 1, 1) == JPR_MIX_DUCK);           /* stays down while the clip plays */
    frames = 0;
    while (d < 1000) {
        d = jpr_mix_duck_step(d, 0, 1);
        frames++;
    }
    CHECK(d == 1000 && frames == JPR_MIX_DUCK_UP);
    CHECK(jpr_mix_duck_step(1000, 1, 0) == 1000);                /* ducking off */
    CHECK(jpr_mix_duck_step(JPR_MIX_DUCK, 1, 0) > JPR_MIX_DUCK); /* turned off mid-clip: comes back */
}

static void test_the_game_keeps_its_own_volumes_and_ours_rides_on_top(void) {
    struct jpr_mix_state m;
    struct jpr_mix_levels l;
    jpr_mix_state_init(&m);
    jpr_mix_defaults(&l);                                         /* music 6 (0.36), effects 10 */
    CHECK(jpr_mix_set_gains(&m, &l, 1000) == 1);
    CHECK(jpr_mix_set_gains(&m, &l, 1000) == 0);                 /* nothing changed */
    CHECK(near(jpr_mix_output(&m, 0), 0.36f) && near(jpr_mix_output(&m, 1), 0.36f));
    CHECK(near(jpr_mix_output(&m, 2), 1.0f) && near(jpr_mix_output(&m, 11), 1.0f));
    /* a trainer spots you: the game dims the music to 0.5, then fades from what it set */
    CHECK(near(jpr_mix_game_set(&m, 0, 0.5f), 0.18f));
    CHECK(near(jpr_mix_game_get(&m, 0), 0.5f));                   /* a fade starts from 0.5, not 0.18 */
    CHECK(near(jpr_mix_output(&m, 0), 0.18f));
    /* a voice clip ducks the music players only */
    CHECK(jpr_mix_set_gains(&m, &l, JPR_MIX_DUCK) == 1);
    CHECK(near(jpr_mix_output(&m, 0), 0.5f * 0.36f * 0.4f) && near(jpr_mix_output(&m, 2), 1.0f));
    CHECK(near(jpr_mix_game_set(&m, 3, -1.0f), 0.0f));            /* never below silence */
    l.effects = 5;
    jpr_mix_set_gains(&m, &l, 1000);
    CHECK(near(jpr_mix_output(&m, 11), 0.25f));                   /* cries follow the effects */
    CHECK(near(jpr_mix_game_get(&m, 99), 1.0f) && near(jpr_mix_game_set(&m, 99, 0.7f), 0.7f));   /* no such player */
}

static void test_voice_gain(void) {
    struct jpr_mix_levels l;
    jpr_mix_defaults(&l);
    CHECK(near(jpr_mix_voice_gain(&l), 1.0f));
    l.voice = 5;
    CHECK(near(jpr_mix_voice_gain(&l), 0.25f));
}

int main(void) {
    test_levels_and_sound_txt();
    test_curve();
    test_duck_ramps_down_fast_and_back_slowly();
    test_the_game_keeps_its_own_volumes_and_ours_rides_on_top();
    test_voice_gain();
    if (failures) {
        printf("mix: %d check(s) failed\n", failures);
        return 1;
    }
    printf("mix: all checks passed\n");
    return 0;
}
