#include "capture.h"
#include "util.h"

#define MSG_SLOTS 128 /* power of two */
#define BUF_SLOTS 32  /* power of two */

struct msg_slot {
    const void *msg;
    s32 arc;
    u32 dat;
};

struct buf_slot {
    const void *buf;
    struct jpr_ident id;
};

struct jpr_capture jpr_cap;

static struct msg_slot msg_slots[MSG_SLOTS];
static struct buf_slot buf_slots[BUF_SLOTS];
static u32 msg_next;
static u32 buf_next;
static const void *pending_talk;
static int pending_has_id;
static struct jpr_ident pending_id;

static struct jpr_ident unknown_ident(void) {
    struct jpr_ident id;
    id.arc = JPR_ARC_UNKNOWN;
    id.dat = JPR_NONE;
    id.line = JPR_NONE;
    id.hdr_total = 0;
    id.hdr_lines = 0;
    id.reserved = 0;
    return id;
}

static struct msg_slot *find_msg(const void *msg) {
    for (u32 i = 0; i < MSG_SLOTS; i++)
        if (msg_slots[i].msg == msg) return &msg_slots[i];
    return 0;
}

static struct buf_slot *find_buf(const void *buf) {
    for (u32 i = 0; i < BUF_SLOTS; i++)
        if (buf_slots[i].buf == buf) return &buf_slots[i];
    return 0;
}

static struct jpr_ident ident_of(const void *buf) {
    const struct buf_slot *slot = find_buf(buf);
    return slot ? slot->id : unknown_ident();
}

static void set_buf(const void *buf, const struct jpr_ident *id) {
    struct buf_slot *slot = find_buf(buf);
    if (!slot) {
        slot = &buf_slots[buf_next];
        buf_next = (buf_next + 1) & (BUF_SLOTS - 1);
    }
    slot->buf = buf;
    slot->id = *id;
}

void jpr_cap_init(void) {
    jpr_memset(&jpr_cap, 0, (u32)sizeof jpr_cap);
    jpr_memset(msg_slots, 0, (u32)sizeof msg_slots);
    jpr_memset(buf_slots, 0, (u32)sizeof buf_slots);
    msg_next = 0;
    buf_next = 0;
    pending_talk = 0;
    pending_has_id = 0;
    jpr_cap.magic = JPR_CAP_MAGIC;
}

void jpr_cap_msgdata_new(const void *msg, s32 arc, u32 dat) {
    struct msg_slot *slot = find_msg(msg);
    if (!slot) slot = find_msg(0); /* a free slot */
    if (!slot) {
        slot = &msg_slots[msg_next];
        msg_next = (msg_next + 1) & (MSG_SLOTS - 1);
    }
    slot->msg = msg;
    slot->arc = arc;
    slot->dat = dat;
}

void jpr_cap_msgdata_del(const void *msg) {
    struct msg_slot *slot = find_msg(msg);
    if (slot) slot->msg = 0;
}

void jpr_cap_fetch(const void *msg, u32 line, const void *dst, u16 hdr_lines, u32 hdr_total) {
    struct jpr_ident id = unknown_ident();
    const struct msg_slot *slot = find_msg(msg);
    if (slot) {
        id.arc = slot->arc;
        id.dat = slot->dat;
    }
    id.line = line;
    id.hdr_lines = hdr_lines;
    id.hdr_total = hdr_total;
    set_buf(dst, &id);
}

struct jpr_ident jpr_cap_ident_of(const void *buf) {
    return ident_of(buf);
}

void jpr_cap_copy_ident(const void *dst, const void *src) {
    struct jpr_ident id = ident_of(src);
    set_buf(dst, &id);
}

void jpr_cap_begin_buf(const void *talk, const void *src) {
    pending_id = ident_of(src);
    pending_has_id = 1;
    pending_talk = talk;
}

void jpr_cap_begin_id(const void *talk) {
    pending_has_id = 0;
    pending_talk = talk;
}

const void *jpr_cap_pending_talk(void) {
    return pending_talk;
}

void jpr_cap_commit(const void *final_buf, const u16 *text, u32 len) {
    struct jpr_msg *slot;
    struct jpr_ident id;
    u32 keep;

    if (!pending_talk) return;
    id = pending_has_id ? pending_id : ident_of(final_buf);
    pending_talk = 0;
    if (!text) len = 0;
    keep = len > JPR_MSG_MAX ? JPR_MSG_MAX : len;

    slot = &jpr_cap.ring[jpr_cap.seq & (JPR_RING - 1)];
    slot->seq = 0;
    slot->id = id;
    slot->len = (u16)keep;
    slot->full_len = (u16)(len > 0xFFFF ? 0xFFFF : len);
    jpr_memcpy(slot->text, text, keep * 2u);
    jpr_cap.seq++;
    slot->seq = jpr_cap.seq;
}

void jpr_cap_set_open(const void *talk, int open) {
    (void)talk;
    if (open) {
        jpr_cap.open = 1;
    } else {
        jpr_cap.open = 0;
        jpr_cap.end_count++;
    }
}
