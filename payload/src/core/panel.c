#include "panel.h"

#include "reply.h"
#include "util.h"

int jpr_touch_hide(struct jpr_touch_gate *g, u32 down, u32 visible) {
    if (!down) {
        g->held = 0;
        g->hiding = 0;
        return 0;
    }
    if (!g->held) {
        g->held = 1;
        g->hiding = visible != 0;
    }
    return (int)g->hiding;
}

int jpr_pages_step(struct jpr_pages *t, u32 state, u32 tag) {
    u32 waiting = state == JPR_WIN_WAITING && tag == JPR_TAG_CLEAR;
    int cleared = t->waiting_clear && !waiting;
    t->waiting_clear = waiting;
    return cleared;
}

void jpr_panel_init(struct jpr_panel *p) {
    jpr_memset(p, 0, (u32)sizeof *p);
    p->st.magic = JPR_PANEL_MAGIC;
    p->st.card_entry = JPR_NO_ENTRY;
    p->add_entry = JPR_NO_ENTRY;
}

static void measure_card(struct jpr_panel *p, const struct jpr_font *f, const u16 *text);
static const char *problem(const struct jpr_panel_state *st);
static void row_range(const struct jpr_panel *p, u32 at, u32 len, u32 *from, u32 *to);

/* The rows view's card is up for a selected word, or to say what is wrong with the data files. */
static int rows_card(const struct jpr_panel *p) {
    return p->st.selected || problem(&p->st);
}

/* The text area's bottom: the lines of the block shown, one to four; in the rows view, the card or the bar. */
static s32 text_bottom(const struct jpr_panel *p) {
    u32 lines = p->layout.blocks[p->st.view_block].count;
    if (p->rows) return rows_card(p) ? JPR_PEEK_CARD_TOP - JPR_CARD_GAP : JPR_BAR_TOP - 1 - (p->status[0] ? 16 : 0);
    if (lines < 1) lines = 1;
    if (lines > JPR_VISIBLE_LINES) lines = JPR_VISIBLE_LINES;
    return JPR_TEXT_TOP + (s32)(lines * JPR_LINE_PITCH) + 4;
}

s32 jpr_panel_card_top(const struct jpr_panel *p) {
    if (p->rows) return JPR_PEEK_CARD_TOP;
    return text_bottom(p) + JPR_CARD_GAP;
}

/* Does the selected word's card have a kokoro row? Only dictionary words can be in kokoro. */
static int kokoro_row(const struct jpr_panel *p) {
    return p->st.kokoro_on && p->st.selected && p->st.card_status == JPR_CARD_OK && p->card.kind == JPR_ENTRY_DICTIONARY;
}

/* The add button: a word kokoro does not have, not being added, not refused for the token, while no other
 * add waits for its answer. */
static int add_shown(const struct jpr_panel *p) {
    return kokoro_row(p) && p->st.card_kokoro == JPR_KOKORO_NONE && p->add_entry == JPR_NO_ENTRY &&
           (p->st.add_state == JPR_ADD_NONE || p->st.add_state == JPR_ADD_FAILED);
}

/* The first row below the card's content: its bottom border, or the kokoro row. */
static s32 card_inner_bottom(const struct jpr_panel *p) {
    return JPR_CARD_BOTTOM - 1 - (kokoro_row(p) ? JPR_KOKORO_ROW : 0);
}

u32 jpr_panel_card_area(const struct jpr_panel *p) {
    return (u32)(card_inner_bottom(p) - 1 - jpr_panel_card_top(p));
}

static u32 width_of(const void *ctx, u16 cp) {
    return jpr_font_advance((const struct jpr_font *)ctx, JPR_FACE_TEXT, cp);
}

static u32 max_scroll(const struct jpr_panel *p) {
    u32 lines = p->layout.blocks[p->st.view_block].count;
    if (p->rows) {
        u32 visible = (u32)(text_bottom(p) - JPR_TEXT_TOP);
        return p->rows_height > visible ? p->rows_height - visible : 0;
    }
    return lines > JPR_VISIBLE_LINES ? (lines - JPR_VISIBLE_LINES) * JPR_LINE_PITCH : 0;
}

static void clear_selection(struct jpr_panel *p) {
    p->st.selected = 0;
    p->st.sel_start = 0;
    p->st.sel_len = 0;
    p->st.card_entry = JPR_NO_ENTRY;
    p->st.card_status = JPR_CARD_NONE;
    p->card_wanted = 0;
    p->label.len = 0;
    p->card_scroll = 0;
    p->card_height = 0;
    p->st.card_scroll = 0;
    p->st.add_state = JPR_ADD_NONE;
    p->st.card_kokoro = 0;
}

static void select_unit(struct jpr_panel *p, u32 k) {
    const struct jpr_unit *u = &p->units[k];
    clear_selection(p);
    p->st.selected = k + 1;
    p->st.sel_start = u->start;
    p->st.sel_len = u->len;
    p->st.card_entry = u->entry;
    p->st.card_kokoro = p->marks[k];
    if (u->entry != JPR_NO_ENTRY && u->entry == p->add_entry) p->st.add_state = JPR_ADD_ASKING;   /* still on its way */
    if (u->entry == JPR_NO_ENTRY) p->st.card_status = JPR_CARD_NO_ENTRY;
    else p->card_wanted = 1;
}

/* Shows block b from its top, with nothing selected. */
static void view(struct jpr_panel *p, u32 b) {
    p->st.view_block = b;
    p->st.scroll = 0;
    clear_selection(p);
}

void jpr_panel_rows(struct jpr_panel *p, const struct jpr_peek_rows *r) {
    p->rows = r;
}

static const u8 *pool_at(const struct jpr_peek_rows *r, struct jpr_peek_str s) {
    return r->pool + s.at;
}

/* A row's literal and English from y, a small line's top: on one line when both fit, else each wrapped on lines of
 * its own, the English muted. Pens without a surface only measure. Returns the y below them. */
static s32 lay_notes(const struct jpr_peek_rows *r, const struct jpr_peek_row *row, const struct jpr_pen *lit,
                     const struct jpr_pen *muted, s32 y) {
    static const u8 sep[] = " \xC2\xB7 ";
    struct jpr_peek_str l = row->literal, e = row->english;
    s32 left = JPR_TEXT_X, right = JPR_TEXT_X + JPR_TEXT_W, base = y + JPR_PEEK_NOTE_BASE;
    if (l.len && e.len &&
        jpr_pen_measure_utf8(lit, pool_at(r, l), l.len) + jpr_pen_measure_utf8(muted, sep, sizeof sep - 1) +
                jpr_pen_measure_utf8(muted, pool_at(r, e), e.len) <= JPR_TEXT_W) {
        s32 x = jpr_draw_utf8(lit, left, base, pool_at(r, l), l.len);
        jpr_draw_utf8(muted, jpr_draw_utf8(muted, x, base, sep, sizeof sep - 1), base, pool_at(r, e), e.len);
        return y + JPR_PEEK_NOTE;
    }
    if (l.len) {
        base = jpr_draw_wrapped(lit, left, base, left, right, JPR_PEEK_NOTE, 0x7FFFFFFF, pool_at(r, l), l.len);
        y = base - JPR_PEEK_NOTE_BASE + JPR_PEEK_NOTE;
        base = y + JPR_PEEK_NOTE_BASE;
    }
    if (e.len) {
        base = jpr_draw_wrapped(muted, left, base, left, right, JPR_PEEK_NOTE, 0x7FFFFFFF, pool_at(r, e), e.len);
        y = base - JPR_PEEK_NOTE_BASE + JPR_PEEK_NOTE;
    }
    return y;
}

/* The meta's width: the type's pill, then the rest (PP 30/30). */
static s32 meta_width(const struct jpr_peek_rows *r, const struct jpr_peek_row *row, const struct jpr_pen *small) {
    s32 w = 0;
    if (row->pill.len) w = (s32)jpr_pen_measure_utf8(small, pool_at(r, row->pill), row->pill.len) + 12;
    if (row->meta.len) w += (w ? 6 : 0) + (s32)jpr_pen_measure_utf8(small, pool_at(r, row->meta), row->meta.len);
    return w;
}

static s32 line_width(const u16 *text, struct jpr_line line, const struct jpr_font *f) {
    u32 i = line.start;
    s32 w = 0;
    u16 cp;
    while (jpr_next_unit(text, &i, line.end, &cp)) w += (s32)jpr_font_advance(f, JPR_FACE_TEXT, cp);
    return w;
}

/* Where each row and each of its lines goes: the Japanese lines, the meta beside the first when it fits (else on a
 * small line of its own), the notes; rows below the message are at least JPR_PEEK_ROW_MIN tall. */
static void lay_rows(struct jpr_panel *p, const u16 *text, const struct jpr_font *f) {
    const struct jpr_peek_rows *r = p->rows;
    struct jpr_pen small;
    u32 n = r->n < JPR_PEEK_ROWS ? r->n : JPR_PEEK_ROWS, k, li = 0;
    s32 y = JPR_TEXT_TOP;
    jpr_memset(&small, 0, (u32)sizeof small);
    small.f = f;
    small.face = JPR_FACE_SMALL;
    jpr_memset(p->geo, 0, (u32)sizeof p->geo);
    for (k = 0; k < JPR_LAYOUT_LINES; k++) p->line_top[k] = -1;
    for (k = 0; k < n; k++) {
        const struct jpr_peek_row *row = &r->row[k];
        struct jpr_row_geo *g = &p->geo[k];
        s32 top = y, first = -1;
        y += JPR_PEEK_PAD;
        for (; li < p->layout.line_count && p->layout.lines[li].start < row->end; li++) {
            u32 i = p->layout.lines[li].start;
            u16 cp;
            if (i < row->start || !jpr_next_unit(text, &i, p->layout.lines[li].end, &cp)) continue;   /* blank */
            p->line_top[li] = (s16)y;
            if (first < 0) first = (s32)li;
            y += JPR_PEEK_LINE;
        }
        if (row->meta.len || row->pill.len) {
            if (first >= 0 && line_width(text, p->layout.lines[first], f) + JPR_PEEK_GAP + meta_width(r, row, &small) <=
                                  JPR_TEXT_W) {
                g->inline_meta = 1;
            } else {
                g->meta_top = (u16)y;
                y += JPR_PEEK_NOTE;
            }
        }
        g->note_top = (u16)y;
        y = lay_notes(r, row, &small, &small, y) + JPR_PEEK_PAD;
        if (!(k == 0 && r->header) && y - top < JPR_PEEK_ROW_MIN) y = top + JPR_PEEK_ROW_MIN;
        g->top = (u16)top;
        g->height = (u16)(y - top);
    }
    p->rows_height = (u32)(y - JPR_TEXT_TOP);
}

