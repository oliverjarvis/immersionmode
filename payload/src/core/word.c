#include "word.h"

#include "grammar.h"

static int raw(u8 *out, u32 *at, u32 cap, const char *s) {
    while (*s) {
        if (*at >= cap) return -1;
        out[(*at)++] = (u8)*s++;
    }
    return 0;
}

/* A value on one line: newlines and tabs become spaces. */
static int value(u8 *out, u32 *at, u32 cap, const u8 *p, u32 n) {
    u32 k;
    if (*at + n > cap) return -1;
    for (k = 0; k < n; k++) out[(*at)++] = p[k] == '\n' || p[k] == '\r' || p[k] == '\t' ? (u8)' ' : p[k];
    return 0;
}

/* The id in decimal: the v2 add (stage 9C) names the word by it. */
static int number(u8 *out, u32 *at, u32 cap, u32 v) {
    u8 digits[10];
    u32 n = 0;
    do {
        digits[n++] = (u8)('0' + v % 10);
        v /= 10;
    } while (v);
    if (*at + n > cap) return -1;
    while (n) out[(*at)++] = digits[--n];
    return 0;
}

u32 jpr_word_body(u8 *out, u32 cap, const struct jpr_card *c, const u8 *sentence, u32 slen) {
    u32 at = 0;
    if (c->jmdict && (raw(out, &at, cap, "jmdict=") || number(out, &at, cap, c->jmdict) || raw(out, &at, cap, "\n")))
        return 0;
    if (raw(out, &at, cap, "front=") || value(out, &at, cap, c->headword.p, c->headword.len) ||
        raw(out, &at, cap, "\nreading=") || value(out, &at, cap, c->reading.p, c->reading.len) ||
        raw(out, &at, cap, "\nmeaning="))
        return 0;
    if (c->senses > 0 && value(out, &at, cap, c->sense[0].p, c->sense[0].len)) return 0;
    if (c->senses > 1 && (raw(out, &at, cap, "; ") || value(out, &at, cap, c->sense[1].p, c->sense[1].len))) return 0;
    if (raw(out, &at, cap, "\npos=") || value(out, &at, cap, c->pos.p, c->pos.len) ||
        raw(out, &at, cap, "\nsentence=") || value(out, &at, cap, sentence, slen) || raw(out, &at, cap, "\n"))
        return 0;
    return at;
}

static int is(const u8 *p, u32 n, const char *s) {
    u32 k = 0;
    for (; k < n; k++)
        if (!s[k] || p[k] != (u8)s[k]) return 0;
    return s[n] == 0;
}

u32 jpr_word_reply(const u8 *text, u32 len) {
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) len--;
    /* v2 (stage 9C): added to My words, or queued (kokoro had it, unstudied): either way up next. v1: new. */
    if (is(text, len, "OK added") || is(text, len, "OK queued") || is(text, len, "OK new") ||
        is(text, len, "OK exists new"))
        return JPR_KOKORO_NEW;
    if (is(text, len, "OK exists learning")) return JPR_KOKORO_LEARNING;
    if (is(text, len, "OK exists known")) return JPR_KOKORO_KNOWN;
    return 0;
}

int jpr_word_should_queue(u32 failure) {
    return failure == JPR_NET_NO_WIFI || failure == JPR_NET_BUSY || failure == JPR_NET_UNREACHABLE ||
           failure == JPR_NET_TIMEOUT;
}

u32 jpr_word_outcome(u32 failure, u32 http_status, u32 queued, const u8 *text, u32 len, u32 *state) {
    *state = 0;
    if (failure == JPR_NET_OK) {
        *state = jpr_word_reply(text, len);
        return *state ? JPR_ADD_DONE : JPR_ADD_FAILED;
    }
    if (queued) {
        *state = JPR_KOKORO_QUEUED;
        return JPR_ADD_QUEUED;
    }
    if (failure == JPR_NET_HTTP && http_status == 401) return JPR_ADD_REJECTED;
    return JPR_ADD_FAILED;
}
