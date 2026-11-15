#include "worker.h"

#include "core/grammar.h"
#include "core/http.h"
#include "core/ipc.h"
#include "core/status.h"
#include "core/util.h"
#include "core/outbox.h"
#include "core/word.h"
#include "core/replies.h"
#include "core/reply.h"
#include "debug.h"
#include "fs.h"
#include "game.h"
#include "kokoro.h"
#include "net.h"
#include "svc.h"
#include "tls.h"

#define STACK_BYTES 0x8000
#define PRIORITY 0x3F           /* the lowest: the game always comes first */
#define F_SETFL 4
#define O_NONBLOCK 4
#define HOSTENT_BYTES 0x1A88
#define SOC_IN_USE 0xC8A10C0Bu  /* stands in for soc:U refusing Initialize, for the BUSY fault */

struct jpr_grammar_link jpr_link;

static u8 stack[STACK_BYTES] __attribute__((aligned(8)));
static u32 thread, wake;
static struct jpr_grammar_job job;
static struct jpr_grammar_result result;
static struct jpr_tls tls;
static u8 request[JPR_BODY_MAX + 512];
static u8 body[JPR_BODY_MAX];
static u8 response[16384] __attribute__((aligned(4)));
static u8 hostent[HOSTENT_BYTES];
static u8 ac_config[0x200];
static u32 ac;                  /* the ac:u session, open for the whole request */
static int connecting;          /* this request asked ac for the connection */
static u32 ndm;                 /* the ndm:u session, open for the whole request */
static char log_text[2048];     /* this request's steps, for jpreader/log.txt */
static u32 log_len;
static u32 outbox_seqs[JPR_OUTBOX_SLOTS], outbox_last, outbox_scanned;

static void note(const char *step, s32 value) { jpr_log_value(log_text, sizeof log_text, &log_len, step, (u32)value); }

static struct jpr_http http;

static u32 fault(void) {
#if JPR_DEBUG
    return jpr_dbg.net_fault;
#else
    return 0;
#endif
}

static u32 code_of(u32 step, s32 r) {
    u32 n = (u32)(r < 0 ? -r : r);
    while (n >= 1000) n -= 1000;
    return step * 1000 + n;
}

/* Wi-Fi up, or connected within d, the way Ultra Moon's own nn::ac::Connect does it (code 0x165FC4 and
 * 0x41E2FC): client version, a default config, then for infra priority 0 the radio taken from the
 * background services (ndm's exclusive state) and request EULA version 0.0, otherwise access point type
 * 0x40 denied; ConnectAsync and its result. The ac:u session stays open until the request is over:
 * closing it would drop the connection. *code says what failed: 10 no ac:u, 11 client version refused,
 * 12 connection refused, 13 still not connected, 14 the connection failed; *detail is the 3DS's own
 * result. Both show on the debug line, and every step goes to the log. */
