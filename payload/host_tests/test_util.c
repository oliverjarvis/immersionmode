#include <stdio.h>
#include <string.h>

#include "core/util.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_plausible_pointers(void) {
    CHECK(jpr_plausible_ptr(0x08000000u));
    CHECK(jpr_plausible_ptr(0x0FFFFFFCu));
    CHECK(jpr_plausible_ptr(0x14000000u));
    CHECK(jpr_plausible_ptr(0x32AB23B0u));        /* a talk window seen in stage 2 planning */
    CHECK(!jpr_plausible_ptr(0));
    CHECK(!jpr_plausible_ptr(0x08000002u));       /* misaligned */
    CHECK(!jpr_plausible_ptr(0x10000000u));
    CHECK(!jpr_plausible_ptr(0x006D4000u));       /* the game's image, not an object */
    CHECK(!jpr_plausible_ptr(0x38000000u));
    CHECK(!jpr_plausible_ptr(0xFFFFFFFCu));
}

static void test_memory_helpers(void) {
    u8 a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8];
    jpr_memset(b, 0xEE, sizeof b);
    CHECK(b[0] == 0xEE && b[7] == 0xEE);
    jpr_memcpy(b, a, sizeof a);
    CHECK(b[0] == 1 && b[7] == 8);
}

static void test_format_u32(void) {
    char out[12];
    CHECK(jpr_format_u32(out, 0) == 1 && !strcmp(out, "0"));
    CHECK(jpr_format_u32(out, 7) == 1 && !strcmp(out, "7"));
    CHECK(jpr_format_u32(out, 10) == 2 && !strcmp(out, "10"));
    CHECK(jpr_format_u32(out, 4294967295u) == 10 && !strcmp(out, "4294967295"));
}

int main(void) {
    test_plausible_pointers();
    test_memory_helpers();
    test_format_u32();
    if (failures) {
        printf("util: %d check(s) failed\n", failures);
        return 1;
    }
    printf("util: all checks passed\n");
    return 0;
}