void jpr_panel_message(struct jpr_panel *p, u32 seq, const u16 *text, u32 len, const struct jpr_font *f,
                       const struct jpr_unit *units, u32 count, u32 annot_status) {
    jpr_layout_build(&p->layout, text, len, JPR_TEXT_W, width_of, f);
    if (p->rows) lay_rows(p, text, f);
    p->messages++;
    p->st.msg_seq = seq;
    p->st.block_count = p->layout.block_count;
    p->st.live_block = 0;
    p->pages.waiting_clear = 0;
    p->dragging = 0;
    if (!units || count > JPR_UNITS_MAX) count = 0;
    if (count) jpr_memcpy(p->units, units, count * (u32)sizeof *units);
    p->st.unit_count = count;
    p->st.annot_status = annot_status;
    view(p, 0);
}

void jpr_panel_box(struct jpr_panel *p, u32 state, u32 tag) {
    if (!jpr_pages_step(&p->pages, state, tag) || p->rows) return;   /* the peek's rows have no pages */
    if (p->st.live_block + 1 < p->st.block_count) p->st.live_block++;
    view(p, p->st.live_block);
}

void jpr_panel_marks(struct jpr_panel *p, const u8 *bits, u32 entries, const struct jpr_override *o, u32 n) {
    u32 k, j, known = 0, learning = 0;
    for (k = 0; k < p->st.unit_count; k++) {
        u32 e = p->units[k].entry, s = 0;
        if (p->st.kokoro_on && e != JPR_NO_ENTRY) {
            if (bits) s = jpr_kokoro_state(bits, entries, e);
            for (j = 0; !s && j < n; j++)
                if (o[j].entry == e) s = o[j].state;
        }
        p->marks[k] = (u8)s;
        known += s == JPR_KOKORO_KNOWN;
        learning += s == JPR_KOKORO_LEARNING;
    }
    p->st.marks_known = known;
    p->st.marks_learning = learning;
    p->st.card_kokoro = p->st.selected ? p->marks[p->st.selected - 1] : 0;
}

void jpr_panel_added(struct jpr_panel *p, u32 entry, u32 outcome) {
    if (entry == p->add_entry) {
        p->add_entry = JPR_NO_ENTRY;
        p->add_wanted = 0;
    }
    if (p->st.selected && p->units[p->st.selected - 1].entry == entry) p->st.add_state = outcome;
}

/* Add tapped: its request is built now, from the card and the sentence the word is in, so selecting
 * another word or page before the worker is free cannot lose it. */
static void tap_add(struct jpr_panel *p, const u16 *text, u32 len) {
    static struct jpr_excerpt around;
    const struct jpr_unit *u = &p->units[p->st.selected - 1];
    u32 from = 0, to = len;
    if (p->rows) row_range(p, u->start, len, &from, &to);   /* the peek: the word's own row is its sentence */
    jpr_excerpt(&around, text + from, to - from, u->start - from, (u32)u->start + u->len - from);
    p->add_body_len = jpr_word_body(p->add_body, sizeof p->add_body, &p->card, around.sentence, around.sentence_len);
    if (!p->add_body_len) {
        p->st.add_state = JPR_ADD_FAILED;
        return;
    }
    p->add_entry = u->entry;
    p->add_wanted = 1;
    p->st.add_state = JPR_ADD_ASKING;
}

static int inside(u32 x, u32 y, u32 left, u32 top, u32 w, u32 h) {
    return x >= left && x < left + w && y >= top && y < top + h;
}

static s32 unit_at(const struct jpr_panel *p, u32 i) {
    u32 k;
    for (k = 0; k < p->st.unit_count; k++)
        if (i >= p->units[k].start && i < (u32)p->units[k].start + p->units[k].len) return (s32)k;
    return -1;
}

s32 jpr_panel_hit(const struct jpr_panel *p, const struct jpr_font *f, const u16 *text, u32 x, u32 y) {
    const struct jpr_block *b = &p->layout.blocks[p->st.view_block];
    s32 row = (s32)y + (s32)p->st.scroll - JPR_TEXT_TOP, gx = JPR_TEXT_X;
    u32 k = 0, i;
    struct jpr_line line;
    u16 cp;
    if (p->rows) {
        s32 at = (s32)y + (s32)p->st.scroll;
        if ((s32)y >= text_bottom(p)) return -1;
        for (k = 0; k < p->layout.line_count; k++)
            if (p->line_top[k] >= 0 && at >= p->line_top[k] && at < p->line_top[k] + JPR_PEEK_LINE) break;
        if (k == p->layout.line_count) return -1;
        line = p->layout.lines[k];
        i = line.start;
        while (jpr_next_unit(text, &i, line.end, &cp)) {
            s32 adv = (s32)jpr_font_advance(f, JPR_FACE_TEXT, cp);
            if ((s32)x >= gx && (s32)x < gx + adv) return unit_at(p, i - 1);
            gx += adv;
        }
        return -1;
    }
    if ((s32)y >= text_bottom(p) || row < 0) return -1;
    while (row >= JPR_LINE_PITCH) {
        row -= JPR_LINE_PITCH;
        k++;
    }
    if (k >= b->count) return -1;
    line = p->layout.lines[b->first + k];
    i = line.start;
    while (jpr_next_unit(text, &i, line.end, &cp)) {
        s32 adv = (s32)jpr_font_advance(f, JPR_FACE_TEXT, cp);
        if ((s32)x >= gx && (s32)x < gx + adv) return unit_at(p, i - 1);
        gx += adv;
    }
    return -1;
}

static const struct jpr_block *viewed(const struct jpr_panel *p) {
    return &p->layout.blocks[p->st.view_block];
}

/* Does the block on screen show any characters? */
static int block_has_text(const struct jpr_panel *p, const u16 *text) {
    const struct jpr_block *b = viewed(p);
    u32 k;
    for (k = 0; k < b->count; k++) {
        struct jpr_line line = p->layout.lines[b->first + k];
        u32 i = line.start;
        u16 cp;
        if (jpr_next_unit(text, &i, line.end, &cp)) return 1;
    }
    return 0;
}

/* The rows view: the row holding text[at], else the message's row, else everything; as [*from, *to). */
static void row_range(const struct jpr_panel *p, u32 at, u32 len, u32 *from, u32 *to) {
    const struct jpr_peek_rows *r = p->rows;
    u32 k, n = r->n < JPR_PEEK_ROWS ? r->n : JPR_PEEK_ROWS;
    *from = 0;
    *to = len;
    for (k = 0; k < n; k++)
        if (at >= r->row[k].start && at < r->row[k].end) break;
    if (k == n && r->header && n && r->row[0].end > r->row[0].start) k = 0;
    if (k < n && r->row[k].end <= len) {
        *from = r->row[k].start;
        *to = r->row[k].end;
    }
}

/* Does any line hold a character? */
static int any_text(const struct jpr_panel *p, const u16 *text) {
    u32 k;
    for (k = 0; k < p->layout.line_count; k++) {
        u32 i = p->layout.lines[k].start;
        u16 cp;
        if (jpr_next_unit(text, &i, p->layout.lines[k].end, &cp)) return 1;
    }
    return 0;
}

static void sync_points(struct jpr_panel *p);

static void sync_grammar(struct jpr_panel *p) {
    p->st.gr_open = p->gr.open;
    p->st.gr_state = p->gr.state;
    p->st.gr_scroll = p->gr.scroll;
    sync_points(p);
}

/* Starts a request for the sentence in the view: the platform hands it to the worker. */
static void ask(struct jpr_panel *p) {
    p->gr.state = JPR_GR_ASKING;
    p->gr.job = ++p->gr.next_id;
    p->gr.pending = 1;
    p->gr.scroll = 0;
    p->gr.reply_len = 0;
    p->gr.height = 0;
    p->gr.answered_len = 0;   /* the stored reply is gone: never offer it for its sentence again */
    p->gr.point_count = 0;
    p->gr.other_count = 0;
    p->gr.message[0] = 0;
    p->st.gr_message[0] = 0;
}

static void open_grammar(struct jpr_panel *p, const u16 *text, u32 len) {
    const struct jpr_block *b = viewed(p);
    u32 from = p->layout.lines[b->first].start, to = p->layout.lines[b->first + b->count - 1].end, k;
    int same;
    if (p->rows) {   /* the peek: the tapped word's row, else the message, each on its own */
        row_range(p, p->st.selected ? p->st.sel_start : 0xFFFFFFFFu, len, &from, &to);
        jpr_excerpt(&p->gr.ex, text + from, to - from, 0, to - from);
    } else {
        jpr_excerpt(&p->gr.ex, text, len, from, to);
    }
    p->gr.open = 1;
    p->gr.scroll = 0;
    same = p->gr.answered_len && p->gr.answered_len == p->gr.ex.sentence_len;
    for (k = 0; same && k < p->gr.answered_len; k++) same = p->gr.answered[k] == p->gr.ex.sentence[k];
    if (same) p->gr.state = JPR_GR_DONE;   /* the stored reply: no new request (spec 8.3) */
    else ask(p);
}

static void close_grammar(struct jpr_panel *p) {
    if (p->gr.state == JPR_GR_ASKING) {
        p->gr.cancel = p->gr.job;
        p->gr.pending = 0;
        p->gr.state = JPR_GR_IDLE;
    }
    p->gr.open = 0;
}

static u32 reply_area(void) {
    return JPR_BAR_TOP - 1 - JPR_GR_TOP - 2;   /* the reply is drawn from 2 px into the area */
}

static void scroll_reply(struct jpr_panel *p, s32 want) {
    s32 limit = (s32)p->gr.height - (s32)reply_area();
    if (limit < 0) limit = 0;
    if (want > limit) want = limit;
    if (want < 0) want = 0;
    p->gr.scroll = (u32)want;
}

#define GR_X 12   /* left edge of the reply's text */

/* The pens for reply text over one background (or, without a surface, measuring pens). */
struct reply_pens {
    struct jpr_pen translation, chunk, meaning, note, plain;
    struct jpr_pen pattern, muted, error, title;   /* grammar point rows */
    jpr_rgb bg;
};

static void reply_pens(struct reply_pens *r, const struct jpr_surface *s, const struct jpr_font *f, jpr_rgb bg,
                       s32 top, s32 bottom) {
    if (!s) {
        jpr_memset(r, 0, (u32)sizeof *r);
        r->translation.f = r->chunk.f = r->meaning.f = r->note.f = r->plain.f = f;
        r->pattern.f = r->muted.f = r->error.f = r->title.f = f;
        r->translation.face = r->note.face = JPR_FACE_ITALIC;
        r->chunk.face = JPR_FACE_TEXT;
        r->meaning.face = r->plain.face = JPR_FACE_SMALL;
        r->pattern.face = r->muted.face = r->error.face = r->title.face = JPR_FACE_SMALL;
    } else {
        jpr_pen_init(&r->translation, s, f, JPR_FACE_ITALIC, bg, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&r->chunk, s, f, JPR_FACE_TEXT, bg, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&r->meaning, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&r->note, s, f, JPR_FACE_ITALIC, bg, JPR_COLOR_MUTED, top, bottom);
        jpr_pen_init(&r->plain, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&r->pattern, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&r->muted, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_MUTED, top, bottom);
        jpr_pen_init(&r->error, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_ERROR, top, bottom);
        jpr_pen_init(&r->title, s, f, JPR_FACE_SMALL, bg, JPR_COLOR_MUTED, top, bottom);
    }
    r->bg = bg;
    jpr_pen_bold(&r->chunk);
    jpr_pen_bold(&r->pattern);
    jpr_pen_bold(&r->title);
}

