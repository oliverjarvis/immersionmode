#ifndef JPR_WORKER_H
#define JPR_WORKER_H

#include "core/grammar.h"

/* The grammar request thread (spec 9.3). */
extern struct jpr_grammar_link jpr_link;
/* Hands a job to the worker, starting its thread the first time. 0, or -1 when the worker is busy or
 * cannot be started. */
int jpr_worker_post(const struct jpr_grammar_job *job);

#endif
