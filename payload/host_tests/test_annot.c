#include <stdio.h>
#include <string.h>

#include "core/annot.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 rec[512];
static u32 rec_len;
static u16 shown[64];
static u32 shown_len;
static struct jpr_unit units[JPR_UNITS_MAX];

static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static void start(u32 segs, u32 count) {
    memset(rec, 0, sizeof rec);
    put16(rec, segs);
    put16(rec + 2, count);
    rec_len = 4;
}

static void seg_text(const u16 *run, u32 n) {
    rec[rec_len] = JPR_SEG_TEXT;
    put16(rec + rec_len + 2, n);
    put16(rec + rec_len + 4, run[0]);
    put16(rec + rec_len + 6, run[n - 1]);
    rec_len += 8;
}

static void seg_tag(u32 n) {
    rec[rec_len] = 1;
    put16(rec + rec_len + 2, n);
    rec_len += 8;
}

static void unit(u32 at, u32 n, u32 entry) {
    put16(rec + rec_len, at);
    put16(rec + rec_len + 2, n);
    put16(rec + rec_len + 4, entry & 0xFFFF);
    put16(rec + rec_len + 6, entry >> 16);
    put16(rec + rec_len + 8, JPR_NO_LABEL);
    rec[rec_len + 10] = JPR_UNIT_WORD;
    rec_len += 12;
}

static void show(const u16 *u, u32 n) {
    memcpy(shown + shown_len, u, n * 2);
    shown_len += n;
}

/* やあ {name} さん　げんき？ ; units やあ, さん, げんき */
static const u16 YAA[] = {0x3084, 0x3042};
static const u16 SAN[] = {0x3055, 0x3093, 0x3000, 0x3052, 0x3093, 0x304D, 0xFF1F};
static const u16 YOU[] = {0x30E8, 0x30A6};
static const u16 NAME_TAG[] = {0x0010, 2, 0x0100, 0x0000};

static void record(void) {
    start(3, 3);
    seg_text(YAA, 2);
    seg_tag(4);
    seg_text(SAN, 7);
    unit(0, 2, 7);
    unit(6, 2, 8);
    unit(9, 3, 9);
}

static s32 place(void) {
    return jpr_annot_place(rec, rec_len, shown, shown_len, units, JPR_UNITS_MAX);
}

static void test_inserted_name(void) {
    record();
    shown_len = 0;
    show(YAA, 2); show(YOU, 2); show(SAN, 7);
    CHECK(place() == 3);
    CHECK(units[0].start == 0 && units[1].start == 4 && units[2].start == 7);
    CHECK(units[1].len == 2 && units[1].entry == 8 && units[1].label == JPR_NO_LABEL && units[1].kind == JPR_UNIT_WORD);
}

static void test_tag_left_in_place(void) {
    record();
    shown_len = 0;
    show(YAA, 2); show(NAME_TAG, 4); show(SAN, 7);
    CHECK(place() == 3 && units[1].start == 6 && units[2].start == 9);
}

static void test_empty_insertion(void) {
    record();
    shown_len = 0;
    show(YAA, 2); show(SAN, 7);
    CHECK(place() == 3 && units[1].start == 2);
}

static void test_name_at_the_end(void) {
    start(2, 1);
    seg_text(SAN, 3);
    seg_tag(4);
    unit(0, 2, 1);
    shown_len = 0;
    show(SAN, 3); show(YOU, 2);
    CHECK(place() == 1 && units[0].start == 0);
    shown_len = 2;
    CHECK(place() == -1);
}

static void test_text_that_does_not_fit(void) {
    static const u16 bang[] = {0xFF01};
    record();
    shown_len = 0;
    show(YAA, 2); show(YOU, 2); show(SAN, 6); show(bang, 1);   /* last character changed */
    CHECK(place() == -1);
    shown_len = 0;
    show(YAA, 2); show(YOU, 2); show(SAN, 5);                  /* truncated */
    CHECK(place() == -1);
    shown_len = 0;
    show(YAA, 2); show(YOU, 2); show(SAN, 7); show(bang, 1);   /* longer */
    CHECK(place() == -1);
}

static void test_malformed_records(void) {
    record();
    shown_len = 0;
    show(YAA, 2); show(YOU, 2); show(SAN, 7);
    CHECK(jpr_annot_place(rec, rec_len - 1, shown, shown_len, units, JPR_UNITS_MAX) == -1);  /* cut short */
    CHECK(jpr_annot_place(rec, rec_len, shown, shown_len, units, 2) == -1);                  /* too many units */
    put16(rec, JPR_SEGMENTS_MAX + 1);
    CHECK(jpr_annot_place(rec, sizeof rec, shown, shown_len, units, JPR_UNITS_MAX) == -1);
    record();
    put16(rec + 4 + 3 * 8 + 12, 3);                           /* a unit starting inside the tag */
    CHECK(place() == -1);
}

/* {player}の{pokemon}は　げんき with the player called のぞみ: の could sit inside the name. */
static void test_ambiguous_name_gives_nothing(void) {
    static const u16 NO[] = {0x306E};
    static const u16 NOZOMI[] = {0x306E, 0x305E, 0x307F};
    static const u16 MOKUROO[] = {0x30E2, 0x30AF, 0x30ED, 0x30FC};
    static const u16 WA[] = {0x306F, 0x3000, 0x3052, 0x3093, 0x304D};
    start(4, 2);
    seg_tag(4);
    seg_text(NO, 1);
    seg_tag(4);
    seg_text(WA, 5);
    unit(4, 1, 3);
    unit(9, 3, 7);
    shown_len = 0;
    show(NOZOMI, 3); show(NO, 1); show(MOKUROO, 4); show(WA, 5);
    CHECK(place() == -1);
    shown_len = 0;
    show(YOU, 2); show(NO, 1); show(MOKUROO, 4); show(WA, 5);   /* no の in the name: one way to fit */
    CHECK(place() == 2 && units[0].start == 2 && units[1].start == 7);
}

int main(void) {
    test_inserted_name();
    test_tag_left_in_place();
    test_empty_insertion();
    test_name_at_the_end();
    test_text_that_does_not_fit();
    test_malformed_records();
    test_ambiguous_name_gives_nothing();
    if (failures) {
        printf("annot: %d check(s) failed\n", failures);
        return 1;
    }
    printf("annot: all checks passed\n");
    return 0;
}