/* What a point row's add column shows. */
#define COL_NONE 0
#define COL_ADD 1        /* "+ Add" */
#define COL_ADD_OFF 2    /* greyed: another add waits for its answer */
#define COL_RETRY 3      /* the last add of this point failed: try again */
#define COL_ADDING 4
#define COL_ADDED 5      /* kokoro answered OK (this session) */
#define COL_QUEUED 6     /* saved in the outbox */
#define COL_REJECTED 7   /* kokoro refused the device token */

/* A point row's add column, and in *state the point's state to show: kokoro's, or where that is none, this
 * session's (spec 3: overrides). */
static u32 point_column(const struct jpr_panel *p, u32 id, u32 status, u32 *state) {
    u32 k;
    *state = status;
    if (status != JPR_POINT_NONE || !id) return COL_NONE;
    for (k = 0; k < p->gr.over_count; k++)
        if (p->gr.over[k].entry == id) {
            if (p->gr.over[k].state == JPR_KOKORO_QUEUED) return COL_QUEUED;
            *state = p->gr.over[k].state;
            return COL_ADDED;
        }
    if (id == p->gr.add_id) return COL_ADDING;
    if (!p->st.kokoro_on) return COL_NONE;
    if (id == p->gr.last_id && p->gr.last_outcome == JPR_ADD_REJECTED) return COL_REJECTED;
    if (p->gr.add_id) return COL_ADD_OFF;   /* one add at a time, as the word card's */
    return id == p->gr.last_id && p->gr.last_outcome == JPR_ADD_FAILED ? COL_RETRY : COL_ADD;
}

/* The add column of the row at y, right-aligned at right. */
static void draw_column(const struct reply_pens *r, u32 column, s32 y, s32 right) {
    const struct jpr_surface *s = r->pattern.s;
    const struct jpr_font *f = r->pattern.f;
    s32 top = r->pattern.clip_top, bottom = r->pattern.clip_bottom, base = y + JPR_GR_FIRST, w;
    if (column == COL_ADD || column == COL_ADD_OFF || column == COL_RETRY) {
        const char *label = column == COL_RETRY ? "Retry" : "+ Add";   /* an ASCII plus: the small face has no ＋ */
        jpr_rgb fill = column == COL_ADD ? JPR_COLOR_KNOWN : column == COL_RETRY ? JPR_COLOR_ERROR : JPR_COLOR_BUTTON_OFF;
        struct jpr_pen on;
        u32 n = 0;
        while (label[n]) n++;
        w = (s32)(jpr_measure_ascii(f, JPR_FACE_SMALL, label) + n) + 14;   /* bold: a pixel more each */
        jpr_round_rect(s, right - w, y + 2, w, 15, 7, fill, 0, 0, top, bottom);
        jpr_pen_init(&on, s, f, JPR_FACE_SMALL, fill, column == COL_ADD_OFF ? JPR_COLOR_ARROW_OFF : JPR_COLOR_ON_ACCENT,
                     top, bottom);
        jpr_pen_bold(&on);
        jpr_draw_ascii(&on, right - w + 7, base, label);
    } else if (column == COL_ADDING) {
        w = (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, "Adding") + (s32)jpr_font_advance(f, JPR_FACE_SMALL, 0x2026);
        jpr_draw_char(&r->muted, jpr_draw_ascii(&r->muted, right - w, base, "Adding"), base, 0x2026);
    } else if (column != COL_NONE) {
        const char *text = column == COL_ADDED ? "Added" : column == COL_QUEUED ? "Queued" : "Rejected";
        const struct jpr_pen *pen = column == COL_REJECTED ? &r->error : &r->muted;
        jpr_draw_ascii(pen, right - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, text), base, text);
    }
}

/* The pattern from x on baseline base, cut with "…" where it would pass edge (it never runs into the add column).
 * Returns the x after it. */
static s32 draw_pattern(const struct jpr_pen *pen, s32 x, s32 base, s32 edge, const u8 *p, u32 len) {
    s32 dots = (s32)jpr_pen_advance(pen, 0x2026);
    u32 i = 0;
    if ((s32)jpr_pen_measure_utf8(pen, p, len) <= edge - x) return jpr_draw_utf8(pen, x, base, p, len);
    while (i < len) {
        u32 k = i;
        u16 cp = (u16)jpr_utf8_next(p, len, &k);
        s32 a = (s32)jpr_pen_advance(pen, cp);
        if (x + a + dots > edge) break;
        x += (s32)jpr_draw_char(pen, x, base, cp);
        i = k;
    }
    return x + (s32)jpr_draw_char(pen, x, base, 0x2026);
}

/* A grammar point's row from y (its top), drawn too when r's pens have a surface: the status dot, the pattern in
 * bold, then the note with kokoro's status after it, wrapping below; a row whose point kokoro lacks keeps the add
 * column whatever it shows, so the layout never moves. Returns the y below it. */
static s32 lay_point(const struct jpr_panel *p, const struct jpr_item *it, const struct reply_pens *r, s32 y,
                     s32 left, s32 right) {
    static const char *const suffix[] = {"", " \xC2\xB7 up next", " \xC2\xB7 learning", " \xC2\xB7 known"};
    const struct jpr_surface *s = r->pattern.s;
    struct jpr_point pt;
    u8 text[320];
    u32 n, k, state, column;
    s32 base = y + JPR_GR_FIRST, x, edge = right, last = base;
    jpr_reply_point(p->gr.reply, it, &pt);
    column = point_column(p, pt.id, pt.status, &state);
    if (pt.status == JPR_POINT_NONE && pt.id) edge = right - JPR_GR_ADD_W - 4;
    if (s)
        jpr_round_rect(s, left, y + 7, JPR_GR_DOT, JPR_GR_DOT, 3,
                       state == JPR_POINT_KNOWN ? JPR_COLOR_KNOWN : state == JPR_POINT_LEARNING ? JPR_COLOR_LEARNING :
                       JPR_COLOR_UNDERLINE, 0, 0, r->pattern.clip_top, r->pattern.clip_bottom);
    x = draw_pattern(&r->pattern, left + JPR_GR_DOT + 4, base, edge, p->gr.reply + pt.pattern, pt.pattern_len);
    n = pt.note_len < sizeof text - 24 ? pt.note_len : sizeof text - 24;
    while (n < pt.note_len && n && (p->gr.reply[pt.note + n] & 0xC0) == 0x80) n--;   /* whole characters */
    jpr_memcpy(text, p->gr.reply + pt.note, n);
    for (k = 0; suffix[pt.status][k]; k++) text[n++] = (u8)suffix[pt.status][k];
    if (n) last = jpr_draw_wrapped(&r->muted, x + 6, base, left + JPR_GR_DOT + 4, edge, JPR_GR_PITCH, 0x7FFFFFFF, text, n);
    if (s && column) draw_column(r, column, y, right);
    return last - JPR_GR_FIRST + JPR_GR_PITCH;
}

/* One item's rows, the first starting at y (its top); returns the y below its last row. Chunks are
 * in the larger face, so their rows are taller. */
static s32 lay_item(const struct jpr_panel *p, const struct jpr_item *it, const struct reply_pens *r, s32 y, s32 left,
                    s32 right) {
    const struct jpr_pen *pen = it->kind == JPR_ITEM_CHUNK ? &r->chunk : it->kind == JPR_ITEM_MEANING ? &r->meaning :
                                it->kind == JPR_ITEM_NOTE ? &r->note : it->kind == JPR_ITEM_TRANSLATION ?
                                &r->translation : &r->plain;
    s32 pitch = it->kind == JPR_ITEM_CHUNK ? JPR_SENTENCE_PITCH : JPR_GR_PITCH;
    s32 first = it->kind == JPR_ITEM_CHUNK ? 17 : JPR_GR_FIRST;   /* the baseline, below the row's top */
    s32 base;
    if (it->kind == JPR_ITEM_GRAMMAR) return lay_point(p, it, r, y, left, right);
    base = jpr_draw_wrapped(pen, left, y + first, left, right, pitch, 0x7FFFFFFF, p->gr.reply + it->start, it->len);
    return base - first + pitch;
}

/* Lays out the reply from y = top, or draws it too when s is set (rows outside [clip_top, clip_bottom)
 * are not drawn): the translation in a box with an accent bar, then each chunk as a block, every other
 * block shaded, its grammar points under it, and last the points of no chunk in an unshaded block
 * titled "Other grammar". With collect, the point rows are kept there (top 0 only). Returns the y below
 * the last block. */
static s32 lay_reply(const struct jpr_panel *p, struct jpr_grammar_view *collect, const struct jpr_surface *s,
                     const struct jpr_font *f, s32 top, s32 clip_top, s32 clip_bottom) {
    const struct jpr_grammar_view *g = &p->gr;
    struct jpr_item items[JPR_ITEMS_MAX];
    struct reply_pens measure, on_bg, on_shade, on_card;
    u32 n = jpr_reply_items(g->reply, g->reply_len, items, JPR_ITEMS_MAX), k = 0;
    s32 y = top;

    reply_pens(&measure, 0, f, 0, 0, 0);
    if (s) {
        reply_pens(&on_bg, s, f, JPR_COLOR_BG, clip_top, clip_bottom);
        reply_pens(&on_shade, s, f, JPR_COLOR_SHADE, clip_top, clip_bottom);
        reply_pens(&on_card, s, f, JPR_COLOR_CARD, clip_top, clip_bottom);
    }
    if (n && items[0].kind == JPR_ITEM_TRANSLATION) {
        s32 h = lay_item(p, &items[0], &measure, y + 3, GR_X + 4, JPR_SCREEN_W - 12) + 1 - y;
        if (s) {
            jpr_round_rect(s, 8, y, JPR_SCREEN_W - 16, h, 4, JPR_COLOR_CARD, 0, 0, clip_top, clip_bottom);
            jpr_round_rect(s, 8, y, 3, h, 0, JPR_COLOR_ACCENT, 0, 0, clip_top, clip_bottom);
            lay_item(p, &items[0], &on_card, y + 3, GR_X + 4, JPR_SCREEN_W - 12);
        }
        y += h + 6;
        k = 1;
    }
    for (; k < n && items[k].block == 0; k++)
        y = lay_item(p, &items[k], s ? &on_bg : &measure, y, JPR_CARD_X, JPR_CARD_RIGHT);
    while (k < n) {
        u32 block = items[k].block, end = k, j;
        int other = block == JPR_BLOCK_OTHER, shaded = (block & 1) && !other;
        s32 h, row = y + 4;
        while (end < n && items[end].block == block) end++;
        if (other) row += JPR_GR_PITCH;   /* the title */
        for (j = k; j < end; j++) {
            if (collect && items[j].kind == JPR_ITEM_GRAMMAR && collect->point_count < JPR_GR_POINTS) {
                struct jpr_gr_point *pt = &collect->points[collect->point_count++];
                struct jpr_point fields;
                jpr_reply_point(g->reply, &items[j], &fields);
                pt->id = fields.id;
                pt->top = (u16)(row - top);
                pt->status = (u8)fields.status;
                pt->slot = fields.status == JPR_POINT_NONE && fields.id;
                collect->other_count += (u32)other;
            }
            row = lay_item(p, &items[j], &measure, row, GR_X, JPR_CARD_RIGHT);
        }
        h = row + 4 - y;
        if (s) {
            const struct reply_pens *r = shaded ? &on_shade : &on_bg;
            if (shaded) jpr_round_rect(s, 4, y, JPR_SCREEN_W - 8, h, 4, JPR_COLOR_SHADE, 0, 0, clip_top, clip_bottom);
            row = y + 4;
            if (other) {
                jpr_draw_ascii(&r->title, GR_X, row + JPR_GR_FIRST, "Other grammar");
                row += JPR_GR_PITCH;
            }
            for (j = k; j < end; j++) row = lay_item(p, &items[j], r, row, GR_X, JPR_CARD_RIGHT);
        }
        y += h + 4;
        k = end;
    }
    return y;
}

