#ifndef JPR_PLATFORM_SCREEN_H
#define JPR_PLATFORM_SCREEN_H

#include "core/annot.h"
#include "core/capture.h"
#include "core/screen.h"

/* What the screens show, for the peek (stages 13 and 14; docs/stage13-battle-findings.md). */

extern struct jpr_screen jpr_screen;

/* The peek for this frame: the latest window message (or, if a talk message came after it, that one), then a row for
 * each button or group of texts the screens show, the lower screen's first; the text holds each row's Japanese, with
 * every piece's words, and *out_rows the rest: the literal and English lines strings.bin has for them, a move's
 * type, and what is not Japanese (stage 15). *key changes whenever any of it does. */
void jpr_screen_peek(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language,
                     const u16 **text, u32 *len, const struct jpr_unit **out, u32 *out_count, u32 *key,
                     const struct jpr_peek_rows **out_rows);

/* Hooks (hooks.py) */
void jpr_h_msgwin_set(void *msgwin, const void *strbuf);
/* gfl2::lyt draws a LytWk on a display (stage 14); once a frame, at the lower screen's present, the list is done. */
void jpr_h_lyt_draw(const void *wk, u32 display);
void jpr_screen_frame(void);

#endif
