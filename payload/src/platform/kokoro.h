#ifndef JPR_PLATFORM_KOKORO_H
#define JPR_PLATFORM_KOKORO_H

#include "core/kokoro.h"

/* kokoro's word list on the 3DS, in its own memory block (game.h): the sorted list from the last sync, and
 * from it the marks of every dict.bin entry. There are two sets of marks: the worker builds one while the
 * panel's thread reads the other. Without the block there are no marks. */
void jpr_kokoro_init(void *region);
/* Worker: kokoro.bin's list and its marks, once, when no list is in use yet. scratch is a word-aligned buffer. */
void jpr_kokoro_load(u8 *scratch, u32 size);
/* Worker: the parser for a sync's response, writing into the list; null without the memory block. */
struct jpr_words *jpr_kokoro_words(void);
/* Worker: after a whole response, keeps the list (kokoro.bin) and switches to its marks. The word count,
 * or -1 when the response was not a whole word list (the marks in use stay). */
s32 jpr_kokoro_keep(u8 *scratch, u32 size, u32 synced);
/* Panel's thread: the marks in use and how many entries they cover; null (and 0) before any list. */
const u8 *jpr_kokoro_marks(u32 *entries);
/* Words in the list the marks in use came from. */
u32 jpr_kokoro_count(void);

#endif
