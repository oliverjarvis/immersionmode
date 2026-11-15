#include <stdio.h>
#include <string.h>

#include "core/grammar.h"
#include "core/word.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u32 put_string(u8 *out, const char *s) {
    u32 n = (u32)strlen(s);
    out[0] = (u8)n;
    out[1] = (u8)(n >> 8);
    memcpy(out + 2, s, n);
    return 2 + n;
}

/* 猫 【ねこ】 noun: three senses, the second with a tab and a newline in it. */
static u32 cat(u8 *out, u32 senses) {
    u32 n = 8;
    out[0] = JPR_ENTRY_DICTIONARY; out[1] = (u8)senses; out[2] = out[3] = 0;
    out[4] = 0xF8; out[5] = 0x64; out[6] = 0x16; out[7] = 0;      /* JMdict 1467640 (猫) */
    n += put_string(out + n, "\xE7\x8C\xAB");
    n += put_string(out + n, "\xE3\x81\xAD\xE3\x81\x93");
    n += put_string(out + n, "noun");
    n += put_string(out + n, "cat");
    if (senses > 1) n += put_string(out + n, "small\tfeline\nanimal");
    if (senses > 2) n += put_string(out + n, "third");
    return n;
}

static void test_body(void) {
    static u8 entry[256], out[512];
    static const u8 sentence[] = "\xE7\x8C\xAB\xE3\x81\x8C\n\xE3\x81\x84\xE3\x82\x8B";
    struct jpr_card c;
    u32 n;
    const char *want = "jmdict=1467640\nfront=\xE7\x8C\xAB\nreading=\xE3\x81\xAD\xE3\x81\x93\nmeaning=cat; small feline animal\npos=noun\n"
                       "sentence=\xE7\x8C\xAB\xE3\x81\x8C \xE3\x81\x84\xE3\x82\x8B\n";
    CHECK(jpr_card_parse(&c, entry, cat(entry, 3)) == 0);
    n = jpr_word_body(out, sizeof out, &c, sentence, sizeof sentence - 1);
    CHECK(n == strlen(want) && !memcmp(out, want, n));
    CHECK(jpr_word_body(out, 40, &c, sentence, sizeof sentence - 1) == 0);
    CHECK(jpr_card_parse(&c, entry, cat(entry, 1)) == 0);
    n = jpr_word_body(out, sizeof out, &c, sentence, 0);
    CHECK(n && strstr((const char *)out, "meaning=cat\n") && strstr((const char *)out, "sentence=\n"));
    c.jmdict = 0;                                                  /* a word with no id: the v1 body */
    n = jpr_word_body(out, sizeof out, &c, sentence, 0);
    CHECK(n && !memcmp(out, "front=", 6));
}

static void test_reply(void) {
    CHECK(jpr_word_reply((const u8 *)"OK new\n", 7) == JPR_KOKORO_NEW);
    CHECK(jpr_word_reply((const u8 *)"OK exists known\n", 16) == JPR_KOKORO_KNOWN);
    CHECK(jpr_word_reply((const u8 *)"OK exists learning", 18) == JPR_KOKORO_LEARNING);
    CHECK(jpr_word_reply((const u8 *)"OK exists new\r\n", 15) == JPR_KOKORO_NEW);
    CHECK(jpr_word_reply((const u8 *)"OK added\n", 9) == JPR_KOKORO_NEW);     /* v2: a new My words card, up next */
    CHECK(jpr_word_reply((const u8 *)"OK queued\n", 10) == JPR_KOKORO_NEW);   /* v2: kokoro had it: pulled forward */
    CHECK(jpr_word_reply((const u8 *)"OK exists later\n", 16) == 0);
    CHECK(jpr_word_reply((const u8 *)"kokoro error: adding failed\n", 28) == 0);
    CHECK(jpr_word_reply((const u8 *)"", 0) == 0);
}

static void test_outcome(void) {
    u32 state = 9;
    CHECK(jpr_word_outcome(JPR_NET_OK, 200, 0, (const u8 *)"OK new\n", 7, &state) == JPR_ADD_DONE && state == JPR_KOKORO_NEW);
    CHECK(jpr_word_outcome(JPR_NET_OK, 200, 0, (const u8 *)"OK exists known", 15, &state) == JPR_ADD_DONE &&
          state == JPR_KOKORO_KNOWN);
    CHECK(jpr_word_outcome(JPR_NET_OK, 200, 0, (const u8 *)"what", 4, &state) == JPR_ADD_FAILED && state == 0);
    CHECK(jpr_word_outcome(JPR_NET_NO_WIFI, 0, 1, 0, 0, &state) == JPR_ADD_QUEUED && state == JPR_KOKORO_QUEUED);
    CHECK(jpr_word_outcome(JPR_NET_NO_WIFI, 0, 0, 0, 0, &state) == JPR_ADD_FAILED && state == 0);   /* not saved */
    CHECK(jpr_word_outcome(JPR_NET_HTTP, 401, 0, 0, 0, &state) == JPR_ADD_REJECTED);
    CHECK(jpr_word_outcome(JPR_NET_HTTP, 500, 0, 0, 0, &state) == JPR_ADD_FAILED);
    CHECK(jpr_word_outcome(JPR_NET_CERT, 0, 0, 0, 0, &state) == JPR_ADD_FAILED);
    CHECK(jpr_word_should_queue(JPR_NET_NO_WIFI) && jpr_word_should_queue(JPR_NET_BUSY) &&
          jpr_word_should_queue(JPR_NET_UNREACHABLE) && jpr_word_should_queue(JPR_NET_TIMEOUT));
    CHECK(!jpr_word_should_queue(JPR_NET_OK) && !jpr_word_should_queue(JPR_NET_HTTP) &&
          !jpr_word_should_queue(JPR_NET_CERT) && !jpr_word_should_queue(JPR_NET_CANCELLED));
}

int main(void) {
    test_body();
    test_reply();
    test_outcome();
    if (failures) {
        printf("word: %d check(s) failed\n", failures);
        return 1;
    }
    printf("word: all checks passed\n");
    return 0;
}
