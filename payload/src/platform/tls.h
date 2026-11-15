#ifndef JPR_TLS_H
#define JPR_TLS_H

#include "bearssl.h"
#include "net.h"


struct jpr_tls_io { u32 soc; s32 fd; const struct jpr_deadline *d; s32 why; };   /* why: 0, JPR_EXPIRED, JPR_CANCELLED, or -errno */
struct jpr_tls {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context io;
    struct jpr_tls_io net;
    u8 buf[BR_SSL_BUFSIZE_BIDI];
};
/* Starts a TLS 1.2 session with host over fd (the certificate must name it) and runs the handshake within d.
 * Returns JPR_NET_OK, or JPR_NET_CERT / JPR_NET_TIMEOUT / JPR_NET_CANCELLED / JPR_NET_UNREACHABLE with *code. */
u32 jpr_tls_open(struct jpr_tls *t, u32 soc, s32 fd, const char *host, const struct jpr_deadline *d, u64 time_ms,
                 u32 *code);
/* After a failed BearSSL call: which JPR_NET_* failure it was, with *code for "Could not reach". */
u32 jpr_tls_failure(struct jpr_tls *t, u32 *code);

#endif
