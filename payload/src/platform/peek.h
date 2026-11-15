#ifndef JPR_PEEK_H
#define JPR_PEEK_H

#include "core/types.h"

/* The peek (stage 13; research: docs/stage13-battle-findings.md): SELECT, which the game never reads, swaps the bottom
 * screen to the panel, which shows what the game has on its screens (a battle's message, the moves); SELECT again
 * gives the screen back. While it is open the game sees no buttons, D-pad or circle pad (touches are already kept
 * from it while the panel is up). */

/* The input polls' exits (hooks.py): the button word in regs[1], the D-pad vector in regs[1], the circle pad's in
 * regs[5]. Debug builds also press buttons for the test driver here (debug.c). */
void jpr_h_pad_exit(u32 *regs);
void jpr_h_cross_exit(u32 *regs);
void jpr_h_stick_exit(u32 *regs);

#endif
