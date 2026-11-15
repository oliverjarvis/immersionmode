#include "mix.h"

static int space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static int same(const char *a, u32 n, const char *b) {
    u32 k = 0;
    for (; k < n; k++)
        if (!b[k] || a[k] != b[k]) return 0;
    return b[n] == 0;
}

void jpr_mix_defaults(struct jpr_mix_levels *l) {
    l->music = 6;
    l->effects = JPR_MIX_LEVELS;
    l->voice = JPR_MIX_LEVELS;
    l->duck = 1;
}

/* A level 0..10, or -1. */
static s32 level_of(const char *v, u32 n) {
    u32 k, x = 0;
    if (n == 0 || n > 2) return -1;
    for (k = 0; k < n; k++) {
        if (v[k] < '0' || v[k] > '9') return -1;
        x = x * 10 + (u32)(v[k] - '0');
    }
    return x <= JPR_MIX_LEVELS ? (s32)x : -1;
}

void jpr_mix_parse(struct jpr_mix_levels *l, const char *text, u32 len) {
    u32 i = 0;
    jpr_mix_defaults(l);
    while (i < len) {
        u32 start = i, end, eq, ks, ke, vs, ve;
        s32 x;
        while (i < len && text[i] != '\n') i++;
        end = i++;
        ks = start;
        while (ks < end && space(text[ks])) ks++;
        for (eq = ks; eq < end && text[eq] != '='; eq++) {}
        if (eq == end) continue;
        ke = eq;
        while (ke > ks && space(text[ke - 1])) ke--;
        vs = eq + 1;
        while (vs < end && space(text[vs])) vs++;
        ve = end;
        while (ve > vs && space(text[ve - 1])) ve--;
        x = level_of(text + vs, ve - vs);
        if (x < 0) continue;
        if (same(text + ks, ke - ks, "music")) l->music = (u8)x;
        else if (same(text + ks, ke - ks, "effects")) l->effects = (u8)x;
        else if (same(text + ks, ke - ks, "voice")) l->voice = (u8)x;
        else if (same(text + ks, ke - ks, "duck") && x <= 1) l->duck = (u8)x;
    }
}

static char *put(char *out, const char *s) {
    while (*s) *out++ = *s++;
    return out;
}

/* Two digits, so the file is always JPR_MIX_TEXT long. */
static char *put2(char *out, u32 x) {
    *out++ = (char)('0' + x / 10 % 10);
    *out++ = (char)('0' + x % 10);
    return out;
}

void jpr_mix_format(const struct jpr_mix_levels *l, char *out) {
    out = put(out, "music=");
    out = put2(out, l->music);
    out = put(out, "\neffects=");
    out = put2(out, l->effects);
    out = put(out, "\nvoice=");
    out = put2(out, l->voice);
    out = put(out, "\nduck=");
    *out++ = l->duck ? '1' : '0';
    *out = '\n';
}

u32 jpr_mix_curve(u32 level) {
    if (level > JPR_MIX_LEVELS) level = JPR_MIX_LEVELS;
    return level * level * 1000 / (JPR_MIX_LEVELS * JPR_MIX_LEVELS);
}

u32 jpr_mix_duck_step(u32 duck, u32 playing, u32 on) {
    u32 down = (1000 - JPR_MIX_DUCK) / JPR_MIX_DUCK_DOWN, up = (1000 - JPR_MIX_DUCK) / JPR_MIX_DUCK_UP;
    if (playing && on) return duck > JPR_MIX_DUCK + down ? duck - down : JPR_MIX_DUCK;
    return duck + up < 1000 ? duck + up : 1000;
}

void jpr_mix_state_init(struct jpr_mix_state *m) {
    u32 i;
    for (i = 0; i < JPR_MIX_PLAYERS; i++) {
        m->shadow[i] = 1.0f;                /* nw::snd's own starting volume */
        m->gain[i] = 1.0f;
    }
}

int jpr_mix_set_gains(struct jpr_mix_state *m, const struct jpr_mix_levels *l, u32 duck) {
    float music = (float)jpr_mix_curve(l->music) * (float)duck / 1000000.0f;
    float effects = (float)jpr_mix_curve(l->effects) / 1000.0f;
    int changed = 0;
    u32 i;
    for (i = 0; i < JPR_MIX_PLAYERS; i++) {
        float g = i < JPR_MIX_MUSIC_PLAYERS ? music : effects;
        if (m->gain[i] != g) {
            m->gain[i] = g;
            changed = 1;
        }
    }
    return changed;
}

float jpr_mix_output(const struct jpr_mix_state *m, u32 i) {
    float v = m->shadow[i] * m->gain[i];
    return v < 0.0f ? 0.0f : v;
}

float jpr_mix_game_set(struct jpr_mix_state *m, u32 i, float v) {
    if (i >= JPR_MIX_PLAYERS) return v < 0.0f ? 0.0f : v;
    m->shadow[i] = v;
    return jpr_mix_output(m, i);
}

float jpr_mix_game_get(const struct jpr_mix_state *m, u32 i) {
    return i < JPR_MIX_PLAYERS ? m->shadow[i] : 1.0f;
}

float jpr_mix_voice_gain(const struct jpr_mix_levels *l) {
    return (float)jpr_mix_curve(l->voice) / 1000.0f;
}
