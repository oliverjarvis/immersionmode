#ifndef JPR_SND_H
#define JPR_SND_H

#include "core/types.h"

/* One voice of the game's own sound library (nn::snd::CTR), borrowed to play voice clips (spec: voice 4;
 * research: docs/stage10-snd-findings.md). Every call takes the sound thread's lock. */

#define JPR_WAVE_FREE 0
#define JPR_WAVE_WAIT 1
#define JPR_WAVE_PLAY 2
#define JPR_WAVE_DONE 3

struct jpr_wave {              /* nn::snd::CTR::WaveBuffer, 0x18 bytes */
    const void *address;       /* in the payload's linear block (jpr_lin_alloc) */
    s32 samples;
    const void *adpcm;         /* unused: 0 */
    u32 user;
    u8 loop;
    volatile u8 status;        /* JPR_WAVE_*: the sound thread writes it */
    u16 id;
    struct jpr_wave *next;     /* the library's */
};
_Static_assert(sizeof(struct jpr_wave) == 0x18, "nn::snd WaveBuffer is 0x18 bytes");

/* The payload's linear memory (allocated on first use, 128 KB): 32-byte aligned pieces, 0 when full. */
void *jpr_lin_alloc(u32 bytes);
/* Replaces nn::snd's VA->DSP address check (0x172be4): device memory, or the payload's linear block. */
u32 jpr_dsp_address(u32 va, u32 bytes);

/* The sound thread's lock (recursive): hold it to check that a voice is still ours and use it. */
void jpr_snd_lock(void);
void jpr_snd_unlock(void);

void *jpr_snd_voice_alloc(void (*dropped)(void *voice, u32 arg), u32 arg);   /* mono PCM16 at rate, paused */
void jpr_snd_voice_free(void *voice);
void jpr_snd_set_rate(void *voice, u32 rate);
void jpr_snd_wave_init(struct jpr_wave *w);
void jpr_snd_append(void *voice, struct jpr_wave *w);      /* flushes the samples first */
void jpr_snd_play(void *voice);
void jpr_snd_stop(void *voice);                            /* every queued buffer becomes DONE */
void jpr_snd_set_volume(void *voice, u32 fbits);           /* Voice::SetVolume: a float, as its bits */
/* Progress, read without the lock: samples into the current buffer, and whether the DSP is playing. */
u32 jpr_snd_position(const void *voice);
u32 jpr_snd_active(const void *voice);
u32 jpr_snd_fed(const void *voice);

#endif
