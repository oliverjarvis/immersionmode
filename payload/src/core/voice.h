#ifndef JPR_VOICE_H
#define JPR_VOICE_H

#include "types.h"

/* Voice clips (spec: voice): voice.bin's index, and when and what to play. Pure logic; the platform side
 * (platform/voice.c) reads the file and feeds the game's sound library. */

#define JPR_VOICE_OK 0
#define JPR_VOICE_BAD 1
#define JPR_VOICE_RATE 22050
#define JPR_VOICE_HEADER 20         /* "JPRV", version 2, clip count, sample rate, part count */
#define JPR_VOICE_ENTRY 24
#define JPR_VOICE_MAX 0x100000       /* clips: the index is searched on the SD card, never held in memory */
#define JPR_VOICE_PARTS 64           /* voice-N.bin files of up to 1 GB each (FAT32 holds no 4 GB file) */
#define JPR_VOICE_SLOTS 4            /* wave buffers in the ring */
#define JPR_VOICE_CHUNK 5512         /* samples per buffer: a quarter second */
#define JPR_VOICE_FREE 0             /* wave buffer status, as nn::snd keeps it */
#define JPR_VOICE_QUEUED 1
#define JPR_VOICE_PLAYING 2
#define JPR_VOICE_DONE 3
#define JPR_VOICE_NONE 0             /* what a message or a button asks for */
#define JPR_VOICE_START 1
#define JPR_VOICE_SYSTEM 0x8000      /* in an entry's file: a system text file's clip (story files are below it) */

/* Reads index entry k (24 bytes, sorted by (file, line)) into entry; non-zero when it could not. An entry's file is a
 * story text file, or a system text file + JPR_VOICE_SYSTEM. */
typedef int (*jpr_voice_reader)(void *ctx, u32 k, u8 *entry);

struct jpr_voice_index {
    u32 count, parts;
    jpr_voice_reader read;
    void *ctx;
};

struct jpr_voice_clip {
    u16 file, line, part;            /* the clip's samples are in voice-<part>.bin */
    u32 at[2];                       /* byte offsets in that part: normal, slow */
    u32 samples[2];
};

/* header: voice.bin's first 20 bytes; entries are read through `read` when a line is looked up. */
int jpr_voice_index_parse(struct jpr_voice_index *ix, const u8 *header, u32 len, jpr_voice_reader read, void *ctx);
/* The clip's position in the index, or -1 (also when the SD card fails). */
s32 jpr_voice_find(const struct jpr_voice_index *ix, u32 file, u32 line, struct jpr_voice_clip *out);
/* "/jpreader/voice-<part>.bin" into out (at least JPR_VOICE_PATH_MAX bytes): the file a clip's samples are in. */
#define JPR_VOICE_PATH_MAX 32
void jpr_voice_part_path(char *out, u32 part);
/* The file a message's clip is filed under: story text (archive 40 or 41) by its file, system text (30 or 31) by its
 * file + JPR_VOICE_SYSTEM; -1 for any other message, which has no clip. */
s32 jpr_voice_file(s32 arc, u32 dat);

struct jpr_voice_player {
    const struct jpr_voice_index *ix;
    s32 clip;                        /* playing (or about to): its index; -1 for none */
    u32 speed;                       /* 0 normal, 1 slow (0.7x) */
    u32 part, at, next, total;       /* the clip's samples: their part, where they start, the next to read, how many */
    struct jpr_voice_clip message;   /* the current message's clip, when message_clip >= 0 */
    u32 slot;                        /* the ring slot to fill next: buffers are queued in order */
    s32 message_clip;                /* the current message's clip, for the buttons; -1 for none */
    u32 seq;                         /* the last message seen */
    s32 arc;
    u32 dat, line;
};

struct jpr_voice_fill {
    u32 slot;
    u32 part;                        /* voice-<part>.bin */
    u32 offset;                      /* in that part */
    u32 samples;
};

void jpr_voice_init(struct jpr_voice_player *p);
/* The text file a message's clip is looked up under. A message built in memory (arc JPR_ARC_MEMORY: every
 * cutscene line) has no file of its own; it takes the one its text was found in by header (found_arc and
 * found_dat, from the underline lookup, story or system; found_arc < 0 when none). Any other message keeps its own. */
void jpr_voice_origin(s32 arc, u32 dat, s32 found_arc, u32 found_dat, s32 *out_arc, u32 *out_dat);
/* A message is showing: starts its clip when it is a new story or system line with one and autoplay is on. */
u32 jpr_voice_message(struct jpr_voice_player *p, const struct jpr_voice_index *ix, u32 seq, s32 arc, u32 dat,
                      u32 line, u32 autoplay);
/* ▶ (speed 0) or 0.7x (speed 1): the current message's clip from the start. */
u32 jpr_voice_replay(struct jpr_voice_player *p, u32 speed);
/* The ring's buffers to fill now, in order: fills[0..n). status: each slot's nn::snd status. */
u32 jpr_voice_refill(struct jpr_voice_player *p, const u8 status[JPR_VOICE_SLOTS], struct jpr_voice_fill *fills);
/* Every sample read and every buffer played: the voice may be let go. */
int jpr_voice_finished(const struct jpr_voice_player *p, const u8 status[JPR_VOICE_SLOTS]);
/* The game took the voice back (nn::snd's drop callback): nothing is playing any more. */
void jpr_voice_dropped(struct jpr_voice_player *p);

#endif
