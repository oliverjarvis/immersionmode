#include "voice.h"

#include "capture.h"

static u32 get16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 get32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void clip_from(const u8 *e, struct jpr_voice_clip *c) {
    c->file = (u16)get16(e);
    c->line = (u16)get16(e + 2);
    c->part = (u16)get16(e + 4);
    c->at[0] = get32(e + 8);
    c->samples[0] = get32(e + 12);
    c->at[1] = get32(e + 16);
    c->samples[1] = get32(e + 20);
}

int jpr_voice_index_parse(struct jpr_voice_index *ix, const u8 *header, u32 len, jpr_voice_reader read, void *ctx) {
    u32 count, parts;
    ix->count = 0;
    if (len < JPR_VOICE_HEADER || header[0] != 'J' || header[1] != 'P' || header[2] != 'R' || header[3] != 'V' ||
        get32(header + 4) != 2 || get32(header + 12) != JPR_VOICE_RATE)
        return JPR_VOICE_BAD;
    count = get32(header + 8);
    parts = get32(header + 16);
    if (count > JPR_VOICE_MAX || !parts || parts > JPR_VOICE_PARTS) return JPR_VOICE_BAD;
    ix->count = count;
    ix->parts = parts;
    ix->read = read;
    ix->ctx = ctx;
    return JPR_VOICE_OK;
}

s32 jpr_voice_find(const struct jpr_voice_index *ix, u32 file, u32 line, struct jpr_voice_clip *out) {
    u32 lo = 0, hi = ix->count, want = (file << 16) | line;
    u8 e[JPR_VOICE_ENTRY];
    while (lo < hi) {
        u32 mid = (lo + hi) / 2, key;
        if (ix->read(ix->ctx, mid, e)) return -1;
        key = (get16(e) << 16) | get16(e + 2);
        if (key == want) {
            clip_from(e, out);
            return out->part < ix->parts ? (s32)mid : -1;
        }
        if (key < want) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

void jpr_voice_part_path(char *out, u32 part) {
    static const char head[] = "/jpreader/voice-";
    u32 n = 0, k, digits[3], d = 0;
    for (k = 0; head[k]; k++) out[n++] = head[k];
    do {                                    /* JPR_VOICE_PARTS is below 1000 */
        digits[d++] = part % 10;
        part /= 10;
    } while (part && d < 3);
    for (k = d; k; k--) out[n++] = (char)('0' + digits[k - 1]);
    out[n++] = '.';
    out[n++] = 'b';
    out[n++] = 'i';
    out[n++] = 'n';
    out[n] = 0;
}

s32 jpr_voice_file(s32 arc, u32 dat) {
    if (dat >= JPR_VOICE_SYSTEM) return -1;
    if (arc == 40 || arc == 41) return (s32)dat;
    if (arc == 30 || arc == 31) return (s32)(dat | JPR_VOICE_SYSTEM);
    return -1;
}

void jpr_voice_init(struct jpr_voice_player *p) {
    p->ix = 0;
    p->clip = -1;
    p->speed = 0;
    p->part = p->at = p->next = p->total = 0;
    p->slot = 0;
    p->message_clip = -1;
    p->seq = 0;
    p->arc = -1;
    p->dat = p->line = 0xFFFFFFFFu;
}

void jpr_voice_origin(s32 arc, u32 dat, s32 found_arc, u32 found_dat, s32 *out_arc, u32 *out_dat) {
    int found = arc == JPR_ARC_MEMORY && found_arc >= 0;
    *out_arc = found ? found_arc : arc;
    *out_dat = found ? found_dat : dat;
}

static u32 start(struct jpr_voice_player *p, s32 clip, u32 speed) {
    const struct jpr_voice_clip *c = &p->message;
    p->clip = clip;
    p->speed = speed;
    p->part = c->part;
    p->at = c->at[speed];
    p->total = c->samples[speed];
    p->next = 0;
    p->slot = 0;
    return JPR_VOICE_START;
}

u32 jpr_voice_message(struct jpr_voice_player *p, const struct jpr_voice_index *ix, u32 seq, s32 arc, u32 dat,
                      u32 line, u32 autoplay) {
    int same_line = arc == p->arc && dat == p->dat && line == p->line;
    s32 file = jpr_voice_file(arc, dat);
    if (seq == p->seq) return JPR_VOICE_NONE;
    p->seq = seq;
    p->ix = ix;
    if (same_line) return JPR_VOICE_NONE;            /* a later box of the line already playing */
    p->arc = arc;
    p->dat = dat;
    p->line = line;
    p->message_clip = file >= 0 && ix && ix->count ? jpr_voice_find(ix, (u32)file, line, &p->message) : -1;
    if (p->message_clip < 0 || !autoplay) return JPR_VOICE_NONE;
    return start(p, p->message_clip, 0);
}

u32 jpr_voice_replay(struct jpr_voice_player *p, u32 speed) {
    if (p->message_clip < 0 || !p->ix) return JPR_VOICE_NONE;
    return start(p, p->message_clip, speed ? 1 : 0);
}

u32 jpr_voice_refill(struct jpr_voice_player *p, const u8 status[JPR_VOICE_SLOTS], struct jpr_voice_fill *fills) {
    u32 n = 0;
    while (p->clip >= 0 && p->next < p->total &&
           (status[p->slot] == JPR_VOICE_FREE || status[p->slot] == JPR_VOICE_DONE) && n < JPR_VOICE_SLOTS) {
        u32 count = p->total - p->next < JPR_VOICE_CHUNK ? p->total - p->next : JPR_VOICE_CHUNK;
        fills[n].slot = p->slot;
        fills[n].part = p->part;
        fills[n].offset = p->at + 2 * p->next;
        fills[n].samples = count;
        n++;
        p->next += count;
        p->slot = (p->slot + 1) % JPR_VOICE_SLOTS;
        if (n < JPR_VOICE_SLOTS && status[p->slot] != JPR_VOICE_FREE && status[p->slot] != JPR_VOICE_DONE) break;
    }
    return n;
}

int jpr_voice_finished(const struct jpr_voice_player *p, const u8 status[JPR_VOICE_SLOTS]) {
    u32 k;
    if (p->clip < 0 || p->next < p->total) return 0;
    for (k = 0; k < JPR_VOICE_SLOTS; k++)
        if (status[k] == JPR_VOICE_QUEUED || status[k] == JPR_VOICE_PLAYING) return 0;
    return 1;
}

void jpr_voice_dropped(struct jpr_voice_player *p) {
    p->clip = -1;
    p->next = p->total = 0;
}
