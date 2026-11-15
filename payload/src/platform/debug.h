#ifndef JPR_DEBUG_H
#define JPR_DEBUG_H

#include "core/types.h"

#define JPR_DBG_MAGIC 0x4452504Au /* "JPRD" */

/* Read, and partly written, by the test driver over the emulator's memory channel. */
struct jpr_debug {
    u32 magic;             /* set once jpr_init has finished */
    u32 protect_result;    /* r0 after svcControlProcessMemory */
    u32 sock_buf_addr;     /* 0 when the allocation failed */
    u32 sock_buf_result;
    u32 app_mem_total;     /* bytes in the application memory region */
    u32 app_mem_used_init; /* bytes used when jpr_init started */
    u32 app_mem_used_now;  /* refreshed every 64 button polls in debug builds */
    u32 polls;             /* button polls seen (debug builds) */
    u32 pad_mask;          /* written by the driver: button bits to hold */
    u32 pad_polls;         /* written by the driver: polls left to hold them */
    u32 touch_x;           /* written by the driver: where to touch */
    u32 touch_y;
    u32 touch_polls;       /* written by the driver: touch polls left to hold it */
    u32 touch_hidden;      /* touch polls kept from the game because the panel was up */
    u32 touch_passed;      /* touch polls the game saw */
    u32 net_fault;         /* written by the driver (debug builds): JPR_FAULT_* for the next requests */
    u32 net_fault_arg;
    u32 link_result;       /* address of the worker's last result (struct jpr_grammar_result) */
    u32 forget_reply;      /* written by the driver (debug builds): the panel forgets its stored reply */
    u32 panel_image_addr;  /* the panel's own image buffer; 0 when the allocation failed */
    u32 kokoro_addr;       /* kokoro's list and marks; 0 when the allocation failed */
    u32 voice_ptr;         /* the borrowed nn::snd voice, 0 for none */
    u32 snd_pos;           /* samples into the voice's current buffer */
    u32 snd_active;        /* 1 while the DSP plays the voice */
    u32 snd_fed;           /* buffers handed to the DSP */
    u32 lin_addr;          /* the payload's linear block, 0 before first use or when it failed */
    u32 dsp_calls;         /* calls to the replaced VA->DSP address check */
    u32 voice_starts;      /* clips (or the spike's tone) started */
    u32 voice_clip;        /* the playing clip's index in voice.bin, 0xFFFFFFFF for none */
    u32 voice_speed;       /* 0 normal, 1 slow */
    u32 voice_next;        /* samples read so far */
    u32 voice_total;       /* samples in the clip */
    u32 voice_fault;       /* JPR_VOICE_FAULT_* */
    u32 voice_test;        /* written by the driver (debug builds): non-zero, every message counts as story file 27 line voice_test-1 */
    u32 voice_ring;        /* the ring's first buffer (linear memory); the others follow every 11,040 bytes */
    u32 mix_sets;          /* the game's player-volume sets through our hook (stage 11) */
    u32 mix_gets;
    u32 mix_applies;       /* times our levels were written to the players */
    u32 mix_saves;         /* sound.txt writes */
    u32 mix_duck;          /* the music's duck, thousandths */
    u32 mix_out0;          /* what player 0 (music) should hold, a float's bits */
    u32 mix_out2;          /* player 2 (effects) */
    u32 mix_shadow0;       /* what the game last asked player 0 for */
    u32 mix_voice;         /* our voice's volume */
    u32 mix_player0;       /* player 0's volume as the game holds it now */
    u32 mix_players;       /* address of player 0's volume, 0 before the sound system is up */
    u32 mix_test;          /* written by the driver (debug builds): 1 the game sets its music players to 0.5, 2 fades them to 1.0 */
    u32 menus_shown;       /* choice menus captured (stage 12) */
    u32 menu_showing;      /* one is on screen, not chosen yet */
    u32 menu_count;        /* its options */
    u32 menu_found;        /* options whose words were found */
    u32 peek_toggle;       /* written by the driver (debug builds): non-zero, the next button poll is a SELECT press */
    u32 hid_addr;          /* the HID hold word SELECT is read from, 0 until found (stage 13) */
    u32 select_presses;    /* SELECT presses seen */
    u32 screen_msgs;       /* message-window texts seen (not the talk window's) */
    u32 screen_texts;      /* texts the last walk of the lower screen found (stage 14) */
    u32 screen_matched;    /* of those the peek listed, the ones strings.bin knows */
    u32 peek_composes;     /* times the peek's text was put together */
};

extern struct jpr_debug jpr_dbg;

/* The test driver's buttons, D-pad and circle pad (debug builds; called from the input hooks in peek.c). */
void jpr_h_pad_tail(u32 *regs);
void jpr_h_cross_tail(u32 *regs);
void jpr_h_stick_tail(u32 *regs);

/* Faults the test driver can inject into grammar requests (debug builds only), for the rows of
 * spec 9.4 that cannot happen for real in the emulator, and to make the others happen on demand. */
#define JPR_FAULT_NO_WIFI 1
#define JPR_FAULT_BUSY 2
#define JPR_FAULT_REFUSED 3       /* connect to 127.0.0.1:1 instead */
#define JPR_FAULT_CLOCK 4         /* the clock says 2000-01-01 */
#define JPR_FAULT_TIMEOUT 5       /* a 1 ms time limit */
#define JPR_FAULT_STATUS 6        /* replace the HTTP status with net_fault_arg */
#define JPR_FAULT_NO_ENTROPY 7    /* ssl:C gives no random bytes */
#define JPR_FAULT_CONNECT 8       /* connect through ac even when the Wi-Fi is already up */

#endif
