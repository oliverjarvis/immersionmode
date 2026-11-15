#ifndef JPR_CHOICES_H
#define JPR_CHOICES_H

#include "core/annot.h"
#include "core/capture.h"
#include "core/menu.h"

/* Choice menus on the bottom screen (stage 12; docs/stage12-choices-findings.md). */

extern struct jpr_menu jpr_menu;

/* The panel's composed text and its units, shared by the choices and the peek (only one shows at a time): `owner`
 * says which composed it last, so the other composes again before using it. */
#define JPR_SHOWN_CHOICES 1
#define JPR_SHOWN_PEEK 2
#define JPR_PIECE_UNITS 32           /* units one option or name can have */
extern u16 jpr_shown_text[JPR_MSG_MAX];
extern struct jpr_unit jpr_shown_units[JPR_UNITS_MAX];
extern u32 jpr_shown_owner;

/* Each frame: a field menu's state word says when the player has chosen. */
void jpr_choices_frame(void);
/* What the panel shows for msg (the latest message): with a menu showing under it, the message and its options,
 * each option's words found by its own origin; else msg itself and the units given. *key changes whenever the
 * composed text does. */
void jpr_choices_shown(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language,
                       const u16 **text, u32 *len, const struct jpr_unit **out, u32 *out_count, u32 *key);

/* Hooks (hooks.py): App::Tool::ListMenu and app::tool::MenuWindow. */
void jpr_h_listmenu_add(void *menu, u32 id, const void *src);
void jpr_h_listmenu_start(void *menu);
void jpr_h_menuwin_add_buf(void *window, const void *src);
void jpr_h_menuwin_start(void *window);
void jpr_h_menuwin_end(void *window);

#endif
