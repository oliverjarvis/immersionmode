#include <stdio.h>
#include <string.h>

#include "core/url.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
    struct jpr_url u;
    CHECK(jpr_url_parse("https://happy-otter-123.convex.site", &u) == 0);
    CHECK(!strcmp(u.host, "happy-otter-123.convex.site") && u.port == 443 && !strcmp(u.path, "") &&
          !strcmp(u.authority, "happy-otter-123.convex.site"));
    CHECK(jpr_url_parse("https://localhost:8443/", &u) == 0);
    CHECK(!strcmp(u.host, "localhost") && u.port == 8443 && !strcmp(u.authority, "localhost:8443") && !strcmp(u.path, ""));
    CHECK(jpr_url_parse("https://x.example/base/", &u) == 0 && !strcmp(u.path, "/base"));
    CHECK(jpr_url_parse("http://x.example", &u) == -1);
    CHECK(jpr_url_parse("https://", &u) == -1);
    CHECK(jpr_url_parse("https://a:0", &u) == -1);
    CHECK(jpr_url_parse("https://a:70000", &u) == -1);
    CHECK(jpr_url_parse("https://a:", &u) == -1);
    CHECK(jpr_url_parse("https://a:44x", &u) == -1);
    CHECK(jpr_url_parse("https://a b", &u) == -1);
    CHECK(jpr_url_parse("https://a/b c", &u) == -1);
    {
        char longer[100] = "https://";
        memset(longer + 8, 'h', 70);
        CHECK(jpr_url_parse(longer, &u) == -1);   /* a host of 70 characters */
    }
    if (failures) {
        printf("url: %d check(s) failed\n", failures);
        return 1;
    }
    printf("url: all checks passed\n");
    return 0;
}
