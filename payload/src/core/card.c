#include "card.h"

int jpr_text_read(struct jpr_text *t, const u8 *p, u32 len, u32 *at) {
    u32 n;
    if (*at + 2 > len) return -1;
    n = (u32)p[*at] | ((u32)p[*at + 1] << 8);
    if (n > len - *at - 2) return -1;
    t->p = p + *at + 2;
    t->len = n;
    *at += 2 + n;
    return 0;
}

int jpr_card_parse(struct jpr_card *c, const u8 *entry, u32 len) {
    u32 at = 8, k, senses;
    if (len < 8 || entry[0] > JPR_ENTRY_LEMMA) return -1;
    c->kind = entry[0];
    senses = entry[1];
    c->jmdict = (u32)entry[4] | ((u32)entry[5] << 8) | ((u32)entry[6] << 16) | ((u32)entry[7] << 24);
    c->senses = 0;
    if (jpr_text_read(&c->headword, entry, len, &at) || jpr_text_read(&c->reading, entry, len, &at) ||
        jpr_text_read(&c->pos, entry, len, &at))
        return -1;
    for (k = 0; k < senses && k < JPR_CARD_SENSES; k++) {
        if (jpr_text_read(&c->sense[k], entry, len, &at)) return -1;
        c->senses++;
    }
    return 0;
}
