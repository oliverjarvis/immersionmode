#include <stdio.h>

#define memcpy jpr_t_memcpy
#define memmove jpr_t_memmove
#define memset jpr_t_memset
#define memcmp jpr_t_memcmp
#define strlen jpr_t_strlen
#include "libc/string.c"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
    unsigned char a[16] = "abcdefghijklmno", b[16];
    memcpy(b, a, 16);
    CHECK(memcmp(a, b, 16) == 0);
    memmove(a + 2, a, 5);                         /* overlapping, forwards */
    CHECK(memcmp(a, "ababcdeh", 8) == 0);
    memmove(b, b + 3, 5);                         /* overlapping, backwards */
    CHECK(memcmp(b, "defghfgh", 8) == 0);
    memset(a, 'z', 3);
    CHECK(a[0] == 'z' && a[2] == 'z' && a[3] == 'b');
    CHECK(memcmp("abc", "abd", 3) < 0 && memcmp("abd", "abc", 3) > 0);
    CHECK(strlen("hello") == 5 && strlen("") == 0);
    if (failures) {
        printf("string: %d check(s) failed\n", failures);
        return 1;
    }
    printf("string: all checks passed\n");
    return 0;
}
