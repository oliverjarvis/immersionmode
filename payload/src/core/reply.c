#include "reply.h"

#define PATTERN_MAX 256   /* a longer pattern is tested on its first bytes */

static u32 kind_of(u8 tag) {
    switch (tag) {
    case 'T': return JPR_ITEM_TRANSLATION;
    case 'C': return JPR_ITEM_CHUNK;
    case 'M': return JPR_ITEM_MEANING;
    case 'N': return JPR_ITEM_NOTE;
    case 'G': return JPR_ITEM_GRAMMAR;
    default: return JPR_ITEM_PLAIN;
    }
}

static int bytes_of(u8 lead) {
    return lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

/* Does p (left bytes) start with the string c? */
static int is_char(const u8 *p, u32 left, const char *c) {
    u32 k;
    for (k = 0; c[k]; k++)
        if (k >= left || p[k] != (u8)c[k]) return 0;
    return 1;
}

static int ascii_space(u8 c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D);
}

static int ends_with(const u8 *s, u32 n, const u8 *t, u32 m) {
    u32 k;
    if (m > n) return 0;
    for (k = 0; k < m; k++)
        if (s[n - m + k] != t[k]) return 0;
    return 1;
}

static int contains(const u8 *s, u32 n, const u8 *t, u32 m) {
    u32 i, k;
    for (i = 0; i + m <= n; i++) {
        for (k = 0; k < m && s[i + k] == t[k]; k++) {}
        if (k == m) return 1;
    }
    return 0;
}

/* Does one piece, trimmed as JavaScript trims (ASCII spaces and the ideographic space), fit the chunk
 * (protocol.ts fits)? A one-character piece must end it; a longer one may be anywhere in it. */
static int piece_fits(const u8 *piece, u32 n, const u8 *chunk, u32 clen) {
    u32 k = 0, i;
    for (;;) {
        if (n && ascii_space(piece[0])) piece++, n--;
        else if (is_char(piece, n, "\xE3\x80\x80")) piece += 3, n -= 3;
        else break;
    }
    for (;;) {
        if (n && ascii_space(piece[n - 1])) n--;
        else if (n >= 3 && is_char(piece + n - 3, 3, "\xE3\x80\x80")) n -= 3;
        else break;
    }
    if (!n) return 0;
    for (i = 0; i < n; i += (u32)bytes_of(piece[i])) k++;
    return k == 1 ? ends_with(chunk, clen, piece, n) : contains(chunk, clen, piece, n);
}

/* kokoro's placement test for a pattern against a chunk (protocol.ts patternPieces and fits): notes in
 * brackets, （…） or (…), dropped, then pieces split at ・ and at 〜, ～ or ~; any piece fitting is enough. */
static int pattern_fits(const u8 *pat, u32 plen, const u8 *chunk, u32 clen) {
    u8 buf[PATTERN_MAX];
    u32 n = 0, i = 0, start;
    while (i < plen && n < sizeof buf) {
        const char *close = is_char(pat + i, plen - i, "\xEF\xBC\x88") ? "\xEF\xBC\x89" : pat[i] == '(' ? ")" : 0;
        if (close) {
            u32 j = i;
            while (j < plen && !is_char(pat + j, plen - j, close)) j++;
            if (j < plen) {   /* closed: the note goes; else the bracket stays, as the regex leaves it */
                i = j + (close[0] == ')' ? 1 : 3);
                continue;
            }
        }
        buf[n++] = pat[i++];
    }
    for (i = 0, start = 0; i <= n;) {
        u32 sep = i == n || buf[i] == '~' ? 1 :
                  is_char(buf + i, n - i, "\xE3\x83\xBB") || is_char(buf + i, n - i, "\xE3\x80\x9C") ||
                  is_char(buf + i, n - i, "\xEF\xBD\x9E") ? 3 : 0;
        if (!sep) {
            i++;
            continue;
        }
        if (i > start && piece_fits(buf + start, i - start, chunk, clen)) return 1;
        i += sep;
        start = i;
    }
    return 0;
}