static u32 wifi(const struct jpr_deadline *d, u32 *code, u32 *detail) {
    u32 event = 0, exclusive = 0, *cmd = jpr_cmdbuf(), *bufs = jpr_staticbufs();
    u32 failure = JPR_NET_NO_WIFI, save0, save1, priority = 0;
    int signalled = 0;
    s32 r;
    if (fault() == JPR_FAULT_NO_WIFI) return JPR_NET_NO_WIFI;
    if (jpr_service("ac:u", &ac)) {
        ac = 0;
        *code = 10;
        return JPR_NET_NO_WIFI;
    }
    jpr_ipc_ac_set_client_version(cmd);
    r = jpr_call(ac);
    note("ac client version", r);
    if (r) {
        *code = 11;
        *detail = (u32)r;
        return JPR_NET_NO_WIFI;
    }
    jpr_ipc_ac_get_wifi_status(cmd);   /* a 3DS answers with an error while it is not connected */
    r = jpr_call(ac);
    note("ac wifi status", r);
    note("ac wifi status value", r ? 0 : cmd[2]);
    if (!r && cmd[2] && fault() != JPR_FAULT_CONNECT) return JPR_NET_OK;   /* the game's connection */

    if (jpr_service("ndm:u", &ndm)) ndm = 0;
    save0 = bufs[0];
    save1 = bufs[1];
    bufs[0] = (0x200u << 14) | 2u;
    bufs[1] = (u32)ac_config;
    jpr_ipc_ac_create_default_config(cmd);
    r = jpr_call(ac);
    note("ac default config", r);
    if (!r) {
        jpr_ipc_ac_get_infra_priority(cmd, (u32)ac_config);
        r = jpr_call(ac);
        priority = cmd[2] & 0xFF;
        note("ac infra priority", r);
        note("ac infra priority value", priority);
    }
    if (!r && !priority) {
        if (ndm) {
            jpr_ipc_ndm_enter_exclusive_state(cmd);
            r = jpr_call(ndm);
            note("ndm enter exclusive", r);
            exclusive = r == 0;
        }
        jpr_ipc_ac_set_request_eula_version(cmd, (u32)ac_config);
        r = jpr_call(ac);
        note("ac eula version", r);
    } else if (!r) {
        jpr_ipc_ac_add_deny_ap_type(cmd, (u32)ac_config);
        r = jpr_call(ac);
        note("ac deny ap type", r);
    }
    bufs[0] = save0;
    bufs[1] = save1;
    *code = 12;
    if (r) {
        *detail = (u32)r;
    } else if (!svcCreateEvent(&event, 0)) {
        jpr_ipc_ac_connect_async(cmd, event, (u32)ac_config);
        r = jpr_call(ac);
        note("ac connect", r);
        if (r) {
            *detail = (u32)r;
        } else {
            connecting = 1;
            *code = 13;
            while (jpr_deadline_check(d) == JPR_LIVE) {
                if (svcWaitSynchronization(event, 100000000LL) == 0) {
                    signalled = 1;
                    break;
                }
            }
            note("ac connect signalled", (u32)signalled);
        }
        if (signalled) {
            jpr_ipc_ac_get_connect_result(cmd);
            r = jpr_call(ac);
            note("ac connect result", r);
            *code = r ? 14 : 0;
            *detail = (u32)r;
            if (!r) failure = JPR_NET_OK;
        }
        if (jpr_deadline_check(d) == JPR_CANCELLED) failure = JPR_NET_CANCELLED;
        svcCloseHandle(event);
    }
    if (exclusive) {                    /* as the game does once its connection is settled */
        jpr_ipc_ndm_leave_exclusive_state(cmd);
        note("ndm leave exclusive", jpr_call(ndm));
    }
    return failure;
}

/* Ends what wifi() started: the connection, if this request opened it, and the ac:u and ndm:u sessions. */
static void wifi_close(void) {
    u32 event = 0, *cmd = jpr_cmdbuf();
    if (connecting && !svcCreateEvent(&event, 0)) {
        jpr_ipc_ac_close_async(cmd, event);
        if (!jpr_call(ac)) svcWaitSynchronization(event, 3000000000LL);
        svcCloseHandle(event);
    }
    connecting = 0;
    if (ac) svcCloseHandle(ac);
    ac = 0;
    if (ndm) svcCloseHandle(ndm);
    ndm = 0;
}

/* One HTTPS exchange on the open soc:U session: host's address, a socket to port, TLS checked against
 * host, the request sent, and the response read into http (set up by the caller). JPR_NET_OK when a
 * whole response came, whatever its status; else the failure, with *code for "Could not reach". */
