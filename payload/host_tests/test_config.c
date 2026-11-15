#include <stdio.h>
#include <string.h>

#include "core/config.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static struct jpr_config c;

static void parse(const char *s) {
    jpr_config_parse(&c, s, (u32)strlen(s));
}

int main(void) {
    parse("debug=1\nkokoro_url=https://k.example\nkokoro_token=kkr_t\n");
    CHECK(c.debug == 1 && !strcmp(c.kokoro_url, "https://k.example") && !strcmp(c.kokoro_token, "kkr_t"));
    parse("api_key=x\nmodel=y\nkokoro_url=https://k.example\nkokoro_token=kkr_t\n");   /* an older card */
    CHECK(!strcmp(c.kokoro_url, "https://k.example") && !strcmp(c.kokoro_token, "kkr_t") && c.debug == 0);
    parse("# comment\r\n  kokoro_token = kkr_x \r\n\r\nother=1\r\n");
    CHECK(!strcmp(c.kokoro_token, "kkr_x") && c.kokoro_url[0] == 0 && c.debug == 0);
    parse("");
    CHECK(c.kokoro_url[0] == 0 && c.kokoro_token[0] == 0);
    CHECK(c.voice == 1);                                       /* voice clips play unless turned off */
    parse("voice=0\n");
    CHECK(c.voice == 0);
    parse("voice=1\n");
    CHECK(c.voice == 1);
    {
        static char long_line[400];
        memset(long_line, 'k', sizeof long_line);
        memcpy(long_line, "kokoro_token=", 13);
        jpr_config_parse(&c, long_line, sizeof long_line);   /* no newline at the end, and too long */
        CHECK(c.kokoro_token[0] == 0);
    }
    parse("kokoro_token=abc");                                 /* last line without a newline */
    CHECK(!strcmp(c.kokoro_token, "abc"));
    if (failures) {
        printf("config: %d check(s) failed\n", failures);
        return 1;
    }
    printf("config: all checks passed\n");
    return 0;
}
