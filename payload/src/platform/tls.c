#include "tls.h"

#include "core/grammar.h"
#include "core/ipc.h"
#include "debug.h"
#include "svc.h"
#include "trust_anchors.h"

#define POLLIN 0x01
#define POLLOUT 0x10
#define DAYS_TO_1900 693961u   /* days from 0000-01-01 to 1900-01-01, BearSSL's epoch */

struct pollfd3 { s32 fd; s32 events; s32 revents; };

static void wait_for(u32 soc, s32 fd, s32 events) {
    static struct pollfd3 p;
    u32 *cmd = jpr_cmdbuf(), *bufs = jpr_staticbufs(), save0 = bufs[0], save1 = bufs[1];
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    bufs[0] = (12u << 14) | 2u;
    bufs[1] = (u32)&p;
    jpr_ipc_soc_poll(cmd, (u32)&p, 1, 100);
    jpr_soc_call(soc);
    bufs[0] = save0;
    bufs[1] = save1;
}

static int io_read(void *ctx, unsigned char *buf, size_t len) {
    struct jpr_tls_io *n = ctx;
    for (;;) {
        s32 r;
        int state = jpr_deadline_check(n->d);
        if (state != JPR_LIVE) {
            n->why = state;
            return -1;
        }
        jpr_ipc_soc_recv(jpr_cmdbuf(), (u32)n->fd, (u32)buf, (u32)len);
        r = jpr_soc_call(n->soc);
        if (r > 0) return r;
        if (r != JPR_EAGAIN) {
            n->why = r ? r : -1000;   /* 0: the server closed the connection */
            return -1;
        }
        wait_for(n->soc, n->fd, POLLIN);
    }
}

static int io_write(void *ctx, const unsigned char *buf, size_t len) {
    struct jpr_tls_io *n = ctx;
    for (;;) {
        s32 r;
        int state = jpr_deadline_check(n->d);
        if (state != JPR_LIVE) {
            n->why = state;
            return -1;
        }
        jpr_ipc_soc_send(jpr_cmdbuf(), (u32)n->fd, (u32)buf, (u32)len);
        r = jpr_soc_call(n->soc);
        if (r > 0) return r;
        if (r != JPR_EAGAIN) {
            n->why = r ? r : -1000;
            return -1;
        }
        wait_for(n->soc, n->fd, POLLOUT);
    }
}

u32 jpr_tls_failure(struct jpr_tls *t, u32 *code) {
    int err = br_ssl_engine_last_error(&t->sc.eng);
    if (err >= BR_ERR_X509_OK + 1 && err < 64) return JPR_NET_CERT;
    if (err == BR_ERR_IO || err == 0) {
        if (t->net.why == JPR_EXPIRED) return JPR_NET_TIMEOUT;
        if (t->net.why == JPR_CANCELLED) return JPR_NET_CANCELLED;
        *code = 6000 + (u32)(t->net.why < 0 ? -t->net.why : t->net.why) % 1000;
        return JPR_NET_UNREACHABLE;
    }
    *code = 5000 + (u32)err;
    return JPR_NET_UNREACHABLE;
}

u32 jpr_tls_open(struct jpr_tls *t, u32 soc, s32 fd, const char *host, const struct jpr_deadline *d, u64 time_ms,
                 u32 *code) {
    u8 seed[32];
    u64 days = time_ms / 86400000u;
    br_ssl_client_init_full(&t->sc, &t->xc, TAs, TAs_NUM);
    br_ssl_engine_set_versions(&t->sc.eng, BR_TLS12, BR_TLS12);
    br_x509_minimal_set_time(&t->xc, DAYS_TO_1900 + (u32)days, (u32)((time_ms - days * 86400000u) / 1000u));
    br_ssl_engine_set_buffer(&t->sc.eng, t->buf, sizeof t->buf, 1);
    if (jpr_random(seed, sizeof seed)
#if JPR_DEBUG
        || jpr_dbg.net_fault == JPR_FAULT_NO_ENTROPY
#endif
    ) {
        *code = 5900;   /* no real random bytes: a guessable seed would expose the key */
        return JPR_NET_UNREACHABLE;
    }
    br_ssl_engine_inject_entropy(&t->sc.eng, seed, sizeof seed);
    if (!br_ssl_client_reset(&t->sc, host, 0)) {
        *code = 5000 + (u32)br_ssl_engine_last_error(&t->sc.eng);
        return JPR_NET_UNREACHABLE;
    }
    t->net.soc = soc;
    t->net.fd = fd;
    t->net.d = d;
    t->net.why = 0;
    br_sslio_init(&t->io, &t->sc.eng, io_read, &t->net, io_write, &t->net);
    if (br_sslio_flush(&t->io) < 0) return jpr_tls_failure(t, code);
    return JPR_NET_OK;
}
