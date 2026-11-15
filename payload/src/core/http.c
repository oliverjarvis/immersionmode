#include "http.h"

#include "util.h"

enum { S_STATUS, S_HEADER, S_BODY, S_BODY_CLOSE, S_CHUNK_SIZE, S_CHUNK_DATA, S_CHUNK_END, S_TRAILER, S_DONE, S_BAD };

static int put(u8 *out, u32 *at, u32 cap, const char *s) {
    while (*s) {
        if (*at >= cap) return -1;
        out[(*at)++] = (u8)*s++;
    }
    return 0;
}

u32 jpr_http_compose(u8 *out, u32 cap, const char *method, const char *authority, const char *path, const char *token,
                     const char *type, const u8 *body, u32 body_len) {
    char number[12];
    u32 at = 0, k;
    if (put(out, &at, cap, method) || put(out, &at, cap, " ") || put(out, &at, cap, path) ||
        put(out, &at, cap, " HTTP/1.1\r\nHost: ") || put(out, &at, cap, authority) ||
        put(out, &at, cap, "\r\nAuthorization: Bearer ") || put(out, &at, cap, token))
        return 0;
    if (body) {
        jpr_format_u32(number, body_len);
        if (put(out, &at, cap, "\r\nContent-Type: ") || put(out, &at, cap, type) ||
            put(out, &at, cap, "\r\nContent-Length: ") || put(out, &at, cap, number))
            return 0;
    }
    if (put(out, &at, cap, "\r\nX-Title: jpreader\r\nConnection: close\r\n\r\n") || at + body_len > cap) return 0;
    for (k = 0; body && k < body_len; k++) out[at++] = body[k];
    return at;
}

void jpr_http_init(struct jpr_http *h, u8 *body, u32 cap) {
    h->state = S_STATUS;
    h->status = 0;
    h->chunked = 0;
    h->has_length = 0;
    h->remaining = 0;
    h->line_len = 0;
    h->body_len = 0;
    h->overflow = 0;
    h->body = body;
    h->cap = cap;
    h->sink = 0;
    h->sink_ctx = 0;
}

void jpr_http_sink_to(struct jpr_http *h, jpr_http_sink sink, void *ctx) {
    h->sink = sink;
    h->sink_ctx = ctx;
}

static u8 lower(u8 c) {
    return c >= 'A' && c <= 'Z' ? (u8)(c + 32) : c;
}

/* Does the line start with header `name` (lower case, with its colon)? Returns the value's offset. */
static u32 header(const struct jpr_http *h, const char *name) {
    u32 k = 0;
    while (name[k]) {
        if (k >= h->line_len || lower(h->line[k]) != (u8)name[k]) return 0;
        k++;
    }
    while (k < h->line_len && (h->line[k] == ' ' || h->line[k] == '\t')) k++;
    return k;
}

static int contains(const struct jpr_http *h, u32 from, const char *word) {
    u32 k, j;
    for (k = from; k < h->line_len; k++) {
        for (j = 0; word[j] && k + j < h->line_len && lower(h->line[k + j]) == (u8)word[j]; j++) {}
        if (!word[j]) return 1;
    }
    return 0;
}

/* A complete line (without its CR LF) in h->line. */
static void line(struct jpr_http *h) {
    u32 k, v;
    switch (h->state) {
    case S_STATUS:
        if (h->line_len < 12 || h->line[0] != 'H' || h->line[1] != 'T' || h->line[2] != 'T' || h->line[3] != 'P' ||
            h->line[4] != '/' || h->line[8] != ' ') {
            h->state = S_BAD;
            return;
        }
        h->status = 0;
        for (k = 9; k < 12; k++) {
            if (h->line[k] < '0' || h->line[k] > '9') {
                h->state = S_BAD;
                return;
            }
            h->status = h->status * 10 + (u32)(h->line[k] - '0');
        }
        h->chunked = 0;
        h->has_length = 0;
        h->state = S_HEADER;
        return;
    case S_HEADER:
        if (!h->line_len) {
            if (h->status >= 100 && h->status < 200) h->state = S_STATUS;
            else if (h->chunked) h->state = S_CHUNK_SIZE;
            else if (h->has_length) h->state = h->remaining ? S_BODY : S_DONE;
            else h->state = S_BODY_CLOSE;
            return;
        }
        if ((v = header(h, "content-length:")) != 0) {
            h->remaining = 0;
            h->has_length = 1;
            for (k = v; k < h->line_len && h->line[k] >= '0' && h->line[k] <= '9'; k++) {
                if (h->remaining > 99999999u) {
                    h->state = S_BAD;
                    return;
                }
                h->remaining = h->remaining * 10 + (u32)(h->line[k] - '0');
            }
        } else if ((v = header(h, "transfer-encoding:")) != 0) {
            h->chunked = contains(h, v, "chunked");
        }
        return;
    case S_CHUNK_SIZE:
        h->remaining = 0;
        for (k = 0; k < h->line_len && h->line[k] != ';'; k++) {
            u8 c = lower(h->line[k]);
            u32 d = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : 16;
            if (d > 15 || h->remaining > 0x00FFFFFFu) {
                h->state = S_BAD;
                return;
            }
            h->remaining = (h->remaining << 4) | d;
        }
        if (!k) {
            h->state = S_BAD;
            return;
        }
        h->state = h->remaining ? S_CHUNK_DATA : S_TRAILER;
        return;
    case S_CHUNK_END:
        h->state = h->line_len ? S_BAD : S_CHUNK_SIZE;
        return;
    case S_TRAILER:
        if (!h->line_len) h->state = S_DONE;
        return;
    default:
        return;
    }
}

static void keep(struct jpr_http *h, const u8 *data, u32 n) {
    u32 k;
    if (h->sink) {
        if (n) h->sink(h->sink_ctx, data, n);
        h->body_len += n;
        return;
    }
    for (k = 0; k < n; k++) {
        if (h->body_len < h->cap) h->body[h->body_len++] = data[k];
        else h->overflow = 1;
    }
}

int jpr_http_feed(struct jpr_http *h, const u8 *data, u32 len) {
    u32 i = 0;
    while (i < len && h->state != S_DONE && h->state != S_BAD) {
        if (h->state == S_BODY || h->state == S_CHUNK_DATA) {
            u32 n = len - i < h->remaining ? len - i : h->remaining;
            keep(h, data + i, n);
            i += n;
            h->remaining -= n;
            if (!h->remaining) h->state = h->state == S_BODY ? S_DONE : S_CHUNK_END;
        } else if (h->state == S_BODY_CLOSE) {
            keep(h, data + i, len - i);
            i = len;
        } else {
            u8 c = data[i++];
            if (c == '\n') {
                if (h->line_len && h->line[h->line_len - 1] == '\r') h->line_len--;
                line(h);
                h->line_len = 0;
            } else if (h->line_len < JPR_HTTP_LINE) {
                h->line[h->line_len++] = c;
            }
        }
    }
    return h->state == S_DONE ? 1 : h->state == S_BAD ? -1 : 0;
}

int jpr_http_close(struct jpr_http *h) {
    if (h->state == S_BODY_CLOSE) h->state = S_DONE;
    return h->state == S_DONE ? 1 : -1;
}
