#include "config.h"

static int space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static int same(const char *a, u32 n, const char *b) {
    u32 k = 0;
    for (; k < n; k++)
        if (!b[k] || a[k] != b[k]) return 0;
    return b[n] == 0;
}

/* Copies value v (n bytes) into field (cap bytes with its NUL) if it fits; else leaves it empty. */
static void set(char *field, u32 cap, const char *v, u32 n) {
    u32 k;
    field[0] = 0;
    if (n >= cap) return;
    for (k = 0; k < n; k++) field[k] = v[k];
    field[n] = 0;
}

void jpr_config_parse(struct jpr_config *c, const char *text, u32 len) {
    u32 i = 0;
    c->debug = 0;
    c->voice = 1;
    c->kokoro_url[0] = 0;
    c->kokoro_token[0] = 0;
    while (i < len) {
        u32 start = i, end, eq, ks, ke, vs, ve;
        while (i < len && text[i] != '\n') i++;
        end = i++;
        ks = start;
        while (ks < end && space(text[ks])) ks++;
        if (ks == end || text[ks] == '#') continue;
        for (eq = ks; eq < end && text[eq] != '='; eq++) {}
        if (eq == end) continue;
        ke = eq;
        while (ke > ks && space(text[ke - 1])) ke--;
        vs = eq + 1;
        while (vs < end && space(text[vs])) vs++;
        ve = end;
        while (ve > vs && space(text[ve - 1])) ve--;
        if (same(text + ks, ke - ks, "debug")) c->debug = ve > vs && text[vs] == '1';
        else if (same(text + ks, ke - ks, "voice")) c->voice = !(ve > vs && text[vs] == '0');
        else if (same(text + ks, ke - ks, "kokoro_url")) set(c->kokoro_url, JPR_URL_MAX, text + vs, ve - vs);
        else if (same(text + ks, ke - ks, "kokoro_token")) set(c->kokoro_token, JPR_TOKEN_MAX, text + vs, ve - vs);
    }
}
