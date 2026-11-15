#include <stdio.h>
#include <string.h>

#include "core/draw.h"
#include "font_blob.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const struct test_glyph text_glyphs[] = {{0x3042, 17, 15, 15, 1, -15}};
static const struct test_glyph small_glyphs[] = {{0x20, 4, 0, 0, 0, 0}, {0x41, 8, 6, 9, 1, -9}, {0x42, 9, 7, 9, 1, -9}};
static u8 blob[2048] __attribute__((aligned(4)));
static u8 fb[320 * 240 * 3];
static struct jpr_font font;
static struct jpr_surface surf;

static u32 colour(u32 x, u32 y) {
    const u8 *p = fb + (x * 240 + (239 - y)) * 3;
    return ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
}

static void test_char_sits_on_the_baseline(void) {
    struct jpr_pen pen;
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_TEXT, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_char(&pen, 10, 40, 0x3042) == 17);
    CHECK(colour(11, 25) == 0xFFFFFF && colour(25, 39) == 0xFFFFFF);   /* rows 25..39, columns 11..25 */
    CHECK(colour(11, 24) == 0 && colour(11, 40) == 0 && colour(10, 30) == 0 && colour(26, 30) == 0);
}

static void test_missing_char_is_a_box(void) {
    struct jpr_pen pen;
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_TEXT, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_char(&pen, 10, 40, 0x4E00) == 17);
    CHECK(colour(11, 40 - 15) == 0xFFFFFF && colour(11, 39) == 0xFFFFFF);  /* left side */
    CHECK(colour(18, 32) == 0);                                            /* hollow */
}

static void test_clip(void) {
    struct jpr_pen pen;
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_TEXT, 0x000000, 0xFFFFFF, 30, 35);
    jpr_draw_char(&pen, 10, 40, 0x3042);
    CHECK(colour(11, 29) == 0 && colour(11, 30) == 0xFFFFFF && colour(11, 34) == 0xFFFFFF && colour(11, 35) == 0);
    jpr_draw_char(&pen, 10, 40, 0x4E00);                  /* boxes clip too */
    CHECK(colour(11, 25) == 0);
}

static void test_ascii(void) {
    struct jpr_pen pen;
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_SMALL, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_ascii(&pen, 10, 40, "AB") == 10 + 8 + 9);
    CHECK(jpr_measure_ascii(&font, JPR_FACE_SMALL, "AB") == 17);
    CHECK(colour(11, 31) == 0xFFFFFF && colour(19, 31) == 0xFFFFFF);
}

static void test_utf8(void) {
    static const u8 s[] = {'A', 0xC3, 0xA9, 0xE3, 0x81, 0x82, 0xF0, 0x9F, 0x98, 0x80, 0xE3, 0x81};
    u32 i = 0;
    CHECK(jpr_utf8_next(s, sizeof s, &i) == 'A' && i == 1);
    CHECK(jpr_utf8_next(s, sizeof s, &i) == 0xE9 && i == 3);
    CHECK(jpr_utf8_next(s, sizeof s, &i) == 0x3042 && i == 6);
    CHECK(jpr_utf8_next(s, sizeof s, &i) == 0xFFFD && i == 10);   /* outside the BMP */
    CHECK(jpr_utf8_next(s, sizeof s, &i) == 0xFFFD && i == 12);   /* cut short */
}

static void test_wrapped(void) {
    struct jpr_pen pen;
    static const u8 words[] = "AB AB AB";         /* each word 17 wide, a space 4 */
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_SMALL, 0x000000, 0xFFFFFF, 0, 240);
    /* a 40-pixel column: "AB AB" fits (17 + 4 + 17 = 38), the third word goes to the next line */
    CHECK(jpr_draw_wrapped(&pen, 10, 40, 10, 50, 17, 200, words, 8) == 57);
    CHECK(colour(11, 31) == 0xFFFFFF && colour(11, 48) == 0xFFFFFF);  /* "AB AB" then "AB" below */
    CHECK(colour(32, 48) == 0);                                        /* nothing after it on line 2 */
    CHECK(jpr_draw_wrapped(&pen, 10, 40, 10, 50, 17, 50, words, 8) == 0);   /* no room for line 2 */
    CHECK(jpr_measure_utf8(&font, JPR_FACE_SMALL, words, 2) == 17);
}

static void test_long_word_breaks_anywhere(void) {
    struct jpr_pen pen;
    static const u8 word[] = "ABABAB";            /* 51 wide in a 20-pixel column */
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_SMALL, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_wrapped(&pen, 10, 40, 10, 30, 17, 200, word, 6) == 74);   /* "AB" on each of three lines */
}

static u32 lit(u32 x0, u32 x1, u32 y0, u32 y1) {
    u32 x, y, n = 0;
    for (x = x0; x < x1; x++)
        for (y = y0; y < y1; y++) n += colour(x, y) != 0;
    return n;
}

static void test_bold_double_strikes(void) {
    struct jpr_pen pen, measure;
    u32 plain, bold;
    memset(fb, 0, sizeof fb);
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_SMALL, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_char(&pen, 10, 40, 0x41) == 8);
    plain = lit(0, 40, 0, 60);
    CHECK(colour(17, 35) == 0);                    /* one past the glyph's right edge (x 11..16) */
    memset(fb, 0, sizeof fb);
    jpr_pen_bold(&pen);
    CHECK(jpr_draw_char(&pen, 10, 40, 0x41) == 9); /* one pixel wider */
    bold = lit(0, 40, 0, 60);
    CHECK(bold > plain && colour(17, 35) == 0xFFFFFF);
    memset(&measure, 0, sizeof measure);           /* a measuring pen, bold */
    measure.f = &font;
    measure.face = JPR_FACE_SMALL;
    jpr_pen_bold(&measure);
    CHECK(jpr_pen_advance(&measure, 0x41) == 9 && jpr_draw_char(&measure, 0, 0, 0x41) == 9);
}

static void test_bold_wraps_sooner(void) {
    struct jpr_pen pen;
    static const u8 words[] = "AB AB";            /* 17 + 4 + 17 = 38 wide; bold 19 + 5 + 19 = 43 */
    jpr_pen_init(&pen, &surf, &font, JPR_FACE_SMALL, 0x000000, 0xFFFFFF, 0, 240);
    CHECK(jpr_draw_wrapped(&pen, 0, 40, 0, 40, 17, 200, words, 5) == 40);       /* one line */
    jpr_pen_bold(&pen);
    CHECK(jpr_pen_measure_utf8(&pen, words, 5) == 43);
    CHECK(jpr_draw_wrapped(&pen, 0, 40, 0, 40, 17, 200, words, 5) == 57);       /* two lines */
}

int main(void) {
    const struct test_glyph *g[2] = {text_glyphs, small_glyphs};
    const u32 counts[2] = {1, 3}, sizes[2] = {17, 13}, ascents[2] = {15, 10};
    test_font(blob, 2, g, counts, sizes, ascents);
    jpr_font_init(&font);
    jpr_font_load(&font, blob, sizeof blob);
    jpr_surface_init(&surf, fb, JPR_GL_RGB8, 320, 240);
    test_char_sits_on_the_baseline();
    test_missing_char_is_a_box();
    test_clip();
    test_ascii();
    test_utf8();
    test_wrapped();
    test_long_word_breaks_anywhere();
    test_bold_double_strikes();
    test_bold_wraps_sooner();
    if (failures) {
        printf("draw: %d check(s) failed\n", failures);
        return 1;
    }
    printf("draw: all checks passed\n");
    return 0;
}
