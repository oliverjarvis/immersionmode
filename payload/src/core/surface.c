#include "surface.h"

int jpr_surface_init(struct jpr_surface *s, void *base, u32 format, u32 width, u32 height) {
    u32 bpp;
    switch (format) {
    case JPR_GL_RGBA8: bpp = 4; break;
    case JPR_GL_RGB8: bpp = 3; break;
    case JPR_GL_RGB565:
    case JPR_GL_RGB5A1:
    case JPR_GL_RGBA4: bpp = 2; break;
    default: return 0;
    }
    if (!base || !width || !height) return 0;
    s->base = base;
    s->format = format;
    s->bpp = bpp;
    s->width = width;
    s->height = height;
    return 1;
}

u32 jpr_surface_bytes(const struct jpr_surface *s) {
    return s->width * s->height * s->bpp;
}

/* The bytes of one pixel of colour c, in memory order. */
static void encode(const struct jpr_surface *s, jpr_rgb c, u8 out[4]) {
    u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF, v;
    switch (s->format) {
    case JPR_GL_RGBA8:
        out[0] = 0xFF; out[1] = (u8)b; out[2] = (u8)g; out[3] = (u8)r;
        return;
    case JPR_GL_RGB8:
        out[0] = (u8)b; out[1] = (u8)g; out[2] = (u8)r;
        return;
    case JPR_GL_RGB565: v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;
    case JPR_GL_RGB5A1: v = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1; break;
    default: v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | 0xF; break;
    }
    out[0] = (u8)v;
    out[1] = (u8)(v >> 8);
}

/* Words may alias the framebuffer's bytes. */
typedef u32 __attribute__((may_alias)) u32_alias;

/* Writes n bytes of repeated pixels starting at a pixel boundary: bytes until the pointer is
 * word-aligned, then whole words (12 bytes hold a whole number of 2-, 3- or 4-byte pixels), then
 * the rest. Drawing runs once per frame, so this is the hot loop. */
static void fill_run(u8 *p, u32 n, const u8 *px, u32 bpp) {
    u32 k = 0; /* index of the next byte within a pixel */
    while (n && ((unsigned long)p & 3)) {
        *p++ = px[k];
        if (++k == bpp) k = 0;
        n--;
    }
    if (n >= 12) {
        u8 pattern[12];
        u32 words[3], i, j = k;
        u32_alias *q = (u32_alias *)p;
        for (i = 0; i < 12; i++) {
            pattern[i] = px[j];
            if (++j == bpp) j = 0;
        }
        for (i = 0; i < 3; i++)
            words[i] = (u32)pattern[4 * i] | ((u32)pattern[4 * i + 1] << 8) |
                       ((u32)pattern[4 * i + 2] << 16) | ((u32)pattern[4 * i + 3] << 24);
        while (n >= 48) {
            u32 a = words[0], b = words[1], c = words[2];
            q[0] = a; q[1] = b; q[2] = c;
            q[3] = a; q[4] = b; q[5] = c;
            q[6] = a; q[7] = b; q[8] = c;
            q[9] = a; q[10] = b; q[11] = c;
            q += 12;
            n -= 48;
        }
        while (n >= 12) {
            q[0] = words[0];
            q[1] = words[1];
            q[2] = words[2];
            q += 3;
            n -= 12;
        }
        p = (u8 *)q;
    }
    while (n) {
        *p++ = px[k];
        if (++k == bpp) k = 0;
        n--;
    }
}

