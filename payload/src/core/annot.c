#include "annot.h"

#define TAG_MARK 0x0010

static u32 rd16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

/* Is the text segment `seg` at text[at]? Its first and last units must match. */
static int run_at(const u16 *text, u32 len, u32 at, const u8 *seg) {
    u32 n = rd16(seg + 2);
    return n && at + n <= len && text[at] == rd16(seg + 4) && text[at + n - 1] == rd16(seg + 6);
}

/* Is a tag of n units left in place at text[at]? */
static int tag_at(const u16 *text, u32 len, u32 at, u32 n) {
    return n >= 2 && at + n <= len && text[at] == TAG_MARK && text[at + 1] == n - 2;
}

static int segment_at(const u16 *text, u32 len, u32 at, const u8 *seg) {
    return seg[0] == JPR_SEG_TEXT ? run_at(text, len, at, seg) : tag_at(text, len, at, rd16(seg + 2));
}

/* Where each segment starts in the text, taking the earliest (latest 0) or latest (1) place for the
 * text after each replaced tag. 0, or -1 when the text does not fit. */
static int walk(const u8 *seg, u32 segs, const u16 *text, u32 len, int latest, u32 *shown) {
    u32 k, at = 0;
    for (k = 0; k < segs; k++) {
        const u8 *s = seg + k * 8;
        u32 n = rd16(s + 2);
        shown[k] = at;
        if (s[0] == JPR_SEG_TEXT) {
            if (!run_at(text, len, at, s)) return -1;
            at += n;
        } else if (tag_at(text, len, at, n)) {
            at += n;
        } else if (k + 1 == segs) {
            at = len;                   /* an inserted name ends the line */
        } else if (!latest) {
            while (at < len && !segment_at(text, len, at, s + 8)) at++;
            if (at >= len) return -1;
        } else {
            u32 p = len;
            while (p > at && !segment_at(text, len, p - 1, s + 8)) p--;
            if (p == at) return -1;
            at = p - 1;
        }
    }
    return at == len ? 0 : -1;
}

s32 jpr_annot_place(const u8 *rec, u32 rec_len, const u16 *text, u32 len, struct jpr_unit *out, u32 max) {
    u32 shown[JPR_SEGMENTS_MAX], other[JPR_SEGMENTS_MAX], stored[JPR_SEGMENTS_MAX];
    u32 segs, units, k, j, from = 0;
    const u8 *seg, *unit;

    if (rec_len < 4) return -1;
    segs = rd16(rec);
    units = rd16(rec + 2);
    if (segs > JPR_SEGMENTS_MAX || units > max || 4 + segs * 8 + units * 12 > rec_len) return -1;
    seg = rec + 4;
    unit = seg + segs * 8;

    /* An inserted name ends where the next segment is found. The earliest and the latest choice at
     * every replaced tag agree exactly when the text fits one way only; when they differ (a name
     * holding the next run, as のぞみ before の), the line gets no units rather than misplaced ones. */
    if (walk(seg, segs, text, len, 0, shown) || walk(seg, segs, text, len, 1, other)) return -1;
    for (k = 0; k < segs; k++) {
        if (shown[k] != other[k]) return -1;
        stored[k] = from;
        from += rd16(seg + k * 8 + 2);
    }

    for (k = 0; k < units; k++) {
        const u8 *u = unit + k * 12;
        u32 start = rd16(u), n = rd16(u + 2);
        for (j = 0; j < segs; j++) {
            const u8 *s = seg + j * 8;
            if (s[0] == JPR_SEG_TEXT && start >= stored[j] && start + n <= stored[j] + rd16(s + 2)) break;
        }
        if (j == segs || !n) return -1;
        out[k].start = (u16)(shown[j] + start - stored[j]);
        out[k].len = (u16)n;
        out[k].entry = rd16(u + 4) | (rd16(u + 6) << 16);
        out[k].label = (u16)rd16(u + 8);
        out[k].kind = u[10];
        out[k].pad = 0;
    }
    return (s32)units;
}
