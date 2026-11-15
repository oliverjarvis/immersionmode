#include "replies.h"

#include "util.h"

static u32 fnv(u32 h, const u8 *p, u32 n) {
    while (n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

static u32 cstr_len(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void put32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

u32 jpr_reply_key(const char *source, const u8 *sentence, u32 slen) {
    static const u8 zero = 0;
    u32 h = 2166136261u;
    h = fnv(h, (const u8 *)source, cstr_len(source));
    h = fnv(h, &zero, 1);
    return fnv(h, sentence, slen);
}

u32 jpr_reply_offset(u32 key) {
    return (key % JPR_REPLY_SLOTS) * JPR_REPLY_SLOT_BYTES;
}

u32 jpr_reply_pack(u8 *out, u32 key, const u8 *sentence, u32 slen, const u8 *reply, u32 rlen) {
    u32 n = JPR_REPLY_HEAD + slen + rlen;
    if (slen > 0xFFFF || rlen > 0xFFFF || n > JPR_REPLY_SLOT_BYTES) return 0;
    put32(out, JPR_REPLY_MAGIC);
    put32(out + 4, key);
    put32(out + 8, slen | (rlen << 16));
    put32(out + 12, 0);
    jpr_memcpy(out + JPR_REPLY_HEAD, sentence, slen);
    jpr_memcpy(out + JPR_REPLY_HEAD + slen, reply, rlen);
    return n;
}

int jpr_reply_head(const u8 *head, u32 got, u32 key, const u8 *sentence, u32 slen, u32 *rlen) {
    u32 lens, k;
    if (got < JPR_REPLY_HEAD + slen || get32(head) != JPR_REPLY_MAGIC || get32(head + 4) != key) return 0;
    lens = get32(head + 8);
    if ((lens & 0xFFFF) != slen) return 0;
    for (k = 0; k < slen; k++)
        if (head[JPR_REPLY_HEAD + k] != sentence[k]) return 0;
    *rlen = lens >> 16;
    return *rlen > 0 && JPR_REPLY_HEAD + slen + *rlen <= JPR_REPLY_SLOT_BYTES;
}
