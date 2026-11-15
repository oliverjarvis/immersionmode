#ifndef JPR_JSON_H
#define JPR_JSON_H

#include "types.h"

/* Appends ASCII s at out[*at]. 0, or -1 when it does not fit cap. */
int jpr_json_raw(u8 *out, u32 *at, u32 cap, const char *s);
/* Appends UTF-8 text escaped for the inside of a JSON string (no quotes). 0 or -1. */
int jpr_json_escaped(u8 *out, u32 *at, u32 cap, const u8 *text, u32 len);
/* The string at `path` (object keys and array indices separated by dots, such as
 * "choices.0.message.content"), decoded to UTF-8 in out. Returns its length (cut at a character
 * boundary to fit cap), or -1 when the value is missing, not a string, or the document is malformed. */
s32 jpr_json_get_string(const u8 *json, u32 len, const char *path, u8 *out, u32 cap);

#endif