void jpr_fill(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, jpr_rgb c) {
    u8 px[4];
    s32 x1 = x + w, y1 = y + h, col;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (s32)s->width) x1 = (s32)s->width;
    if (y1 > (s32)s->height) y1 = (s32)s->height;
    if (x >= x1 || y >= y1) return;
    encode(s, c, px);
    if (y == 0 && y1 == (s32)s->height) {   /* whole columns lie next to each other: one run */
        fill_run(s->base + (u32)x * s->height * s->bpp, (u32)(x1 - x) * s->height * s->bpp, px, s->bpp);
        return;
    }
    for (col = x; col < x1; col++) {
        /* Within a column, memory runs from the bottom of the screen upwards. */
        u8 *p = s->base + ((u32)col * s->height + s->height - (u32)y1) * s->bpp;
        fill_run(p, (u32)(y1 - y) * s->bpp, px, s->bpp);
    }
}

void jpr_put(const struct jpr_surface *s, s32 x, s32 y, jpr_rgb c) {
    jpr_fill(s, x, y, 1, 1, c);
}

void jpr_shades(const struct jpr_surface *s, jpr_rgb bg, jpr_rgb fg, struct jpr_shades *out) {
    static const u32 weight[3] = {85, 171, 256};
    u32 k;
    for (k = 0; k < 3; k++) {
        u32 w = weight[k], v = 256 - w;
        u32 r = (((bg >> 16) & 0xFF) * v + ((fg >> 16) & 0xFF) * w) >> 8;
        u32 g = (((bg >> 8) & 0xFF) * v + ((fg >> 8) & 0xFF) * w) >> 8;
        u32 b = ((bg & 0xFF) * v + (fg & 0xFF) * w) >> 8;
        encode(s, (r << 16) | (g << 8) | b, out->px[k]);
    }
}

void jpr_coverage(const struct jpr_surface *s, s32 x, s32 y, const u8 *bits, u32 stride, u32 w, u32 h,
                  const struct jpr_shades *sh, s32 clip_top, s32 clip_bottom) {
    s32 r0 = 0, r1 = (s32)h, c0 = 0, c1 = (s32)w, c;
    u32 bpp = s->bpp, rows, k;
    if (clip_top < 0) clip_top = 0;
    if (clip_bottom > (s32)s->height) clip_bottom = (s32)s->height;
    if (y + r0 < clip_top) r0 = clip_top - y;
    if (y + r1 > clip_bottom) r1 = clip_bottom - y;
    if (x + c0 < 0) c0 = -x;
    if (x + c1 > (s32)s->width) c1 = (s32)s->width - x;
    if (r0 >= r1 || c0 >= c1) return;
    rows = (u32)(r1 - r0);
    /* Column by column, as the framebuffer is laid out: each row down is one pixel back. Drawing
     * runs every frame, so this is the hot loop (walking rows instead measured slower). */
    for (c = c0; c < c1; c++) {
        const u8 *src = bits + (u32)r0 * stride + ((u32)c >> 2);
        u32 shift = 6 - (((u32)c & 3) << 1), n;
        u8 *p = s->base + ((u32)(x + c) * s->height + s->height - 1 - (u32)(y + r0)) * bpp;
        if (bpp == 3) {
            for (n = rows; n; n--, src += stride, p -= 3) {
                u32 level = (*src >> shift) & 3;
                if (level) {
                    const u8 *px = sh->px[level - 1];
                    p[0] = px[0];
                    p[1] = px[1];
                    p[2] = px[2];
                }
            }
        } else {
            for (n = rows; n; n--, src += stride, p -= bpp) {
                u32 level = (*src >> shift) & 3;
                if (level)
                    for (k = 0; k < bpp; k++) p[k] = sh->px[level - 1][k];
            }
        }
    }
}

/* Coverage level of bitmap pixel (row, col), 0 outside its w columns. */
static u32 level_at(const u8 *bits, u32 stride, u32 w, u32 row, s32 col) {
    if (col < 0 || (u32)col >= w) return 0;
    return (bits[row * stride + ((u32)col >> 2)] >> (6 - (((u32)col & 3) << 1))) & 3;
}

