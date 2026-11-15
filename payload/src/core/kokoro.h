#ifndef JPR_KOKORO_H
#define JPR_KOKORO_H

#include "types.h"

/* kokoro's word list on the 3DS (spec: word identity 6). Words with a JMdict id are kept as (id << 2) | state
 * in one sorted table; kokoro cards with no id come by spelling, kept as (hash & ~3) | state in a second.
 * Marks per dict.bin entry come from ids.bin: the entry's id first, then the hashes of its word's spellings. */
#define JPR_KOKORO_WORDS 8000                      /* the most kokoro sends */
#define JPR_KOKORO_FALLBACK 2048                   /* kokoro cards with no id the 3DS keeps */
#define JPR_KOKORO_ID_LIMIT (1u << 24)             /* JMdict ids are 7 digits, below this; id << 2 fits */
#define JPR_KOKORO_ENTRIES 32768                   /* dict.bin entries the marks cover */
#define JPR_KOKORO_BITS (JPR_KOKORO_ENTRIES / 4)   /* bytes of marks: two bits an entry */
#define JPR_KOKORO_NONE 0
#define JPR_KOKORO_NEW 1                           /* in kokoro, never studied: after the person's own add */
#define JPR_KOKORO_LEARNING 2
#define JPR_KOKORO_KNOWN 3
#define JPR_KOKORO_QUEUED 4                        /* the panel only: saved in the outbox for later */
#define JPR_WORDS_LINE 256                         /* longer lines are counted but not kept */

/* kokoro.bin: "JPKW", version 2, id count, fallback count, seconds since 1900 when synced; then the sorted id
 * table and the sorted fallback table. */
#define JPR_KOKORO_PATH "/jpreader/kokoro.bin"
#define JPR_KOKORO_MAGIC 0x574B504Au
#define JPR_KOKORO_HEAD 20
/* ids.bin (written by the build): "JPRI", version 2, entry count, dict.bin's stamp, pair count; then each
 * entry's JMdict id (0 when kokoro could not hold it); then (entry, spelling hash) pairs sorted by entry. */
#define JPR_IDS_PATH "/jpreader/ids.bin"
#define JPR_IDS_MAGIC 0x4952504Au
#define JPR_IDS_HEAD 20

u32 jpr_kokoro_hash(const u8 *p, u32 n);

/* Reads GET /jpreader/words?v=2 as it arrives: "KOKORO-WORDS 2 <count>", then "<id>\t<state>" or
 * "~<spelling>\t<state>" lines. */
struct jpr_words {
    u32 *table, *fallback;
    u32 cap, count, fb_cap, fb_count, fb_dropped, bad_ids;
    u32 expected, lines, state, line_len, too_long;
    u8 line[JPR_WORDS_LINE];
};
void jpr_words_init(struct jpr_words *w, u32 *table, u32 cap, u32 *fallback, u32 fb_cap);
void jpr_words_feed(struct jpr_words *w, const u8 *p, u32 n);
/* After the last byte: sorts both tables and merges repeats, keeping the best state. The number of ids
 * kept (w->fb_count for spellings), or -1 when the header never came, the lines fell short of it, or a
 * line was neither "<id>\t" nor "~<spelling>\t". An id of 2^24 or more is skipped (w->bad_ids). */
s32 jpr_words_finish(struct jpr_words *w);
/* JPR_KOKORO_* of a key in a sorted table (the low two bits of `key` are ignored); NONE when absent. */
u32 jpr_words_find(const u32 *table, u32 count, u32 key);
/* JPR_KOKORO_* of a JMdict id in the id table. */
u32 jpr_words_find_id(const u32 *table, u32 count, u32 id);

void jpr_kokoro_head_pack(u8 *out, u32 count, u32 fb_count, u32 synced);
/* The id count in a kokoro.bin header (and *fb the fallback count); -1 when it is not one, or either
 * table holds more than its cap. */
s32 jpr_kokoro_head_counts(const u8 *head, u32 got, u32 cap, u32 fb_cap, u32 *fb);
/* The entry count in an ids.bin header made for the dict.bin with this stamp (and *pairs the pair count);
 * -1 otherwise (and for stamp 0). */
s32 jpr_ids_count(const u8 *head, u32 got, u32 stamp, u32 *pairs);

/* Sets the marks of entries first .. first + n - 1 from their ids (0: no word kokoro could hold). */
void jpr_kokoro_mark_ids(u8 *bits, u32 first, const u32 *ids, u32 n, const u32 *table, u32 count);
/* Raises an entry's mark where one of its spellings, n (entry, hash) pairs, is a fallback word. */
void jpr_kokoro_mark_forms(u8 *bits, const u32 *pairs, u32 n, const u32 *fallback, u32 count);
/* An entry's mark; JPR_KOKORO_NONE past the `entries` marked. */
u32 jpr_kokoro_state(const u8 *bits, u32 entries, u32 entry);

#endif
