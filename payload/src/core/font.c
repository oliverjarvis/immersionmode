#include "font.h"

static const u8 default_size[JPR_FACES] = {17, 13, 13};

static u32 rd16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 rd32(const u8 *p) {
    return rd16(p) | (rd16(p + 2) << 16);
}

static void empty_face(struct jpr_face *face, u32 size) {
    face->index = 0;
    face->data = 0;
    face->count = 0;
    face->data_size = 0;
    face->size = size;
    face->ascent = (size * 7) >> 3;
    face->descent = 0;
}

void jpr_font_init(struct jpr_font *f) {
    u32 k;
    for (k = 0; k < JPR_FACES; k++) empty_face(&f->faces[k], default_size[k]);
    empty_face(&f->builtin, default_size[JPR_FACE_SMALL]);
}

/* Faces of a font file, at most `max` of them. 0, -1 for a bad header, -2 when it is cut short. */
static int parse(struct jpr_face *faces, u32 max, const u8 *blob, u32 size) {
    u32 n, k;
    if (!blob || size < JPR_FONT_HEADER || rd32(blob) != JPR_FONT_MAGIC || rd32(blob + 4) != JPR_FONT_VERSION)
        return -1;
    n = rd32(blob + 8);
    if (n > max) n = max;
    if (JPR_FONT_HEADER + n * JPR_FACE_ENTRY > size) return -2;
    for (k = 0; k < n; k++) {
        const u8 *e = blob + JPR_FONT_HEADER + k * JPR_FACE_ENTRY;
        u32 count = rd32(e), index = rd32(e + 4), data = rd32(e + 8);
        if (count > 0x10000 || index > size || count * JPR_GLYPH_ENTRY > size - index || data > size) return -2;
        faces[k].index = blob + index;
        faces[k].count = count;
        faces[k].data = blob + data;
        faces[k].data_size = size - data;
        faces[k].size = e[12];
        faces[k].ascent = e[13];
        faces[k].descent = e[14];
    }
    return 0;
}

int jpr_font_load(struct jpr_font *f, const u8 *blob, u32 size) {
    int err = parse(f->faces, JPR_FACES, blob, size);
    u32 k;
    if (err)
        for (k = 0; k < JPR_FACES; k++) empty_face(&f->faces[k], default_size[k]);
    return err;
}

int jpr_font_builtin(struct jpr_font *f, const u8 *blob, u32 size) {
    int err = parse(&f->builtin, 1, blob, size);
    if (err) empty_face(&f->builtin, default_size[JPR_FACE_SMALL]);
    return err;
}

static int find(const struct jpr_face *face, u16 cp, struct jpr_glyph *g) {
    u32 lo = 0, hi = face->count;
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        const u8 *e = face->index + mid * JPR_GLYPH_ENTRY;
        u32 at = rd16(e);
        if (at == cp) {
            u32 w = e[3], h = e[4], stride = (w + 3) >> 2, off = rd32(e + 8);
            if (off > face->data_size || stride * h > face->data_size - off) return 0;
            g->bits = face->data + off;
            g->stride = stride;
            g->x = (s8)e[5];
            g->y = (s8)e[6];
            g->w = w;
            g->h = h;
            g->advance = e[2];
            return 1;
        }
        if (at < cp) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

struct jpr_glyph jpr_font_glyph(const struct jpr_font *f, u32 face, u16 cp) {
    struct jpr_glyph g;
    const struct jpr_face *want = &f->faces[face];
    u32 other = face == JPR_FACE_TEXT ? JPR_FACE_SMALL : JPR_FACE_TEXT;
    if (find(want, cp, &g) || (face == JPR_FACE_ITALIC && find(&f->faces[JPR_FACE_SMALL], cp, &g)) ||
        find(&f->faces[other], cp, &g) || find(&f->builtin, cp, &g))
        return g;
    g.advance = cp >= 0x2E80 ? want->size : want->size >> 1;
    g.bits = 0;
    g.stride = 0;
    g.x = 1;
    g.w = g.advance > 2 ? g.advance - 2 : 1;
    g.y = -(s32)want->ascent;
    g.h = want->ascent;
    return g;
}

u32 jpr_font_advance(const struct jpr_font *f, u32 face, u16 cp) {
    return jpr_font_glyph(f, face, cp).advance;
}