/* Where each point row's tappable column is, and what it shows, for the test driver. */
static void sync_points(struct jpr_panel *p) {
    u32 k, buttons = 0, state;
    s32 first = 0;
    for (k = 0; k < p->gr.point_count; k++) {
        const struct jpr_gr_point *pt = &p->gr.points[k];
        u32 column = point_column(p, pt->id, pt->status, &state);
        if (column != COL_ADD && column != COL_RETRY) continue;
        if (!buttons) first = JPR_GR_TOP + 2 + (s32)pt->top - (s32)p->gr.scroll;
        buttons++;
    }
    p->st.gr_points = p->gr.point_count;
    p->st.gr_other = p->gr.other_count;
    p->st.gr_buttons = buttons;
    p->st.gr_button_y = first > 0 ? (u32)first : 0;
    p->st.gr_add_state = p->gr.add_id ? JPR_ADD_ASKING : p->gr.last_outcome;
}

/* A press at y in the add column: the point of that row is added, if its button is showing. */
static int tap_point(struct jpr_panel *p, u32 y) {
    s32 at = (s32)y - (JPR_GR_TOP + 2) + (s32)p->gr.scroll;
    u32 k, state, column;
    for (k = 0; k < p->gr.point_count; k++) {
        const struct jpr_gr_point *pt = &p->gr.points[k];
        if (!pt->slot || at < (s32)pt->top || at >= (s32)pt->top + JPR_GR_PITCH) continue;
        column = point_column(p, pt->id, pt->status, &state);
        if (column != COL_ADD && column != COL_RETRY) return 0;
        p->gr.add_id = pt->id;
        p->gr.add_wanted = 1;
        p->st.taps++;
        return 1;
    }
    return 0;
}

/* The session's state for a point, the oldest dropped when full. */
static void remember_point(struct jpr_panel *p, u32 id, u32 state) {
    u32 k;
    for (k = 0; k < p->gr.over_count; k++)
        if (p->gr.over[k].entry == id) {
            p->gr.over[k].state = state;
            return;
        }
    if (p->gr.over_count == JPR_OVERRIDES) {
        for (k = 1; k < JPR_OVERRIDES; k++) p->gr.over[k - 1] = p->gr.over[k];
        p->gr.over_count--;
    }
    p->gr.over[p->gr.over_count].entry = id;
    p->gr.over[p->gr.over_count++].state = state;
}

void jpr_panel_point_added(struct jpr_panel *p, u32 id, u32 outcome, u32 state) {
    if (id == p->gr.add_id) {
        p->gr.add_id = 0;
        p->gr.add_wanted = 0;
    }
    p->gr.last_id = id;
    p->gr.last_outcome = outcome;
    if (state) remember_point(p, id, state);
    sync_points(p);
}

void jpr_panel_grammar_result(struct jpr_panel *p, const struct jpr_font *f, u32 id, u32 failure, u32 code,
                              u32 http_status, const u8 *text, u32 len) {
    if (id != p->gr.job || p->gr.state != JPR_GR_ASKING) return;
    p->gr.failure = failure;
    p->gr.code = code;
    p->gr.http_status = http_status;
    p->st.gr_failure = failure;
    p->st.gr_http = http_status;
    if (failure == JPR_NET_OK) {
        if (len > JPR_REPLY_MAX) len = JPR_REPLY_MAX;
        jpr_memcpy(p->gr.reply, text, len);
        p->gr.reply_len = len;
        jpr_memcpy(p->gr.answered, p->gr.ex.sentence, p->gr.ex.sentence_len);
        p->gr.answered_len = p->gr.ex.sentence_len;
        p->gr.point_count = 0;
        p->gr.other_count = 0;
        p->gr.height = (u32)lay_reply(p, &p->gr, 0, f, 0, 0, 0);
        p->gr.state = JPR_GR_DONE;
    } else {
        jpr_grammar_message(p->gr.message, JPR_MESSAGE_MAX, failure, code, http_status, text, len);
        jpr_memcpy(p->st.gr_message, p->gr.message, JPR_MESSAGE_MAX);
        p->gr.state = JPR_GR_ERROR;
    }
    sync_grammar(p);
}

static void sync_mix(struct jpr_panel *p) {
    p->st.mix_open = p->mix.open;
    p->st.mix_music = p->mix.levels.music;
    p->st.mix_effects = p->mix.levels.effects;
    p->st.mix_voice = p->mix.levels.voice;
    p->st.mix_duck = p->mix.levels.duck;
}

void jpr_panel_mix_levels(struct jpr_panel *p, const struct jpr_mix_levels *l) {
    p->mix.levels = *l;
    sync_mix(p);
}

void jpr_panel_mix_saved(struct jpr_panel *p) {
    p->mix.save = 0;
}

void jpr_panel_mix_close(struct jpr_panel *p) {
    const struct jpr_mix_levels *a = &p->mix.levels, *b = &p->mix.opened;
    if (!p->mix.open) return;
    p->mix.open = 0;
    if (a->music != b->music || a->effects != b->effects || a->voice != b->voice || a->duck != b->duck)
        p->mix.save = 1;
    sync_mix(p);
}

/* A level row's -, step or + under (x, y) moves that level. */
static void press_level(u8 *level, u32 x) {
    if (x >= JPR_MIX_MINUS_X && x < JPR_MIX_MINUS_X + JPR_MIX_BTN_W) {
        if (*level > 0) (*level)--;
    } else if (x >= JPR_MIX_PLUS_X && x < JPR_MIX_PLUS_X + JPR_MIX_BTN_W) {
        if (*level < JPR_MIX_LEVELS) (*level)++;
    } else if (x >= JPR_MIX_BAR_X && x < JPR_MIX_BAR_X + JPR_MIX_LEVELS * JPR_MIX_STEP) {
        *level = (u8)((x - JPR_MIX_BAR_X) / JPR_MIX_STEP + 1);
    }
}

static void press_mix(struct jpr_panel *p, u32 x, u32 y) {
    u8 *rows[3];
    u32 k;
    rows[0] = &p->mix.levels.music;
    rows[1] = &p->mix.levels.effects;
    rows[2] = &p->mix.levels.voice;
    if (inside(x, y, JPR_BACK_X, JPR_BTN_Y, JPR_BTN_W, JPR_BTN_H)) {
        jpr_panel_mix_close(p);
        return;
    }
    for (k = 0; k < 3; k++)
        if (y >= (u32)JPR_MIX_ROW_Y(k) && y < (u32)(JPR_MIX_ROW_Y(k) + JPR_MIX_ROW_H)) press_level(rows[k], x);
    if (inside(x, y, JPR_MIX_TOGGLE_X, JPR_MIX_DUCK_Y, JPR_MIX_TOGGLE_W, JPR_MIX_ROW_H))
        p->mix.levels.duck = (u8)!p->mix.levels.duck;
    sync_mix(p);
}

static void press_grammar(struct jpr_panel *p, u32 x, u32 y) {
    if (p->gr.state == JPR_GR_DONE && y >= JPR_GR_TOP && y < JPR_BAR_TOP - 1 && x >= JPR_CARD_RIGHT - JPR_GR_ADD_W &&
        tap_point(p, y)) {
        /* a point's "+ Add": the platform sends it when the worker is free */
    } else if (inside(x, y, JPR_BACK_X, JPR_BTN_Y, JPR_BTN_W, JPR_BTN_H)) {
        close_grammar(p);
    } else if (p->gr.state == JPR_GR_ERROR && inside(x, y, JPR_RETRY_X, JPR_BTN_Y, JPR_RETRY_W, JPR_BTN_H)) {
        ask(p);
    } else if (inside(x, y, JPR_UP_X, JPR_BTN_Y, JPR_ARROW_W, JPR_BTN_H)) {
        scroll_reply(p, (s32)p->gr.scroll - 3 * JPR_GR_PITCH);
    } else if (inside(x, y, JPR_DOWN_X, JPR_BTN_Y, JPR_ARROW_W, JPR_BTN_H)) {
        scroll_reply(p, (s32)p->gr.scroll + 3 * JPR_GR_PITCH);
    } else if (y >= JPR_GR_TOP && y < JPR_BAR_TOP) {
        p->dragging = 1;
        p->moved = 0;
        p->press_x = x;
        p->press_y = y;
        p->press_scroll = p->gr.scroll;
    }
}

/* The rows view's scroll, kept within what there is to show. */
static void scroll_rows(struct jpr_panel *p, s32 want) {
    s32 limit = (s32)max_scroll(p);
    if (want > limit) want = limit;
    if (want < 0) want = 0;
    p->st.scroll = (u32)want;
}

/* Scrolls the rows so the selected word's line shows above the card. */
static void reveal(struct jpr_panel *p) {
    u32 at = p->st.sel_start, k;
    for (k = 0; k < p->layout.line_count; k++) {
        s32 t = p->line_top[k];
        if (t < 0 || at < p->layout.lines[k].start || at >= p->layout.lines[k].end) continue;
        if (t + JPR_PEEK_LINE - (s32)p->st.scroll > text_bottom(p)) scroll_rows(p, t + JPR_PEEK_LINE - text_bottom(p));
        if (t - (s32)p->st.scroll < JPR_TEXT_TOP) scroll_rows(p, t - JPR_TEXT_TOP);
        return;
    }
}

