#include "screen.h"

#include "layout.h"
#include "util.h"

void jpr_screen_init(struct jpr_screen *s) {
    jpr_memset(s, 0, (u32)sizeof *s);
}

void jpr_screen_message(struct jpr_screen *s, const struct jpr_ident *id, const u16 *text, u32 len, u32 talk_seq) {
    u32 k;
    if (!text) len = 0;
    len = jpr_text_cut(text, len, JPR_SCREEN_TEXT);
    for (k = 0; k < len; k++) s->msg[k] = text[k];
    s->msg_len = (u16)len;
    s->msg_id = *id;
    s->msg_after = talk_seq;
    s->msg_seq++;
    s->version++;
}

u32 jpr_compose(struct jpr_piece *pieces, u32 n, u16 *out, u32 max) {
    u32 len = 0, k, j;
    for (k = 0; k < n; k++) {
        struct jpr_piece *p = &pieces[k];
        u32 need = p->len + (p->line && len ? 1 : 0);
        p->start = JPR_PIECE_NONE;
        if (len + need > max) continue;
        if (p->line && len) out[len++] = 0x000A;
        p->start = (u16)len;
        for (j = 0; j < p->len; j++) out[len++] = p->text[j];
    }
    return len;
}

void jpr_drawn_add(struct jpr_drawn *d, const void *wk, u32 display) {
    struct jpr_drawn_frame *f = &d->frame[d->cur & 1];
    u32 k;
    for (k = 0; k < f->n; k++)
        if (f->wk[k] == wk && f->display[k] == display) return;
    if (f->n >= JPR_DRAWN) return;
    f->wk[f->n] = wk;
    f->display[f->n] = (u8)display;
    f->n++;
}

const struct jpr_drawn_frame *jpr_drawn_flip(struct jpr_drawn *d) {
    const struct jpr_drawn_frame *done = &d->frame[d->cur & 1];
    d->cur ^= 1;
    d->frame[d->cur].n = 0;
    return done;
}

#define PICK_GROUPS 32                /* groups remembered to find one shown twice */

u32 jpr_screen_pick(const struct jpr_seen_text *t, u32 n, u32 msg_hash, u16 *keep, u8 *starts, u32 max) {
    u32 sigs[PICK_GROUPS], groups = 0, kept = 0, k = 0;
    while (k < n && kept < max) {
        u32 end = k, first = kept, sig = 0x811C9DC5u, j, i, again = 0;
        while (end < n && t[end].group == t[k].group) end++;
        for (j = k; j < end; j++) {            /* the run's texts worth listing, each once */
            int twice = 0;
            if (!t[j].hash || t[j].hash == msg_hash) continue;
            for (i = first; i < kept; i++)
                if (t[keep[i]].hash == t[j].hash) twice = 1;
            if (twice || kept >= max) continue;
            keep[kept] = (u16)j;
            starts[kept] = (u8)(kept == first);
            kept++;
            sig = (sig ^ t[j].hash) * 0x01000193u;
        }
        for (i = 0; i < groups; i++)
            if (sigs[i] == sig) again = 1;
        if (kept > first && again) kept = first;   /* a group seen already: drop it again */
        else if (kept > first && groups < PICK_GROUPS) sigs[groups++] = sig;
        k = end;
    }
    return kept;
}

void jpr_peek_rows_clear(struct jpr_peek_rows *r) {
    r->n = 0;
    r->header = 0;
    r->used = 0;
    jpr_memset(r->row, 0, (u32)sizeof r->row);
}

static u32 utf8_bytes(u16 u) {
    return u < 0x80 ? 1 : u < 0x800 ? 2 : 3;
}

int jpr_peek_append(struct jpr_peek_rows *r, struct jpr_peek_str *s, const u16 *text, u32 len, const char *sep) {
    u32 need = 0, at, k;
    if (s->len && (u32)s->at + s->len != r->used) return -1;
    if (s->len)
        for (k = 0; sep[k]; k++) need++;
    for (k = 0; k < len; k++) need += utf8_bytes(text[k]);
    if (r->used + need > JPR_PEEK_POOL) return -1;
    if (!s->len) s->at = (u16)r->used;
    at = r->used;
    if (s->len)
        for (k = 0; sep[k]; k++) r->pool[at++] = (u8)sep[k];
    for (k = 0; k < len; k++) {
        u16 u = text[k];
        if (u < 0x80) {
            r->pool[at++] = (u8)u;
        } else if (u < 0x800) {
            r->pool[at++] = (u8)(0xC0 | (u >> 6));
            r->pool[at++] = (u8)(0x80 | (u & 0x3F));
        } else {
            r->pool[at++] = (u8)(0xE0 | (u >> 12));
            r->pool[at++] = (u8)(0x80 | ((u >> 6) & 0x3F));
            r->pool[at++] = (u8)(0x80 | (u & 0x3F));
        }
    }
    s->len = (u16)(s->len + need);
    r->used = at;
    return 0;
}

u32 jpr_text_cut(const u16 *text, u32 len, u32 max) {
    u32 i = 0;
    while (i < len) {
        u32 n = 1;
        if (text[i] == JPR_TAG_MARK) {
            if (i + 1 >= len) break;
            n = 2u + text[i + 1];
        }
        if (i + n > max || i + n > len) break;
        i += n;
    }
    return i;
}

u32 jpr_plain_text(const u16 *text, u32 len, u16 *out, u32 max) {
    u32 i = 0, n = 0;
    while (i < len && n < max) {
        u16 u = text[i];
        if (u == JPR_TAG_MARK) {
            if (i + 1 >= len) break;
            i += 2u + text[i + 1];
            continue;
        }
        out[n++] = u == 0x000A ? 0x0020 : u;
        i++;
    }
    return n;
}

int jpr_screen_japanese(const u16 *text, u32 len) {
    u32 k = 0;
    u16 u;
    while (jpr_next_unit(text, &k, len, &u)) {
        if ((u >= 0x3041 && u <= 0x30FA) || u == 0x3005 || (u >= 0x3400 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF))
            return 1;
    }
    return 0;
}
