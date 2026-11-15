#include <stdio.h>
#include <string.h>

#include "core/surface.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 fb[320 * 240 * 4];

/* Byte offset of screen pixel (x, y) in a rotated 240-pixel-tall framebuffer. */
static u32 at(u32 x, u32 y, u32 bpp) {
    return (x * 240 + (239 - y)) * bpp;
}

static u32 sum(void) {
    u32 total = 0, i;
    for (i = 0; i < sizeof fb; i++) total += fb[i];
    return total;
}

static void test_init(void) {
    struct jpr_surface s;
    CHECK(jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240) && s.bpp == 3);
    CHECK(jpr_surface_init(&s, fb, JPR_GL_RGBA8, 320, 240) && s.bpp == 4);
    CHECK(jpr_surface_init(&s, fb, JPR_GL_RGB565, 320, 240) && s.bpp == 2);
    CHECK(jpr_surface_init(&s, fb, JPR_GL_RGB5A1, 320, 240) && s.bpp == 2);
    CHECK(jpr_surface_init(&s, fb, JPR_GL_RGBA4, 320, 240) && s.bpp == 2);
    CHECK(jpr_surface_bytes(&s) == 320 * 240 * 2);
    CHECK(!jpr_surface_init(&s, fb, 0x1234, 320, 240));
    CHECK(!jpr_surface_init(&s, 0, JPR_GL_RGB8, 320, 240));
}

static void test_put_formats(void) {
    struct jpr_surface s;
    u32 o;

    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_put(&s, 0, 0, 0x112233);                  /* top-left: end of the first column */
    o = at(0, 0, 3);
    CHECK(o == 239 * 3);
    CHECK(fb[o] == 0x33 && fb[o + 1] == 0x22 && fb[o + 2] == 0x11);
    jpr_put(&s, 319, 239, 0x445566);              /* bottom-right: start of the last column */
    o = at(319, 239, 3);
    CHECK(o == 319 * 240 * 3);
    CHECK(fb[o] == 0x66 && fb[o + 1] == 0x55 && fb[o + 2] == 0x44);

    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGBA8, 320, 240);
    jpr_put(&s, 5, 7, 0x112233);
    o = at(5, 7, 4);
    CHECK(fb[o] == 0xFF && fb[o + 1] == 0x33 && fb[o + 2] == 0x22 && fb[o + 3] == 0x11);

    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB565, 320, 240);
    jpr_put(&s, 5, 7, 0xFF0000);
    o = at(5, 7, 2);
    CHECK(fb[o] == 0x00 && fb[o + 1] == 0xF8);
    jpr_put(&s, 6, 7, 0x0000FF);
    o = at(6, 7, 2);
    CHECK(fb[o] == 0x1F && fb[o + 1] == 0x00);

    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB5A1, 320, 240);
    jpr_put(&s, 5, 7, 0x000000);
    o = at(5, 7, 2);
    CHECK(fb[o] == 0x01 && fb[o + 1] == 0x00);    /* alpha bit set */

    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGBA4, 320, 240);
    jpr_put(&s, 5, 7, 0xFF0000);
    o = at(5, 7, 2);
    CHECK(fb[o] == 0x0F && fb[o + 1] == 0xF0);
}

static void test_put_clips(void) {
    struct jpr_surface s;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_put(&s, -1, 0, 0xFFFFFF);
    jpr_put(&s, 0, -1, 0xFFFFFF);
    jpr_put(&s, 320, 0, 0xFFFFFF);
    jpr_put(&s, 0, 240, 0xFFFFFF);
    jpr_fill(&s, 310, 230, 50, 50, 0x000000);     /* partly off screen: black is all zeros */
    CHECK(sum() == 0);
    jpr_fill(&s, 318, 238, 50, 50, 0x010101);
    CHECK(sum() == 4 * 3);                         /* only the 2 x 2 pixels on screen */
}

static void test_fill_covers_exactly_the_rectangle(void) {
    struct jpr_surface s;
    u32 x, y;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_fill(&s, 10, 20, 5, 7, 0x0A0B0C);
    for (x = 8; x < 17; x++)
        for (y = 18; y < 29; y++) {
            int in = x >= 10 && x < 15 && y >= 20 && y < 27;
            CHECK(fb[at(x, y, 3)] == (in ? 0x0C : 0));
        }
}

static void test_shades(void) {
    struct jpr_surface s;
    struct jpr_shades sh;
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_shades(&s, 0x000000, 0xFFFFFF, &sh);
    CHECK(sh.px[0][0] == 84 && sh.px[1][0] == 170 && sh.px[2][0] == 255);   /* 255 * 85 >> 8, 255 * 171 >> 8 */
    jpr_shades(&s, 0xF4F0E6, 0x202028, &sh);
    CHECK(sh.px[2][0] == 0x28 && sh.px[2][1] == 0x20 && sh.px[2][2] == 0x20);   /* full: the text colour */
}