static u32 exchange(u32 soc, const char *host, u32 port, const u8 *req, u32 n, const struct jpr_deadline *total,
                    u32 *code) {
    struct jpr_deadline step;
    u32 *cmd = jpr_cmdbuf(), *bufs = jpr_staticbufs(), failure = JPR_NET_OK, k, hlen = 0;
    s32 fd, r;
    u8 address[8];
    int done = 0;

    while (host[hlen]) hlen++;
    jpr_deadline_set(&step, 10000, &jpr_link.cancel, job.id);
    {
        u32 save0 = bufs[0], save1 = bufs[1];
        bufs[0] = ((u32)HOSTENT_BYTES << 14) | 2u;
        bufs[1] = (u32)hostent;
        jpr_ipc_soc_gethostbyname(cmd, (u32)host, hlen + 1, HOSTENT_BYTES);
        r = jpr_soc_call(soc);
        bufs[0] = save0;
        bufs[1] = save1;
    }
    note("soc gethostbyname", r);
    if (r < 0 || hostent[4] == 0) {
        *code = code_of(2, r);
        return JPR_NET_UNREACHABLE;
    }
    address[0] = 8;
    address[1] = 2;                 /* AF_INET */
    address[2] = (u8)(port >> 8);
    address[3] = (u8)port;
    for (k = 0; k < 4; k++) address[4 + k] = hostent[0x1908 + k];
    if (fault() == JPR_FAULT_REFUSED) {
        address[2] = 0;
        address[3] = 1;
        address[4] = 127; address[5] = 0; address[6] = 0; address[7] = 1;
    }
    jpr_ipc_soc_socket(cmd);
    fd = jpr_soc_call(soc);
    note("soc socket", fd);
    if (fd < 0) {
        *code = code_of(3, fd);
        return JPR_NET_UNREACHABLE;
    }
    jpr_ipc_soc_fcntl(cmd, (u32)fd, F_SETFL, O_NONBLOCK);
    jpr_soc_call(soc);
    for (;;) {
        int state;
        jpr_ipc_soc_connect(cmd, (u32)fd, (u32)address);
        r = jpr_soc_call(soc);
        if (r == 0 || r == JPR_EISCONN) break;
        if (r != JPR_EINPROGRESS && r != JPR_EAGAIN && r != -7 /* EALREADY */) {
            note("soc connect", r);
            failure = JPR_NET_UNREACHABLE;
            *code = code_of(4, r);
            goto out;
        }
        state = jpr_deadline_check(&step);
        if (state != JPR_LIVE) {
            failure = state == JPR_CANCELLED ? JPR_NET_CANCELLED : JPR_NET_TIMEOUT;
            goto out;
        }
        svcSleepThread(50000000LL);
    }

    jpr_deadline_set(&step, 20000, &jpr_link.cancel, job.id);
    if (step.at > total->at) step.at = total->at;
    failure = jpr_tls_open(&tls, soc, fd, host, &step,
                           fault() == JPR_FAULT_CLOCK ? 3155673600000ull /* 2000-01-01 */ : jpr_time_ms(), code);
    note("tls", (s32)failure);
    if (failure) goto out;
    tls.net.d = total;
    if (br_sslio_write_all(&tls.io, req, n) < 0 || br_sslio_flush(&tls.io) < 0) {
        failure = jpr_tls_failure(&tls, code);
        goto out;
    }
    while (!done) {
        static u8 chunk[2048];
        int got = br_sslio_read(&tls.io, chunk, sizeof chunk);
        if (got < 0) {
            if (jpr_http_close(&http) == 1) break;   /* the server closed after a whole response */
            failure = jpr_tls_failure(&tls, code);
            goto out;
        }
        r = jpr_http_feed(&http, chunk, (u32)got);
        if (r < 0) {
            failure = JPR_NET_UNREACHABLE;
            *code = 6998;
            goto out;
        }
        done = r == 1;
    }
out:
    jpr_ipc_soc_close(cmd, (u32)fd);
    jpr_soc_call(soc);
    return failure;
}

/* The response's status, after a debug build's STATUS fault. */
static u32 status_of(void) {
#if JPR_DEBUG
    if (fault() == JPR_FAULT_STATUS) http.status = jpr_dbg.net_fault_arg;
#endif
    result.http_status = http.status;
    note("http status", (s32)http.status);
    return http.status;
}

/* kokoro's path prefix, then tail. */
static const char *kokoro_path(const char *tail) {
    static char path[JPR_PATH_MAX + 32];
    u32 n = 0, k;
    for (k = 0; job.kokoro.path[k]; k++) path[n++] = job.kokoro.path[k];
    for (k = 0; tail[k]; k++) path[n++] = tail[k];
    path[n] = 0;
    return path;
}

/* One request to kokoro; its answer in http (and in response, unless the caller set a sink). JPR_NET_OK
 * for HTTP 200, JPR_NET_HTTP for any other status. */
