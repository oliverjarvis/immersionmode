#include "core/capture.h"
#include "choices.h"
#include "screen.h"
#include "display.h"
#include "game.h"

static void *field(const void *object, u32 offset) {
    return *(void *const *)((const u8 *)object + offset);
}

static u16 rd16(const u8 *p) {
    return (u16)(p[0] | (p[1] << 8));
}

static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* gfl2::str::MsgData::MsgData(arcId, datId, heap, loadType) */
void jpr_h_msgdata_ctor_ids(void *self, s32 arc, u32 dat) {
    jpr_cap_msgdata_new(self, arc, dat);
}

/* gfl2::str::MsgData::MsgData(const void *data, heap) */
void jpr_h_msgdata_ctor_mem(void *self) {
    jpr_cap_msgdata_new(self, JPR_ARC_MEMORY, JPR_NONE);
}

/* gfl2::str::MsgData::~MsgData, both variants */
void jpr_h_msgdata_dtor(void *self) {
    jpr_cap_msgdata_del(self);
}

/* gfl2::str::MsgData::GetString(index, StrBuf &dst) */
void jpr_h_getstring(void *self, u32 line, void *dst) {
    const u8 *header = field(self, MSGDATA_HEADER);
    jpr_cap_fetch(self, line, dst,
                  header ? rd16(header + TEXT_HDR_LINES) : 0,
                  header ? rd32(header + TEXT_HDR_TOTAL) : 0);
}

/* print::WordSet::Expand(StrBuf *dst, const StrBuf *src) */
void jpr_h_expand(void *wordset, void *dst, const void *src) {
    (void)wordset;
    jpr_cap_copy_ident(dst, src);
}

/* App::Tool::TalkWindow::SetMessage(const StrBuf *) */
void jpr_h_talk_set_buf(void *talk, const void *src) {
    jpr_talk = talk;
    jpr_cap_begin_buf(talk, src);
}

/* App::Tool::TalkWindow::SetMessage(u32 id, StrBuf *name) */
void jpr_h_talk_set_id(void *talk) {
    jpr_talk = talk;
    jpr_cap_begin_id(talk);
}

/* Runs when either SetMessage overload returns. */
void jpr_h_talk_set_done(void) {
    const void *talk = jpr_cap_pending_talk();
    const void *buf;
    const u16 *text = 0;
    u32 len = 0;

    if (!talk) return;
    buf = field(talk, field(talk, TALK_WORDSET) ? TALK_BUF_EXPANDED : TALK_BUF_RAW);
    if (buf) {
        text = field(buf, STRBUF_PTR);
        len = *(const u16 *)((const u8 *)buf + STRBUF_LEN);
    }
    jpr_cap_commit(buf, text, len);
    jpr_display_committed();
}

/* App::Tool::TalkWindow::CallMsgWin() */
void jpr_h_talk_call(void *talk) {
    jpr_talk = talk;
    jpr_cap_set_open(talk, 1);
}

/* App::Tool::TalkWindow::End() */
void jpr_h_talk_end(void *talk) {
    jpr_cap_set_open(talk, 0);
    if (jpr_talk == talk) jpr_talk = 0;
}

/* app::ui::UIView::~UIView(), which every talk window's destructor ends in. A window destroyed
 * without End() must not leave the panel up, or keep a pointer to freed memory. */
void jpr_h_view_dtor(void *view) {
    jpr_menu_gone(&jpr_menu, view);          /* a menu is a view too: never read it once it is freed */
    if (view != jpr_talk) return;
    jpr_talk = 0;
    if (jpr_cap.open) jpr_cap_set_open(view, 0);
}
