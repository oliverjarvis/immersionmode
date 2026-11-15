#define _DARWIN_C_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint_least16_t char16_t; /* macOS has no <uchar.h>; u"" literals are UTF-16 */

#include "core/sentence.h"
#include "core/util.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u16 text[2048];
static u32 len;
static struct jpr_excerpt e;
static u8 want[4096];

static u32 add(const char16_t *s) {
    u32 at = len;
    while (*s) text[len++] = (u16)*s++;
    return at;
}

static void clear_tag(void) { text[len++] = 0x0010; text[len++] = 1; text[len++] = 0xBE01; }

/* UTF-8 of a UTF-16 literal, for comparing. */
static u32 utf8(const char16_t *s) {
    u32 at = 0;
    while (*s) jpr_utf8_put(want, &at, sizeof want, *s++);
    return at;
}

static int sentence_is(const char16_t *s) {
    u32 n = utf8(s);
    return e.sentence_len == n && !memcmp(e.sentence, want, n);
}

static int context_is(const char16_t *s) {
    u32 n = utf8(s);
    return e.context_len == n && !memcmp(e.context, want, n);
}

static void test_one_block(void) {
    len = 0;
    add(u"これは\u3000テスト。");
    jpr_excerpt(&e, text, len, 0, len);
    CHECK(sentence_is(u"これは\u3000テスト。") && context_is(u"これは\u3000テスト。"));
}

static void test_extends_across_blocks(void) {
    u32 b2;
    len = 0;
    add(u"ねえ、\n");
    clear_tag();
    text[len++] = 0x000A;
    b2 = add(u"きいて。それから\uE08D");
    jpr_excerpt(&e, text, len, b2, len);
    CHECK(sentence_is(u"ねえ、きいて。それから…"));
    CHECK(context_is(u"ねえ、きいて。それから…"));
}

static void test_stops_at_the_previous_end(void) {
    u32 b2;
    len = 0;
    add(u"「あ。」");
    clear_tag();
    b2 = add(u"いい。うう");
    clear_tag();
    add(u"ええ。おお");
    jpr_excerpt(&e, text, len, b2, b2 + 5);          /* "いい。うう": extends forwards to ええ。 */
    CHECK(sentence_is(u"いい。うう\u3000ええ。") == 0);   /* no space is invented */
    CHECK(sentence_is(u"いい。ううええ。"));
}

static void test_symbols_tags_and_breaks(void) {
    len = 0;
    add(u"やあ\n");
    text[len++] = 0x0010; text[len++] = 2; text[len++] = 0x0100; text[len++] = 0;   /* a tag left in place */
    add(u"\uE08E\uE07F\uE000です！");
    jpr_excerpt(&e, text, len, 0, len);
    CHECK(sentence_is(u"やあ♂ です！"));
}

static void test_long_block_is_cut(void) {
    u32 k;
    len = 0;
    for (k = 0; k < 250; k++) add(u"あ");
    jpr_excerpt(&e, text, len, 0, len);
    CHECK(e.sentence_len == 200 * 3);
}

static void test_context_window(void) {
    u32 k, from;
    len = 0;
    for (k = 0; k < 400; k++) add(u"か");
    add(u"。");
    from = add(u"これだ。");
    for (k = 0; k < 400; k++) add(u"さ");
    jpr_excerpt(&e, text, len, from, from + 4);
    CHECK(sentence_is(u"これだ。"));
    CHECK(e.context_len == 600 * 3);
    CHECK(memmem(e.context, e.context_len, e.sentence, e.sentence_len) != 0);
}

static void test_empty_block(void) {
    len = 0;
    add(u"あ");
    jpr_excerpt(&e, text, len, 1, 1);
    CHECK(e.sentence_len == 0);
}

int main(void) {
    test_one_block();
    test_extends_across_blocks();
    test_stops_at_the_previous_end();
    test_symbols_tags_and_breaks();
    test_long_block_is_cut();
    test_context_window();
    test_empty_block();
    if (failures) {
        printf("sentence: %d check(s) failed\n", failures);
        return 1;
    }
    printf("sentence: all checks passed\n");
    return 0;
}
