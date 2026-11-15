#include "grammar.h"

#include "util.h"

static int bytes_of(u8 lead) {
    return lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

/* Appends up to n bytes of s, whole UTF-8 characters only, leaving room for the NUL. */
static void append(char *out, u32 cap, u32 *at, const char *s, u32 n) {
    u32 k = 0;
    while (k < n && s[k]) {
        u32 w = (u32)bytes_of((u8)s[k]), j;
        if (k + w > n || *at + w + 1 > cap) break;
        for (j = 0; j < w; j++) out[(*at)++] = s[k + j];
        k += w;
    }
}

void jpr_grammar_message(char *out, u32 cap, u32 failure, u32 code, u32 http_status, const u8 *detail, u32 detail_len) {
    char number[12];
    u32 at = 0, n;
    const char *fixed = 0;
    switch (failure) {
    case JPR_NET_NO_KEY: fixed = "Grammar needs kokoro: run jpreader kokoro-link"; break;
    case JPR_NET_NO_WIFI: fixed = "No Wi-Fi connection"; break;
    case JPR_NET_BUSY: fixed = "Network is busy (the game is online)"; break;
    case JPR_NET_CERT: fixed = "Could not verify kokoro (check the console's date)"; break;
    case JPR_NET_TIMEOUT: fixed = "Timed out"; break;
    case JPR_NET_UNREACHABLE:
        n = jpr_format_u32(number, code);
        append(out, cap, &at, "Could not reach kokoro (code ", 64);
        append(out, cap, &at, number, n);
        append(out, cap, &at, ")", 1);
        break;
    case JPR_NET_HTTP:
        if (http_status == 401 || http_status == 403) {
            fixed = "Token rejected: run jpreader kokoro-link";
        } else if (http_status == 429) {
            fixed = "kokoro is busy, try again soon";
        } else if (http_status >= 500) {
            fixed = "kokoro could not explain that, try again";
        } else {
            n = jpr_format_u32(number, http_status);
            append(out, cap, &at, "kokoro error ", 64);
            append(out, cap, &at, number, n);
            append(out, cap, &at, ": ", 2);
            for (n = 0; n < detail_len && detail[n] != '\n'; n++) {}
            append(out, cap, &at, (const char *)detail, n);   /* its first line, whole characters */
        }
        break;
    default:
        break;
    }
    if (fixed) append(out, cap, &at, fixed, 128);
    out[at] = 0;
}

static int put_line(u8 *out, u32 cap, u32 *at, const char *name, const u8 *v, u32 n) {
    u32 k;
    for (k = 0; name[k]; k++) {
        if (*at >= cap) return -1;
        out[(*at)++] = (u8)name[k];
    }
    for (k = 0; k < n; k++) {
        if (*at >= cap) return -1;
        out[(*at)++] = v[k] == '\n' || v[k] == '\r' || v[k] == '\t' ? (u8)' ' : v[k];
    }
    if (*at >= cap) return -1;
    out[(*at)++] = '\n';
    return 0;
}

u32 jpr_explain_body(u8 *out, u32 cap, const u8 *sentence, u32 slen, const u8 *context, u32 clen) {
    u32 at = 0;
    if (put_line(out, cap, &at, "sentence=", sentence, slen) || put_line(out, cap, &at, "context=", context, clen))
        return 0;
    return at;
}

int jpr_link_idle(const struct jpr_grammar_link *l) {
    return l->posted == l->started && l->started == l->finished;
}

int jpr_link_post(struct jpr_grammar_link *l, const struct jpr_grammar_job *job) {
    if (!jpr_link_idle(l)) return -1;
    jpr_memcpy(&l->job, job, (u32)sizeof *job);
    jpr_barrier();
    l->posted = job->id;
    return 0;
}

int jpr_link_take(struct jpr_grammar_link *l, struct jpr_grammar_job *copy) {
    u32 id = l->posted;
    if (id == l->started) return 0;
    jpr_barrier();
    jpr_memcpy(copy, &l->job, (u32)sizeof *copy);
    jpr_barrier();
    l->started = id;
    return 1;
}

void jpr_link_finish(struct jpr_grammar_link *l, const struct jpr_grammar_result *r) {
    jpr_memcpy(&l->result, r, (u32)sizeof *r);
    jpr_barrier();
    l->finished = l->started;
}

int jpr_link_cancelled(const struct jpr_grammar_link *l, u32 id) {
    return l->cancel == id;
}

int jpr_link_result(const struct jpr_grammar_link *l, u32 id) {
    if (l->finished != id) return 0;
    jpr_barrier();
    return 1;
}

u32 jpr_job_deadline_ms(u32 kind) {
    return kind == JPR_JOB_GRAMMAR ? 90000 : 45000;
}
