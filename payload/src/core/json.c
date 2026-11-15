#include "json.h"

#include "util.h"

#define DEPTH_MAX 16

struct cursor {
    const u8 *p;
    u32 len, i, depth;
};

int jpr_json_raw(u8 *out, u32 *at, u32 cap, const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    if (*at + n > cap) return -1;
    while (*s) out[(*at)++] = (u8)*s++;
    return 0;
}

int jpr_json_escaped(u8 *out, u32 *at, u32 cap, const u8 *text, u32 len) {
    static const char hex[] = "0123456789abcdef";
    u32 k;
    for (k = 0; k < len; k++) {
        u8 c = text[k];
        char esc[7];
        u32 n = 0, j;
        if (c == '"' || c == '\\') {
            esc[n++] = '\\';
            esc[n++] = (char)c;
        } else if (c == '\n') {
            esc[n++] = '\\';
            esc[n++] = 'n';
        } else if (c == '\r') {
            esc[n++] = '\\';
            esc[n++] = 'r';
        } else if (c == '\t') {
            esc[n++] = '\\';
            esc[n++] = 't';
        } else if (c < 0x20) {
            esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
            esc[4] = hex[c >> 4]; esc[5] = hex[c & 15];
            n = 6;
        } else {
            esc[n++] = (char)c;
        }
        if (*at + n > cap) return -1;
        for (j = 0; j < n; j++) out[(*at)++] = (u8)esc[j];
    }
    return 0;
}

static void ws(struct cursor *c) {
    while (c->i < c->len && (c->p[c->i] == ' ' || c->p[c->i] == '\t' || c->p[c->i] == '\n' || c->p[c->i] == '\r'))
        c->i++;
}

static int hex4(struct cursor *c, u32 *v) {
    u32 k;
    *v = 0;
    if (c->i + 4 > c->len) return -1;
    for (k = 0; k < 4; k++) {
        u8 h = c->p[c->i++];
        u32 d = h >= '0' && h <= '9' ? (u32)(h - '0') : h >= 'a' && h <= 'f' ? (u32)(h - 'a' + 10)
              : h >= 'A' && h <= 'F' ? (u32)(h - 'A' + 10) : 16;
        if (d > 15) return -1;
        *v = (*v << 4) | d;
    }
    return 0;
}

/* Reads a string at the cursor (which is on its opening quote). With out, decodes it into out
 * (stopping quietly when full) and sets *n to the bytes written. 0, or -1 when malformed. */
static int string(struct cursor *c, u8 *out, u32 cap, u32 *n) {
    u32 at = 0, full = 0;
    if (c->i >= c->len || c->p[c->i] != '"') return -1;
    c->i++;
    while (c->i < c->len) {
        u8 ch = c->p[c->i++];
        u32 cp;
        if (ch == '"') {
            if (n) *n = at;
            return 0;
        }
        if (ch != '\\') {
            if (out && !full) {
                if (at < cap) out[at++] = ch;
                else full = 1;
            }
            continue;
        }
        if (c->i >= c->len) return -1;
        ch = c->p[c->i++];
        switch (ch) {
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = 8; break;
        case 'f': cp = 12; break;
        case '"': case '\\': case '/': cp = ch; break;
        case 'u':
            if (hex4(c, &cp)) return -1;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                u32 lo;
                if (c->i + 6 <= c->len && c->p[c->i] == '\\' && c->p[c->i + 1] == 'u') {
                    c->i += 2;
                    if (hex4(c, &lo)) return -1;
                    cp = lo >= 0xDC00 && lo <= 0xDFFF ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            break;
        default:
            return -1;
        }
        if (out && !full && jpr_utf8_put(out, &at, cap, cp)) full = 1;
    }
    return -1;
}

static int value(struct cursor *c);

static int container(struct cursor *c, u8 open, u8 close) {
    if (++c->depth > DEPTH_MAX) return -1;
    c->i++;
    ws(c);
    if (c->i < c->len && c->p[c->i] == close) {
        c->i++;
        c->depth--;
        return 0;
    }
    while (c->i < c->len) {
        if (open == '{') {
            if (string(c, 0, 0, 0)) return -1;
            ws(c);
            if (c->i >= c->len || c->p[c->i++] != ':') return -1;
        }
        if (value(c)) return -1;
        ws(c);
        if (c->i >= c->len) return -1;
        if (c->p[c->i] == ',') {
            c->i++;
            ws(c);
            continue;
        }
        if (c->p[c->i] == close) {
            c->i++;
            c->depth--;
            return 0;
        }
        return -1;
    }
    return -1;
}

/* Skips the value at the cursor. */
static int value(struct cursor *c) {
    ws(c);
    if (c->i >= c->len) return -1;
    switch (c->p[c->i]) {
    case '"': return string(c, 0, 0, 0);
    case '{': return container(c, '{', '}');
    case '[': return container(c, '[', ']');
    default:
        while (c->i < c->len && c->p[c->i] != ',' && c->p[c->i] != '}' && c->p[c->i] != ']' &&
               c->p[c->i] != ' ' && c->p[c->i] != '\n' && c->p[c->i] != '\r' && c->p[c->i] != '\t')
            c->i++;
        return 0;
    }
}

/* Moves the cursor to the member or element named by path[0..n). */
static int step(struct cursor *c, const char *name, u32 n) {
    ws(c);
    if (c->i >= c->len) return -1;
    if (c->p[c->i] == '[') {
        u32 index = 0, k;
        for (k = 0; k < n; k++) {
            if (name[k] < '0' || name[k] > '9') return -1;
            index = index * 10 + (u32)(name[k] - '0');
        }
        c->i++;
        for (k = 0; k < index; k++) {
            if (value(c)) return -1;
            ws(c);
            if (c->i >= c->len || c->p[c->i++] != ',') return -1;
        }
        ws(c);
        return c->i < c->len && c->p[c->i] != ']' ? 0 : -1;
    }
    if (c->p[c->i] != '{') return -1;
    c->i++;
    for (;;) {
        u8 key[64];
        u32 klen = 0, k;
        int same;
        ws(c);
        if (c->i >= c->len || c->p[c->i] == '}') return -1;
        if (string(c, key, sizeof key, &klen)) return -1;
        ws(c);
        if (c->i >= c->len || c->p[c->i++] != ':') return -1;
        same = klen == n;
        for (k = 0; same && k < n; k++) same = key[k] == (u8)name[k];
        if (same) return 0;
        if (value(c)) return -1;
        ws(c);
        if (c->i >= c->len || c->p[c->i++] != ',') return -1;
    }
}

s32 jpr_json_get_string(const u8 *json, u32 len, const char *path, u8 *out, u32 cap) {
    struct cursor c;
    u32 n = 0;
    c.p = json;
    c.len = len;
    c.i = 0;
    c.depth = 0;
    while (*path) {
        u32 k = 0;
        while (path[k] && path[k] != '.') k++;
        if (step(&c, path, k)) return -1;
        path += k;
        if (*path == '.') path++;
    }
    ws(&c);
    if (string(&c, out, cap, &n)) return -1;
    if (n) {   /* a character cut off by the end of out: drop what came of it */
        u32 k = n, need;
        while (k && (out[k - 1] & 0xC0) == 0x80) k--;
        if (k) {
            need = out[k - 1] >= 0xF0 ? 4 : out[k - 1] >= 0xE0 ? 3 : out[k - 1] >= 0xC0 ? 2 : 1;
            if (n - (k - 1) < need) n = k - 1;
        }
    }
    return (s32)n;
}
