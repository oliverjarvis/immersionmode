#ifndef JPR_WORD_H
#define JPR_WORD_H

#include "card.h"
#include "kokoro.h"
#include "types.h"

/* Adding the selected word to kokoro (spec: kokoro integration 2.2, 4). */
#define JPR_ADD_NONE 0
#define JPR_ADD_ASKING 1     /* "Adding…" */
#define JPR_ADD_DONE 2       /* "Added" */
#define JPR_ADD_QUEUED 3     /* "Saved: will send later" */
#define JPR_ADD_REJECTED 4   /* kokoro refused the device token */
#define JPR_ADD_FAILED 5     /* "kokoro error"; the button stays, to try again */
#define JPR_WORD_BODY_MAX 1600

/* POST /jpreader/word's body for a card: front, reading, meaning (the first two senses joined by "; ", as
 * kokoro's own lookups make it), pos and sentence, each on one line (newlines and tabs become spaces).
 * Its length, or 0 when it does not fit cap. */
u32 jpr_word_body(u8 *out, u32 cap, const struct jpr_card *c, const u8 *sentence, u32 slen);
/* kokoro's answer: "OK new" is JPR_KOKORO_NEW, "OK exists <state>" that state; 0 for anything else. */
u32 jpr_word_reply(const u8 *text, u32 len);
/* Is this a failure to reach kokoro at all, so the add waits in the outbox? */
int jpr_word_should_queue(u32 failure);
/* What became of an add (JPR_ADD_*), from the worker's failure, HTTP status, whether it saved the add in
 * the outbox, and kokoro's answer; *state is the word's JPR_KOKORO_* state to show from now on, 0 for none. */
u32 jpr_word_outcome(u32 failure, u32 http_status, u32 queued, const u8 *text, u32 len, u32 *state);

#endif