static u32 kokoro(u32 soc, const char *method, const char *tail, const u8 *b, u32 blen, const struct jpr_deadline *total,
                  u32 *code) {
    u32 n = jpr_http_compose(request, sizeof request, method, job.kokoro.authority, kokoro_path(tail), job.key,
                             b ? "text/plain; charset=utf-8" : 0, b, blen);
    u32 failure;
    if (!n) {
        *code = 6999;
        return JPR_NET_UNREACHABLE;
    }
    failure = exchange(soc, job.kokoro.host, job.kokoro.port, request, n, total, code);
    if (failure) return failure;
    return status_of() == 200 ? JPR_NET_OK : JPR_NET_HTTP;
}

/* kokoro's answer, as text for the panel's thread. */
static void take_text(void) {
    u32 n = http.body_len < JPR_REPLY_MAX ? http.body_len : JPR_REPLY_MAX;
    jpr_memcpy(result.text, response, n);
    result.text_len = n;
}

static void words_sink(void *ctx, const u8 *p, u32 n) {
    jpr_words_feed((struct jpr_words *)ctx, p, n);
}

/* GET /jpreader/words?v=2, kept as kokoro.bin and turned into marks. */
static u32 sync(u32 soc, const struct jpr_deadline *total, u32 *code) {
    struct jpr_words *w = jpr_kokoro_words();
    u32 failure;
    s32 count;
    if (!w) {
        *code = 8001;   /* no memory block for the list */
        return JPR_NET_UNREACHABLE;
    }
    jpr_http_init(&http, response, sizeof response);
    jpr_http_sink_to(&http, words_sink, w);
    failure = kokoro(soc, "GET", "/jpreader/words?v=2", 0, 0, total, code);
    if (failure) return failure;
    count = jpr_kokoro_keep(response, sizeof response, (u32)(jpr_time_ms() / 1000));
    note("words", count);
    if (w->fb_dropped) note("spellings past the table", (s32)w->fb_dropped);   /* their marks are lost */
    if (w->bad_ids) note("ids out of range", (s32)w->bad_ids);
    if (count < 0) {
        *code = 8002;   /* not a whole word list */
        return JPR_NET_UNREACHABLE;
    }
    result.words = (u32)count;
    return JPR_NET_OK;
}

/* An add's route: a word, or a grammar point (stage 17). */
static const char *add_route(u32 kind) {
    return kind == JPR_OUTBOX_GRAMMAR ? "/jpreader/grammar" : "/jpreader/word";
}

/* A word or grammar point to kokoro; its answer in result.text. */
static u32 add(u32 soc, u32 kind, const struct jpr_deadline *total, u32 *code) {
    u32 failure;
    jpr_http_init(&http, response, sizeof response);
    failure = kokoro(soc, "POST", add_route(kind), job.context, job.context_len, total, code);
    if (failure == JPR_NET_OK || failure == JPR_NET_HTTP) take_text();
    return failure;
}

/* Grammar: kokoro explains the sentence; the tagged reply (or kokoro's message) in result.text. */
static u32 grammar(u32 soc, const struct jpr_deadline *total, u32 *code) {
    u32 n = jpr_explain_body(body, sizeof body, job.sentence, job.sentence_len, job.context, job.context_len);
    u32 failure;
    if (!n) {
        *code = 6999;
        return JPR_NET_UNREACHABLE;
    }
    jpr_http_init(&http, response, sizeof response);
    failure = kokoro(soc, "POST", "/jpreader/explain", body, n, total, code);
    if (failure == JPR_NET_OK) {
        u32 got = http.body_len < sizeof response ? http.body_len : sizeof response;
        result.text_len = jpr_reply_keep(result.text, JPR_REPLY_MAX, response, got);
        if (result.text_len < got) note("reply cut: bytes dropped", (s32)(got - result.text_len));   /* its last lines */
    } else if (failure == JPR_NET_HTTP) {
        take_text();
    }
    if (failure == JPR_NET_OK) {   /* remembered for the next time this sentence is asked about */
        u32 key = jpr_reply_key(JPR_EXPLAIN_SOURCE, job.sentence, job.sentence_len);
        n = jpr_reply_pack(request, key, job.sentence, job.sentence_len, result.text, result.text_len);
        if (n) note("reply stored", jpr_fs_write_at(JPR_REPLIES_PATH, jpr_reply_offset(key), request, n));
    }
    return failure;
}

