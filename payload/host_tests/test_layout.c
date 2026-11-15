#include <stdio.h>
#include <string.h>

#include "core/layout.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static struct jpr_layout lay;
static u16 buf[2048];
static u32 len;

static u32 width(const void *ctx, u16 cp) {
    (void)ctx;
    return cp < 0x100 ? 8 : 16;
}

static void reset(void) { len = 0; }
static void add(u16 u) { buf[len++] = u; }
static void kana(u32 n) { while (n--) add(0x3042); }   /* あ */
static void nl(void) { add(0x000A); }
static void tag(u16 code) { add(0x0010); add(1); add(code); }
static void name_tag(void) { add(0x0010); add(2); add(0x0100); add(0x0000); }

static void build(void) { jpr_layout_build(&lay, buf, len, 304, width, 0); }

/* Visible units of line k, so a test can compare them. */
static u32 visible(u32 k, u16 *out) {
    u32 i = lay.lines[k].start, n = 0;
    u16 cp;
    while (jpr_next_unit(buf, &i, lay.lines[k].end, &cp)) out[n++] = cp;
    return n;
}

static u32 line_of(u32 block, u32 k) { return lay.blocks[block].first + k; }

static void test_clear_tag_starts_a_block_and_eats_its_line_break(void) {
    u16 out[64];
    reset(); kana(3); tag(JPR_TAG_CLEAR); nl(); kana(2); build();
    CHECK(lay.block_count == 2);
    CHECK(lay.blocks[0].count == 1 && lay.blocks[1].count == 1);
    CHECK(visible(line_of(0, 0), out) == 3);
    CHECK(visible(line_of(1, 0), out) == 2);
}

static void test_scroll_tag_is_invisible_and_its_line_break_counts(void) {
    u16 out[64];
    reset(); kana(1); tag(JPR_TAG_SCROLL); nl(); kana(1); build();
    CHECK(lay.block_count == 1);
    CHECK(lay.blocks[0].count == 2);
    CHECK(visible(line_of(0, 0), out) == 1 && out[0] == 0x3042);
}

static void test_wraps_at_the_width(void) {
    reset(); kana(20); build();                   /* 19 glyphs of 16 px fit in 304 px */
    CHECK(lay.block_count == 1 && lay.blocks[0].count == 2);
    CHECK(lay.lines[line_of(0, 0)].end - lay.lines[line_of(0, 0)].start == 19);
    reset(); kana(18); add('A'); add('B'); build(); /* 288 + 8 + 8 = 304: still one line */
    CHECK(lay.blocks[0].count == 1);
}

static void test_blank_edges_are_dropped(void) {
    reset(); nl(); nl(); kana(1); nl(); build();
    CHECK(lay.block_count == 1 && lay.blocks[0].count == 1);
    reset(); kana(1); tag(JPR_TAG_CLEAR); build();  /* nothing after the last clear */
    CHECK(lay.block_count == 1);
    reset(); build();                               /* empty message */
    CHECK(lay.block_count == 1 && lay.blocks[0].count == 0);
}

static void test_name_tags_take_no_space(void) {
    u16 out[64];
    reset(); name_tag(); kana(19); build();
    CHECK(lay.blocks[0].count == 1);
    CHECK(visible(line_of(0, 0), out) == 19);
}

static void test_truncated_tag_stops_the_layout(void) {
    reset(); kana(1); add(0x0010); add(5); build();
    CHECK(lay.block_count == 1 && lay.blocks[0].count == 1);
}

static void test_too_many_blocks(void) {
    u32 k;
    reset();
    for (k = 0; k < 40; k++) { kana(1); tag(JPR_TAG_CLEAR); nl(); }
    kana(1);
    build();
    CHECK(lay.block_count == JPR_LAYOUT_BLOCKS);
    CHECK(lay.line_count <= JPR_LAYOUT_LINES);
}

static void test_too_many_lines(void) {
    u32 k;
    reset();
    for (k = 0; k < 150; k++) { kana(1); nl(); }
    build();
    CHECK(lay.line_count == JPR_LAYOUT_LINES);
    CHECK(lay.blocks[0].count == JPR_LAYOUT_LINES);
}

int main(void) {
    test_clear_tag_starts_a_block_and_eats_its_line_break();
    test_scroll_tag_is_invisible_and_its_line_break_counts();
    test_wraps_at_the_width();
    test_blank_edges_are_dropped();
    test_name_tags_take_no_space();
    test_truncated_tag_stops_the_layout();
    test_too_many_blocks();
    test_too_many_lines();
    if (failures) {
        printf("layout: %d check(s) failed\n", failures);
        return 1;
    }
    printf("layout: all checks passed\n");
    return 0;
}