static void test_coverage(void) {
    struct jpr_surface s;
    struct jpr_shades sh;
    static const u8 bits[2] = {0x1B, 0xC0};               /* row 0: 0 1 2 3; row 1: 3 0 0 0 */
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_shades(&s, 0x000000, 0xFFFFFF, &sh);
    jpr_coverage(&s, 100, 50, bits, 1, 4, 2, &sh, 0, 240);
    CHECK(fb[at(100, 50, 3)] == 0);                       /* coverage 0 leaves the background */
    CHECK(fb[at(101, 50, 3)] == 84 && fb[at(102, 50, 3)] == 170 && fb[at(103, 50, 3)] == 255);
    CHECK(fb[at(100, 51, 3)] == 255 && fb[at(101, 51, 3)] == 0);
    memset(fb, 0, sizeof fb);
    jpr_coverage(&s, 100, 50, bits, 1, 4, 2, &sh, 51, 240);  /* clip off the top row */
    CHECK(fb[at(103, 50, 3)] == 0 && fb[at(100, 51, 3)] == 255);
    memset(fb, 0, sizeof fb);
    jpr_coverage(&s, -3, 0, bits, 1, 4, 2, &sh, 0, 240);   /* off the left edge */
    CHECK(fb[at(0, 0, 3)] == 255);
}

static void test_bold_coverage_keeps_the_darker_level(void) {
    struct jpr_surface s;
    struct jpr_shades sh;
    static const u8 bits[1] = {0x70};                     /* 1 3 0 0: a soft edge, then the stroke */
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_shades(&s, 0x000000, 0xFFFFFF, &sh);
    jpr_coverage_bold(&s, 100, 50, bits, 1, 2, 1, &sh, 0, 240);
    CHECK(fb[at(100, 50, 3)] == 84);                      /* max(1, nothing) */
    CHECK(fb[at(101, 50, 3)] == 255);                     /* max(3, 1): the stroke stays dark */
    CHECK(fb[at(102, 50, 3)] == 255);                     /* max(0, 3): one column wider */
    CHECK(fb[at(103, 50, 3)] == 0);
}

static void test_coverage_other_formats_and_edges(void) {
    struct jpr_surface s;
    struct jpr_shades sh;
    static const u8 bits[2] = {0x1B, 0xC0};               /* row 0: 0 1 2 3; row 1: 3 0 0 0 */
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGBA8, 320, 240);
    jpr_shades(&s, 0x000000, 0x00FF00, &sh);
    jpr_coverage(&s, 317, 238, bits, 1, 4, 2, &sh, 0, 240);  /* off the right and bottom edges */
    CHECK(fb[at(318, 238, 4) + 2] == 84 && fb[at(319, 238, 4) + 2] == 170);
    CHECK(fb[at(317, 239, 4) + 2] == 255 && fb[at(317, 238, 4)] == 0);
    CHECK(sum() == 0xFF * 3 + 84 + 170 + 255);            /* three pixels on screen: alpha bytes plus green */
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB565, 320, 240);
    jpr_shades(&s, 0x000000, 0xFFFFFF, &sh);
    jpr_coverage(&s, 10, 10, bits, 1, 4, 2, &sh, 0, 240);
    CHECK(fb[at(13, 10, 2)] == 0xFF && fb[at(13, 10, 2) + 1] == 0xFF && fb[at(10, 11, 2)] == 0xFF);
    CHECK(fb[at(10, 10, 2)] == 0 && fb[at(11, 11, 2)] == 0);
    memset(fb, 0, sizeof fb);
    jpr_coverage(&s, 10, 10, bits, 1, 4, 2, &sh, 20, 30);  /* clipped away entirely */
    CHECK(sum() == 0);
}

static void test_full_height_fill(void) {
    struct jpr_surface s;
    u32 x, y, wrong = 0;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_fill(&s, 5, 0, 6, 240, 0x123456);                 /* columns 5..10, the whole height */
    for (x = 4; x <= 11; x++)
        for (y = 0; y < 240; y++) {
            u32 in = x >= 5 && x <= 10;
            const u8 *p = fb + at(x, y, 3);
            wrong += in ? !(p[0] == 0x56 && p[1] == 0x34 && p[2] == 0x12) : (p[0] || p[1] || p[2]);
        }
    CHECK(wrong == 0);
    memset(fb, 0xAA, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGBA4, 320, 240);
    jpr_fill(&s, 0, 0, 320, 240, 0xFFFFFF);               /* the whole screen, 2-byte pixels */
    CHECK(fb[0] == 0xFF && fb[320 * 240 * 2 - 1] == 0xFF && fb[320 * 240 * 2] == 0xAA);
}

static void test_triangle(void) {
    struct jpr_surface s;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_triangle(&s, 30, 100, 8, 1, 0xFFFFFF);    /* pointing left: tip at x 26, base at x 34 */
    CHECK(fb[at(26, 100, 3)] == 0xFF);
    CHECK(fb[at(25, 100, 3)] == 0);
    CHECK(fb[at(26, 99, 3)] == 0);
    CHECK(fb[at(34, 92, 3)] == 0xFF);
    CHECK(fb[at(34, 108, 3)] == 0xFF);

    memset(fb, 0, sizeof fb);
    jpr_triangle(&s, 30, 100, 8, 0, 0xFFFFFF);    /* pointing right: tip at x 34, base at x 26 */
    CHECK(fb[at(34, 100, 3)] == 0xFF);
    CHECK(fb[at(35, 100, 3)] == 0);
    CHECK(fb[at(26, 92, 3)] == 0xFF);
}


