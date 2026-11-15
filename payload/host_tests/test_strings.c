#include <stdio.h>
#include <string.h>

#include "core/strings.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_the_key_drops_tags_and_spaces(void) {
    /* "\nたたかう　\x10\x02\xFF00\x01ね\n" : a colour tag, an ideographic space, line breaks */
    static const u16 text[] = {0x0A, 0x305F, 0x305F, 0x304B, 0x3046, 0x3000, 0x10, 0x02, 0xFF00, 0x01, 0x306D, 0x0A,
                               0x20};
    u16 key[8];
    u32 n = jpr_text_key(text, 13, key, 8);
    CHECK(n == 5 && key[0] == 0x305F && key[3] == 0x3046 && key[4] == 0x306D);
}

static void test_a_tag_cut_short_ends_the_key(void) {
    static const u16 text[] = {0x3042, 0x10, 0x05, 0x0100};
    u16 key[8];
    CHECK(jpr_text_key(text, 4, key, 8) == 1);
}

static void test_a_key_too_long_is_none(void) {
    static const u16 text[] = {0x3042, 0x3044, 0x3046};
    u16 key[2];
    CHECK(jpr_text_key(text, 3, key, 2) == 0);
}

static void test_the_hash_matches_the_build(void) {
    /* tests/test_formats.py checks the same value: FNV-1a over the units' bytes, little-endian */
    static const u16 key[] = {0x305F, 0x305F, 0x304B, 0x3046};
    CHECK(jpr_key_hash(key, 4) == 0xF7462340u);
}

static void test_the_text_hash_is_the_keys(void) {
    static const u16 text[] = {0x0A, 0x305F, 0x305F, 0x304B, 0x3046, 0x3000, 0x10, 0x02, 0xFF00, 0x01, 0x0A};
    static const u16 key[] = {0x305F, 0x305F, 0x304B, 0x3046};
    static u16 long_text[100];
    u32 k;
    CHECK(jpr_text_hash(text, 11) == jpr_key_hash(key, 4));
    for (k = 0; k < 100; k++) long_text[k] = (u16)(0x3042 + k % 40);
    CHECK(jpr_text_hash(long_text, 100) != 0 && jpr_text_hash(long_text, 99) != jpr_text_hash(long_text, 100));
    CHECK(jpr_text_hash(text + 5, 1) == 0);                  /* only a space: nothing to read */
}

int main(void) {
    test_the_text_hash_is_the_keys();
    test_the_key_drops_tags_and_spaces();
    test_a_tag_cut_short_ends_the_key();
    test_a_key_too_long_is_none();
    test_the_hash_matches_the_build();
    if (failures) {
        printf("strings: %d check(s) failed\n", failures);
        return 1;
    }
    printf("strings: all checks passed\n");
    return 0;
}
