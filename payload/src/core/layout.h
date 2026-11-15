#ifndef JPR_LAYOUT_H
#define JPR_LAYOUT_H

#include "types.h"

#define JPR_TAG_MARK 0x0010
#define JPR_TAG_SCROLL 0xBE00 /* box waits, then scrolls a line */
#define JPR_TAG_CLEAR 0xBE01  /* box waits, then clears: a new block starts */
#define JPR_LAYOUT_BLOCKS 32
#define JPR_LAYOUT_LINES 96

struct jpr_line {
    u16 start; /* half-open range of text units; tags inside it are invisible */
    u16 end;
};

struct jpr_block {
    u16 first; /* index of its first line */
    u16 count;
};

struct jpr_layout {
    u32 block_count;
    u32 line_count;
    struct jpr_block blocks[JPR_LAYOUT_BLOCKS];
    struct jpr_line lines[JPR_LAYOUT_LINES];
};

typedef u32 (*jpr_width_fn)(const void *ctx, u16 cp);

void jpr_layout_build(struct jpr_layout *l, const u16 *text, u32 len, u32 max_width,
                      jpr_width_fn width, const void *ctx);
/* Steps *i past tags and line breaks to the next visible unit before `end`. Returns 1 with the
 * unit in *cp, or 0 when there is none. */
int jpr_next_unit(const u16 *text, u32 *i, u32 end, u16 *cp);

#endif
