#include <stdio.h>
#include <string.h>

#include "core/grammar.h"
#include "core/json.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char msg[JPR_MESSAGE_MAX];

static const char *say(u32 failure, u32 code, u32 status, const char *detail) {
    jpr_grammar_message(msg, sizeof msg, failure, code, status, (const u8 *)detail, detail ? (u32)strlen(detail) : 0);
    return msg;
}

static void test_messages(void) {
    CHECK(!strcmp(say(JPR_NET_NO_KEY, 0, 0, 0), "Grammar needs kokoro: run jpreader kokoro-link"));
    CHECK(!strcmp(say(JPR_NET_NO_WIFI, 0, 0, 0), "No Wi-Fi connection"));
    CHECK(!strcmp(say(JPR_NET_BUSY, 0, 0, 0), "Network is busy (the game is online)"));
    CHECK(!strcmp(say(JPR_NET_UNREACHABLE, 3026, 0, 0), "Could not reach kokoro (code 3026)"));
    CHECK(!strcmp(say(JPR_NET_CERT, 0, 0, 0), "Could not verify kokoro (check the console's date)"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 401, "x"), "Token rejected: run jpreader kokoro-link"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 403, "x"), "Token rejected: run jpreader kokoro-link"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 429, "x"), "kokoro is busy, try again soon"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 500, "kokoro error: explaining failed\n"),
                  "kokoro could not explain that, try again"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 503, "x"), "kokoro could not explain that, try again"));
    CHECK(!strcmp(say(JPR_NET_HTTP, 0, 400, "kokoro error: sentence is required\nmore"),
                  "kokoro error 400: kokoro error: sentence is required"));
    CHECK(!strcmp(say(JPR_NET_TIMEOUT, 0, 0, 0), "Timed out"));
    CHECK(!strcmp(say(JPR_NET_CANCELLED, 0, 0, 0), ""));
    say(JPR_NET_HTTP, 0, 404, "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
                              "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
                              "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
                              "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
                              "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82");
    CHECK(strlen(msg) < sizeof msg && ((u8)msg[strlen(msg) - 1] == 0x82));  /* cut between characters */
}

static void test_explain_body(void) {
    static u8 body[JPR_BODY_MAX];
    static const u8 s[] = "\xE5\xBE\x85\xE3\x81\xA3\xE3\x81\xA6\n\xE3\x80\x82";   /* 待って\n。 */
    static const u8 c[] = "a\tb\r\nc";
    u32 n = jpr_explain_body(body, sizeof body, s, sizeof s - 1, c, sizeof c - 1);
    const char *want = "sentence=\xE5\xBE\x85\xE3\x81\xA3\xE3\x81\xA6 \xE3\x80\x82\ncontext=a b  c\n";
    CHECK(n == strlen(want) && !memcmp(body, want, n));
    CHECK(jpr_explain_body(body, 12, s, sizeof s - 1, c, sizeof c - 1) == 0);
}

static void test_deadlines(void) {
    CHECK(jpr_job_deadline_ms(JPR_JOB_GRAMMAR) == 90000);   /* kokoro asks a model: room for a slow answer */
    CHECK(jpr_job_deadline_ms(JPR_JOB_SYNC) == 45000);
    CHECK(jpr_job_deadline_ms(JPR_JOB_ADD_WORD) == 45000);
    CHECK(jpr_job_deadline_ms(JPR_JOB_FLUSH) == 45000);
}

static void test_link(void) {
    static struct jpr_grammar_link l;
    static struct jpr_grammar_job job, copy;
    static struct jpr_grammar_result r;
    memset(&l, 0, sizeof l);
    CHECK(jpr_link_idle(&l));
    job.id = 1;
    strcpy(job.key, "k");
    CHECK(jpr_link_post(&l, &job) == 0 && !jpr_link_idle(&l));
    job.id = 2;
    CHECK(jpr_link_post(&l, &job) == -1);          /* one at a time */
    CHECK(jpr_link_take(&l, &copy) == 1 && copy.id == 1 && !strcmp(copy.key, "k"));
    CHECK(jpr_link_take(&l, &copy) == 0);          /* nothing new */
    l.cancel = 1;                                  /* Back */
    CHECK(jpr_link_cancelled(&l, 1) && !jpr_link_result(&l, 1));
    r.id = 1;
    r.failure = JPR_NET_CANCELLED;
    jpr_link_finish(&l, &r);
    CHECK(jpr_link_result(&l, 1) && jpr_link_idle(&l));
    CHECK(jpr_link_post(&l, &job) == 0 && !jpr_link_result(&l, 2));
}

int main(void) {
    test_messages();
    test_explain_body();
    test_deadlines();
    test_link();
    if (failures) {
        printf("grammar: %d check(s) failed\n", failures);
        return 1;
    }
    printf("grammar: all checks passed\n");
    return 0;
}