void jpr_panel_touch(struct jpr_panel *p, u32 down, u32 x, u32 y, const struct jpr_font *f, const u16 *text,
                     u32 len) {
    if (down && !p->prev_down) {
        if (p->mix.open) {
            press_mix(p, x, y);
        } else if (p->gr.open) {
            press_grammar(p, x, y);
        } else if (inside(x, y, JPR_SOUND_X, JPR_BTN_Y, JPR_SOUND_W, JPR_BTN_H)) {
            p->mix.open = 1;
            p->mix.opened = p->mix.levels;
            clear_selection(p);
            if (p->rows) scroll_rows(p, (s32)p->st.scroll);   /* the card has gone: less to scroll */
            sync_mix(p);
        } else if (inside(x, y, JPR_VOICE_X, JPR_BTN_Y, JPR_VOICE_W, JPR_BTN_H) ||
                   inside(x, y, JPR_SLOW_X, JPR_BTN_Y, JPR_VOICE_W, JPR_BTN_H)) {
            if (p->st.voice_available) {
                p->voice_request = inside(x, y, JPR_SLOW_X, JPR_BTN_Y, JPR_VOICE_W, JPR_BTN_H) ? 2 : 1;
                p->st.voice_requests++;
            }
        } else if (p->rows && (inside(x, y, JPR_PREV_X, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H) ||
                               inside(x, y, JPR_NEXT_X, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H))) {
            s32 want = (s32)p->st.scroll + (x >= JPR_NEXT_X ? JPR_PEEK_SCROLL : -JPR_PEEK_SCROLL);
            scroll_rows(p, want);
            p->st.taps++;
        } else if (inside(x, y, JPR_PREV_X, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H)) {
            if (p->st.view_block > 0) {
                view(p, p->st.view_block - 1);
                p->st.taps++;
            }
        } else if (inside(x, y, JPR_NEXT_X, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H)) {
            if (p->st.view_block + 1 < p->st.block_count) {
                view(p, p->st.view_block + 1);
                p->st.taps++;
            }
        } else if (inside(x, y, JPR_GRAMMAR_X, JPR_BTN_Y, JPR_GRAMMAR_W, JPR_BTN_H)) {
            if (p->rows ? any_text(p, text) : block_has_text(p, text)) open_grammar(p, text, len);
        } else if ((s32)y < text_bottom(p)) {
            p->dragging = 1;
            p->moved = 0;
            p->press_x = x;
            p->press_y = y;
            p->press_scroll = p->st.scroll;
        } else if (add_shown(p) && inside(x, y, JPR_ADD_X, JPR_KOKORO_TOP, JPR_ADD_W, JPR_KOKORO_ROW)) {
            tap_add(p, text, len);
            p->st.taps++;
        } else if ((s32)y >= jpr_panel_card_top(p) && y < JPR_CARD_BOTTOM && p->st.selected) {
            p->dragging = 2;
            p->moved = 0;
            p->press_x = x;
            p->press_y = y;
            p->press_scroll = p->card_scroll;
        }
    }
    if (down && p->dragging) {
        u32 dx = x > p->press_x ? x - p->press_x : p->press_x - x;
        u32 dy = y > p->press_y ? y - p->press_y : p->press_y - y;
        if (dx > p->moved) p->moved = dx;
        if (dy > p->moved) p->moved = dy;
        if (p->moved > JPR_TAP_SLOP) {
            s32 want = (s32)p->press_scroll + (s32)p->press_y - (s32)y;
            if (p->dragging == 2) {
                s32 limit = (s32)(p->card_height + JPR_CARD_PAD) - (s32)jpr_panel_card_area(p);
                if (limit < 0) limit = 0;
                if (want > limit) want = limit;
                if (want < 0) want = 0;
                p->card_scroll = (u32)want;
                p->st.card_scroll = p->card_scroll;
            } else if (p->gr.open) {
                scroll_reply(p, want);
            } else {
                u32 limit = max_scroll(p);
                if (want < 0) want = 0;
                if ((u32)want > limit) want = (s32)limit;
                p->st.scroll = (u32)want;
            }
        }
    }
    if (!down && p->dragging == 1 && p->moved <= JPR_TAP_SLOP && !p->gr.open && !p->mix.open) {
        s32 k = jpr_panel_hit(p, f, text, p->press_x, p->press_y);
        if (p->rows) {   /* a word opens its card; it again, or anything else in the rows, closes it */
            if (k >= 0 && p->st.selected != (u32)k + 1) {
                select_unit(p, (u32)k);
                reveal(p);
            } else {
                clear_selection(p);
            }
            scroll_rows(p, (s32)p->st.scroll);
        } else if (k >= 0) {
            select_unit(p, (u32)k);
        }
    }
    if (!down) p->dragging = 0;
    p->prev_down = down;
    sync_grammar(p);
}

void jpr_panel_card(struct jpr_panel *p, const struct jpr_font *f, const u16 *text, int ok, u32 entry_len,
                    u32 label_len) {
    u32 at = 0;
    p->card_wanted = 0;
    p->label.len = 0;
    if (!p->st.selected) return;
    if (!ok || jpr_card_parse(&p->card, p->entry_bytes, entry_len) ||
        (label_len && jpr_text_read(&p->label, p->label_bytes, label_len, &at))) {
        p->label.len = 0;
        p->st.card_status = JPR_CARD_UNREADABLE;
        return;
    }
    p->st.card_status = JPR_CARD_OK;
    measure_card(p, f, text);
}

/* A rectangle cut to the text area, rows [top, bottom). */
static void fill_text_area(const struct jpr_surface *s, s32 x, s32 y, s32 w, s32 h, jpr_rgb c, s32 top, s32 bottom) {
    s32 y1 = y + h;
    if (y < top) y = top;
    if (y1 > bottom) y1 = bottom;
    if (y1 > y && w > 0) jpr_fill(s, x, y, w, y1 - y, c);
}

static jpr_rgb mark_colour(const struct jpr_panel *p, u32 u) {
    u32 m = p->st.kokoro_on ? p->marks[u] : 0;
    return m == JPR_KOKORO_KNOWN ? JPR_COLOR_KNOWN : m == JPR_KOKORO_LEARNING ? JPR_COLOR_LEARNING : JPR_COLOR_UNDERLINE;
}

/* A line from its box's top: baseline the baseline's offset in the box, pitch the box's height. */
static void draw_line(const struct jpr_panel *p, const struct jpr_pen *plain, const struct jpr_pen *chosen,
                      const u16 *text, struct jpr_line line, s32 top, s32 baseline, s32 pitch) {
    u32 i = line.start, u = 0;
    s32 x = JPR_TEXT_X, base = top + baseline;
    u16 cp;
    while (jpr_next_unit(text, &i, line.end, &cp)) {
        u32 at = i - 1;
        s32 adv = (s32)jpr_font_advance(plain->f, JPR_FACE_TEXT, cp);
        int in, sel;
        while (u < p->st.unit_count && (u32)p->units[u].start + p->units[u].len <= at) u++;
        in = u < p->st.unit_count && p->units[u].start <= at;
        sel = in && p->st.selected == u + 1;
        if (sel) fill_text_area(plain->s, x, top + 1, adv, pitch - 2, JPR_COLOR_SELECT, plain->clip_top, plain->clip_bottom);
        jpr_draw_char(sel ? chosen : plain, x, base, cp);
        if (in) {
            int last = at + 1 == (u32)p->units[u].start + p->units[u].len;
            jpr_rgb c = sel ? JPR_COLOR_ACCENT : mark_colour(p, u);
            s32 h = sel ? JPR_UNDERLINE_SEL : c == JPR_COLOR_UNDERLINE ? JPR_UNDERLINE_PLAIN : JPR_UNDERLINE_MARK;
            fill_text_area(plain->s, x, base + JPR_UNDERLINE, adv - (last ? 2 : 0), h, c, plain->clip_top, plain->clip_bottom);
        }
        x += adv;
    }
}

/* The selected unit as it appears in the text. */
static s32 draw_word(const struct jpr_pen *pen, s32 x, s32 base, const u16 *text, const struct jpr_unit *u) {
    u32 k;
    for (k = 0; k < u->len; k++) x += (s32)jpr_draw_char(pen, x, base, text[u->start + k]);
    return x;
}

static const char *problem(const struct jpr_panel_state *st) {
    u32 file = st->data_error >> 8;
    if (st->font_status == JPR_FONT_MISSING) return "No font: copy jpreader/font.bin";
    if (st->font_status == JPR_FONT_BAD) return "font.bin is from another version";
    if (st->data_status == JPR_DATA_MISSING)
        return file == JPR_DATA_DICT ? "No dict.bin: copy jpreader/dict.bin" : "No annot.bin: copy jpreader/annot.bin";
    if (st->data_status == JPR_DATA_BAD)
        return file == JPR_DATA_DICT ? "dict.bin is from another version" : "annot.bin is from another version";
    if (st->data_status == JPR_DATA_MISMATCH) return "dict.bin does not match annot.bin";
    return 0;
}

/* The pens a card is drawn (or, without a surface, measured) with. */
struct card_pens {
    struct jpr_pen head, word, small, muted, pill, chip, badge;
};

/* Lays out (with measuring pens) or draws a readable card from baseline head; returns the last
 * baseline. Nothing stops at the card's bottom: the pens clip, and the card scrolls. */
