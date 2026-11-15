#ifndef JPR_PLATFORM_MIX_H
#define JPR_PLATFORM_MIX_H

#include "core/mix.h"

/* The game's music and effects at the panel's levels, and the music ducked under a voice clip (stage 11).
 * Main thread only, every bottom-screen frame: loads sound.txt once, writes it when the Sound view asks. */
void jpr_mix_frame(void);

/* Replace the game's player-volume setter and getter (hooks.py): on whichever thread the game calls them, with
 * the game's sound lock held. */
void jpr_h_player_vol_set(void *sys, u32 label, u32 vbits);
u32 jpr_h_player_vol_get(void *sys, u32 label);

/* A game function taking (a0, a1, a float): mix_asm.s. */
void jpr_call_f32(u32 fn, u32 a0, u32 a1, u32 fbits);

#endif