/* Which outbox slots hold adds, read from the file the first time. */
static void outbox_scan(void) {
    u8 head[JPR_OUTBOX_HEAD];
    u32 file = 0, k, got, len;
    if (outbox_scanned) return;
    outbox_scanned = 1;
    if (jpr_fs_open(JPR_OUTBOX_PATH, &file)) return;   /* nothing saved yet */
    for (k = 0; k < JPR_OUTBOX_SLOTS; k++) {
        got = 0;
        if (jpr_fs_read(file, k * JPR_OUTBOX_SLOT, head, sizeof head, &got)) break;
        outbox_seqs[k] = jpr_outbox_head(head, got, &len);
        if (outbox_seqs[k] > outbox_last) outbox_last = outbox_seqs[k];
    }
    jpr_fs_close(file);
}

static u32 outbox_count(void) {
    u32 k, n = 0;
    outbox_scan();
    for (k = 0; k < JPR_OUTBOX_SLOTS; k++) n += outbox_seqs[k] != 0;
    return n;
}

/* Saves an add for later; 1 when it is on the SD card. */
static u32 outbox_put(u32 kind, const u8 *b, u32 len) {
    u32 k, n;
    outbox_scan();
    k = jpr_outbox_slot(outbox_seqs, JPR_OUTBOX_SLOTS);
    if (outbox_seqs[k]) note("outbox full: dropped add", (s32)outbox_seqs[k]);
    n = jpr_outbox_pack(request, outbox_last + 1, kind, b, len);
    if (!n || jpr_fs_write_at(JPR_OUTBOX_PATH, k * JPR_OUTBOX_SLOT, request, n)) return 0;
    outbox_seqs[k] = ++outbox_last;
    return 1;
}

static void outbox_clear(u32 k) {
    static const u8 empty[JPR_OUTBOX_HEAD];
    jpr_fs_write_at(JPR_OUTBOX_PATH, k * JPR_OUTBOX_SLOT, empty, sizeof empty);
    outbox_seqs[k] = 0;
}

/* Sends the outbox's adds, oldest first, each to its kind's route, until one cannot be sent. One kokoro
 * refuses for good (400) is dropped. */
static u32 flush(u32 soc, const struct jpr_deadline *total, u32 *code) {
    u32 failure = JPR_NET_OK, file, got, len;
    s32 k;
    outbox_scan();
    while ((k = jpr_outbox_next(outbox_seqs, JPR_OUTBOX_SLOTS)) >= 0) {
        file = 0;
        got = 0;
        if (jpr_fs_open(JPR_OUTBOX_PATH, &file)) return JPR_NET_UNREACHABLE;
        if (jpr_fs_read(file, (u32)k * JPR_OUTBOX_SLOT, body, JPR_OUTBOX_SLOT, &got)) got = 0;
        jpr_fs_close(file);
        if (!jpr_outbox_head(body, got, &len) || JPR_OUTBOX_HEAD + len > got) {
            outbox_clear((u32)k);   /* unreadable: nothing to send */
            continue;
        }
        jpr_http_init(&http, response, sizeof response);
        failure = kokoro(soc, "POST", add_route(jpr_outbox_kind(body)), body + JPR_OUTBOX_HEAD, len, total, code);
        note("outbox sent", (s32)failure);
        if (failure == JPR_NET_HTTP && http.status == 400) failure = JPR_NET_OK;
        if (failure) break;
        outbox_clear((u32)k);
    }
    return failure;
}