/* Moves the points after the last chunk that are not its own to the "Other grammar" block. */
static void place_others(const u8 *reply, struct jpr_item *out, u32 n) {
    u32 k, last = n, other = 0;
    for (k = 0; k < n; k++)
        if (out[k].kind == JPR_ITEM_CHUNK) last = k;
    for (k = 0; k < n; k++) {
        struct jpr_point p;
        if (out[k].kind != JPR_ITEM_GRAMMAR) continue;
        if (last == n || out[k].block == 0) {
            out[k].block = JPR_BLOCK_OTHER;
            continue;
        }
        if (k < last) continue;
        jpr_reply_point(reply, &out[k], &p);
        if (!other && !pattern_fits(reply + p.pattern, p.pattern_len, reply + out[last].start, out[last].len)) other = 1;
        if (other) out[k].block = JPR_BLOCK_OTHER;
    }
}

u32 jpr_reply_items(const u8 *reply, u32 len, struct jpr_item *out, u32 max) {
    u32 i = 0, n = 0, block = 0;
    while (i < len && n < max) {
        u32 start = i, end, kind = JPR_ITEM_PLAIN;
        while (i < len && reply[i] != '\n') i++;
        end = i++;
        if (end > start && reply[end - 1] == '\r') end--;
        if (end == start) continue;
        if (end - start >= 2 && reply[start + 1] == ' ') {
            kind = kind_of(reply[start]);
            if (kind == JPR_ITEM_TRANSLATION && n) kind = JPR_ITEM_PLAIN;   /* only the first line translates */
            if (kind != JPR_ITEM_PLAIN || reply[start] == 'T') start += 2;
        }
        if (kind == JPR_ITEM_CHUNK) block++;
        if (end == start) continue;   /* a tag with nothing after it */
        out[n].kind = (u16)kind;
        out[n].block = (u16)block;
        out[n].start = start;
        out[n].len = end - start;
        n++;
    }
    place_others(reply, out, n);
    return n;
}

static u32 status_of(const u8 *p, u32 n) {
    static const char *const names[] = {"none", "new", "learning", "known"};
    u32 k, j;
    for (k = 0; k < 4; k++) {
        for (j = 0; j < n && names[k][j] && p[j] == (u8)names[k][j]; j++) {}
        if (j == n && !names[k][j]) return k;
    }
    return JPR_POINT_NONE;
}

/* "bunpro:<id>": the id, 0 for anything else (and for ids past nine digits). */
static u32 id_of(const u8 *p, u32 n) {
    u32 k, id = 0;
    if (n <= 7 || n > 7 + 9 || !is_char(p, n, "bunpro:")) return 0;
    for (k = 7; k < n; k++) {
        if (p[k] < '0' || p[k] > '9') return 0;
        id = id * 10 + (u32)(p[k] - '0');
    }
    return id;
}

void jpr_reply_point(const u8 *reply, const struct jpr_item *it, struct jpr_point *p) {
    u32 at[4], len[4], k, i = it->start, end = it->start + it->len;
    for (k = 0; k < 4; k++) {
        at[k] = i;
        while (i < end && (k == 3 || reply[i] != '\t')) i++;   /* the note takes the rest of the line */
        len[k] = i - at[k];
        if (i < end) i++;
    }
    p->id = id_of(reply + at[0], len[0]);
    p->status = status_of(reply + at[1], len[1]);
    p->pattern = at[2];
    p->pattern_len = len[2];
    p->note = at[3];
    p->note_len = len[3];
}

u32 jpr_point_body(u8 *out, u32 cap, u32 id) {
    static const char key[] = "key=bunpro:";
    u8 digits[10];
    u32 n = 0, at = 0, k;
    if (!id) return 0;
    while (id) {
        digits[n++] = (u8)('0' + id % 10);
        id /= 10;
    }
    if (sizeof key - 1 + n + 1 > cap) return 0;
    for (k = 0; key[k]; k++) out[at++] = (u8)key[k];
    while (n) out[at++] = digits[--n];
    out[at++] = '\n';
    return at;
}

u32 jpr_reply_keep(u8 *out, u32 cap, const u8 *in, u32 len) {
    u32 i = 0, n = 0;
    while (i < len) {
        u32 start = i, k;
        while (i < len && in[i] != '\n') i++;
        if (i < len) i++;                      /* the line with its newline */
        if (n + (i - start) > cap) break;      /* whole lines only */
        for (k = start; k < i; k++) out[n++] = in[k];
    }
    return n;
}
