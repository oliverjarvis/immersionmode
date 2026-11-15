#ifndef JPR_SENTENCE_H
#define JPR_SENTENCE_H

#include "types.h"

#define JPR_SENTENCE_MAX 200          /* characters */
#define JPR_CONTEXT_MAX 600
#define JPR_SENTENCE_BYTES (JPR_SENTENCE_MAX * 3)
#define JPR_CONTEXT_BYTES (JPR_CONTEXT_MAX * 3)
#define JPR_EXCERPT_UNITS 1024        /* text units considered: a captured message holds at most this */
struct jpr_excerpt {
    u8 sentence[JPR_SENTENCE_BYTES];
    u32 sentence_len;                 /* bytes */
    u8 context[JPR_CONTEXT_BYTES];
    u32 context_len;
};
/* The sentence around text[from, to) (the block on screen) and the message around it, as UTF-8. */
void jpr_excerpt(struct jpr_excerpt *e, const u16 *text, u32 len, u32 from, u32 to);

#endif
