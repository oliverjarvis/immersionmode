#ifndef JPR_PLATFORM_VOICE_H
#define JPR_PLATFORM_VOICE_H

#include "core/types.h"

#define JPR_VOICE_PATH "/jpreader/voice.bin"
#define JPR_VOICE_FAULT_NONE 0
#define JPR_VOICE_FAULT_FILE 1       /* no voice.bin */
#define JPR_VOICE_FAULT_HEADER 2     /* not a voice.bin this payload reads */
#define JPR_VOICE_FAULT_READ 3       /* a read failed or came short: the clip stops */
#define JPR_VOICE_FAULT_VOICE 4      /* the game had no voice to spare: the clip is skipped */
#define JPR_VOICE_FAULT_MEMORY 5     /* no linear memory for the ring */

/* Every frame (spec: voice 4): a new story message starts its clip (when autoplay is on), request
 * 1 (▶) or 2 (0.7x) replays the current message's clip, and the ring is kept fed from voice.bin. */
void jpr_voice_frame(u32 autoplay, u32 request);
/* Whether the current message has a clip (the buttons are drawn greyed out when not). */
u32 jpr_voice_available(void);
/* JPR_VOICE_FAULT_* of the last clip, for the debug line. */
u32 jpr_voice_fault(void);
/* A clip is playing (or about to): the mixer ducks the music under it. */
u32 jpr_voice_playing(void);
/* Our voice's volume (a float's bits), now and for every voice borrowed later. */
void jpr_voice_set_gain(u32 fbits);

#endif
