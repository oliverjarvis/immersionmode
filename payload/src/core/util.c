#include "util.h"

void *jpr_memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *jpr_memset(void *dst, int value, u32 n) {
    u8 *d = dst;
    while (n--) *d++ = (u8)value;
    return dst;
}

int jpr_plausible_ptr(u32 p) {
    if (p & 3) return 0;
    return (p >= 0x08000000u && p < 0x10000000u) || (p >= 0x14000000u && p < 0x1C000000u) ||
           (p >= 0x30000000u && p < 0x38000000u);
}

int jpr_utf8_put(u8 *out, u32 *at, u32 cap, u32 cp) {
    u32 n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4, k;
    u8 *p;
    if (*at + n > cap) return -1;
    p = out + *at;
    if (n == 1) {
        p[0] = (u8)cp;
    } else {
        p[0] = (u8)((n == 2 ? 0xC0 : n == 3 ? 0xE0 : 0xF0) | (cp >> (6 * (n - 1))));
        for (k = 1; k < n; k++) p[k] = (u8)(0x80 | ((cp >> (6 * (n - 1 - k))) & 0x3F));
    }
    *at += n;
    return 0;
}

u32 jpr_format_u32(char *out, u32 n) {
    static const u32 tens[10] = {1000000000u, 100000000u, 10000000u, 1000000u, 100000u, 10000u, 1000u, 100u, 10u, 1u};
    u32 k, at = 0;
    for (k = 0; k < 10; k++) {
        u32 d = 0;
        while (n >= tens[k]) {
            n -= tens[k];
            d++;
        }
        if (d || at || k == 9) out[at++] = (char)('0' + d);
    }
    out[at] = 0;
    return at;
}