void jpr_coverage_bold(const struct jpr_surface *s, s32 x, s32 y, const u8 *bits, u32 stride, u32 w, u32 h,
                       const struct jpr_shades *sh, s32 clip_top, s32 clip_bottom) {
    s32 r0 = 0, r1 = (s32)h, c0 = 0, c1 = (s32)w + 1, c, r;
    u32 bpp = s->bpp, k;
    if (clip_top < 0) clip_top = 0;
    if (clip_bottom > (s32)s->height) clip_bottom = (s32)s->height;
    if (y + r0 < clip_top) r0 = clip_top - y;
    if (y + r1 > clip_bottom) r1 = clip_bottom - y;
    if (x + c0 < 0) c0 = -x;
    if (x + c1 > (s32)s->width) c1 = (s32)s->width - x;
    for (c = c0; c < c1; c++) {
        u8 *p = s->base + ((u32)(x + c) * s->height + s->height - 1 - (u32)(y + r0)) * bpp;
        for (r = r0; r < r1; r++, p -= bpp) {
            u32 a = level_at(bits, stride, w, (u32)r, c), b = level_at(bits, stride, w, (u32)r, c - 1);
            u32 level = a > b ? a : b;   /* the darker of the glyph and itself a pixel to the left */
            if (level)
                for (k = 0; k < bpp; k++) p[k] = sh->px[level - 1][k];
        }
    }
}

void jpr_triangle(const struct jpr_surface *s, s32 cx, s32 cy, s32 size, int left, jpr_rgb c) {
    s32 tip = left ? cx - (size >> 1) : cx + (size >> 1);
    s32 dy;
    for (dy = -size; dy <= size; dy++) {
        s32 d = dy < 0 ? -dy : dy;
        s32 len = size - d + 1;
        if (len <= 0) continue;
        if (left) jpr_fill(s, tip + d, cy + dy, len, 1, c);
        else jpr_fill(s, tip - size, cy + dy, len, 1, c);
    }
}

void jpr_triangle_v(const struct jpr_surface *s, s32 cx, s32 cy, s32 size, int up, jpr_rgb c) {
    s32 tip = up ? cy - (size >> 1) : cy + (size >> 1);
    s32 d;
    for (d = 0; d <= size; d++) jpr_fill(s, cx - d, up ? tip + d : tip - d, 2 * d + 1, 1, c);
}

static u32 isqrt(u32 n) {
    u32 r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* Pixels a rounded rectangle's row k (from its nearest edge) is cut in by its corner of radius r. */
static s32 corner_inset(s32 r, s32 k) {
    s32 c, e;
    if (k >= r) return 0;
    c = 2 * (r - k) - 1;                           /* the row's centre, in half pixels from the corner's centre */
    e = (s32)isqrt((u32)(4 * r * r - c * c));      /* the circle's half-width there, in half pixels */
    return r - (e + 1) / 2;
}

static void round_fill(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, s32 r, jpr_rgb c, s32 top,
                       s32 bottom) {
    s32 k;
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (k = 0; k < h; k++) {
        s32 row = y + k, edge = k < h - 1 - k ? k : h - 1 - k, in = corner_inset(r, edge);
        if (in == 0 && k < h - r) {                /* the straight middle: one fill, cut to the clip */
            s32 from = row > top ? row : top, to = y + h - r < bottom ? y + h - r : bottom;
            if (to > from) jpr_fill(s, x, from, w, to - from, c);
            k = h - r - 1;
            continue;
        }
        if (row >= top && row < bottom) jpr_fill(s, x + in, row, w - 2 * in, 1, c);
    }
}

void jpr_round_rect(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, s32 r, jpr_rgb fill, jpr_rgb border,
                    int bordered, s32 clip_top, s32 clip_bottom) {
    if (!bordered) {
        round_fill(s, x, y, w, h, r, fill, clip_top, clip_bottom);
        return;
    }
    round_fill(s, x, y, w, h, r, border, clip_top, clip_bottom);
    round_fill(s, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill, clip_top, clip_bottom);
}
