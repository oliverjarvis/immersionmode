#include <stdio.h>
#include <string.h>

#include "core/kokoro.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u32 table[16];
static u32 fallback[4];
static struct jpr_words w;

static u32 h(const char *s) { return jpr_kokoro_hash((const u8 *)s, (u32)strlen(s)); }

/* Feeds text in pieces of `step` bytes, then finishes. */
static s32 list(const char *text, u32 step) {
    u32 at = 0, n = (u32)strlen(text);
    jpr_words_init(&w, table, 16, fallback, 4);
    while (at < n) {
        u32 piece = n - at < step ? n - at : step;
        jpr_words_feed(&w, (const u8 *)text + at, piece);
        at += piece;
    }
    return jpr_words_finish(&w);
}

static void test_hash(void) {
    CHECK(h("") == 0x811C9DC5u);
    CHECK(h("a") == 0xE40C292Cu);
    CHECK(h("foobar") == 0xBF9CF968u);
}

static void test_list(void) {
    /* 猫 by id, 犬 by id, a card without an id (ピカチュウ) by its spelling, a state this payload does not know. */
    const char *text = "KOKORO-WORDS 2 4\n1467640\tknown\n1250050\tlearning\r\n~\xE3\x83\x94\xE3\x82\xAB\tknown\n"
                       "1000010\tsomeday\n";
    u32 step;
    for (step = 1; step < 9; step++) {   /* split anywhere, inside characters and lines */
        CHECK(list(text, step) == 2 && w.fb_count == 1);
        CHECK(jpr_words_find_id(table, 2, 1467640) == JPR_KOKORO_KNOWN);
        CHECK(jpr_words_find_id(table, 2, 1250050) == JPR_KOKORO_LEARNING);
        CHECK(jpr_words_find_id(table, 2, 1000010) == JPR_KOKORO_NONE);
        CHECK(jpr_words_find(fallback, 1, h("\xE3\x83\x94\xE3\x82\xAB")) == JPR_KOKORO_KNOWN);
        CHECK(table[0] < table[1]);
    }
    CHECK(list("KOKORO-WORDS 2 1\n5\tknown", 4) == 1);            /* the last line without its newline */
    CHECK(list("KOKORO-WORDS 2 1\n5\tknown\r", 4) == 1 && jpr_words_find_id(table, 1, 5) == JPR_KOKORO_KNOWN);
    CHECK(list("KOKORO-WORDS 2 0\n", 5) == 0 && w.fb_count == 0);
}

static void test_ids_out_of_range_are_skipped(void) {
    /* JMdict ids are seven digits at most: 9999999 (and 5035011, a word the game uses) are kept. */
    CHECK(list("KOKORO-WORDS 2 4\n16777216\tknown\n99999999999\tknown\n9999999\tlearning\n5035011\tknown\n", 7) == 2);
    CHECK(w.bad_ids == 2);
    CHECK(jpr_words_find_id(table, 2, 9999999) == JPR_KOKORO_LEARNING);
    CHECK(jpr_words_find_id(table, 2, 5035011) == JPR_KOKORO_KNOWN);
    CHECK(jpr_words_find_id(table, 2, 0) == JPR_KOKORO_NONE);
}

static void test_bad_lists(void) {
    CHECK(list("KOKORO-WORDS 2 3\n1\tknown\n2\tlearning\n", 7) == -1);  /* cut off: fewer lines than the header */
    CHECK(list("KOKORO-WORDS 1 1\na\tknown\n", 7) == -1);                /* the old list, by spelling */
    CHECK(list("kokoro error: words failed\n", 7) == -1);
    CHECK(list("KOKORO-WORDS 2 1\nno tab here\n", 7) == -1);
    CHECK(list("KOKORO-WORDS 2 1\nfront\tknown\n", 7) == -1);            /* neither an id nor ~spelling */
    CHECK(list("KOKORO-WORDS 2 1\n~\tknown\n", 7) == -1);                /* an empty spelling */
    CHECK(list("", 1) == -1);
}

static void test_long_lines_and_duplicates(void) {
    static char text[1200];
    char *p = text;
    p += sprintf(p, "KOKORO-WORDS 2 4\n~");
    memset(p, 'x', 400);                  /* longer than a line may be: counted, not kept */
    p += 400;
    p += sprintf(p, "\tknown\n7\tlearning\n7\tknown\n7\tlearning\n");
    CHECK(list(text, 50) == 1);
    CHECK(jpr_words_find_id(table, 1, 7) == JPR_KOKORO_KNOWN);   /* the best state of a repeated word */
}

static void test_full_tables(void) {
    static char text[600];
    char *p = text;
    u32 k;
    p += sprintf(p, "KOKORO-WORDS 2 26\n");
    for (k = 0; k < 20; k++) p += sprintf(p, "%u\tknown\n", 100 + k);
    for (k = 0; k < 6; k++) p += sprintf(p, "~w%u\tknown\n", k);
    CHECK(list(text, 64) == 16);           /* the table holds 16 */
    CHECK(w.fb_count == 4 && w.fb_dropped == 2);   /* the fallback holds 4: the rest are counted, never written past */
}

