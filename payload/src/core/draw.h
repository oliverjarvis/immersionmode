#ifndef JPR_DRAW_H
#define JPR_DRAW_H

#include "font.h"
#include "surface.h"

/* Everything needed to draw text in one face and colour over a known background. */
struct jpr_pen {
    const struct jpr_surface *s;
    const struct jpr_font *f;
    u32 face;
    jpr_rgb fg;
    struct jpr_shades shades;
    s32 clip_top, clip_bottom; /* rows drawn: [clip_top, clip_bottom) */
    u32 bold;                  /* 1: each pixel the darker of the glyph and itself a pixel left; one pixel wider */
};

void jpr_pen_init(struct jpr_pen *pen, const struct jpr_surface *s, const struct jpr_font *f, u32 face,
                  jpr_rgb bg, jpr_rgb fg, s32 clip_top, s32 clip_bottom);
/* Makes the pen bold (pens start regular; italic is the JPR_FACE_ITALIC face). */
void jpr_pen_bold(struct jpr_pen *pen);
/* How far the pen moves for a character: its face's advance, one more when bold. */
u32 jpr_pen_advance(const struct jpr_pen *pen, u16 cp);
u32 jpr_pen_measure_utf8(const struct jpr_pen *pen, const u8 *p, u32 len);
/* Draws one character with the pen at (x, base) on the baseline; returns its advance. A pen whose
 * surface is null only measures. */
u32 jpr_draw_char(const struct jpr_pen *pen, s32 x, s32 base, u16 cp);
/* Draws an ASCII string; returns the x after it. */
s32 jpr_draw_ascii(const struct jpr_pen *pen, s32 x, s32 base, const char *str);
u32 jpr_measure_ascii(const struct jpr_font *f, u32 face, const char *str);
/* The code point at p[*i], moving *i past it; U+FFFD when it is malformed or outside the BMP. */
u32 jpr_utf8_next(const u8 *p, u32 len, u32 *i);
/* Draws UTF-8 text; returns the x after it. */
s32 jpr_draw_utf8(const struct jpr_pen *pen, s32 x, s32 base, const u8 *p, u32 len);
u32 jpr_measure_utf8(const struct jpr_font *f, u32 face, const u8 *p, u32 len);
/* Word-wrapped UTF-8 starting at (x, base); later lines start at `left`, `pitch` rows apart, while
 * the baseline stays at or above `last`, breaking at spaces and, for a word wider than the line,
 * anywhere. Returns the last baseline used, or 0 when the text did not fit. */
s32 jpr_draw_wrapped(const struct jpr_pen *pen, s32 x, s32 base, s32 left, s32 right, s32 pitch, s32 last,
                     const u8 *p, u32 len);

#endif
