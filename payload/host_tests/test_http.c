#include <stdio.h>
#include <string.h>

#include "core/http.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static struct jpr_http h;
static u8 body[64];

/* Feeds the response in pieces of `piece` bytes; returns the last feed result. */
static int feed(const char *r, u32 piece) {
    u32 len = (u32)strlen(r), at = 0;
    int got = 0;
    jpr_http_init(&h, body, sizeof body);
    while (at < len && got == 0) {
        u32 n = len - at < piece ? len - at : piece;
        got = jpr_http_feed(&h, (const u8 *)r + at, n);
        at += n;
    }
    return got;
}

static void test_content_length_at_every_split(void) {
    const char *r = "HTTP/1.1 200 OK\r\ncontent-length: 5\r\nX-Other: y\r\n\r\nhello";
    u32 piece;
    for (piece = 1; piece <= 60; piece++) {
        CHECK(feed(r, piece) == 1);
        CHECK(h.status == 200 && h.body_len == 5 && !memcmp(body, "hello", 5));
    }
}

static void test_chunked_at_every_split(void) {
    const char *r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n4;ext=1\r\nWiki\r\n6\r\npedia \r\n"
                    "0\r\nTrailer: x\r\n\r\n";
    u32 piece;
    for (piece = 1; piece <= 90; piece++) {
        CHECK(feed(r, piece) == 1);
        CHECK(h.body_len == 10 && !memcmp(body, "Wikipedia ", 10));
    }
}

static void test_body_to_the_close_and_continue(void) {
    CHECK(feed("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 401 Unauthorized\r\n\r\n{\"e\":1}", 7) == 0);
    CHECK(h.status == 401 && jpr_http_close(&h) == 1 && h.body_len == 7);
}

static void test_overflow_and_bad(void) {
    static char big[300];
    memcpy(big, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n", 40);
    memset(big + 40, 'a', 100);
    big[140] = 0;
    CHECK(feed(big, 13) == 1 && h.overflow && h.body_len == sizeof body);
    CHECK(feed("SSH-2.0-OpenSSH\r\n", 5) == -1);
    CHECK(feed("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", 100) == 0 && jpr_http_close(&h) == -1);
    CHECK(feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", 100) == -1);
}

static void test_compose(void) {
    static u8 out[512];
    static const u8 b[] = "front=x\n";
    const char *get = "GET /jpreader/words HTTP/1.1\r\nHost: localhost:8443\r\nAuthorization: Bearer kkr_t\r\n"
                      "X-Title: jpreader\r\nConnection: close\r\n\r\n";
    const char *post = "POST /p/jpreader/word HTTP/1.1\r\nHost: k.example\r\nAuthorization: Bearer kkr_t\r\n"
                       "Content-Type: text/plain; charset=utf-8\r\nContent-Length: 8\r\nX-Title: jpreader\r\n"
                       "Connection: close\r\n\r\nfront=x\n";
    u32 n = jpr_http_compose(out, sizeof out, "GET", "localhost:8443", "/jpreader/words", "kkr_t", 0, 0, 0);
    CHECK(n == strlen(get) && !memcmp(out, get, n));
    n = jpr_http_compose(out, sizeof out, "POST", "k.example", "/p/jpreader/word", "kkr_t", "text/plain; charset=utf-8", b,
                         sizeof b - 1);
    CHECK(n == strlen(post) && !memcmp(out, post, n));
    CHECK(jpr_http_compose(out, 30, "GET", "k.example", "/", "t", 0, 0, 0) == 0);
}

static u8 sunk[64];
static u32 sunk_len, sink_calls;

static void collect(void *ctx, const u8 *p, u32 n) {
    CHECK(ctx == (void *)sunk);
    memcpy(sunk + sunk_len, p, n);
    sunk_len += n;
    sink_calls++;
}

static void test_sink(void) {
    static u8 tiny[4];   /* too small for the body: a sink needs no buffer */
    const char *r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n6\r\npedia \r\n0\r\n\r\n";
    u32 at = 0, n = (u32)strlen(r);
    int got = 0;
    jpr_http_init(&h, tiny, sizeof tiny);
    jpr_http_sink_to(&h, collect, sunk);
    sunk_len = sink_calls = 0;
    while (at < n) {   /* three bytes at a time */
        u32 piece = n - at < 3 ? n - at : 3;
        got = jpr_http_feed(&h, (const u8 *)r + at, piece);
        at += piece;
    }
    CHECK(got == 1 && h.body_len == 10 && sunk_len == 10 && !memcmp(sunk, "Wikipedia ", 10) && !h.overflow);
    CHECK(sink_calls > 2);
}

int main(void) {
    test_compose();
    test_sink();
    test_content_length_at_every_split();
    test_chunked_at_every_split();
    test_body_to_the_close_and_continue();
    test_overflow_and_bad();
    if (failures) {
        printf("http: %d check(s) failed\n", failures);
        return 1;
    }
    printf("http: all checks passed\n");
    return 0;
}