/* Long runs go through the word-store path: every pixel must still be exact, for each pixel size,
 * from an unaligned start, and nothing outside the rectangle may change. */
static void test_long_runs_are_exact(void) {
    static const u32 formats[3] = {JPR_GL_RGB8, JPR_GL_RGBA8, JPR_GL_RGB565};
    u32 f, x, y, k;
    for (f = 0; f < 3; f++) {
        struct jpr_surface s;
        u8 ref[4];
        memset(fb, 0, sizeof fb);
        jpr_surface_init(&s, fb, formats[f], 320, 240);
        jpr_put(&s, 0, 0, 0x123456);              /* reference encoding of the colour */
        memcpy(ref, fb + at(0, 0, s.bpp), s.bpp);
        memset(fb, 0, sizeof fb);
        jpr_fill(&s, 3, 1, 2, 236, 0x123456);     /* columns 3-4, rows 1-236: odd start */
        for (x = 2; x < 6; x++)
            for (y = 0; y < 240; y++) {
                int in = x >= 3 && x < 5 && y >= 1 && y < 237;
                for (k = 0; k < s.bpp; k++) CHECK(fb[at(x, y, s.bpp) + k] == (in ? ref[k] : 0));
            }
    }
}

static void test_vertical_triangles(void) {
    struct jpr_surface s;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_triangle_v(&s, 100, 100, 7, 1, 0xFFFFFF);         /* up: tip at y 97 */
    CHECK(fb[at(100, 97, 3)] == 0xFF && fb[at(100, 96, 3)] == 0 && fb[at(93, 104, 3)] == 0xFF);
    memset(fb, 0, sizeof fb);
    jpr_triangle_v(&s, 100, 100, 7, 0, 0xFFFFFF);         /* down: tip at y 103 */
    CHECK(fb[at(100, 103, 3)] == 0xFF && fb[at(100, 104, 3)] == 0 && fb[at(93, 96, 3)] == 0xFF);
}

static u32 rgb_at(u32 x, u32 y) {
    const u8 *q = fb + at(x, y, 3);
    return ((u32)q[2] << 16) | ((u32)q[1] << 8) | q[0];
}

static void test_round_rect(void) {
    struct jpr_surface s;
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_fill(&s, 0, 0, 320, 240, 0x101010);
    jpr_round_rect(&s, 20, 30, 100, 40, 6, 0xFBF8F1, 0xE2DCCF, 1, 0, 240);
    CHECK(rgb_at(20, 30) == 0x101010 && rgb_at(119, 69) == 0x101010);    /* corners stay background */
    CHECK(rgb_at(26, 36) == 0xFBF8F1);                                    /* inside */
    CHECK(rgb_at(70, 30) == 0xE2DCCF && rgb_at(70, 69) == 0xE2DCCF);      /* top and bottom edges: border */
    CHECK(rgb_at(20, 50) == 0xE2DCCF && rgb_at(119, 50) == 0xE2DCCF);     /* left and right edges */
    CHECK(rgb_at(70, 31) == 0xFBF8F1 && rgb_at(19, 50) == 0x101010 && rgb_at(120, 50) == 0x101010);
    jpr_round_rect(&s, 200, 30, 20, 12, 6, 0xB5532F, 0, 0, 0, 240);       /* a badge, no border */
    CHECK(rgb_at(210, 30) == 0xB5532F && rgb_at(200, 36) == 0xB5532F && rgb_at(200, 30) == 0x101010);
    jpr_fill(&s, 0, 0, 320, 240, 0x101010);
    jpr_round_rect(&s, 20, 30, 100, 40, 6, 0xFBF8F1, 0xE2DCCF, 1, 40, 60);  /* clipped to rows 40..59 */
    CHECK(rgb_at(70, 39) == 0x101010 && rgb_at(70, 40) == 0xFBF8F1 && rgb_at(70, 60) == 0x101010);
    jpr_round_rect(&s, -30, -30, 400, 400, 6, 0xFBF8F1, 0xE2DCCF, 1, 0, 240);   /* off every edge */
    CHECK(rgb_at(0, 0) == 0xFBF8F1);
}

int main(void) {
    test_init();
    test_put_formats();
    test_put_clips();
    test_fill_covers_exactly_the_rectangle();
    test_shades();
    test_coverage();
    test_bold_coverage_keeps_the_darker_level();
    test_full_height_fill();
    test_vertical_triangles();
    test_coverage_other_formats_and_edges();
    test_triangle();
    test_long_runs_are_exact();
    test_round_rect();
    if (failures) {
        printf("surface: %d check(s) failed\n", failures);
        return 1;
    }
    printf("surface: all checks passed\n");
    return 0;
}