static s32 lay_card(const struct jpr_panel *p, const struct card_pens *k, const u16 *text, s32 head) {
    const struct jpr_card *c = &p->card;
    const struct jpr_unit *u = &p->units[p->st.selected - 1];
    const struct jpr_surface *s = k->head.s;   /* null: measuring only */
    s32 base = head, x, w;
    u32 n;

    x = jpr_draw_utf8(&k->head, JPR_CARD_X, head, c->headword.p, c->headword.len);
    if (c->reading.len) x = jpr_draw_utf8(&k->muted, x + 8, head, c->reading.p, c->reading.len);
    if (c->pos.len) {   /* a pill at the right of the headword row, or on a row of its own */
        s32 px = JPR_CARD_X, py;
        w = (s32)jpr_pen_measure_utf8(&k->pill, c->pos.p, c->pos.len) + 12;
        if (JPR_CARD_RIGHT - w >= x + 8) {
            px = JPR_CARD_RIGHT - w;
            py = head;
        } else {
            base += JPR_CARD_PITCH + 4;
            py = base;
        }
        if (s) jpr_round_rect(s, px, py - 11, w, 15, 7, JPR_COLOR_PILL, 0, 0, k->pill.clip_top, k->pill.clip_bottom);
        jpr_draw_utf8(&k->pill, px + 6, py, c->pos.p, c->pos.len);
    }
    if (p->label.len) {   /* "in text <word> → <ending>", the ending on a chip when it fits */
        base += JPR_CARD_LABEL_PITCH;
        x = jpr_draw_ascii(&k->muted, JPR_CARD_X, base, "in text ");
        x = draw_word(&k->word, x, base, text, u) + 6;
        x += (s32)jpr_draw_char(&k->muted, x, base, 0x2192) + 6;
        w = (s32)jpr_pen_measure_utf8(&k->chip, p->label.p, p->label.len) + 8;
        if (x + w <= JPR_CARD_RIGHT) {
            if (s) jpr_round_rect(s, x, base - 11, w, 15, 4, JPR_COLOR_SELECT, 0, 0, k->chip.clip_top, k->chip.clip_bottom);
            jpr_draw_utf8(&k->chip, x + 4, base, p->label.p, p->label.len);
        } else {
            base = jpr_draw_wrapped(&k->small, x, base, JPR_CARD_X, JPR_CARD_RIGHT, JPR_CARD_PITCH, 0x7FFFFFFF,
                                    p->label.p, p->label.len);
        }
    }
    if (c->kind == JPR_ENTRY_LEMMA) {
        base += JPR_CARD_PITCH;
        jpr_draw_ascii(&k->muted, JPR_CARD_X, base, "no dictionary entry");
        return base;
    }
    for (n = 0; n < c->senses; n++) {
        s32 left = JPR_CARD_X;
        base += JPR_CARD_PITCH + 3;
        if (c->senses > 1) {   /* a round badge with the meaning's number */
            char digit[2] = {(char)('1' + n), 0};
            if (s) jpr_round_rect(s, JPR_CARD_X, base - 11, 14, 14, 7, JPR_COLOR_ACCENT, 0, 0, k->badge.clip_top,
                                  k->badge.clip_bottom);
            jpr_draw_ascii(&k->badge, JPR_CARD_X + ((14 - (s32)jpr_pen_advance(&k->badge, (u16)digit[0])) >> 1),
                           base, digit);
            left = JPR_CARD_X + 19;
        }
        base = jpr_draw_wrapped(&k->small, left, base, left, JPR_CARD_RIGHT, JPR_CARD_PITCH, 0x7FFFFFFF,
                                c->sense[n].p, c->sense[n].len);
    }
    return base;
}

/* Pens for a card on surface s (null: measuring pens), clipped to the rows between top and bottom. */
static void card_pens(struct card_pens *k, const struct jpr_surface *s, const struct jpr_font *f, s32 top,
                      s32 bottom) {
    if (!s) {
        jpr_memset(k, 0, (u32)sizeof *k);
        k->head.f = k->word.f = k->small.f = k->muted.f = k->pill.f = k->chip.f = k->badge.f = f;
        k->head.face = k->word.face = JPR_FACE_TEXT;
        k->small.face = k->muted.face = k->pill.face = k->chip.face = k->badge.face = JPR_FACE_SMALL;
    } else {
        jpr_pen_init(&k->head, s, f, JPR_FACE_TEXT, JPR_COLOR_CARD, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&k->word, s, f, JPR_FACE_TEXT, JPR_COLOR_CARD, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&k->small, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&k->muted, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_MUTED, top, bottom);
        jpr_pen_init(&k->pill, s, f, JPR_FACE_SMALL, JPR_COLOR_PILL, JPR_COLOR_PILL_TEXT, top, bottom);
        jpr_pen_init(&k->chip, s, f, JPR_FACE_SMALL, JPR_COLOR_SELECT, JPR_COLOR_TEXT, top, bottom);
        jpr_pen_init(&k->badge, s, f, JPR_FACE_SMALL, JPR_COLOR_ACCENT, JPR_COLOR_ON_ACCENT, top, bottom);
    }
    jpr_pen_bold(&k->head);
    jpr_pen_bold(&k->badge);
}

/* How tall the card's content is, from a layout with measuring pens (no surface). */
static void measure_card(struct jpr_panel *p, const struct jpr_font *f, const u16 *text) {
    struct card_pens k;
    s32 top = jpr_panel_card_top(p);
    card_pens(&k, 0, f, 0, 0);
    p->card_height = (u32)(lay_card(p, &k, text, top + JPR_CARD_HEAD) + 4 - top);
    p->card_scroll = 0;
    p->st.card_scroll = 0;
}

/* The card's last row in kokoro mode: a dot and the word's kokoro status, then the add button or what
 * became of adding it. */
void jpr_overrides_sent(struct jpr_override *o, u32 n) {
    u32 k;
    for (k = 0; k < n; k++)
        if (o[k].state == JPR_KOKORO_QUEUED) o[k].state = JPR_KOKORO_NEW;
}

void jpr_panel_kokoro_texts(const struct jpr_panel *p, const char **status, const char **after) {
    static const char *const names[] = {"Not in kokoro", "Up next in kokoro", "Learning", "Known", "Not in kokoro"};
    u32 mark = p->st.card_kokoro <= JPR_KOKORO_QUEUED ? p->st.card_kokoro : 0, add = p->st.add_state;
    *status = names[mark];
    *after = 0;
    if (add == JPR_ADD_FAILED) *status = "kokoro error";
    if (add == JPR_ADD_REJECTED) *status = "Token rejected: run jpreader kokoro-link";
    if (mark == JPR_KOKORO_QUEUED || add == JPR_ADD_QUEUED) *after = "Saved: will send later";
    else if (add == JPR_ADD_DONE) *after = mark == JPR_KOKORO_NEW ? "Added: up next" : "Already in kokoro";
}

static void draw_kokoro_row(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f) {
    static const jpr_rgb dots[] = {JPR_COLOR_RULE, JPR_COLOR_MUTED, JPR_COLOR_LEARNING, JPR_COLOR_KNOWN, JPR_COLOR_RULE};
    struct jpr_pen left, right;
    u32 mark = p->st.card_kokoro <= JPR_KOKORO_QUEUED ? p->st.card_kokoro : 0, add = p->st.add_state;
    s32 top = JPR_KOKORO_TOP, bottom = top + JPR_KOKORO_ROW, base = top + 13, w;
    const char *status, *after;
    int trouble = add == JPR_ADD_FAILED || add == JPR_ADD_REJECTED;

    jpr_panel_kokoro_texts(p, &status, &after);

    jpr_fill(s, JPR_CARD_EDGE + 1, top, JPR_SCREEN_W - 2 * JPR_CARD_EDGE - 2, 1, JPR_COLOR_CARD_BORDER);
    jpr_round_rect(s, JPR_CARD_X, top + 6, 7, 7, 3, dots[mark], 0, 0, top, bottom);
    jpr_pen_init(&left, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, trouble ? JPR_COLOR_ERROR : JPR_COLOR_MUTED, top, bottom);
    jpr_draw_ascii(&left, JPR_CARD_X + 12, base, status);

    jpr_pen_init(&right, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_MUTED, top, bottom);
    if (add == JPR_ADD_ASKING) {
        w = (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, "Adding") + (s32)jpr_font_advance(f, JPR_FACE_SMALL, 0x2026);
        jpr_draw_char(&right, jpr_draw_ascii(&right, JPR_CARD_RIGHT - w, base, "Adding"), base, 0x2026);
    } else if (after) {
        jpr_draw_ascii(&right, JPR_CARD_RIGHT - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, after), base, after);
    } else if (add_shown(p)) {
        static const char label[] = "+ Add to kokoro";
        struct jpr_pen on;
        jpr_round_rect(s, JPR_ADD_X, top + 2, JPR_ADD_W, JPR_KOKORO_ROW - 3, 7, JPR_COLOR_KNOWN, 0, 0, top, bottom);
        jpr_pen_init(&on, s, f, JPR_FACE_SMALL, JPR_COLOR_KNOWN, JPR_COLOR_ON_ACCENT, top, bottom);
        jpr_pen_bold(&on);
        w = (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, label) + (s32)(sizeof label - 1);   /* bold: a pixel more each */
        jpr_draw_ascii(&on, JPR_ADD_X + (JPR_ADD_W - w) / 2, base, label);
    }
}

/* The card panel: a rounded box from below the text to above the bar, with the selected word's entry,
 * a hint, or what is wrong with the data files. */
static void draw_card(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f,
                      const u16 *text) {
    struct card_pens k;
    struct jpr_pen error;
    const struct jpr_unit *u;
    const char *trouble = problem(&p->st);
    s32 top = jpr_panel_card_top(p), head = top + JPR_CARD_HEAD, x;

    jpr_round_rect(s, JPR_CARD_EDGE, top, JPR_SCREEN_W - 2 * JPR_CARD_EDGE, JPR_CARD_BOTTOM - top, 6, JPR_COLOR_CARD,
                   JPR_COLOR_CARD_BORDER, 1, 0, JPR_SCREEN_H);
    card_pens(&k, s, f, top + 1, card_inner_bottom(p));
    jpr_pen_init(&error, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_ERROR, top + 1, JPR_CARD_BOTTOM - 1);
    if (trouble) {
        jpr_draw_ascii(&error, JPR_CARD_X, head, trouble);
        return;
    }
    if (!p->st.selected) {
        if (p->st.unit_count) jpr_draw_ascii(&k.muted, JPR_CARD_X, head, "Tap a word to look it up");
        return;
    }
    u = &p->units[p->st.selected - 1];
    if (p->st.card_status == JPR_CARD_NO_ENTRY) {
        x = draw_word(&k.word, JPR_CARD_X, head, text, u);
        jpr_draw_ascii(&k.muted, x + 10, head, "no dictionary entry");
        return;
    }
    if (p->st.card_status == JPR_CARD_UNREADABLE) {
        jpr_draw_ascii(&error, JPR_CARD_X, head, "Could not read dict.bin");
        return;
    }
    if (p->st.card_status != JPR_CARD_OK) return;
    lay_card(p, &k, text, head - (s32)p->card_scroll);
    if (p->card_scroll + jpr_panel_card_area(p) < p->card_height + JPR_CARD_PAD)   /* more below */
        jpr_triangle_v(s, JPR_CARD_RIGHT - 4, card_inner_bottom(p) - 7, 3, 0, JPR_COLOR_MUTED);
    if (kokoro_row(p)) draw_kokoro_row(p, s, f);
}

/* Writes n (at most 99) in decimal and returns the number of characters. */
static u32 small_number(u32 n, char *out) {
    u32 tens = 0;
    if (n > 99) n = 99;
    while (n >= 10) {
        n -= 10;
        tens++;
    }
    if (!tens) {
        out[0] = (char)('0' + n);
        return 1;
    }
    out[0] = (char)('0' + tens);
    out[1] = (char)('0' + n);
    return 2;
}

static void draw_button(const struct jpr_surface *s, s32 x, int enabled, int left) {
    jpr_round_rect(s, x, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H, 6, enabled ? JPR_COLOR_BUTTON : JPR_COLOR_BUTTON_OFF, 0, 0, 0,
                   JPR_SCREEN_H);
    jpr_triangle(s, x + (JPR_PAGE_W >> 1), JPR_BTN_Y + (JPR_BTN_H >> 1), 7, left,
                 enabled ? JPR_COLOR_ARROW : JPR_COLOR_ARROW_OFF);
}

