#ifndef JPR_MIX_H
#define JPR_MIX_H

#include "types.h"

/* The sound mixer (stage 11; research: docs/stage11-sound-findings.md). Pure logic; platform/mix.c hooks
 * the game's player-volume setter and getter and feeds our voice.
 *
 * The game's sound plays on 12 nw::snd players: 0 and 1 the music (jingles too), 2 to 11 the effects and
 * the Pokémon cries. Each player's volume is what the game asked for (its shadow, which fades and the
 * trainer-spotted dim keep moving) times ours (the gain): music level × the duck, or effects level. */

#define JPR_MIX_LEVELS 10               /* each level is 0..10 */
#define JPR_MIX_PLAYERS 12
#define JPR_MIX_MUSIC_PLAYERS 2         /* players 0 and 1 */
#define JPR_MIX_PATH "/jpreader/sound.txt"
#define JPR_MIX_TEXT 36                 /* sound.txt is always this long: rewritten in place */
#define JPR_MIX_DUCK 400                /* the music under a voice clip, in thousandths */
#define JPR_MIX_DUCK_DOWN 8             /* frames to duck */
#define JPR_MIX_DUCK_UP 20              /* frames to come back */

struct jpr_mix_levels {
    u8 music, effects, voice;           /* 0..JPR_MIX_LEVELS */
    u8 duck;                            /* 1: quieter music while a voice clip plays */
};

/* The levels a card without sound.txt has: music 6, effects 10, voice 10, duck on. */
void jpr_mix_defaults(struct jpr_mix_levels *l);
/* sound.txt: "music=6" and so on, one a line; unknown lines and out-of-range values are ignored. */
void jpr_mix_parse(struct jpr_mix_levels *l, const char *text, u32 len);
/* Writes exactly JPR_MIX_TEXT bytes (no NUL). */
void jpr_mix_format(const struct jpr_mix_levels *l, char *out);

/* A level's gain in thousandths: (level/10)², so each step sounds about as big as the last. */
u32 jpr_mix_curve(u32 level);
/* One frame of the duck (thousandths, 1000 = none): down to JPR_MIX_DUCK over JPR_MIX_DUCK_DOWN frames while
 * a clip plays and ducking is on, back to 1000 over JPR_MIX_DUCK_UP frames otherwise. */
u32 jpr_mix_duck_step(u32 duck, u32 playing, u32 on);

struct jpr_mix_state {
    float shadow[JPR_MIX_PLAYERS];      /* what the game last asked each player for */
    float gain[JPR_MIX_PLAYERS];        /* ours */
};

void jpr_mix_state_init(struct jpr_mix_state *m);
/* The gains for these levels and duck: music players level × duck, the others the effects level. Returns 1 when a
 * gain changed (the players need writing). */
int jpr_mix_set_gains(struct jpr_mix_state *m, const struct jpr_mix_levels *l, u32 duck);
/* The game sets player i to v: what to write to the player (v × gain; never below 0). */
float jpr_mix_game_set(struct jpr_mix_state *m, u32 i, float v);
/* The game reads player i (to start a fade from it): its own value, so ours is never counted twice. */
float jpr_mix_game_get(const struct jpr_mix_state *m, u32 i);
/* What player i should hold now. */
float jpr_mix_output(const struct jpr_mix_state *m, u32 i);
/* Our voice clip's volume for these levels. */
float jpr_mix_voice_gain(const struct jpr_mix_levels *l);

#endif
