#include "kokoro.h"

#define W_HEAD 0
#define W_LINES 1
#define W_BAD 2

u32 jpr_kokoro_hash(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static int is(const u8 *p, u32 n, const char *s) {
    u32 k = 0;
    for (; k < n; k++)
        if (!s[k] || p[k] != (u8)s[k]) return 0;
    return s[n] == 0;
}

void jpr_words_init(struct jpr_words *w, u32 *table, u32 cap, u32 *fallback, u32 fb_cap) {
    w->table = table;
    w->cap = cap;
    w->count = 0;
    w->fallback = fallback;
    w->fb_cap = fb_cap;
    w->fb_count = 0;
    w->fb_dropped = 0;
    w->bad_ids = 0;
    w->expected = 0;
    w->lines = 0;
    w->state = W_HEAD;
    w->line_len = 0;
    w->too_long = 0;
}

static void header(struct jpr_words *w) {
    static const char head[] = "KOKORO-WORDS 2 ";
    u32 k, value = 0;
    for (k = 0; head[k]; k++)
        if (k >= w->line_len || w->line[k] != (u8)head[k]) {
            w->state = W_BAD;
            return;
        }
    if (k == w->line_len) {
        w->state = W_BAD;
        return;
    }
    for (; k < w->line_len; k++) {
        if (w->line[k] < '0' || w->line[k] > '9' || value > 9999999u) {
            w->state = W_BAD;
            return;
        }
        value = value * 10 + (u32)(w->line[k] - '0');
    }
    w->expected = value;
    w->state = W_LINES;
}

/* A whole line (without its newline) in w->line. */
static void line(struct jpr_words *w) {
    u32 k, d, state, id = 0;
    if (w->state == W_HEAD) {
        if (w->too_long) w->state = W_BAD;
        else header(w);
        return;
    }
    w->lines++;
    if (w->too_long) return;
    for (k = 0; k < w->line_len && w->line[k] != '\t'; k++) {}
    if (!k || k == w->line_len) {
        w->state = W_BAD;
        return;
    }
    state = is(w->line + k + 1, w->line_len - k - 1, "known")      ? JPR_KOKORO_KNOWN
            : is(w->line + k + 1, w->line_len - k - 1, "learning") ? JPR_KOKORO_LEARNING
            : is(w->line + k + 1, w->line_len - k - 1, "new")      ? JPR_KOKORO_NEW
                                                                  : 0;
    if (w->line[0] == '~') {   /* a kokoro card with no id: by its spelling */
        if (k == 1) {
            w->state = W_BAD;
            return;
        }
        if (!state) return;   /* a state from a later kokoro */
        if (w->fb_count >= w->fb_cap) {
            w->fb_dropped++;
            return;
        }
        w->fallback[w->fb_count++] = (jpr_kokoro_hash(w->line + 1, k - 1) & ~3u) | state;
        return;
    }
    for (d = 0; d < k; d++) {   /* a word by its JMdict id */
        if (w->line[d] < '0' || w->line[d] > '9') {
            w->state = W_BAD;
            return;
        }
        if (id < JPR_KOKORO_ID_LIMIT) id = id * 10 + (u32)(w->line[d] - '0');
    }
    if (id >= JPR_KOKORO_ID_LIMIT) {
        w->bad_ids++;
        return;
    }
    if (!state || w->count >= w->cap) return;   /* a state from a later kokoro, or no room */
    w->table[w->count++] = (id << 2) | state;
}

void jpr_words_feed(struct jpr_words *w, const u8 *p, u32 n) {
    u32 i;
    for (i = 0; i < n && w->state != W_BAD; i++) {
        if (p[i] == '\n') {
            if (w->line_len && w->line[w->line_len - 1] == '\r') w->line_len--;
            line(w);
            w->line_len = 0;
            w->too_long = 0;
        } else if (w->line_len < JPR_WORDS_LINE) {
            w->line[w->line_len++] = p[i];
        } else {
            w->too_long = 1;
        }
    }
}

static void sift(u32 *a, u32 root, u32 n) {
    for (;;) {
        u32 child = 2 * root + 1, t;
        if (child >= n) return;
        if (child + 1 < n && a[child + 1] > a[child]) child++;
        if (a[root] >= a[child]) return;
        t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

/* Heapsort: in place, no recursion, n log n even for 8,000 words. */
static void sort(u32 *a, u32 n) {
    u32 k, t;
    for (k = n / 2; k-- > 0;) sift(a, k, n);
    for (k = n; k-- > 1;) {
        t = a[0];
        a[0] = a[k];
        a[k] = t;
        sift(a, 0, k);
    }
}

/* Sorts a table and merges equal keys; equal keys sit together, the best state last. */
static u32 merge(u32 *a, u32 count) {
    u32 k, n = 0;
    sort(a, count);
    for (k = 0; k < count; k++) {
        if (n && (a[n - 1] & ~3u) == (a[k] & ~3u)) a[n - 1] = a[k];
        else a[n++] = a[k];
    }
    return n;
}

s32 jpr_words_finish(struct jpr_words *w) {
    if ((w->line_len || w->too_long) && w->state != W_BAD) {   /* a last line without its newline */
        if (w->line_len && w->line[w->line_len - 1] == '\r') w->line_len--;
        line(w);
        w->line_len = 0;
        w->too_long = 0;
    }
    if (w->state != W_LINES || w->lines != w->expected) return -1;
    w->count = merge(w->table, w->count);
    w->fb_count = merge(w->fallback, w->fb_count);
    return (s32)w->count;
}

u32 jpr_words_find(const u32 *table, u32 count, u32 hash) {
    u32 lo = 0, hi = count, key = hash & ~3u;
    while (lo < hi) {
        u32 mid = lo + ((hi - lo) >> 1), v = table[mid] & ~3u;
        if (v == key) return table[mid] & 3u;
        if (v < key) lo = mid + 1;
        else hi = mid;
    }
    return JPR_KOKORO_NONE;
}

u32 jpr_words_find_id(const u32 *table, u32 count, u32 id) {
    return id && id < JPR_KOKORO_ID_LIMIT ? jpr_words_find(table, count, id << 2) : JPR_KOKORO_NONE;
}

void jpr_kokoro_head_pack(u8 *out, u32 count, u32 fb_count, u32 synced) {
    wr32(out, JPR_KOKORO_MAGIC);
    wr32(out + 4, 2);
    wr32(out + 8, count);
    wr32(out + 12, fb_count);
    wr32(out + 16, synced);
}

s32 jpr_kokoro_head_counts(const u8 *head, u32 got, u32 cap, u32 fb_cap, u32 *fb) {
    if (got < JPR_KOKORO_HEAD || rd32(head) != JPR_KOKORO_MAGIC || rd32(head + 4) != 2 || rd32(head + 8) > cap ||
        rd32(head + 12) > fb_cap)
        return -1;
    *fb = rd32(head + 12);
    return (s32)rd32(head + 8);
}

s32 jpr_ids_count(const u8 *head, u32 got, u32 stamp, u32 *pairs) {
    if (got < JPR_IDS_HEAD || !stamp || rd32(head) != JPR_IDS_MAGIC || rd32(head + 4) != 2 ||
        rd32(head + 12) != stamp || rd32(head + 8) > 0x00FFFFFFu || rd32(head + 16) > 0x0FFFFFFFu)
        return -1;
    *pairs = rd32(head + 16);
    return (s32)rd32(head + 8);
}

static void set(u8 *bits, u32 e, u32 s) {
    u32 shift = (e & 3) * 2;
    bits[e >> 2] = (u8)((bits[e >> 2] & ~(3u << shift)) | (s << shift));
}

u32 jpr_kokoro_state(const u8 *bits, u32 entries, u32 entry) {
    if (entry >= entries || entry >= JPR_KOKORO_ENTRIES) return JPR_KOKORO_NONE;
    return (bits[entry >> 2] >> ((entry & 3) * 2)) & 3u;
}

void jpr_kokoro_mark_ids(u8 *bits, u32 first, const u32 *ids, u32 n, const u32 *table, u32 count) {
    u32 k;
    for (k = 0; k < n && first + k < JPR_KOKORO_ENTRIES; k++)
        set(bits, first + k, jpr_words_find_id(table, count, ids[k]));
}

void jpr_kokoro_mark_forms(u8 *bits, const u32 *pairs, u32 n, const u32 *fallback, u32 count) {
    u32 k;
    if (!count) return;
    for (k = 0; k < n; k++) {
        u32 e = pairs[2 * k], s;
        if (e >= JPR_KOKORO_ENTRIES) continue;
        s = jpr_words_find(fallback, count, pairs[2 * k + 1]);
        if (s > jpr_kokoro_state(bits, JPR_KOKORO_ENTRIES, e)) set(bits, e, s);
    }
}
