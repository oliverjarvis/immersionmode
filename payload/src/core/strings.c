#include "strings.h"

#define TAG_MARK 0x10

u32 jpr_text_key(const u16 *text, u32 len, u16 *out, u32 max) {
    u32 k = 0, n = 0;
    while (k < len) {
        u16 c = text[k];
        if (c == TAG_MARK) {                     /* the mark, its unit count, then that many units */
            if (k + 1 >= len || k + 2 + text[k + 1] > len) break;
            k += 2u + text[k + 1];
            continue;
        }
        k++;
        if (c == 0x0020 || c == 0x3000 || c == 0x000A || c == 0x000D) continue;
        if (n >= max) return 0;
        out[n++] = c;
    }
    return n;
}

u32 jpr_key_hash(const u16 *key, u32 n) {
    u32 h = 0x811C9DC5u, k;
    for (k = 0; k < n; k++) {
        h = (h ^ (key[k] & 0xFFu)) * 0x01000193u;
        h = (h ^ (key[k] >> 8)) * 0x01000193u;
    }
    return h;
}

u32 jpr_text_hash(const u16 *text, u32 len) {
    u32 h = 0x811C9DC5u, k = 0, n = 0;
    while (k < len) {
        u16 c = text[k];
        if (c == TAG_MARK) {
            if (k + 1 >= len || k + 2 + text[k + 1] > len) break;
            k += 2u + text[k + 1];
            continue;
        }
        k++;
        if (c == 0x0020 || c == 0x3000 || c == 0x000A || c == 0x000D) continue;
        h = (h ^ (c & 0xFFu)) * 0x01000193u;
        h = (h ^ (c >> 8)) * 0x01000193u;
        n++;
    }
    return n ? h : 0;
}
