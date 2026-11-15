#ifndef JPR_CARD_H
#define JPR_CARD_H

#include "types.h"

#define JPR_CARD_SENSES 3      /* senses a card shows */
#define JPR_ENTRY_MAX 512      /* bytes read for one dict.bin entry; the build keeps entries smaller */
#define JPR_LABEL_MAX 128      /* bytes read for one label */
#define JPR_ENTRY_DICTIONARY 0
#define JPR_ENTRY_PARTICLE 1
#define JPR_ENTRY_NAME 2
#define JPR_ENTRY_LEMMA 3      /* no dictionary match: form and reading only */

struct jpr_text {
    const u8 *p; /* UTF-8, not terminated */
    u32 len;
};

struct jpr_card {
    u32 kind;    /* JPR_ENTRY_* */
    u32 senses;
    u32 jmdict;  /* the word's JMdict entry id (dict.bin v3); 0 for names, particles and lemmas */
    struct jpr_text headword, reading, pos, sense[JPR_CARD_SENSES];
};

/* Reads the u16-prefixed string at p[*at] and moves *at past it. 0, or -1 if it runs past len. */
int jpr_text_read(struct jpr_text *t, const u8 *p, u32 len, u32 *at);
/* A dict.bin v3 entry: u8 kind, u8 senses, u16 0, u32 JMdict id, then the strings. 0, or -1 when the
 * bytes are not a whole entry. */
int jpr_card_parse(struct jpr_card *c, const u8 *entry, u32 len);

#endif
