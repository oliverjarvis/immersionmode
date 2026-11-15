#include "sentence.h"

#include "layout.h"
#include "util.h"

static u16 vis[JPR_EXCERPT_UNITS];   /* the visible characters, in order */

static u32 visible(u16 u) {
    if (u == 0xE07F) return 0x20;
    if (u == 0xE08D) return 0x2026;
    if (u == 0xE08E) return 0x2642;
    if (u == 0xE08F) return 0x2640;
    if (u >= 0xE000 && u <= 0xF8FF) return 0;
    return u;
}

static int ends(u32 cp) {
    return cp == 0x3002 || cp == 0xFF01 || cp == 0xFF1F || cp == '!' || cp == '?';
}

static int closes(u32 cp) {
    return cp == 0x300D || cp == 0x300F || cp == 0xFF09 || cp == ')';
}

/* Does a sentence end just before vis[k]? */
static int ends_before(u32 k) {
    while (k && closes(vis[k - 1])) k--;
    return k && ends(vis[k - 1]);
}

static u32 encode(u8 *out, u32 cap, u32 from, u32 to) {
    u32 at = 0, k;
    for (k = from; k < to; k++)
        if (jpr_utf8_put(out, &at, cap, vis[k])) break;
    return at;
}

void jpr_excerpt(struct jpr_excerpt *e, const u16 *text, u32 len, u32 from, u32 to) {
    u32 n = 0, a = 0, b = 0, i = 0, s, t, start;
    u16 cp;
    if (len > JPR_EXCERPT_UNITS) len = JPR_EXCERPT_UNITS;
    while (jpr_next_unit(text, &i, len, &cp)) {
        u32 v = visible(cp);
        if (i - 1 < from) a = n + (v != 0);
        if (i - 1 < to) b = n + (v != 0);
        if (v) vis[n++] = (u16)v;
    }
    /* a, b: the block's visible characters are vis[a, b) */
    if (a > b) a = b;
    s = a;
    t = b;
    if (t - s > JPR_SENTENCE_MAX) t = s + JPR_SENTENCE_MAX;
    while (s > 0 && !ends_before(s) && t - s < JPR_SENTENCE_MAX) s--;
    if (t > s && !ends_before(t) ) {
        while (t < n && t - s < JPR_SENTENCE_MAX) {
            t++;
            if (ends(vis[t - 1])) {
                while (t < n && closes(vis[t]) && t - s < JPR_SENTENCE_MAX) t++;
                break;
            }
        }
    }
    if (a == b) s = t = a;
    e->sentence_len = encode(e->sentence, JPR_SENTENCE_BYTES, s, t);
    start = 0;
    if (n > JPR_CONTEXT_MAX) {
        u32 spare = JPR_CONTEXT_MAX - (t - s);
        start = s > (spare >> 1) ? s - (spare >> 1) : 0;
        if (start + JPR_CONTEXT_MAX > n) start = n - JPR_CONTEXT_MAX;
    }
    e->context_len = encode(e->context, JPR_CONTEXT_BYTES, start, n < start + JPR_CONTEXT_MAX ? n : start + JPR_CONTEXT_MAX);
}
