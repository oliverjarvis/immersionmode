#ifndef JPR_REPLIES_H
#define JPR_REPLIES_H

#include "types.h"

/* Grammar replies kept on the SD card, so a sentence asked about before comes back without a request:
 * a file of fixed slots, each holding one sentence and its reply; a new reply takes its sentence's slot. */
#define JPR_REPLIES_PATH "/jpreader/replies.bin"
#define JPR_REPLY_SLOTS 256
#define JPR_REPLY_SLOT_BYTES 0x1400        /* header, sentence and reply: 1.25 MB in all */
#define JPR_REPLY_HEAD 16                  /* magic, key, sentence and reply lengths, reserved */
#define JPR_REPLY_MAGIC 0x31525052u        /* "RPR1" */

/* FNV-1a over where replies come from (JPR_EXPLAIN_SOURCE) and the sentence: a new source asks again. */
u32 jpr_reply_key(const char *source, const u8 *sentence, u32 slen);
/* Where the key's slot starts in the file. */
u32 jpr_reply_offset(u32 key);
/* The slot's bytes for a reply; their count, or 0 when they do not fit a slot. */
u32 jpr_reply_pack(u8 *out, u32 key, const u8 *sentence, u32 slen, const u8 *reply, u32 rlen);
/* head holds the first got bytes of a slot. 1 when the slot holds this sentence's reply under this key,
 * with the reply's length in *rlen; the reply follows at JPR_REPLY_HEAD + slen. */
int jpr_reply_head(const u8 *head, u32 got, u32 key, const u8 *sentence, u32 slen, u32 *rlen);

#endif
