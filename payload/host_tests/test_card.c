#include <stdio.h>
#include <string.h>

#include "core/card.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 entry[JPR_ENTRY_MAX];
static u32 entry_len;

static void str(const char *s) {
    u32 n = (u32)strlen(s);
    entry[entry_len] = (u8)n;
    entry[entry_len + 1] = (u8)(n >> 8);
    memcpy(entry + entry_len + 2, s, n);
    entry_len += 2 + n;
}

static void make(u32 kind, u32 senses) {
    entry_len = 8;
    entry[0] = (u8)kind;
    entry[1] = (u8)senses;
    entry[2] = entry[3] = 0;
    entry[4] = 0x10; entry[5] = 0x2D; entry[6] = 0x16; entry[7] = 0;   /* JMdict 1453328 (dict.bin v3) */
    str("\xE7\xA7\x98\xE3\x82\x81\xE3\x82\x8B");   /* 秘める */
    str("\xE3\x81\xB2\xE3\x82\x81\xE3\x82\x8B");   /* ひめる */
    str("verb (ichidan)");
    str("to hide");
    if (senses > 1) str("to keep to oneself");
}

static void test_parse(void) {
    struct jpr_card c;
    make(JPR_ENTRY_DICTIONARY, 2);
    CHECK(jpr_card_parse(&c, entry, entry_len) == 0);
    CHECK(c.kind == JPR_ENTRY_DICTIONARY && c.senses == 2);
    CHECK(c.jmdict == 1453328);
    CHECK(c.headword.len == 9 && c.reading.len == 9 && c.pos.len == 14);
    CHECK(c.sense[1].len == 18 && !memcmp(c.sense[1].p, "to keep to oneself", 18));
}

static void test_bad_entries(void) {
    struct jpr_card c;
    make(JPR_ENTRY_DICTIONARY, 2);
    CHECK(jpr_card_parse(&c, entry, entry_len - 1) == -1);   /* cut short */
    CHECK(jpr_card_parse(&c, entry, 3) == -1);
    CHECK(jpr_card_parse(&c, entry, 7) == -1);              /* no room for the id */
    make(9, 1);                                              /* unknown kind */
    CHECK(jpr_card_parse(&c, entry, entry_len) == -1);
}

static void test_more_senses_than_the_card_shows(void) {
    struct jpr_card c;
    make(JPR_ENTRY_DICTIONARY, 1);
    str("two"); str("three"); str("four");
    entry[1] = 4;
    CHECK(jpr_card_parse(&c, entry, entry_len) == 0 && c.senses == JPR_CARD_SENSES);
}

static void test_label(void) {
    static const u8 label[] = {7, 0, '-', 't', 'e', ' ', 'i', 'r', 'u'};
    struct jpr_text t;
    u32 at = 0;
    CHECK(jpr_text_read(&t, label, sizeof label, &at) == 0 && t.len == 7 && at == 9);
    at = 0;
    CHECK(jpr_text_read(&t, label, 5, &at) == -1);
}

int main(void) {
    test_parse();
    test_bad_entries();
    test_more_senses_than_the_card_shows();
    test_label();
    if (failures) {
        printf("card: %d check(s) failed\n", failures);
        return 1;
    }
    printf("card: all checks passed\n");
    return 0;
}
