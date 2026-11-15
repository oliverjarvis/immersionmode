#include "choices.h"

#include "data.h"
#include "debug.h"
#include "game.h"

/* App::Tool::ListMenu (field and script menus) */
#define LISTMENU_STATE 0x35C          /* u16: 1-3 showing, 4 and up chosen and closing, 6 gone */
/* app::tool::MenuWindow (app menus, the opening's prompts) */
#define MENUWIN_ITEMS 0xD8            /* StrBuf * every 16 bytes */
#define MENUWIN_ITEM_STRIDE 16
#define MENUWIN_COUNT 0x158           /* u8 */
#define MENUWIN_MAX 8

struct jpr_menu jpr_menu;

u16 jpr_shown_text[JPR_MSG_MAX];
struct jpr_unit jpr_shown_units[JPR_UNITS_MAX];
u32 jpr_shown_owner;
static struct jpr_unit item_units[JPR_PIECE_UNITS];
static u32 shown_len, shown_count, composed_seq, composed_msg, composed_units;

static const u16 *strbuf_text(const void *buf) {
    return *(const u16 *const *)((const u8 *)buf + STRBUF_PTR);
}

static u32 strbuf_len(const void *buf) {
    return *(const u16 *)((const u8 *)buf + STRBUF_LEN);
}

void jpr_h_listmenu_add(void *menu, u32 id, const void *src) {
    struct jpr_ident from;
    (void)id;
    if (!src) return;
    from = jpr_cap_ident_of(src);            /* ListMenu keeps a copy, never tagged: the origin is the source's */
    jpr_menu_add(&jpr_menu, menu, JPR_MENU_LIST, &from, strbuf_text(src), strbuf_len(src));
}

void jpr_h_listmenu_start(void *menu) {
    jpr_menu_show(&jpr_menu, menu, jpr_cap.seq);
    jpr_dbg.menus_shown = jpr_menu.seq;
}

static void *menuwin_item(void *window, u32 i) {
    return *(void **)((u8 *)window + MENUWIN_ITEMS + i * MENUWIN_ITEM_STRIDE);
}

/* AddItem(const StrBuf &src): the window copies src into its own item buffer, which takes src's origin. */
void jpr_h_menuwin_add_buf(void *window, const void *src) {
    u32 n = *((const u8 *)window + MENUWIN_COUNT);
    void *dst;
    if (n >= MENUWIN_MAX || !src) return;
    dst = menuwin_item(window, n);
    if (dst) jpr_cap_copy_ident(dst, src);
}

/* StartMenu / StartMenuDisplay: the window's items are all there. */
void jpr_h_menuwin_start(void *window) {
    u32 n = *((const u8 *)window + MENUWIN_COUNT), i;
    if (n > MENUWIN_MAX) n = MENUWIN_MAX;
    for (i = 0; i < n; i++) {
        void *buf = menuwin_item(window, i);
        struct jpr_ident from;
        if (!buf) continue;
        from = jpr_cap_ident_of(buf);
        jpr_menu_add(&jpr_menu, window, JPR_MENU_WINDOW, &from, strbuf_text(buf), strbuf_len(buf));
    }
    jpr_menu_show(&jpr_menu, window, jpr_cap.seq);
    jpr_dbg.menus_shown = jpr_menu.seq;
}

void jpr_h_menuwin_end(void *window) {
    jpr_menu_gone(&jpr_menu, window);
}

void jpr_choices_frame(void) {
    const struct jpr_menu_list *l = &jpr_menu.shown;
    if (jpr_menu.showing && l->kind == JPR_MENU_LIST && l->object)
        jpr_menu_state(&jpr_menu, l->object, *(const volatile u16 *)((const u8 *)l->object + LISTMENU_STATE));
    jpr_dbg.menu_showing = jpr_menu.showing;
    jpr_dbg.menu_count = jpr_menu.showing ? l->count : 0;
}

/* The message and the options under it, once per menu: each option's units found by its own origin and moved to
 * where it sits in the composed text. */
static void compose(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language) {
    u16 starts[JPR_MENU_ITEMS];
    u32 k, j, found = 0;
    shown_len = jpr_menu_compose(&jpr_menu, msg->text, msg->len, jpr_shown_text, JPR_MSG_MAX, starts);
    shown_count = count < JPR_UNITS_MAX ? count : JPR_UNITS_MAX;
    for (k = 0; k < shown_count; k++) jpr_shown_units[k] = units[k];
    for (k = 0; k < jpr_menu.shown.count && starts[k] != 0xFFFF; k++) {
        const struct jpr_menu_item *it = &jpr_menu.shown.items[k];
        u32 room = JPR_UNITS_MAX - shown_count, n;
        n = jpr_data_units(&it->id, it->text, it->len, language, item_units, room < JPR_PIECE_UNITS ? room : JPR_PIECE_UNITS);
        if (n) found++;
        for (j = 0; j < n; j++) {
            item_units[j].start = (u16)(item_units[j].start + starts[k]);
            jpr_shown_units[shown_count++] = item_units[j];
        }
    }
    jpr_dbg.menu_found = found;
    composed_seq = jpr_menu.seq;
    composed_msg = msg->seq;
    composed_units = count;
    jpr_shown_owner = JPR_SHOWN_CHOICES;
}

void jpr_choices_shown(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language,
                       const u16 **text, u32 *len, const struct jpr_unit **out, u32 *out_count, u32 *key) {
    if (!jpr_menu.showing || jpr_menu.msg_seq != msg->seq) {
        *text = msg->text;
        *len = msg->len;
        *out = units;
        *out_count = count;
        *key = 0;
        return;
    }
    if (composed_seq != jpr_menu.seq || composed_msg != msg->seq || composed_units != count ||
        jpr_shown_owner != JPR_SHOWN_CHOICES)
        compose(msg, units, count, language);
    *text = jpr_shown_text;
    *len = shown_len;
    *out = jpr_shown_units;
    *out_count = shown_count;
    *key = jpr_menu.seq;
}
