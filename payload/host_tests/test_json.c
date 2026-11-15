#include <stdio.h>
#include <string.h>

#include "core/json.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 out[512];

static s32 get(const char *json, const char *path) {
    memset(out, 0, sizeof out);
    return jpr_json_get_string((const u8 *)json, (u32)strlen(json), path, out, sizeof out);
}

static void test_escaping(void) {
    static const u8 text[] = "say \"hi\"\\ \n\t\x01 \xE3\x81\x82";
    u32 at = 0;
    CHECK(jpr_json_raw(out, &at, sizeof out, "\"") == 0);
    CHECK(jpr_json_escaped(out, &at, sizeof out, text, sizeof text - 1) == 0);
    CHECK(jpr_json_raw(out, &at, sizeof out, "\"") == 0);
    CHECK(at == strlen("\"say \\\"hi\\\"\\\\ \\n\\t\\u0001 \xE3\x81\x82\""));
    CHECK(!memcmp(out, "\"say \\\"hi\\\"\\\\ \\n\\t\\u0001 \xE3\x81\x82\"", at));
    at = 0;
    CHECK(jpr_json_escaped(out, &at, 4, text, sizeof text - 1) == -1);   /* does not fit */
}

static void test_get_reply(void) {
    const char *reply =
        "{\"id\":\"gen-1\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"content\":\"\\\"Wait.\\\"\\n\\u3061\\u3087\\u3063\\u3068 = a little\"},\"x\":[1,2,{\"y\":null}]}],"
        "\"usage\":{\"total_tokens\":12}}";
    s32 n = get(reply, "choices.0.message.content");
    CHECK(n == (s32)strlen("\"Wait.\"\n\xE3\x81\xA1\xE3\x82\x87\xE3\x81\xA3\xE3\x81\xA8 = a little"));
    CHECK(!memcmp(out, "\"Wait.\"\n\xE3\x81\xA1\xE3\x82\x87\xE3\x81\xA3\xE3\x81\xA8 = a little", (u32)n));
    CHECK(get(reply, "choices.1.message.content") == -1);
    CHECK(get(reply, "usage.total_tokens") == -1);          /* a number, not a string */
    CHECK(get(reply, "choices.0.message.role") == 9 && !memcmp(out, "assistant", 9));
}

static void test_get_error(void) {
    const char *err = " { \"error\" : { \"message\" : \"No auth credentials found\", \"code\" : 401 } } ";
    CHECK(get(err, "error.message") == 25 && !memcmp(out, "No auth credentials found", 25));
    CHECK(get("{\"a\":\"\xE3\x81\x82\"}", "a") == 3);          /* a whole last character stays */
}

static void test_surrogates_and_bad_input(void) {
    CHECK(get("{\"a\":\"\\ud83d\\ude00\"}", "a") == 4 && out[0] == 0xF0);   /* U+1F600 */
    CHECK(get("{\"a\":\"\\ud83d\"}", "a") == 3);                            /* lone half: U+FFFD */
    CHECK(get("{\"a\":\"unterminated}", "a") == -1);
    CHECK(get("{\"a\" \"b\"}", "a") == -1);
    CHECK(get("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[\"deep\"]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]", "0") == -1);
}

static void test_truncation(void) {
    u8 small[5];
    const char *j = "{\"a\":\"\xE3\x81\x82\xE3\x81\x84\"}";
    CHECK(jpr_json_get_string((const u8 *)j, (u32)strlen(j), "a", small, sizeof small) == 3);  /* あ only */
}

int main(void) {
    test_escaping();
    test_get_reply();
    test_get_error();
    test_surrogates_and_bad_input();
    test_truncation();
    if (failures) {
        printf("json: %d check(s) failed\n", failures);
        return 1;
    }
    printf("json: all checks passed\n");
    return 0;
}