/* ▶ (play the clip) or "0.7x" (play it slowed): warm buttons, greyed out when the message has no clip. */
static void voice_button(const struct jpr_surface *s, const struct jpr_font *f, s32 x, int slow, int enabled) {
    jpr_rgb bg = enabled ? JPR_COLOR_VOICE : JPR_COLOR_BUTTON_OFF, fg = enabled ? JPR_COLOR_TEXT : JPR_COLOR_ARROW_OFF;
    jpr_round_rect(s, x, JPR_BTN_Y, JPR_VOICE_W, JPR_BTN_H, 6, bg, 0, 0, 0, JPR_SCREEN_H);
    if (slow) {
        struct jpr_pen pen;
        jpr_pen_init(&pen, s, f, JPR_FACE_SMALL, bg, fg, 0, JPR_SCREEN_H);
        jpr_draw_ascii(&pen, x + (JPR_VOICE_W - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, "0.7x")) / 2, JPR_COUNTER_BASE,
                       "0.7x");
    } else {
        jpr_triangle(s, x + (JPR_VOICE_W >> 1) + 2, JPR_BTN_Y + (JPR_BTN_H >> 1), 7, 0, fg);
    }
}

static void label_button(const struct jpr_surface *s, const struct jpr_font *f, s32 x, s32 w, const char *label,
                         int enabled) {
    struct jpr_pen pen;
    jpr_rgb bg = enabled ? JPR_COLOR_BUTTON : JPR_COLOR_BUTTON_OFF;
    jpr_round_rect(s, x, JPR_BTN_Y, w, JPR_BTN_H, 6, bg, 0, 0, 0, JPR_SCREEN_H);
    jpr_pen_init(&pen, s, f, JPR_FACE_SMALL, bg, enabled ? JPR_COLOR_TEXT : JPR_COLOR_ARROW_OFF, 0, JPR_SCREEN_H);
    jpr_draw_ascii(&pen, x + (w - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, label)) / 2, JPR_COUNTER_BASE, label);
}

static void arrow_button(const struct jpr_surface *s, s32 x, int up, int enabled) {
    jpr_round_rect(s, x, JPR_BTN_Y, JPR_ARROW_W, JPR_BTN_H, 6, enabled ? JPR_COLOR_BUTTON : JPR_COLOR_BUTTON_OFF, 0, 0,
                   0, JPR_SCREEN_H);
    jpr_triangle_v(s, x + (JPR_ARROW_W >> 1), JPR_BTN_Y + (JPR_BTN_H >> 1), 7, up,
                   enabled ? JPR_COLOR_ARROW : JPR_COLOR_ARROW_OFF);
}

/* The reading view's Grammar button: filled with the accent, its label white and bold. */
static void grammar_button(const struct jpr_surface *s, const struct jpr_font *f, int enabled) {
    struct jpr_pen pen;
    jpr_rgb bg = enabled ? JPR_COLOR_ACCENT : JPR_COLOR_BUTTON_OFF;
    jpr_round_rect(s, JPR_GRAMMAR_X, JPR_BTN_Y, JPR_GRAMMAR_W, JPR_BTN_H, 6, bg, 0, 0, 0, JPR_SCREEN_H);
    jpr_pen_init(&pen, s, f, JPR_FACE_SMALL, bg, enabled ? JPR_COLOR_ON_ACCENT : JPR_COLOR_ARROW_OFF, 0, JPR_SCREEN_H);
    jpr_pen_bold(&pen);
    jpr_draw_ascii(&pen, JPR_GRAMMAR_X + (JPR_GRAMMAR_W - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, "Grammar") - 7) / 2,
                   JPR_COUNTER_BASE, "Grammar");
}

static u32 text_len(const char *t) {
    u32 n = 0;
    while (t[n]) n++;
    return n;
}

/* The grammar view (spec 8.2): the sentence, then the reply, a message, or "Asking…". */
static void draw_grammar(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f) {
    const struct jpr_grammar_view *g = &p->gr;
    struct jpr_pen sentence, muted, error;
    s32 first = JPR_GR_TOP + JPR_GR_FIRST;

    jpr_fill(s, 0, 0, JPR_SCREEN_W, JPR_SCREEN_H, JPR_COLOR_BG);
    jpr_pen_init(&sentence, s, f, JPR_FACE_TEXT, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, JPR_GR_SENTENCE_BOTTOM);
    jpr_draw_wrapped(&sentence, JPR_TEXT_X, JPR_TEXT_TOP + JPR_TEXT_BASELINE, JPR_TEXT_X, JPR_TEXT_X + JPR_TEXT_W,
                     JPR_SENTENCE_PITCH, 0x7FFFFFFF, g->ex.sentence, g->ex.sentence_len);
    jpr_fill(s, 0, JPR_GR_TOP - 1, JPR_SCREEN_W, 1, JPR_COLOR_RULE);

    jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_MUTED, JPR_GR_TOP, JPR_BAR_TOP - 1);
    jpr_pen_init(&error, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_ERROR, JPR_GR_TOP, JPR_BAR_TOP - 1);
    if (g->state == JPR_GR_ASKING) {
        s32 x = jpr_draw_ascii(&muted, JPR_CARD_X, first, "Asking");
        jpr_draw_char(&muted, x, first, 0x2026);
    } else if (g->state == JPR_GR_ERROR) {
        jpr_draw_wrapped(&error, JPR_CARD_X, first, JPR_CARD_X, JPR_CARD_RIGHT, JPR_GR_PITCH, JPR_CARD_LAST,
                         (const u8 *)g->message, text_len(g->message));
    } else if (g->state == JPR_GR_DONE) {
        lay_reply(p, 0, s, f, JPR_GR_TOP + 2 - (s32)g->scroll, JPR_GR_TOP, JPR_BAR_TOP - 1);
    }
    jpr_fill(s, 0, JPR_BAR_TOP - 1, JPR_SCREEN_W, 1, JPR_COLOR_RULE);

    label_button(s, f, JPR_BACK_X, JPR_BTN_W, "Back", 1);
    if (g->state == JPR_GR_ERROR) label_button(s, f, JPR_RETRY_X, JPR_RETRY_W, "Retry", 1);
    if (g->state == JPR_GR_DONE && g->height > reply_area()) {
        arrow_button(s, JPR_UP_X, 1, g->scroll > 0);
        arrow_button(s, JPR_DOWN_X, 0, g->scroll + reply_area() < g->height);
    }
}

/* A rounded button with a centred label, anywhere on the panel. */
static void mix_button(const struct jpr_surface *s, const struct jpr_font *f, s32 x, s32 y, s32 w, const char *label,
                       jpr_rgb bg, jpr_rgb fg) {
    struct jpr_pen pen;
    jpr_round_rect(s, x, y, w, JPR_MIX_ROW_H, 6, bg, 0, 0, 0, JPR_SCREEN_H);
    jpr_pen_init(&pen, s, f, JPR_FACE_SMALL, bg, fg, 0, JPR_SCREEN_H);
    jpr_draw_ascii(&pen, x + (w - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, label)) / 2, y + 20, label);
}

/* The Sound view (stage 11). */
static void draw_mix(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f) {
    static const char *const names[3] = {"Music", "Effects", "Voice"};
    u32 levels[3], k, i;
    struct jpr_pen title, label;
    const struct jpr_mix_levels *l = &p->mix.levels;
    levels[0] = l->music;
    levels[1] = l->effects;
    levels[2] = l->voice;
    jpr_fill(s, 0, 0, JPR_SCREEN_W, JPR_SCREEN_H, JPR_COLOR_BG);
    jpr_pen_init(&title, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, JPR_SCREEN_H);
    jpr_pen_bold(&title);
    jpr_draw_ascii(&title, JPR_MIX_LABEL_X, JPR_MIX_TITLE_BASE, "Sound");
    jpr_pen_init(&label, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, JPR_SCREEN_H);
    for (k = 0; k < 3; k++) {
        s32 y = JPR_MIX_ROW_Y(k);
        jpr_draw_ascii(&label, JPR_MIX_LABEL_X, y + 20, names[k]);
        mix_button(s, f, JPR_MIX_MINUS_X, y, JPR_MIX_BTN_W, "-", JPR_COLOR_BUTTON, JPR_COLOR_TEXT);
        for (i = 0; i < JPR_MIX_LEVELS; i++)
            jpr_round_rect(s, JPR_MIX_BAR_X + (s32)(i * JPR_MIX_STEP), y + 6, JPR_MIX_STEP - 2, JPR_MIX_ROW_H - 12, 2,
                           i < levels[k] ? JPR_COLOR_ACCENT : JPR_COLOR_BUTTON_OFF, 0, 0, 0, JPR_SCREEN_H);
        mix_button(s, f, JPR_MIX_PLUS_X, y, JPR_MIX_BTN_W, "+", JPR_COLOR_BUTTON, JPR_COLOR_TEXT);
    }
    jpr_draw_ascii(&label, JPR_MIX_LABEL_X, JPR_MIX_DUCK_Y + 20, "Quieter music while a voice plays");
    if (l->duck) mix_button(s, f, JPR_MIX_TOGGLE_X, JPR_MIX_DUCK_Y, JPR_MIX_TOGGLE_W, "On", JPR_COLOR_ACCENT,
                            JPR_COLOR_ON_ACCENT);
    else mix_button(s, f, JPR_MIX_TOGGLE_X, JPR_MIX_DUCK_Y, JPR_MIX_TOGGLE_W, "Off", JPR_COLOR_BUTTON, JPR_COLOR_TEXT);
    jpr_fill(s, 0, JPR_BAR_TOP - 1, JPR_SCREEN_W, 1, JPR_COLOR_RULE);
    label_button(s, f, JPR_BACK_X, JPR_BTN_W, "Back", 1);
}

/* The bar's Sound button: a small speaker. */
static void sound_button(const struct jpr_surface *s) {
    s32 cy = JPR_BTN_Y + (JPR_BTN_H >> 1);
    jpr_round_rect(s, JPR_SOUND_X, JPR_BTN_Y, JPR_SOUND_W, JPR_BTN_H, 6, JPR_COLOR_BUTTON, 0, 0, 0, JPR_SCREEN_H);
    s32 c;
    jpr_fill(s, JPR_SOUND_X + 5, cy - 3, 4, 7, JPR_COLOR_ARROW);              /* the box */
    for (c = 0; c < 6; c++)                                                   /* the cone, widening right */
        jpr_fill(s, JPR_SOUND_X + 9 + c, cy - 3 - c, 1, 7 + 2 * c, JPR_COLOR_ARROW);
    jpr_fill(s, JPR_SOUND_X + 17, cy - 2, 1, 5, JPR_COLOR_ARROW);             /* two waves */
    jpr_fill(s, JPR_SOUND_X + 20, cy - 4, 1, 9, JPR_COLOR_ARROW);
}

