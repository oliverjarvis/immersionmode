#ifndef JPR_DATA_H
#define JPR_DATA_H

#include "core/annot.h"
#include "core/capture.h"

/* The units of one message, found in annot.bin (spec 6.3). */
struct jpr_annot_result {
    u32 seq;    /* capture sequence number of the message; written last */
    u32 status; /* JPR_ANNOT_* */
    u32 count;
    struct jpr_unit units[JPR_UNITS_MAX];
    s32 arc;    /* the text file the units were found in (for a message built in memory, by its header) */
    u32 dat;    /* arc < 0 when none was */
};

extern struct jpr_annot_result jpr_annot;

/* Opens annot.bin and dict.bin the first time; sets jpr_panel.st.data_status and data_error. */
void jpr_data_open(void);
/* Finds the units of a captured message: by archive and file, or, for one built from memory, by its
 * text file header within the language's archives. */
void jpr_data_message(const struct jpr_msg *msg, u32 language);
/* The units of any text whose origin is id (a choice menu's option): placed in units, at most max; 0 when its line
 * is not known or the text does not match it. */
u32 jpr_data_units(const struct jpr_ident *id, const u16 *text, u32 len, u32 language, struct jpr_unit *units,
                   u32 max);
/* Up to `max` bytes of dict.bin entry or label n. 0, or -1. */
int jpr_data_entry(u32 entry, u8 *buf, u32 max, u32 *len);
int jpr_data_label(u32 label, u8 *buf, u32 max, u32 *len);

/* The stamp annot.bin and dict.bin share (ids.bin carries it too); 0 while they are not open. */
u32 jpr_data_stamp(void);

#endif
