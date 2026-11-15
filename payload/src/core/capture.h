#ifndef JPR_CAPTURE_H
#define JPR_CAPTURE_H

#include "types.h"

#define JPR_CAP_MAGIC   0x4352504Au /* "JPRC" */
#define JPR_MSG_MAX     1024        /* UTF-16 units kept per message */
#define JPR_RING        8           /* power of two */
#define JPR_ARC_MEMORY  (-1)        /* message data built from a memory block */
#define JPR_ARC_UNKNOWN (-2)        /* origin of the text is not known */
#define JPR_NONE        0xFFFFFFFFu

/* Where a line of text came from. */
struct jpr_ident {
    s32 arc;       /* text archive number, or one of JPR_ARC_* */
    u32 dat;       /* file index inside the archive, or JPR_NONE */
    u32 line;      /* line index inside the file, or JPR_NONE */
    u32 hdr_total; /* total length field of the text file header */
    u16 hdr_lines; /* line count field of the text file header */
    u16 reserved;
};

struct jpr_msg {
    u32 seq;       /* 1-based message number; 0 while the slot is being written */
    struct jpr_ident id;
    u16 len;       /* units stored in text */
    u16 full_len;  /* units the game had; larger than len when truncated */
    u16 text[JPR_MSG_MAX];
};

struct jpr_capture {
    u32 magic;
    u32 seq;       /* messages committed so far */
    u32 open;      /* 1 while a talk window is showing */
    u32 end_count; /* times a talk window ended */
    struct jpr_msg ring[JPR_RING];
};

extern struct jpr_capture jpr_cap;

void jpr_cap_init(void);
void jpr_cap_msgdata_new(const void *msg, s32 arc, u32 dat);
void jpr_cap_msgdata_del(const void *msg);
void jpr_cap_fetch(const void *msg, u32 line, const void *dst, u16 hdr_lines, u32 hdr_total);
void jpr_cap_copy_ident(const void *dst, const void *src);
/* Where the text in buf came from, as GetString and Expand tagged it; JPR_ARC_UNKNOWN when it never was. */
struct jpr_ident jpr_cap_ident_of(const void *buf);
void jpr_cap_begin_buf(const void *talk, const void *src);
void jpr_cap_begin_id(const void *talk);
const void *jpr_cap_pending_talk(void);
void jpr_cap_commit(const void *final_buf, const u16 *text, u32 len);
void jpr_cap_set_open(const void *talk, int open);

#endif
