#include "debug.h"
#include "svc.h"

struct jpr_debug jpr_dbg;

#define PAD_LEFT    0x1
#define PAD_RIGHT   0x2
#define PAD_UP      0x4
#define PAD_DOWN    0x8
#define PAD_BUTTONS 0xFFFF  /* bits that go into the game's button word */
#define STICK_LEFT  0x10000 /* the rest move the circle pad */
#define STICK_RIGHT 0x20000
#define STICK_UP    0x40000
#define STICK_DOWN  0x80000
#define F32_ONE   0x3F800000u /* 1.0f */
#define F32_MINUS 0xBF800000u /* -1.0f */

/* End of the game's button poll: r1 points at the game's button word. */
void jpr_h_pad_tail(u32 *regs) {
    u32 *out = (u32 *)regs[1];
    jpr_dbg.polls++;
    if (jpr_dbg.pad_polls) {
        *out |= jpr_dbg.pad_mask & PAD_BUTTONS;
        jpr_dbg.pad_polls--;
    }
    if ((jpr_dbg.polls & 63) == 0) {
        s64 used = 0;
        svcGetSystemInfo(&used, 0, 1);
        jpr_dbg.app_mem_used_now = (u32)used;
    }
}

static void steer(u32 *vec, u32 left, u32 right, u32 up, u32 down) {
    u32 mask = jpr_dbg.pad_mask;
    if (!jpr_dbg.pad_polls) return;
    if (mask & left) vec[0] = F32_MINUS;
    if (mask & right) vec[0] = F32_ONE;
    if (mask & up) vec[1] = F32_ONE;
    if (mask & down) vec[1] = F32_MINUS;
}

/* End of the D-pad poll: r1 points at its (x, y) float vector. Menus read the D-pad here. */
void jpr_h_cross_tail(u32 *regs) {
    steer((u32 *)regs[1], PAD_LEFT, PAD_RIGHT, PAD_UP, PAD_DOWN);
}

/* End of the circle-pad poll: r5 points at its (x, y) float vector. The player walks with this. */
void jpr_h_stick_tail(u32 *regs) {
    steer((u32 *)regs[5], STICK_LEFT, STICK_RIGHT, STICK_UP, STICK_DOWN);
}
