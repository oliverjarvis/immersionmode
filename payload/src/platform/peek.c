#include "peek.h"

#include "core/panel.h"
#include "core/util.h"
#include "debug.h"
#include "game_addrs.h"

#define HID_SELECT 0x4u              /* in the HID hold word; the game's own button word never has it */
#define BUTTON_HID 8                 /* gfl2::ui::CTR_Button: u32 *, the HID hold word */

extern struct jpr_panel jpr_panel;

static const volatile u32 *hid;
static u32 select_was;

/* The HID hold word, found once from the button device the poll ran for (the flag-0 device leaves `this` in r0;
 * the other has its button word there, which is no CTR_Button). */
static void find_hid(u32 r0) {
    u32 p;
    if (hid || !jpr_plausible_ptr(r0) || *(const u32 *)r0 != JPR_BUTTON_VTABLE) return;
    p = *(const u32 *)(r0 + BUTTON_HID);
    if (!jpr_plausible_ptr(p)) return;
    hid = (const volatile u32 *)p;
    jpr_dbg.hid_addr = p;
}

void jpr_h_pad_exit(u32 *regs) {
    u32 select, toggle = 0;
#if JPR_DEBUG
    jpr_h_pad_tail(regs);
    if (jpr_dbg.peek_toggle) {                /* the test driver's SELECT */
        jpr_dbg.peek_toggle = 0;
        toggle = 1;
    }
#endif
    /* nn::hid folds SELECT into START unless this is set (hooks.py): set, SELECT stays its own button, which the
     * game never reads, and START is START alone. The reader ran before this poll, so it holds from the next frame. */
    *(volatile u8 *)JPR_HID_SELECT_FLAG = 1;
    find_hid(regs[0]);
    select = hid ? *hid & HID_SELECT : 0;
    if (select && !select_was) {
        toggle = 1;
        jpr_dbg.select_presses++;
    }
    select_was = select;
    if (toggle) jpr_panel_peek(&jpr_panel, !jpr_panel.peek);
    if (jpr_panel.peek) *(u32 *)regs[1] = 0;   /* the game sees nothing pressed, so nothing fires when it closes */
}

void jpr_h_cross_exit(u32 *regs) {
#if JPR_DEBUG
    jpr_h_cross_tail(regs);
#endif
    if (jpr_panel.peek) {
        ((u32 *)regs[1])[0] = 0;
        ((u32 *)regs[1])[1] = 0;
    }
}

void jpr_h_stick_exit(u32 *regs) {
#if JPR_DEBUG
    jpr_h_stick_tail(regs);
#endif
    if (jpr_panel.peek) {
        ((u32 *)regs[5])[0] = 0;
        ((u32 *)regs[5])[1] = 0;
    }
}