static void test_files(void) {
    u8 head[JPR_KOKORO_HEAD];
    u32 fb = 0, pairs = 0;
    jpr_kokoro_head_pack(head, 7, 3, 1234);
    CHECK(jpr_kokoro_head_counts(head, JPR_KOKORO_HEAD, 8000, 2048, &fb) == 7 && fb == 3);
    CHECK(jpr_kokoro_head_counts(head, JPR_KOKORO_HEAD - 1, 8000, 2048, &fb) == -1);
    CHECK(jpr_kokoro_head_counts(head, JPR_KOKORO_HEAD, 6, 2048, &fb) == -1);   /* more than fit */
    CHECK(jpr_kokoro_head_counts(head, JPR_KOKORO_HEAD, 8000, 2, &fb) == -1);
    head[4] = 1;                                                                  /* an 8B kokoro.bin */
    CHECK(jpr_kokoro_head_counts(head, JPR_KOKORO_HEAD, 8000, 2048, &fb) == -1);
    memset(head, 0, sizeof head);
    memcpy(head, "JPRI", 4);
    head[4] = 2;
    head[8] = 9;
    head[12] = 0x44; head[13] = 0x33; head[14] = 0x22; head[15] = 0x11;
    head[16] = 21;
    CHECK(jpr_ids_count(head, JPR_IDS_HEAD, 0x11223344u, &pairs) == 9 && pairs == 21);
    CHECK(jpr_ids_count(head, JPR_IDS_HEAD, 0x11223345u, &pairs) == -1);   /* another build's dict.bin */
    CHECK(jpr_ids_count(head, JPR_IDS_HEAD, 0, &pairs) == -1);             /* dict.bin not open */
    CHECK(jpr_ids_count(head, JPR_IDS_HEAD - 1, 0x11223344u, &pairs) == -1);
    head[4] = 1;
    CHECK(jpr_ids_count(head, JPR_IDS_HEAD, 0x11223344u, &pairs) == -1);
}

static void test_marks_by_id(void) {
    static u8 bits[JPR_KOKORO_BITS];
    u32 ids[6] = {1467640, 0, 1250050, 555, 1467640, 1250050};
    CHECK(list("KOKORO-WORDS 2 2\n1467640\tknown\n1250050\tlearning\n", 9) == 2);
    memset(bits, 0xFF, sizeof bits);
    jpr_kokoro_mark_ids(bits, 0, ids, 4, table, 2);
    jpr_kokoro_mark_ids(bits, 4, ids + 4, 2, table, 2);              /* a second chunk */
    CHECK(jpr_kokoro_state(bits, 6, 0) == JPR_KOKORO_KNOWN);
    CHECK(jpr_kokoro_state(bits, 6, 1) == JPR_KOKORO_NONE);           /* a name or particle: no id */
    CHECK(jpr_kokoro_state(bits, 6, 2) == JPR_KOKORO_LEARNING);
    CHECK(jpr_kokoro_state(bits, 6, 3) == JPR_KOKORO_NONE);
    CHECK(jpr_kokoro_state(bits, 6, 4) == JPR_KOKORO_KNOWN && jpr_kokoro_state(bits, 6, 5) == JPR_KOKORO_LEARNING);
    CHECK(jpr_kokoro_state(bits, 6, 6) == JPR_KOKORO_NONE);           /* past the entries marked */
    CHECK(jpr_kokoro_state(bits, 0xFFFFFFFFu, JPR_KOKORO_ENTRIES) == JPR_KOKORO_NONE);
    jpr_kokoro_mark_ids(bits, JPR_KOKORO_ENTRIES - 1, ids, 2, table, 2);   /* the second is past the end */
    CHECK(jpr_kokoro_state(bits, JPR_KOKORO_ENTRIES, JPR_KOKORO_ENTRIES - 1) == JPR_KOKORO_KNOWN);
}

static void test_marks_by_spelling(void) {
    static u8 bits[JPR_KOKORO_BITS];
    u32 ids[3] = {1006970, 1000020, 0};
    u32 pairs[8];
    /* それ is in kokoro only as a card with no id, written 其れ; entry 1 is learning by id. */
    CHECK(list("KOKORO-WORDS 2 3\n1000020\tlearning\n~\xE5\x85\xB6\xE3\x82\x8C\tknown\n~x\tknown\n", 5) == 1);
    memset(bits, 0, sizeof bits);
    jpr_kokoro_mark_ids(bits, 0, ids, 3, table, 1);
    pairs[0] = 0; pairs[1] = h("\xE3\x81\x9D\xE3\x82\x8C");        /* entry 0: それ */
    pairs[2] = 0; pairs[3] = h("\xE5\x85\xB6\xE3\x82\x8C");        /* entry 0: 其れ */
    pairs[4] = 1; pairs[5] = h("x");                               /* entry 1 is learning by id: x is known */
    pairs[6] = JPR_KOKORO_ENTRIES; pairs[7] = h("x");              /* past the end: ignored */
    jpr_kokoro_mark_forms(bits, pairs, 4, fallback, w.fb_count);
    CHECK(jpr_kokoro_state(bits, 3, 0) == JPR_KOKORO_KNOWN);
    CHECK(jpr_kokoro_state(bits, 3, 1) == JPR_KOKORO_KNOWN);        /* the better state wins */
    CHECK(jpr_kokoro_state(bits, 3, 2) == JPR_KOKORO_NONE);
}

int main(void) {
    test_hash();
    test_list();
    test_ids_out_of_range_are_skipped();
    test_bad_lists();
    test_long_lines_and_duplicates();
    test_full_tables();
    test_files();
    test_marks_by_id();
    test_marks_by_spelling();
    if (failures) {
        printf("kokoro: %d check(s) failed\n", failures);
        return 1;
    }
    printf("kokoro: all checks passed\n");
    return 0;
}
