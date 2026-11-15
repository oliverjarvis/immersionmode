#ifndef JPR_REPLY_H
#define JPR_REPLY_H

#include "types.h"

/* The Grammar reply's lines (spec: panel redesign 1): "T " the translation (first line only), then per
 * chunk "C " the Japanese, "M " its meaning, "N " the grammar note, and "G " each kokoro grammar point
 * the chunk uses (spec: kokoro integration 2.3, stage 17). A line without a tag is kept as plain text,
 * so a reply in another format still shows every line. */
#define JPR_ITEM_PLAIN 0
#define JPR_ITEM_TRANSLATION 1
#define JPR_ITEM_CHUNK 2
#define JPR_ITEM_MEANING 3
#define JPR_ITEM_NOTE 4
#define JPR_ITEM_GRAMMAR 5     /* "G <key>\t<status>\t<pattern>\t<note>": see jpr_reply_point */
#define JPR_BLOCK_OTHER 0xFFFF /* a point kokoro placed under no chunk: the "Other grammar" block, last */
#define JPR_ITEMS_MAX 64   /* far more lines than a reply's chunks need; later lines are dropped */

/* The reply the 3DS keeps (and stores in replies.bin): kokoro's text cut at the last whole line that fits
 * cap. Its length. */
u32 jpr_reply_keep(u8 *out, u32 cap, const u8 *in, u32 len);

struct jpr_item {
    u16 kind;    /* JPR_ITEM_* */
    u16 block;   /* chunk number from 1; 0 before the first chunk */
    u32 start;   /* the text, after its tag, at reply + start */
    u32 len;
};

/* Splits reply into at most max items, skipping empty lines and empty items; returns how many. A grammar
 * point is in the block of the chunk above it. kokoro writes the points that fit no chunk after the last
 * chunk's own, so those after the last chunk go to JPR_BLOCK_OTHER from the first that does not fit it by
 * kokoro's own test (protocol.ts placeAll: a one-character piece of the pattern ends the chunk, a longer
 * one is in it; brackets dropped, split at ・ and 〜). With no chunk at all, every point is other. */
u32 jpr_reply_items(const u8 *reply, u32 len, struct jpr_item *out, u32 max);

/* A grammar point's status: the values of JPR_KOKORO_NONE..KNOWN (kokoro.h). */
#define JPR_POINT_NONE 0       /* not in kokoro's grammar reviews: the add button */
#define JPR_POINT_NEW 1        /* in them, never studied */
#define JPR_POINT_LEARNING 2
#define JPR_POINT_KNOWN 3      /* in review, or marked known */

struct jpr_point {
    u32 id;                    /* the Bunpro id of "bunpro:<id>"; 0 for "-" or anything else: no add */
    u32 status;                /* JPR_POINT_*; NONE for a word it does not know */
    u32 pattern, pattern_len;  /* at reply + pattern */
    u32 note, note_len;
};

/* A JPR_ITEM_GRAMMAR item's fields; a field the line lacks is empty. */
void jpr_reply_point(const u8 *reply, const struct jpr_item *it, struct jpr_point *p);
/* POST /jpreader/grammar's body: "key=bunpro:<id>\n". Its length, or 0 (no id, or cap too small). */
u32 jpr_point_body(u8 *out, u32 cap, u32 id);

#endif
