#ifndef JPR_FONT_H
#define JPR_FONT_H

#include "types.h"

#define JPR_FONT_MAGIC 0x4652504Au /* "JPRF" */
#define JPR_FONT_VERSION 3
#define JPR_FONT_HEADER 16
#define JPR_FACE_ENTRY 16  /* u32 count, u32 index offset, u32 data offset, u8 size, ascent, descent, 0 */
#define JPR_GLYPH_ENTRY 12 /* u16 code point, u8 advance, w, h, s8 x, y, u8 0, u32 data offset */
#define JPR_FACES 3
#define JPR_FACE_TEXT 0    /* 17 px: dialog text and headwords */
#define JPR_FACE_SMALL 1   /* 13 px: the rest of the card, the counter, messages */
#define JPR_FACE_ITALIC 2  /* 13 px italic: English only; anything else falls back to the upright faces */

struct jpr_face {
    const u8 *index; /* `count` entries sorted by code point */
    const u8 *data;
    u32 count;
    u32 data_size;   /* bytes from data to the end of the file */
    u32 size;        /* pixels per em */
    u32 ascent;      /* height of a full-height glyph above the baseline: the box for a missing one */
    u32 descent;
};

struct jpr_font {
    struct jpr_face faces[JPR_FACES]; /* from font.bin; empty until it is read */
    struct jpr_face builtin;          /* the payload's own ASCII face */
};

/* One glyph: its 2-bit rows, and where they go relative to the pen position on the baseline. bits
 * is null when no face has the character; then w x h is the box to draw instead. */
struct jpr_glyph {
    const u8 *bits;
    u32 stride;
    s32 x, y;
    u32 w, h, advance;
};

void jpr_font_init(struct jpr_font *f);
/* Faces from font.bin: 0, -1 for a bad header, -2 when it is cut short. On failure the faces are
 * empty and only the built-in face and boxes remain. */
int jpr_font_load(struct jpr_font *f, const u8 *blob, u32 size);
int jpr_font_builtin(struct jpr_font *f, const u8 *blob, u32 size);
/* Looks in the wanted face, then the other upright one (both, for italic), then the built-in face; else a box. */
struct jpr_glyph jpr_font_glyph(const struct jpr_font *f, u32 face, u16 cp);
u32 jpr_font_advance(const struct jpr_font *f, u32 face, u16 cp);

#endif
