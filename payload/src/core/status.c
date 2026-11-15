#include "status.h"

#include "util.h"

#define TICKS_PER_TENTH_MS 26811u   /* the 3DS's 268,111,856 Hz tick counter */
#define NONE 0xFFFFFFFFu

static void put(char *out, u32 cap, u32 *at, const char *s) {
    while (*s && *at + 1 < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_number(char *out, u32 cap, u32 *at, u32 n) {
    char digits[12];
    jpr_format_u32(digits, n);
    put(out, cap, at, digits);
}

static void put_hex(char *out, u32 cap, u32 *at, u32 n) {
    char digits[9];
    u32 k;
    for (k = 0; k < 8; k++) digits[k] = "0123456789ABCDEF"[(n >> (28 - 4 * k)) & 0xF];
    digits[8] = 0;
    put(out, cap, at, digits);
}

/* ticks as milliseconds with one decimal */
static void put_ms(char *out, u32 cap, u32 *at, u32 ticks) {
    u32 tenths = ticks / TICKS_PER_TENTH_MS;
    char tenth[2] = {(char)('0' + tenths % 10), 0};
    put_number(out, cap, at, tenths / 10);
    put(out, cap, at, ".");
    put(out, cap, at, tenth);
}

void jpr_status_line(char *out, u32 cap, u32 free_kb, s32 arc, u32 dat, u32 line, u32 err, u32 detail,
                     u32 draw_ticks, u32 fill_ticks, u32 copy_ticks) {
    u32 at = 0;
    if (!cap) return;
    out[0] = 0;
    put(out, cap, &at, "mem ");
    put_number(out, cap, &at, free_kb);
    put(out, cap, &at, "K ");
    if (arc < 0) put(out, cap, &at, "mem");
    else put_number(out, cap, &at, (u32)arc);
    put(out, cap, &at, "/");
    if (dat == NONE) put(out, cap, &at, "-");
    else put_number(out, cap, &at, dat);
    put(out, cap, &at, "/");
    if (line == NONE) put(out, cap, &at, "-");
    else put_number(out, cap, &at, line);
    put(out, cap, &at, " err ");
    put_number(out, cap, &at, err);
    if (detail) {
        put(out, cap, &at, ":");
        put_hex(out, cap, &at, detail);
    }
    put(out, cap, &at, " draw ");
    put_ms(out, cap, &at, draw_ticks);
    put(out, cap, &at, "/");
    put_ms(out, cap, &at, fill_ticks);
    put(out, cap, &at, "/");
    put_ms(out, cap, &at, copy_ticks);
    put(out, cap, &at, "ms");
}

void jpr_log_value(char *out, u32 cap, u32 *len, const char *name, u32 value) {
    u32 at = *len, n = 0;
    while (name[n]) n++;
    if (at + n + 11 > cap) return;   /* name, space, 8 digits, newline, NUL */
    put(out, cap, &at, name);
    put(out, cap, &at, " ");
    put_hex(out, cap, &at, value);
    put(out, cap, &at, "\n");
    *len = at;
}

void jpr_status_voice(char *out, u32 cap, u32 fault) {
    u32 at = 0;
    if (!fault) return;
    while (at < cap && out[at]) at++;
    if (at + 4 >= cap) return;               /* " v" and a digit, and the NUL */
    put(out, cap, &at, " v");
    put_number(out, cap, &at, fault);
}
