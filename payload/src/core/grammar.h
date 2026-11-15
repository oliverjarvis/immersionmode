#ifndef JPR_GRAMMAR_H
#define JPR_GRAMMAR_H

#include "config.h"
#include "sentence.h"
#include "types.h"
#include "url.h"

#define JPR_NET_OK 0
#define JPR_NET_NO_KEY 1        /* kokoro is not linked */
#define JPR_NET_NO_WIFI 2
#define JPR_NET_BUSY 3
#define JPR_NET_UNREACHABLE 4   /* code says which step failed and how */
#define JPR_NET_CERT 5
#define JPR_NET_HTTP 6          /* http_status, and kokoro's message in the text */
#define JPR_NET_TIMEOUT 7
#define JPR_NET_CANCELLED 8
#define JPR_MESSAGE_MAX 96
/* The reply kept, G lines included: as much as a replies.bin slot holds next to the longest sentence
 * (JPR_REPLY_SLOT_BYTES 5120 - JPR_REPLY_HEAD 16 - JPR_SENTENCE_BYTES 600; test_replies checks it). */
#define JPR_REPLY_MAX 4504
#define JPR_BODY_MAX 6144
/* The one-line message for a failure (spec 9.4), NUL-terminated, whole characters only. */
void jpr_grammar_message(char *out, u32 cap, u32 failure, u32 code, u32 http_status, const u8 *detail, u32 detail_len);
/* replies.bin key source: Grammar answers come from kokoro, with their grammar points (stage 17: replies kept
 * before, without them, are asked again). */
#define JPR_EXPLAIN_SOURCE "kokoro-explain-g"
/* POST /jpreader/explain's body: sentence= and context= lines, tabs and newlines as spaces. Its length, or 0. */
u32 jpr_explain_body(u8 *out, u32 cap, const u8 *sentence, u32 slen, const u8 *context, u32 clen);
#define JPR_JOB_GRAMMAR 0
#define JPR_JOB_SYNC 1          /* GET kokoro's word list */
#define JPR_JOB_ADD_WORD 2      /* POST a word to kokoro, or save it in the outbox */
#define JPR_JOB_FLUSH 3         /* send the outbox's adds */
#define JPR_JOB_ADD_GRAMMAR 4   /* POST a grammar point to kokoro, or save it in the outbox (stage 17) */
/* A job's whole time limit: Grammar waits on kokoro asking a model, the others on kokoro alone. */
u32 jpr_job_deadline_ms(u32 kind);
/* What the worker needs for one request; the panel's thread fills it in. */
struct jpr_grammar_job {
    u32 id;
    u32 kind;               /* JPR_JOB_* */
    u32 log;                /* config.txt has debug=1: the worker adds each step's result to jpreader/log.txt */
    char key[JPR_TOKEN_MAX];  /* the device token for kokoro */
    struct jpr_url kokoro;  /* kokoro jobs: where kokoro is */
    u8 sentence[JPR_SENTENCE_BYTES];
    u32 sentence_len;
    u8 context[JPR_CONTEXT_BYTES];   /* JPR_JOB_ADD_WORD and _ADD_GRAMMAR: the request's body */
    u32 context_len;
};
/* The worker's answer: the reply, or kokoro's error message. Read by the test driver: keep the layout. */
struct jpr_grammar_result {
    u32 id, failure, code, http_status, text_len;
    u8 text[JPR_REPLY_MAX];
    u32 detail;             /* the 3DS's own result code for a failed step, for the debug line */
    u32 queued;             /* the add jobs: kokoro was out of reach, and the add is saved in the outbox */
    u32 outbox;             /* kokoro jobs: adds waiting in the outbox afterwards */
    u32 words;              /* JPR_JOB_SYNC: words in the list kept */
};
/* Shared by the panel's thread and the worker; each field has one writer (see grammar.c). */
struct jpr_grammar_link {
    volatile u32 posted;    /* id of the last job posted (panel's thread) */
    volatile u32 started;   /* id the worker took (worker) */
    volatile u32 finished;  /* id whose result is ready (worker) */
    volatile u32 cancel;    /* id to abandon (panel's thread) */
    struct jpr_grammar_job job;
    struct jpr_grammar_result result;
};
int jpr_link_idle(const struct jpr_grammar_link *l);
int jpr_link_post(struct jpr_grammar_link *l, const struct jpr_grammar_job *job);      /* 0, or -1 busy */
int jpr_link_take(struct jpr_grammar_link *l, struct jpr_grammar_job *copy);           /* worker: 1 if a job came */
void jpr_link_finish(struct jpr_grammar_link *l, const struct jpr_grammar_result *r);  /* worker */
int jpr_link_cancelled(const struct jpr_grammar_link *l, u32 id);
int jpr_link_result(const struct jpr_grammar_link *l, u32 id);                         /* 1 when id's result is ready */

#endif