/* The whole job; fills result. */
static void run(void) {
    struct jpr_deadline total, step;
    u32 soc = 0, block = 0, *cmd = jpr_cmdbuf(), code = 0, failure;
    s32 r;

    result.id = job.id;
    result.code = 0;
    result.http_status = 0;
    result.text_len = 0;
    result.detail = 0;
    result.queued = 0;
    result.outbox = 0;
    result.words = 0;
    log_len = 0;
    log_text[0] = 0;
    note("request", (s32)job.id);
    note("kind", (s32)job.kind);
    if (job.kind == JPR_JOB_SYNC) jpr_kokoro_load(response, sizeof response);   /* the last list first: marks offline too */
    jpr_deadline_set(&total, fault() == JPR_FAULT_TIMEOUT ? 1 : jpr_job_deadline_ms(job.kind), &jpr_link.cancel, job.id);

    jpr_deadline_set(&step, 15000, &jpr_link.cancel, job.id);
    failure = wifi(&step, &code, &result.detail);
    if (failure) goto out;
    code = 0;

    if (svcCreateMemoryBlock(&block, JPR_SOCK_BUF_ADDR, JPR_SOCK_BUF_SIZE, 0, 3)) {
        failure = JPR_NET_BUSY;
        goto out;
    }
    if (jpr_service("soc:U", &soc)) {
        failure = JPR_NET_UNREACHABLE;
        code = 1001;
        goto out;
    }
    jpr_ipc_soc_initialize(cmd, JPR_SOCK_BUF_SIZE, block);
    r = jpr_call(soc);
    note("soc initialize", r);
    if (!r && fault() == JPR_FAULT_BUSY) {   /* undo the real Initialize, then refuse */
        jpr_ipc_soc_shutdown(cmd);
        jpr_call(soc);
        r = (s32)SOC_IN_USE;
    }
    if (r) {
        failure = JPR_NET_BUSY;   /* refused: the likely reason is that the game itself is online */
        code = code_of(1, r);
        svcCloseHandle(soc);
        soc = 0;
        goto out;
    }

    switch (job.kind) {
    case JPR_JOB_SYNC: failure = sync(soc, &total, &code); break;
    case JPR_JOB_ADD_WORD: failure = add(soc, JPR_OUTBOX_WORD, &total, &code); break;
    case JPR_JOB_ADD_GRAMMAR: failure = add(soc, JPR_OUTBOX_GRAMMAR, &total, &code); break;
    case JPR_JOB_FLUSH: failure = flush(soc, &total, &code); break;
    default: failure = grammar(soc, &total, &code); break;
    }
out:
    if (soc) {
        jpr_ipc_soc_shutdown(cmd);
        jpr_call(soc);
        svcCloseHandle(soc);
    }
    if (block) svcCloseHandle(block);
    wifi_close();
    if (jpr_link_cancelled(&jpr_link, job.id)) failure = JPR_NET_CANCELLED;
    if ((job.kind == JPR_JOB_ADD_WORD || job.kind == JPR_JOB_ADD_GRAMMAR) && jpr_word_should_queue(failure))
        result.queued = outbox_put(job.kind == JPR_JOB_ADD_GRAMMAR ? JPR_OUTBOX_GRAMMAR : JPR_OUTBOX_WORD, job.context,
                                   job.context_len);
    if (job.kind != JPR_JOB_GRAMMAR) result.outbox = outbox_count();
    result.failure = failure;
    result.code = code;
    note("failure", (s32)failure);
    note("code", (s32)code);
    note("detail", (s32)result.detail);
    if (job.log) jpr_fs_append("/jpreader/log.txt", log_text, log_len);
}

static void main_loop(void *arg) {
    (void)arg;
    for (;;) {
        svcWaitSynchronization(wake, -1);
        while (jpr_link_take(&jpr_link, &job)) {
            run();
            jpr_link_finish(&jpr_link, &result);
        }
    }
}

int jpr_worker_post(const struct jpr_grammar_job *j) {
    if (!thread) {
        if (svcCreateEvent(&wake, 0)) return -1;
        if (svcCreateThread(&thread, main_loop, 0, (u32 *)(stack + STACK_BYTES), PRIORITY, -2)) {
            thread = 0;
            return -1;
        }
    }
    if (jpr_link_post(&jpr_link, j)) return -1;
    svcSignalEvent(wake);
    return 0;
}
