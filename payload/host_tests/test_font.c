#include <stdio.h>
#include <string.h>

#include "core/font.h"
#include "font_blob.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const struct test_glyph text_glyphs[] = {{0x3042, 17, 15, 15, 1, -15}, {0x6F22, 17, 16, 16, 0, -15}};
static const struct test_glyph small_glyphs[] = {{0x41, 8, 7, 9, 0, -9}, {0x3042, 13, 11, 11, 1, -11}};
static const struct test_glyph italic_glyphs[] = {{0x41, 7, 8, 9, 1, -9}, {0x61, 6, 6, 7, 1, -7}};
static const struct test_glyph builtin_glyphs[] = {{0x41, 8, 7, 9, 0, -9}, {0x42, 8, 6, 9, 1, -9}};
static u8 blob[4096] __attribute__((aligned(4))), builtin[1024] __attribute__((aligned(4)));
static u32 blob_size, builtin_size;

static void make(void) {
    const struct test_glyph *g[3] = {text_glyphs, small_glyphs, italic_glyphs}, *b[1] = {builtin_glyphs};
    const u32 counts[3] = {2, 2, 2}, sizes[3] = {17, 13, 13}, ascents[3] = {15, 10, 10};
    const u32 bcount[1] = {2}, bsize[1] = {13}, bascent[1] = {10};
    blob_size = test_font(blob, 3, g, counts, sizes, ascents);
    builtin_size = test_font(builtin, 1, b, bcount, bsize, bascent);
}

static void test_lookup_in_the_wanted_face(void) {
    struct jpr_font f;
    struct jpr_glyph g;
    jpr_font_init(&f);
    CHECK(jpr_font_load(&f, blob, blob_size) == 0);
    g = jpr_font_glyph(&f, JPR_FACE_TEXT, 0x3042);
    CHECK(g.bits && g.advance == 17 && g.w == 15 && g.h == 15 && g.x == 1 && g.y == -15 && g.stride == 4);
    g = jpr_font_glyph(&f, JPR_FACE_SMALL, 0x3042);
    CHECK(g.advance == 13 && g.w == 11);
}

static void test_fallbacks(void) {
    struct jpr_font f;
    struct jpr_glyph g;
    jpr_font_init(&f);
    jpr_font_builtin(&f, builtin, builtin_size);
    jpr_font_load(&f, blob, blob_size);
    g = jpr_font_glyph(&f, JPR_FACE_SMALL, 0x6F22);       /* kanji in a card line: the text face */
    CHECK(g.bits && g.advance == 17);
    g = jpr_font_glyph(&f, JPR_FACE_TEXT, 0x42);          /* only the built-in face has B */
    CHECK(g.bits && g.advance == 8 && g.x == 1);
    g = jpr_font_glyph(&f, JPR_FACE_TEXT, 0x4E00);        /* nowhere: a full-width box */
    CHECK(!g.bits && g.advance == 17 && g.h == 15 && g.y == -15);
    g = jpr_font_glyph(&f, JPR_FACE_SMALL, 0x00E9);       /* nowhere, narrow: half the face size */
    CHECK(!g.bits && g.advance == 6);
    CHECK(jpr_font_advance(&f, JPR_FACE_TEXT, 0x3042) == 17);
}

static void test_italic_face(void) {
    struct jpr_font f;
    struct jpr_glyph g;
    jpr_font_init(&f);
    jpr_font_builtin(&f, builtin, builtin_size);
    CHECK(jpr_font_load(&f, blob, blob_size) == 0);
    g = jpr_font_glyph(&f, JPR_FACE_ITALIC, 0x61);        /* a: the italic face */
    CHECK(g.bits && g.advance == 6);
    g = jpr_font_glyph(&f, JPR_FACE_ITALIC, 0x3042);      /* Japanese in a note: upright, from the small face */
    CHECK(g.bits && g.advance == 13);
    g = jpr_font_glyph(&f, JPR_FACE_ITALIC, 0x6F22);      /* kanji: on to the text face */
    CHECK(g.bits && g.advance == 17);
    g = jpr_font_glyph(&f, JPR_FACE_ITALIC, 0x42);        /* only the built-in face has B */
    CHECK(g.bits && g.advance == 8);
    g = jpr_font_glyph(&f, JPR_FACE_SMALL, 0x61);         /* the upright faces never use the italic one */
    CHECK(!g.bits);
}

static void test_old_versions_are_refused(void) {
    struct jpr_font f;
    u8 old[64];
    jpr_font_init(&f);
    memcpy(old, blob, sizeof old);
    old[4] = 2;                                           /* version 2: before the italic face */
    CHECK(jpr_font_load(&f, old, sizeof old) == -1);
}

static void test_bad_files_leave_the_builtin_face(void) {
    struct jpr_font f;
    u8 bad[64];
    jpr_font_init(&f);
    jpr_font_builtin(&f, builtin, builtin_size);
    memcpy(bad, blob, sizeof bad);
    bad[4] = 1;                                           /* version 1: the Unifont file */
    CHECK(jpr_font_load(&f, bad, sizeof bad) == -1);
    CHECK(f.faces[0].count == 0 && jpr_font_glyph(&f, JPR_FACE_TEXT, 0x41).bits);
    CHECK(jpr_font_load(&f, blob, 40) == -2);            /* cut short */
    CHECK(f.faces[0].count == 0 && f.faces[1].count == 0);
    CHECK(jpr_font_glyph(&f, JPR_FACE_TEXT, 0x3042).advance == 17);   /* box at the default size */
}

static void test_glyph_data_past_the_end_is_ignored(void) {
    struct jpr_font f;
    jpr_font_init(&f);
    CHECK(jpr_font_load(&f, blob, blob_size) == 0);
    f.faces[0].data_size = 8;                             /* as if the file ended early */
    CHECK(!jpr_font_glyph(&f, JPR_FACE_TEXT, 0x6F22).bits);
}

int main(void) {
    make();
    test_lookup_in_the_wanted_face();
    test_fallbacks();
    test_italic_face();
    test_old_versions_are_refused();
    test_bad_files_leave_the_builtin_face();
    test_glyph_data_past_the_end_is_ignored();
    if (failures) {
        printf("font: %d check(s) failed\n", failures);
        return 1;
    }
    printf("font: all checks passed\n");
    return 0;
}
