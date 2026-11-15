#ifndef JPR_DISPLAY_H
#define JPR_DISPLAY_H

#include "core/panel.h"

extern struct jpr_panel jpr_panel; /* jpr_panel.st is read by the test driver */
extern const void *jpr_talk;       /* the talk window on screen, or null */

void jpr_display_init(void);
/* Reads /jpreader/font.bin the first time a Japanese message is set. */
void jpr_display_ensure_font(void);
/* After each committed message: the font, the data files, and the message's units. */
void jpr_display_committed(void);

#endif
