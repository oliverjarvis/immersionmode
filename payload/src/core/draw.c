#include "draw.h"

void jpr_pen_init(struct jpr_pen *pen, const struct jpr_surface *s, const struct jpr_font *f, u32 face,
                  jpr_rgb bg, jpr_rgb fg, s32 clip_top, s32 clip_bottom) {
    pen->s = s;
    pen->f = f;
    pen->face = face;
    pen->fg = fg;
    pen->clip_top = clip_top;
    pen->clip_bottom = clip_bottom;
    pen->bold = 0;
    jpr_shades(s, bg, fg, &pen->shades);
}

void jpr_pen_bold(struct jpr_pen *pen) {
    pen->bold = 1;
}

u32 jpr_pen_advance(const struct jpr_pen *pen, u16 cp) {
    return jpr_font_advance(pen->f, pen->face, cp) + pen->bold;
}

/* A filled rectangle cut to the pen's rows. */
static void fill(const struct jpr_pen *pen, s32 x, s32 y, s32 w, s32 h) {
    s32 y1 = y + h;
    if (y < pen->clip_top) y = pen->clip_top;
    if (y1 > pen->clip_bottom) y1 = pen->clip_bottom;
    if (y1 > y) jpr_fill(pen->s, x, y, w, y1 - y, pen->fg);
}

u32 jpr_draw_char(const struct jpr_pen *pen, s32 x, s32 base, u16 cp) {
    struct jpr_glyph g = jpr_font_glyph(pen->f, pen->face, cp);
    s32 left = x + g.x, top = base + g.y, w = (s32)g.w, h = (s32)g.h;
    if (!pen->s) return g.advance + pen->bold;   /* a measuring pen: lays text out without drawing it */
    if (g.bits) {
        if (pen->bold)   /* bold without a bold face: the glyph and itself a pixel to the right */
            jpr_coverage_bold(pen->s, left, top, g.bits, g.stride, g.w, g.h, &pen->shades, pen->clip_top,
                              pen->clip_bottom);
        else
            jpr_coverage(pen->s, left, top, g.bits, g.stride, g.w, g.h, &pen->shades, pen->clip_top, pen->clip_bottom);
    } else if (w > 0 && h > 0) {  /* no glyph anywhere: an empty box */
        fill(pen, left, top, w, 1);
        fill(pen, left, top + h - 1, w, 1);
        fill(pen, left, top, 1, h);
        fill(pen, left + w - 1, top, 1, h);
    }
    return g.advance + pen->bold;
}

s32 jpr_draw_ascii(const struct jpr_pen *pen, s32 x, s32 base, const char *str) {
    for (; *str; str++) x += (s32)jpr_draw_char(pen, x, base, (u16)(u8)*str);
    return x;
}

u32 jpr_measure_ascii(const struct jpr_font *f, u32 face, const char *str) {
    u32 w = 0;
    for (; *str; str++) w += jpr_font_advance(f, face, (u16)(u8)*str);
    return w;
}
u32 jpr_utf8_next(const u8 *p, u32 len, u32 *i) {
    u32 c = p[(*i)++], need, cp, k;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        need = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        cp = c & 0x07;
    } else {
        return 0xFFFD;
    }
    for (k = 0; k < need; k++) {
        if (*i >= len || (p[*i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (p[(*i)++] & 0x3F);
    }
    return cp > 0xFFFF ? 0xFFFD : cp;
}

s32 jpr_draw_utf8(const struct jpr_pen *pen, s32 x, s32 base, const u8 *p, u32 len) {
    u32 i = 0;
    while (i < len) x += (s32)jpr_draw_char(pen, x, base, (u16)jpr_utf8_next(p, len, &i));
    return x;
}

u32 jpr_pen_measure_utf8(const struct jpr_pen *pen, const u8 *p, u32 len) {
    u32 i = 0, w = 0;
    while (i < len) w += jpr_pen_advance(pen, (u16)jpr_utf8_next(p, len, &i));
    return w;
}

u32 jpr_measure_utf8(const struct jpr_font *f, u32 face, const u8 *p, u32 len) {
    u32 i = 0, w = 0;
    while (i < len) w += jpr_font_advance(f, face, (u16)jpr_utf8_next(p, len, &i));
    return w;
}

s32 jpr_draw_wrapped(const struct jpr_pen *pen, s32 x, s32 base, s32 left, s32 right, s32 pitch, s32 last,
                     const u8 *p, u32 len) {
    u32 i = 0;
    if (base > last) return 0;
    while (i < len) {
        u32 end = i;
        s32 w;
        if (p[i] == ' ') {
            if (x > left) x += (s32)jpr_pen_advance(pen, ' ');
            i++;
            continue;
        }
        while (end < len && p[end] != ' ') end++;
        w = (s32)jpr_pen_measure_utf8(pen, p + i, end - i);
        if (x > left && x + w > right) {
            base += pitch;
            if (base > last) return 0;
            x = left;
        }
        while (i < end) {               /* a word wider than the line breaks where it must */
            u32 k = i;
            u16 cp = (u16)jpr_utf8_next(p, end, &k);
            s32 a = (s32)jpr_pen_advance(pen, cp);
            if (x > left && x + a > right) {
                base += pitch;
                if (base > last) return 0;
                x = left;
            }
            jpr_draw_char(pen, x, base, cp);
            x += a;
            i = k;
        }
    }
    return base;
}
