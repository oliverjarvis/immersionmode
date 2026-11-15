#ifndef JPR_MENU_H
#define JPR_MENU_H

#include "capture.h"
#include "types.h"

/* Choice menus (stage 12; research: docs/stage12-choices-findings.md): the options of the menu the game shows
 * under a question (はい/いいえ, a script's choices), captured as the game adds them, so the bottom screen can show
 * them under the question with their words. Pure logic; platform hooks feed it.
 *
 * The game builds a menu item by item (App::Tool::ListMenu::AddItem, or app::tool::MenuWindow's items), then
 * starts it. Each item's text comes with its origin (the capture's ident of the buffer it was copied from), so
 * its words are found as a message's are. */

#define JPR_MENU_ITEMS 12            /* options kept; a longer menu sets `more` */
#define JPR_MENU_TEXT 40             /* UTF-16 units kept per option */
#define JPR_MENU_LIST 1              /* App::Tool::ListMenu: field and script menus */
#define JPR_MENU_WINDOW 2            /* app::tool::MenuWindow: app menus, the opening's prompts */
#define JPR_MENU_BULLET 0x30FB       /* ・ before each option in the composed text */

struct jpr_menu_item {
    struct jpr_ident id;
    u16 len;
    u16 text[JPR_MENU_TEXT];
};

struct jpr_menu_list {
    const void *object;              /* the game's menu; 0 for none */
    u32 kind;                        /* JPR_MENU_* */
    u32 count;
    u32 more;                        /* the game added more than JPR_MENU_ITEMS */
    struct jpr_menu_item items[JPR_MENU_ITEMS];
};

struct jpr_menu {
    u32 seq;                         /* menus shown so far: the one showing is number seq */
    u32 showing;                     /* on screen, and the player has not chosen yet */
    u32 msg_seq;                     /* the capture's message (the question) when it was shown */
    struct jpr_menu_list shown;
    struct jpr_menu_list building;   /* items added to a menu not shown yet */
};

void jpr_menu_init(struct jpr_menu *m);
/* An item added to `object`: a different object starts a new menu. Text past JPR_MENU_TEXT is cut. */
void jpr_menu_add(struct jpr_menu *m, const void *object, u32 kind, const struct jpr_ident *id, const u16 *text,
                  u32 len);
/* `object` is on screen now, with the items added to it; msg_seq is the capture's latest message. */
void jpr_menu_show(struct jpr_menu *m, const void *object, u32 msg_seq);
/* A ListMenu's state word, polled each frame (1-3 showing; 4 and up: the player has chosen). */
void jpr_menu_state(struct jpr_menu *m, const void *object, u32 state);
/* `object` was ended or destroyed: never read again. */
void jpr_menu_gone(struct jpr_menu *m, const void *object);
/* The question's text with each option on a line of its own after it, as "・option"; the start of each option's
 * text (after its bullet) in starts[]. Returns the length, at most max; options that do not fit are left out
 * (their start is 0xFFFF). */
u32 jpr_menu_compose(const struct jpr_menu *m, const u16 *question, u32 qlen, u16 *out, u32 max,
                     u16 starts[JPR_MENU_ITEMS]);

#endif
