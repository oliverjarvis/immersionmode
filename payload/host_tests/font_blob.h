/* Builds small font.bin files for host tests: every glyph is a solid block. */
#include <string.h>

#include "core/font.h"
#include "core/types.h"

struct test_glyph {
    u16 cp;
    u8 advance, w, h;
    s8 x, y;
};

static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

/* Writes a font with `faces` faces into out and returns its size. Face k has counts[k] glyphs
 * from glyphs[k] (sorted by code point), size sizes[k] and ascent ascents[k]. */
static u32 test_font(u8 *out, u32 faces, const struct test_glyph *const *glyphs, const u32 *counts,
                     const u32 *sizes, const u32 *ascents) {
    u32 at = 16 + 16 * faces, k, n;
    memset(out, 0, 16 + 16 * faces);
    memcpy(out, "JPRF", 4);
    put32(out + 4, JPR_FONT_VERSION);
    put32(out + 8, faces);
    for (k = 0; k < faces; k++) {
        u8 *e = out + 16 + 16 * k;
        u32 index = at, data, off = 0;
        at += 12 * counts[k];
        at = (at + 3) & ~3u;
        data = at;
        for (n = 0; n < counts[k]; n++) {
            const struct test_glyph *g = &glyphs[k][n];
            u8 *ix = out + index + 12 * n;
            u32 bytes = ((g->w + 3u) / 4u) * g->h;
            put16(ix, g->cp);
            ix[2] = g->advance; ix[3] = g->w; ix[4] = g->h; ix[5] = (u8)g->x; ix[6] = (u8)g->y; ix[7] = 0;
            put32(ix + 8, off);
            memset(out + data + off, 0xFF, bytes);
            off += bytes;
        }
        at = (data + off + 3) & ~3u;
        put32(e, counts[k]);
        put32(e + 4, index);
        put32(e + 8, data);
        e[12] = (u8)sizes[k]; e[13] = (u8)ascents[k]; e[14] = 2; e[15] = 0;
    }
    return at;
}
