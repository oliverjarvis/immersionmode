#ifndef JPR_ANNOT_H
#define JPR_ANNOT_H

#include "types.h"

/* Limits shared with the build (formats.py). */
#define JPR_SEGMENTS_MAX 64
#define JPR_UNITS_MAX 256
#define JPR_RECORD_MAX (4 + 8 * JPR_SEGMENTS_MAX + 12 * JPR_UNITS_MAX)
#define JPR_SEG_TEXT 0
#define JPR_UNIT_WORD 1
#define JPR_UNIT_PARTICLE 2
#define JPR_UNIT_NAME 3
#define JPR_NO_ENTRY 0xFFFFFFFFu
#define JPR_NO_LABEL 0xFFFFu
#define JPR_ANNOT_NONE 0         /* annot.bin or dict.bin is not available */
#define JPR_ANNOT_OK 1
#define JPR_ANNOT_UNKNOWN_LINE 2 /* no file or line for the message's identity */
#define JPR_ANNOT_NO_RECORD 3    /* the line has nothing to look up */
#define JPR_ANNOT_MISMATCH 4     /* the text does not fit the line's record */
#define JPR_ANNOT_READ_ERROR 5
struct jpr_unit { u16 start; u16 len; u32 entry; u16 label; u8 kind; u8 pad; }; /* start: in the displayed text */
/* Places the units of a line record (spec 7.4) in the displayed text. Returns the number placed,
 * or -1 when the record is malformed or the text does not fit it. */
s32 jpr_annot_place(const u8 *rec, u32 rec_len, const u16 *text, u32 len, struct jpr_unit *out, u32 max);

#endif
