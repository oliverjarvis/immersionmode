#include "layout.h"

/* Length in units of the tag at text[i], or 0 if it runs past `end`. */
static u32 tag_length(const u16 *text, u32 i, u32 end) {
    u32 n;
    if (i + 2 > end) return 0;
    n = text[i + 1];
    if (n < 1 || i + 2 + n > end) return 0;
    return 2 + n;
}

int jpr_next_unit(const u16 *text, u32 *i, u32 end, u16 *cp) {
    while (*i < end) {
        u16 u = text[*i];
        if (u == JPR_TAG_MARK) {
            u32 n = tag_length(text, *i, end);
            if (!n) {
                *i = end;
                return 0;
            }
            *i += n;
            continue;
        }
        (*i)++;
        if (u == 0x000A) continue;
        *cp = u;
        return 1;
    }
    return 0;
}

static int line_blank(const u16 *text, struct jpr_line line) {
    u32 i = line.start;
    u16 cp;
    return !jpr_next_unit(text, &i, line.end, &cp);
}

static void add_line(struct jpr_layout *l, u32 start, u32 end) {
    if (l->line_count >= JPR_LAYOUT_LINES) return;
    l->lines[l->line_count].start = (u16)start;
    l->lines[l->line_count].end = (u16)end;
    l->line_count++;
    l->blocks[l->block_count - 1].count++;
}

static void start_block(struct jpr_layout *l) {
    l->blocks[l->block_count].first = (u16)l->line_count;
    l->blocks[l->block_count].count = 0;
    l->block_count++;
}

static void trim_block(struct jpr_layout *l, const u16 *text) {
    struct jpr_block *b = &l->blocks[l->block_count - 1];
    while (b->count && line_blank(text, l->lines[b->first])) {
        b->first++;
        b->count--;
    }
    while (b->count && line_blank(text, l->lines[b->first + b->count - 1])) b->count--;
}

void jpr_layout_build(struct jpr_layout *l, const u16 *text, u32 len, u32 max_width,
                      jpr_width_fn width, const void *ctx) {
    u32 i = 0, line_start = 0, x = 0;
    int drop_line_break = 0;
    l->block_count = 0;
    l->line_count = 0;
    start_block(l);
    while (i < len) {
        u16 u = text[i];
        if (u == JPR_TAG_MARK) {
            u32 n = tag_length(text, i, len);
            if (!n) break;
            if (text[i + 2] == JPR_TAG_CLEAR && l->block_count < JPR_LAYOUT_BLOCKS) {
                add_line(l, line_start, i);
                trim_block(l, text);
                start_block(l);
                line_start = i + n;
                x = 0;
                drop_line_break = 1;
            }
            i += n;
            continue;
        }
        if (u == 0x000A) {
            if (!drop_line_break) add_line(l, line_start, i);
            drop_line_break = 0;
            i++;
            line_start = i;
            x = 0;
            continue;
        }
        drop_line_break = 0;
        {
            u32 w = width(ctx, u);
            if (x && x + w > max_width) {
                add_line(l, line_start, i);
                line_start = i;
                x = 0;
            }
            x += w;
        }
        i++;
    }
    add_line(l, line_start, i);
    trim_block(l, text);
    if (l->block_count > 1 && l->blocks[l->block_count - 1].count == 0) l->block_count--;
}
