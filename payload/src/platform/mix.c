#include "mix.h"

#include "core/panel.h"
#include "debug.h"
#include "fs.h"
#include "game_addrs.h"
#include "voice.h"

/* The game's 12 nw::snd players (docs/stage11-sound-findings.md): their volume is a float at +0x3C of a 0x5C-byte
 * record, in an array at *(archive player + 0x1C); the archive player is the inner sound system + 0x53C. */
#define INNER_OFFSET 4
#define ARCHIVE_PLAYER 0x53C
#define PLAYERS_AT 0x1C
#define PLAYER_SIZE 0x5C
#define PLAYER_VOLUME 0x3C
#define PLAYER_ID(i) (0x04000000u | (i))

typedef u32 (*label_fn)(u32 label);
typedef void (*cs_fn)(void *cs);

#define CALL(type, addr) ((type)(addr))

extern struct jpr_panel jpr_panel;

static struct jpr_mix_state state;
static u32 started, loaded, applied, duck = 1000, voice_bits;
static float voice_gain = -1.0f;

static u32 bits_of(float v) {
    union { float f; u32 u; } x;
    x.f = v;
    return x.u;
}

static float float_of(u32 b) {
    union { float f; u32 u; } x;
    x.u = b;
    return x.f;
}

/* The inner sound system, or 0 before the game has made it. */
static u8 *inner_system(void) {
    u8 *sys = *(u8 *volatile *)JPR_MIX_SYS;
    return sys ? *(u8 *volatile *)(sys + INNER_OFFSET) : 0;
}

/* Player i's volume, or 0 while the players are not there yet. */
static float *player_volume(u32 i) {
    u8 *inner = inner_system(), *players;
    if (!inner) return 0;
    players = *(u8 *volatile *)(inner + ARCHIVE_PLAYER + PLAYERS_AT);
    return players ? (float *)(void *)(players + i * PLAYER_SIZE + PLAYER_VOLUME) : 0;
}

static void init_once(void) {
    if (started) return;
    started = 1;
    jpr_mix_state_init(&state);
}

void jpr_h_player_vol_set(void *sys, u32 label, u32 vbits) {
    u32 id = CALL(label_fn, JPR_MIX_LABEL_TO_ID)(label), i = id & 0xFFFFFFu;
    float v = float_of(vbits);
    init_once();
    if ((id >> 24) == 4 && i < JPR_MIX_PLAYERS) v = jpr_mix_game_set(&state, i, v);
    jpr_dbg.mix_sets++;
    jpr_call_f32(JPR_MIX_PLAYER_WRITE, *(u32 *)((u8 *)sys + INNER_OFFSET), id, bits_of(v));
}

u32 jpr_h_player_vol_get(void *sys, u32 label) {
    u32 id = CALL(label_fn, JPR_MIX_LABEL_TO_ID)(label), i = id & 0xFFFFFFu;
    u8 *players = *(u8 **)(*(u8 **)((u8 *)sys + INNER_OFFSET) + ARCHIVE_PLAYER + PLAYERS_AT);
    init_once();
    jpr_dbg.mix_gets++;
    if ((id >> 24) == 4 && i < JPR_MIX_PLAYERS) return bits_of(jpr_mix_game_get(&state, i));
    return *(u32 *)(void *)(players + i * PLAYER_SIZE + PLAYER_VOLUME);   /* what the original returned */
}

/* sound.txt, once: before it, the panel has the defaults. */
static void load(void) {
    char text[64];
    u32 len = 0;
    struct jpr_mix_levels l;
    loaded = 1;
    if (jpr_fs_read_file(JPR_MIX_PATH, text, sizeof text, &len)) len = 0;
    jpr_mix_parse(&l, text, len);
    jpr_panel_mix_levels(&jpr_panel, &l);
}

static void save(void) {
    char text[JPR_MIX_TEXT];
    jpr_mix_format(&jpr_panel.mix.levels, text);
    if (!jpr_fs_write_at(JPR_MIX_PATH, 0, text, JPR_MIX_TEXT)) jpr_dbg.mix_saves++;
    jpr_panel_mix_saved(&jpr_panel);
}

void jpr_mix_frame(void) {
    const struct jpr_mix_levels *l = &jpr_panel.mix.levels;
    float g;
    u32 i;
    init_once();
    if (!inner_system()) return;                    /* the game has not set up its sound yet */
#if JPR_DEBUG
    if (jpr_dbg.mix_test) {                         /* the game's own calls, through our replaced setter and getter */
        u32 test = jpr_dbg.mix_test;
        jpr_dbg.mix_test = 0;
        if (test == 1) jpr_call_f32(JPR_MIX_CHANGE_BGM_PLAYER_VOLUME, 0, 0, bits_of(0.5f));
        else jpr_call_f32(JPR_MIX_CHANGE_BGM_PLAYER_VOLUME, 30, 0, bits_of(1.0f));
    }
#endif
    if (!loaded) load();
    if (jpr_panel.mix.save) save();
    duck = jpr_mix_duck_step(duck, jpr_voice_playing(), l->duck);
    CALL(cs_fn, JPR_MIX_CS_ENTER)((void *)JPR_MIX_CS);
    if (jpr_mix_set_gains(&state, l, duck) || !applied) {     /* the first time: the players start at 1.0 */
        applied = 1;
        for (i = 0; i < JPR_MIX_PLAYERS; i++) {
            float *p = player_volume(i);
            if (p) *p = jpr_mix_output(&state, i);
        }
        jpr_dbg.mix_applies++;
    }
    CALL(cs_fn, JPR_MIX_CS_LEAVE)((void *)JPR_MIX_CS);
    g = jpr_mix_voice_gain(l);
    if (g != voice_gain) {
        voice_gain = g;
        voice_bits = bits_of(g);
        jpr_voice_set_gain(voice_bits);
    }
    jpr_dbg.mix_duck = duck;
    jpr_dbg.mix_out0 = bits_of(jpr_mix_output(&state, 0));
    jpr_dbg.mix_out2 = bits_of(jpr_mix_output(&state, 2));
    jpr_dbg.mix_shadow0 = bits_of(jpr_mix_game_get(&state, 0));
    jpr_dbg.mix_voice = voice_bits;
    {
        float *p0 = player_volume(0);
        jpr_dbg.mix_player0 = p0 ? *(u32 *)(void *)p0 : 0;
        jpr_dbg.mix_players = (u32)p0;
    }
}
