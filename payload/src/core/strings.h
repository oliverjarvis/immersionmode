#ifndef JPR_CORE_STRINGS_H
#define JPR_CORE_STRINGS_H

#include "types.h"

/* How a string on the screen is looked up in strings.bin (stage 14): its key is its text without the game's tags,
 * spaces and line breaks, hashed with FNV-1a over the units' bytes (little-endian). jpreader_tools.formats builds the
 * table with the same key and hash. */

/* The key of text[0..len) in out; its length, 0 when it is empty or longer than max. */
u32 jpr_text_key(const u16 *text, u32 len, u16 *out, u32 max);
u32 jpr_key_hash(const u16 *key, u32 n);
/* The hash of text's key, of any length: jpr_key_hash(key) for a key that fits; 0 when the key is empty. */
u32 jpr_text_hash(const u16 *text, u32 len);

#endif
