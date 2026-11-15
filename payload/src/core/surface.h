#ifndef JPR_SURFACE_H
#define JPR_SURFACE_H

#include "types.h"

/* GL format codes the game passes for its display buffers. */
#define JPR_GL_RGBA8  0x8058
#define JPR_GL_RGB8   0x8051
#define JPR_GL_RGB565 0x8D62
#define JPR_GL_RGB5A1 0x8057
#define JPR_GL_RGBA4  0x8056

/* A 3DS framebuffer. It is stored rotated: one column of `height` pixels after another,
 * each column running from the bottom of the screen to the top. */
struct jpr_surface {
    u8 *base;
    u32 format; /* JPR_GL_* */
    u32 bpp;    /* bytes per pixel */
    u32 width;  /* columns: the screen's width */
    u32 height; /* pixels per column: the screen's height */
};

typedef u32 jpr_rgb; /* 0xRRGGBB */

int jpr_surface_init(struct jpr_surface *s, void *base, u32 format, u32 width, u32 height);
u32 jpr_surface_bytes(const struct jpr_surface *s);
void jpr_put(const struct jpr_surface *s, s32 x, s32 y, jpr_rgb c);
void jpr_fill(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, jpr_rgb c);
/* Encoded pixels for coverage 1/3, 2/3 and full of one colour over another. */
struct jpr_shades {
    u8 px[3][4];
};

void jpr_shades(const struct jpr_surface *s, jpr_rgb bg, jpr_rgb fg, struct jpr_shades *out);
/* A 2-bit coverage bitmap with its top-left corner at (x, y): `stride` bytes per row, four pixels
 * per byte, leftmost in the top bits. Only rows whose screen y is in [clip_top, clip_bottom) are
 * drawn; coverage 0 leaves the pixel alone. */
void jpr_coverage(const struct jpr_surface *s, s32 x, s32 y, const u8 *bits, u32 stride, u32 w, u32 h,
                  const struct jpr_shades *sh, s32 clip_top, s32 clip_bottom);
/* A filled rectangle with quarter-circle corners of radius r, and a 1-pixel border in `border` when
 * bordered. Only rows in [clip_top, clip_bottom) are drawn. A badge or pill is r = h / 2. */
void jpr_round_rect(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, s32 r, jpr_rgb fill, jpr_rgb border,
                    int bordered, s32 clip_top, s32 clip_bottom);
/* The same bitmap made bold: one column wider, each pixel the darker of the glyph's own and its left
 * neighbour's, so soft edges never lighten a stroke. */
void jpr_coverage_bold(const struct jpr_surface *s, s32 x, s32 y, const u8 *bits, u32 stride, u32 w, u32 h,
                       const struct jpr_shades *sh, s32 clip_top, s32 clip_bottom);
/* Filled triangle around (cx, cy), `size` pixels from tip to base, pointing left or right. */
void jpr_triangle(const struct jpr_surface *s, s32 cx, s32 cy, s32 size, int left, jpr_rgb c);
/* The same, pointing up or down. */
void jpr_triangle_v(const struct jpr_surface *s, s32 cx, s32 cy, s32 size, int up, jpr_rgb c);

#endif
