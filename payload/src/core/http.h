#ifndef JPR_HTTP_H
#define JPR_HTTP_H

#include "types.h"

#define JPR_HTTP_LINE 256
typedef void (*jpr_http_sink)(void *ctx, const u8 *p, u32 n);
/* A request: method and path on authority (the Host header: the host, with ":port" when it is not 443),
 * with a bearer token, and a body of the given type when body is set (body_len 0 otherwise). Returns its
 * length, or 0 when it does not fit. */
u32 jpr_http_compose(u8 *out, u32 cap, const char *method, const char *authority, const char *path, const char *token,
                     const char *type, const u8 *body, u32 body_len);
struct jpr_http {
    u32 state, status, chunked, has_length, remaining, line_len, body_len, overflow;
    u8 line[JPR_HTTP_LINE];
    u8 *body;
    u32 cap;
    jpr_http_sink sink;   /* set: body bytes go here instead of into body */
    void *sink_ctx;
};
void jpr_http_init(struct jpr_http *h, u8 *body, u32 cap);
/* After jpr_http_init: the body goes to sink as it arrives (body_len still counts it). */
void jpr_http_sink_to(struct jpr_http *h, jpr_http_sink sink, void *ctx);
/* Takes received bytes. 1 when the response is complete, 0 for more, -1 when it is malformed. */
int jpr_http_feed(struct jpr_http *h, const u8 *data, u32 len);
/* The connection has closed: 1 when the response is complete, else -1. */
int jpr_http_close(struct jpr_http *h);

#endif
