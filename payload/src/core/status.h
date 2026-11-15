#ifndef JPR_STATUS_H
#define JPR_STATUS_H

#include "types.h"

/* The debug line (spec 8.3): "mem <free>K <arc>/<file>/<line> err <code> draw <a>/<b>/<c>ms": the panel's
 * last drawing time, a full-screen fill alone and a half-screen copy alone (what drawing could cost at
 * least, and what showing a ready image would). arc < 0 reads
 * "mem" (a memory-built message), a missing file or line "-". A nonzero detail (the 3DS's own result
 * code) follows the code as ":XXXXXXXX". NUL-terminated, cut to cap. */
/* Adds " v<fault>" (JPR_VOICE_FAULT_*) when voice clips cannot play; nothing for 0 or when it does not fit. */
void jpr_status_voice(char *out, u32 cap, u32 fault);
void jpr_status_line(char *out, u32 cap, u32 free_kb, s32 arc, u32 dat, u32 line, u32 err, u32 detail,
                     u32 draw_ticks, u32 fill_ticks, u32 copy_ticks);

/* Appends "<name> XXXXXXXX\n" to a NUL-terminated log of *len bytes, or nothing when the line does not
 * fit in cap. For jpreader/log.txt, written while config.txt has debug=1. */
void jpr_log_value(char *out, u32 cap, u32 *len, const char *name, u32 value);

#endif
