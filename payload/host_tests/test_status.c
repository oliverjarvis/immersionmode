#include <stdio.h>
#include <string.h>

#include "core/status.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
    char out[64];
    /* the whole panel, then a full-screen fill and a half-screen copy on their own */
    jpr_status_line(out, sizeof out, 3412, 41, 22, 7, 0, 0, 268112 * 3 / 2, 268112 / 2, 268112);
    CHECK(!strcmp(out, "mem 3412K 41/22/7 err 0 draw 1.5/0.5/1.0ms"));
    jpr_status_line(out, sizeof out, 900, -1, 0xFFFFFFFFu, 3, 4014, 0, 0, 0, 0);
    CHECK(!strcmp(out, "mem 900K mem/-/3 err 4014 draw 0.0/0.0/0.0ms"));
    jpr_status_line(out, sizeof out, 3468, -1, 0xFFFFFFFFu, 5, 11, 0xE0A09D2E, 0, 0, 0);  /* the 3DS's result */
    CHECK(!strcmp(out, "mem 3468K mem/-/5 err 11:E0A09D2E draw 0.0/0.0/0.0ms"));
    jpr_status_line(out, 12, 3412, 41, 22, 7, 0, 0, 0, 0, 0);
    CHECK(strlen(out) == 11 && !strncmp(out, "mem 3412K 4", 11));
    jpr_status_line(out, sizeof out, 3412, 41, 22, 7, 0, 0, 0, 0, 0);
    jpr_status_voice(out, sizeof out, 0);                                   /* voice fine: nothing added */
    CHECK(!strcmp(out, "mem 3412K 41/22/7 err 0 draw 0.0/0.0/0.0ms"));
    jpr_status_voice(out, sizeof out, 1);                                   /* why a clip is silent */
    CHECK(!strcmp(out, "mem 3412K 41/22/7 err 0 draw 0.0/0.0/0.0ms v1"));
    jpr_status_line(out, sizeof out, 3412, 41, 22, 7, 0, 0, 0, 0, 0);       /* 42 characters */
    jpr_status_voice(out, 44, 3);                                           /* no room: the line is kept whole */
    CHECK(!strcmp(out, "mem 3412K 41/22/7 err 0 draw 0.0/0.0/0.0ms"));
    {
        char log[40];
        u32 len = 0;
        jpr_log_value(log, sizeof log, &len, "connect", 0xC9209C64);
        jpr_log_value(log, sizeof log, &len, "ndm", 0);
        CHECK(!strcmp(log, "connect C9209C64\nndm 00000000\n"));
        jpr_log_value(log, sizeof log, &len, "more than fits", 1);
        CHECK(len == 30 && !strcmp(log, "connect C9209C64\nndm 00000000\n"));   /* whole lines only */
    }
    if (failures) {
        printf("status: %d check(s) failed\n", failures);
        return 1;
    }
    printf("status: all checks passed\n");
    return 0;
}