/* The meta drawn with its right edge at right: the type's pill, then the rest, muted. */
static void draw_meta(const struct jpr_peek_rows *r, const struct jpr_peek_row *row, const struct jpr_surface *s,
                      const struct jpr_font *f, s32 right, s32 base, s32 bottom) {
    struct jpr_pen muted, pill;
    s32 x;
    jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_MUTED, 0, bottom);
    jpr_pen_init(&pill, s, f, JPR_FACE_SMALL, JPR_COLOR_PILL, JPR_COLOR_PILL_TEXT, 0, bottom);
    x = right - meta_width(r, row, &muted);
    if (row->pill.len) {
        s32 w = (s32)jpr_pen_measure_utf8(&pill, pool_at(r, row->pill), row->pill.len) + 12;
        jpr_round_rect(s, x, base - 11, w, 15, 4, JPR_COLOR_PILL, 0, 0, 0, bottom);
        jpr_draw_utf8(&pill, x + 6, base, pool_at(r, row->pill), row->pill.len);
        x += w + 6;
    }
    if (row->meta.len) jpr_draw_utf8(&muted, x, base, pool_at(r, row->meta), row->meta.len);
}

/* The bar's up and down in the rows view: the page buttons' places, with how many rows are further down. */
static void rows_bar(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f) {
    u32 limit = max_scroll(p), k, below = 0, n = p->rows->n < JPR_PEEK_ROWS ? p->rows->n : JPR_PEEK_ROWS;
    struct jpr_pen small;
    char counter[4];
    s32 x;
    for (k = 0; k < 2; k++) {
        int on = k ? p->st.scroll < limit : p->st.scroll > 0;
        x = k ? JPR_NEXT_X : JPR_PREV_X;
        jpr_round_rect(s, x, JPR_BTN_Y, JPR_PAGE_W, JPR_BTN_H, 6, on ? JPR_COLOR_BUTTON : JPR_COLOR_BUTTON_OFF, 0, 0, 0,
                       JPR_SCREEN_H);
        jpr_triangle_v(s, x + (JPR_PAGE_W >> 1), JPR_BTN_Y + (JPR_BTN_H >> 1), 7, !k,
                       on ? JPR_COLOR_ARROW : JPR_COLOR_ARROW_OFF);
    }
    for (k = 0; k < n; k++)
        if ((s32)(p->geo[k].top + p->geo[k].height) - (s32)p->st.scroll > text_bottom(p) + 4) below++;
    if (!below) return;
    counter[0] = '+';
    counter[small_number(below, counter + 1) + 1] = 0;
    jpr_pen_init(&small, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_MUTED, 0, JPR_SCREEN_H);
    jpr_draw_ascii(&small, JPR_COUNTER_X + ((s32)JPR_COUNTER_W - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, counter)) / 2,
                   JPR_COUNTER_BASE, counter);
}

/* The peek's rows (stage 15): a rule above each row after the first, its Japanese with its words underlined, the
 * meta at the right of its first line (or on a line of its own), its notes; the card over the lower rows only while
 * a word is selected. */
static void draw_rows(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f, const u16 *text) {
    const struct jpr_peek_rows *r = p->rows;
    s32 bottom = text_bottom(p), off = -(s32)p->st.scroll;
    u32 n = r->n < JPR_PEEK_ROWS ? r->n : JPR_PEEK_ROWS, k;
    struct jpr_pen plain, chosen, lit, muted;
    jpr_fill(s, 0, 0, JPR_SCREEN_W, JPR_SCREEN_H, JPR_COLOR_BG);
    jpr_pen_init(&plain, s, f, JPR_FACE_TEXT, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, bottom);
    jpr_pen_init(&chosen, s, f, JPR_FACE_TEXT, JPR_COLOR_SELECT, JPR_COLOR_TEXT, 0, bottom);
    jpr_pen_init(&lit, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, bottom);
    jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_MUTED, 0, bottom);
    for (k = 0; k < n; k++) {
        const struct jpr_row_geo *g = &p->geo[k];
        s32 top = (s32)g->top + off;
        if (top >= bottom || top + (s32)g->height <= 0) continue;
        if (k && top >= 0) jpr_fill(s, JPR_CARD_EDGE, top, JPR_SCREEN_W - 2 * JPR_CARD_EDGE, 1, JPR_COLOR_CARD_BORDER);
        if (g->inline_meta) draw_meta(r, &r->row[k], s, f, JPR_TEXT_X + JPR_TEXT_W, top + JPR_PEEK_PAD + JPR_PEEK_BASELINE,
                                      bottom);
        else if (r->row[k].meta.len || r->row[k].pill.len)
            draw_meta(r, &r->row[k], s, f, JPR_TEXT_X + meta_width(r, &r->row[k], &muted),
                      (s32)g->meta_top + off + JPR_PEEK_NOTE_BASE, bottom);
        lay_notes(r, &r->row[k], &lit, &muted, (s32)g->note_top + off);
    }
    for (k = 0; k < p->layout.line_count; k++) {
        s32 top = p->line_top[k] + off;
        if (p->line_top[k] < 0 || top >= bottom || top + JPR_PEEK_LINE <= 0) continue;
        draw_line(p, &plain, &chosen, text, p->layout.lines[k], top, JPR_PEEK_BASELINE, JPR_PEEK_LINE);
    }
    jpr_fill(s, 0, JPR_BAR_TOP - 1, JPR_SCREEN_W, 1, JPR_COLOR_RULE);
    if (rows_card(p)) {
        draw_card(p, s, f, text);
        if (p->status[0]) {
            jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_MUTED, JPR_PEEK_CARD_TOP + 1,
                         card_inner_bottom(p));
            jpr_draw_ascii(&muted, JPR_CARD_X, card_inner_bottom(p) - 3, p->status);
        }
    } else if (p->status[0]) {   /* the debug line, on a strip above the bar */
        jpr_fill(s, 0, JPR_BAR_TOP - 17, JPR_SCREEN_W, 16, JPR_COLOR_CARD);
        jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_MUTED, JPR_BAR_TOP - 17, JPR_BAR_TOP - 1);
        jpr_draw_ascii(&muted, JPR_CARD_X, JPR_BAR_TOP - 5, p->status);
    }
    rows_bar(p, s, f);
    sound_button(s);
    voice_button(s, f, JPR_VOICE_X, 0, (int)p->st.voice_available);
    voice_button(s, f, JPR_SLOW_X, 1, (int)p->st.voice_available);
    grammar_button(s, f, any_text(p, text));
}

void jpr_panel_draw(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f,
                    const u16 *text) {
    const struct jpr_block *b = &p->layout.blocks[p->st.view_block];
    struct jpr_pen plain, chosen, small, muted;
    char counter[8];
    u32 n, k;

    if (p->mix.open) {
        draw_mix(p, s, f);
        return;
    }
    if (p->gr.open) {
        draw_grammar(p, s, f);
        return;
    }
    if (p->rows) {
        draw_rows(p, s, f, text);
        return;
    }
    jpr_fill(s, 0, 0, JPR_SCREEN_W, JPR_SCREEN_H, JPR_COLOR_BG);
    /* the lines themselves end 4 px above text_bottom: a fifth line's box never shows */
    jpr_pen_init(&plain, s, f, JPR_FACE_TEXT, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, text_bottom(p) - 4);
    jpr_pen_init(&chosen, s, f, JPR_FACE_TEXT, JPR_COLOR_SELECT, JPR_COLOR_TEXT, 0, text_bottom(p) - 4);
    jpr_pen_init(&small, s, f, JPR_FACE_SMALL, JPR_COLOR_BG, JPR_COLOR_TEXT, 0, JPR_SCREEN_H);
    for (k = 0; k < b->count; k++) {
        s32 top = JPR_TEXT_TOP + (s32)(k * JPR_LINE_PITCH) - (s32)p->st.scroll;
        if (top + JPR_LINE_PITCH <= 0 || top >= text_bottom(p)) continue;
        draw_line(p, &plain, &chosen, text, p->layout.lines[b->first + k], top, JPR_TEXT_BASELINE, JPR_LINE_PITCH);
    }
    jpr_fill(s, 0, JPR_BAR_TOP - 1, JPR_SCREEN_W, 1, JPR_COLOR_RULE);

    draw_card(p, s, f, text);
    if (p->status[0]) {   /* the debug line, on the card's last row above any kokoro row */
        jpr_pen_init(&muted, s, f, JPR_FACE_SMALL, JPR_COLOR_CARD, JPR_COLOR_MUTED, jpr_panel_card_top(p) + 1,
                     card_inner_bottom(p));
        jpr_draw_ascii(&muted, JPR_CARD_X, card_inner_bottom(p) - 3, p->status);
    }

    draw_button(s, JPR_PREV_X, p->st.view_block > 0, 1);
    draw_button(s, JPR_NEXT_X, p->st.view_block + 1 < p->st.block_count, 0);
    n = small_number(p->st.view_block + 1, counter);
    counter[n++] = ' ';
    counter[n++] = '/';
    counter[n++] = ' ';
    n += small_number(p->st.block_count, counter + n);
    counter[n] = 0;
    jpr_draw_ascii(&small, JPR_COUNTER_X + ((s32)JPR_COUNTER_W - (s32)jpr_measure_ascii(f, JPR_FACE_SMALL, counter)) / 2,
                   JPR_COUNTER_BASE, counter);
    sound_button(s);
    voice_button(s, f, JPR_VOICE_X, 0, (int)p->st.voice_available);
    voice_button(s, f, JPR_SLOW_X, 1, (int)p->st.voice_available);
    grammar_button(s, f, block_has_text(p, text));
}

void jpr_panel_peek(struct jpr_panel *p, u32 open) {
    jpr_panel_mix_close(p);
    if (p->gr.open) close_grammar(p);
    p->peek = open ? 1 : 0;
    p->st.peek_open = p->peek;
}

void jpr_panel_voice_taken(struct jpr_panel *p) {
    p->voice_request = 0;
}

static u32 hash_words(u32 h, const struct jpr_panel *p, u32 from, u32 to) {
    const u32 *w = (const u32 *)(const void *)((const u8 *)p + from);
    u32 n = (to - from) >> 2;
    while (n--) {
        h ^= *w++;
        h *= 16777619u;
    }
    return h;
}

u32 jpr_panel_signature(const struct jpr_panel *p) {
    u32 h = 2166136261u;
    h = hash_words(h, p, 0, (u32)__builtin_offsetof(struct jpr_panel, st.frames));
    h = hash_words(h, p, (u32)__builtin_offsetof(struct jpr_panel, st.frames) + 4,
                   (u32)__builtin_offsetof(struct jpr_panel, st.fb_addr));
    return hash_words(h, p, (u32)__builtin_offsetof(struct jpr_panel, st.draw_ticks) + 4, (u32)sizeof *p);
}
