#include "menu.h"

#include "util.h"

void jpr_menu_init(struct jpr_menu *m) {
    jpr_memset(m, 0, (u32)sizeof *m);
}

void jpr_menu_add(struct jpr_menu *m, const void *object, u32 kind, const struct jpr_ident *id, const u16 *text,
                  u32 len) {
    struct jpr_menu_list *b = &m->building;
    struct jpr_menu_item *it;
    u32 k;
    if (b->object != object) {
        b->object = object;
        b->kind = kind;
        b->count = 0;
        b->more = 0;
    }
    if (b->count == JPR_MENU_ITEMS) {
        b->more = 1;
        return;
    }
    it = &b->items[b->count++];
    it->id = *id;
    if (!text) len = 0;
    it->len = (u16)(len > JPR_MENU_TEXT ? JPR_MENU_TEXT : len);
    for (k = 0; k < it->len; k++) it->text[k] = text[k];
}

void jpr_menu_show(struct jpr_menu *m, const void *object, u32 msg_seq) {
    if (!object || m->building.object != object || !m->building.count) return;
    m->shown = m->building;
    m->building.object = 0;
    m->building.count = 0;
    m->seq++;
    m->showing = 1;
    m->msg_seq = msg_seq;
}

void jpr_menu_state(struct jpr_menu *m, const void *object, u32 state) {
    if (object && object == m->shown.object && state >= 4) m->showing = 0;
}

void jpr_menu_gone(struct jpr_menu *m, const void *object) {
    if (!object) return;
    if (object == m->shown.object) {
        m->shown.object = 0;
        m->showing = 0;
    }
    if (object == m->building.object) {
        m->building.object = 0;
        m->building.count = 0;
    }
}

u32 jpr_menu_compose(const struct jpr_menu *m, const u16 *question, u32 qlen, u16 *out, u32 max,
                     u16 starts[JPR_MENU_ITEMS]) {
    u32 n = 0, k, j;
    for (k = 0; k < JPR_MENU_ITEMS; k++) starts[k] = 0xFFFF;
    for (k = 0; k < qlen && n < max; k++) out[n++] = question[k];
    for (k = 0; k < m->shown.count; k++) {
        const struct jpr_menu_item *it = &m->shown.items[k];
        if (n + 2 + it->len > max) break;
        out[n++] = 0x000A;
        out[n++] = JPR_MENU_BULLET;
        starts[k] = (u16)n;
        for (j = 0; j < it->len; j++) out[n++] = it->text[j];
    }
    return n;
}
