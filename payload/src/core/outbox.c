#include "outbox.h"

#include "util.h"

static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

u32 jpr_outbox_pack(u8 *out, u32 seq, u32 kind, const u8 *body, u32 len) {
    if (!seq || JPR_OUTBOX_HEAD + len > JPR_OUTBOX_SLOT) return 0;
    wr32(out, JPR_OUTBOX_MAGIC);
    wr32(out + 4, seq);
    wr32(out + 8, len);
    wr32(out + 12, kind);
    jpr_memcpy(out + JPR_OUTBOX_HEAD, body, len);
    return JPR_OUTBOX_HEAD + len;
}

u32 jpr_outbox_head(const u8 *head, u32 got, u32 *len) {
    if (got < JPR_OUTBOX_HEAD || rd32(head) != JPR_OUTBOX_MAGIC) return 0;
    *len = rd32(head + 8);
    if (!*len || JPR_OUTBOX_HEAD + *len > JPR_OUTBOX_SLOT) return 0;
    return rd32(head + 4);
}

u32 jpr_outbox_kind(const u8 *head) {
    return rd32(head + 12) == JPR_OUTBOX_GRAMMAR ? JPR_OUTBOX_GRAMMAR : JPR_OUTBOX_WORD;
}

u32 jpr_outbox_slot(const u32 *seqs, u32 n) {
    u32 k, oldest = 0;
    for (k = 0; k < n; k++) {
        if (!seqs[k]) return k;
        if (seqs[k] < seqs[oldest]) oldest = k;
    }
    return oldest;
}

s32 jpr_outbox_next(const u32 *seqs, u32 n) {
    s32 best = -1;
    u32 k;
    for (k = 0; k < n; k++)
        if (seqs[k] && (best < 0 || seqs[k] < seqs[best])) best = (s32)k;
    return best;
}
