#ifndef JPR_OUTBOX_H
#define JPR_OUTBOX_H

#include "types.h"

/* Adds that could not reach kokoro, kept on the SD card until a later request succeeds (spec: kokoro
 * integration 3). A file of fixed slots, like replies.bin, so sending one rewrites only its head: magic,
 * a sequence number (0: the slot is empty; higher is newer), the body's length, the kind; then the body. */
#define JPR_OUTBOX_PATH "/jpreader/outbox.bin"
#define JPR_OUTBOX_SLOTS 200
#define JPR_OUTBOX_SLOT 0x800
#define JPR_OUTBOX_HEAD 16
#define JPR_OUTBOX_MAGIC 0x424F504Au   /* "JPOB" */
#define JPR_OUTBOX_WORD 0              /* POST /jpreader/word; 8B builds left the kind word 0 */
#define JPR_OUTBOX_GRAMMAR 1           /* POST /jpreader/grammar (stage 17) */

/* A slot's bytes; their count, or 0 when seq is 0 or the body does not fit. */
u32 jpr_outbox_pack(u8 *out, u32 seq, u32 kind, const u8 *body, u32 len);
/* A slot's sequence number (0 when it is empty or not a slot), with its body's length in *len. */
u32 jpr_outbox_head(const u8 *head, u32 got, u32 *len);
/* A slot's kind (JPR_OUTBOX_*) from its head; a kind this build does not know is a word add. */
u32 jpr_outbox_kind(const u8 *head);
/* The slot for a new add: an empty one, else the oldest, whose add is dropped. */
u32 jpr_outbox_slot(const u32 *seqs, u32 n);
/* The add to send next: the oldest; -1 when there is none. */
s32 jpr_outbox_next(const u32 *seqs, u32 n);

#endif
