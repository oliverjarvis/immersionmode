#include "core/types.h"

/* Return addresses of functions that have a hook on their way out. */
#define RET_DEPTH 8 /* power of two */

static u32 ret_stack[RET_DEPTH];
static u32 ret_top;

void jpr_ret_push(u32 lr) {
    ret_stack[ret_top & (RET_DEPTH - 1)] = lr;
    ret_top++;
}

u32 jpr_ret_pop(void) {
    ret_top--;
    return ret_stack[ret_top & (RET_DEPTH - 1)];
}
