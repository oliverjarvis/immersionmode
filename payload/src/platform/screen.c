#include "screen.h"

#include "core/strings.h"
#include "core/util.h"
#include "choices.h"
#include "data.h"
#include "debug.h"
#include "display.h"
#include "fs.h"
#include "game.h"
#include "game_addrs.h"

#define NAMES_PATH "/jpreader/names.bin"
#define NAMES_MAGIC 0x4E52504Au      /* "JPRN" */
#define NAMES_SECTIONS 7
#define NAMES_TYPES_KANA 1
#define NAMES_TYPES_EN 3
#define NAME_MAX 24
#define MOVE_NAMES 118               /* system text files */
#define TYPE_NAMES 112

#define STRINGS_PATH "/jpreader/strings.bin"
#define STRINGS_MAGIC 0x5352504Au    /* "JPRS" */
#define STRINGS_VERSION 2            /* the English, then the literal (stage 15) */
#define STRINGS_HEADER 24
#define STRINGS_ENTRY 12
#define BUCKET_READ 32               /* entries read of a bucket (they average under ten) */
#define CACHE 32                     /* lookups remembered */
#define EN_MAX 160                   /* jpreader_tools.build.ENGLISH_MAX */

/* nw::lyt panes (docs/stage13-battle-findings.md, section 2) */
#define WK_BASE 0x04                 /* gfl2::lyt::LytWk: LytWkBase* */
#define BASE_LAYOUT 0x10             /* LytWkBase: nw::lyt::Layout* */
#define LAYOUT_ROOT 0x10             /* Layout: the root pane */
#define PANE_CHILDREN 0x14           /* the child list's anchor: next, prev; a child is its node - 4 */
#define PANE_FLAGS 0x44              /* bit 0: visible */
#define PANE_ALPHA 0x45
#define TEXTBOX_TEXT 0xA4            /* const u16 * */
#define TEXTBOX_LEN 0xCE             /* u16 */
#define PARTS_LAYOUT 0xAC            /* the part's own Layout* */
#define UPPER 0                      /* the display a layout is drawn on (2: the upper screen's right eye) */
#define LOWER 1
#define WALK_PANES 800               /* panes a walk visits at most */
#define WALK_DEPTH 24
#define WALK_CHILDREN 128
#define TEXT_MAX 200                 /* units of a pane's text kept (an item's description runs to about 100) */

#define SEEN 64                      /* texts a walk keeps */
#define PIECES 96

typedef u32 (*type_fn)(u32 waza);
#define CALL_TYPE(waza) (((type_fn)JPR_WAZA_GET_TYPE)(waza))   /* pml::wazadata::GetType: an in-memory table */

struct jpr_screen jpr_screen;
struct jpr_drawn jpr_drawn;
const struct jpr_drawn_frame *jpr_drawn_last;   /* the frame drawn last, complete */

static u32 names_file, names_open, names_ok, names_first[NAMES_SECTIONS], names_count[NAMES_SECTIONS];
static u32 strings_file, strings_open, strings_ok, buckets, entries_at;
static struct { u32 hash, en; u16 file, line; } cache[CACHE];
static u32 cache_next;

static struct jpr_seen_text seen[SEEN];
static u32 seen_count, walk_left;
static u16 groups;

#define peek_text jpr_shown_text
#define peek_units jpr_shown_units
static struct jpr_unit piece_units[JPR_PIECE_UNITS];
struct jpr_peek_rows jpr_peek_shown;   /* the peek's rows; the e2e reads them */
static u16 string_buf[EN_MAX];
static u32 peek_len, peek_count, peek_version = 0xFFFFFFFFu, peek_seq, peek_units_in, peek_sig;

static const u16 *strbuf_text(const void *buf) {
    return *(const u16 *const *)((const u8 *)buf + STRBUF_PTR);
}

static u32 strbuf_len(const void *buf) {
    return *(const u16 *)((const u8 *)buf + STRBUF_LEN);
}

/* The talk window's own message window: its text is the capture's already. */
static const void *talk_msgwin(void) {
    u32 talk = (u32)jpr_talk, g2d;
    if (!jpr_plausible_ptr(talk)) return 0;
    g2d = *(const u32 *)(talk + TALK_G2D);
    return jpr_plausible_ptr(g2d) ? *(const void *const *)(g2d + G2D_MSGWIN) : 0;
}

void jpr_h_msgwin_set(void *msgwin, const void *strbuf) {
    struct jpr_ident id;
    if (!strbuf || msgwin == talk_msgwin()) return;
    id = jpr_cap_ident_of(strbuf);
    jpr_screen_message(&jpr_screen, &id, strbuf_text(strbuf), strbuf_len(strbuf), jpr_cap.seq);
    jpr_dbg.screen_msgs++;
}

void jpr_h_lyt_draw(const void *wk, u32 display) {
    jpr_drawn_add(&jpr_drawn, wk, display);
}

void jpr_screen_frame(void) {
    jpr_drawn_last = jpr_drawn_flip(&jpr_drawn);
}

/* names.bin: the section table once; strings are read when the peek is composed. */
static int names_ready(void) {
    u8 head[12 + 8 * NAMES_SECTIONS];
    u32 got = 0, k;
    if (names_open) return names_ok;
    names_open = 1;
    if (jpr_fs_open(NAMES_PATH, &names_file)) return 0;
    if (jpr_fs_read(names_file, 0, head, sizeof head, &got) || got != sizeof head) return 0;
    if (*(const u32 *)head != NAMES_MAGIC) return 0;
    for (k = 0; k < NAMES_SECTIONS; k++) {
        names_first[k] = *(const u32 *)(head + 12 + 8 * k);
        names_count[k] = *(const u32 *)(head + 16 + 8 * k);
    }
    names_ok = 1;
    return 1;
}

/* String i of a section into out (at most NAME_MAX units); its length, 0 when there is none. */
static u32 name(u32 section, u32 i, u16 *out) {
    u8 at[4];
    u16 len = 0;
    u32 got = 0, offset;
    if (!names_ready() || i >= names_count[section]) return 0;
    if (jpr_fs_read(names_file, 12 + 8 * NAMES_SECTIONS + 4 * (names_first[section] + i), at, 4, &got) || got != 4)
        return 0;
    offset = *(const u32 *)at;
    if (jpr_fs_read(names_file, offset, &len, 2, &got) || got != 2) return 0;
    if (len > NAME_MAX) len = NAME_MAX;
    if (jpr_fs_read(names_file, offset + 2, out, 2u * len, &got) || got != 2u * len) return 0;
    return len;
}

/* strings.bin (jpreader_tools.formats.write_strings): its header once. */
static int strings_ready(void) {
    u32 head[STRINGS_HEADER / 4], got = 0;
    if (strings_open) return strings_ok;
    strings_open = 1;
    if (jpr_fs_open(STRINGS_PATH, &strings_file)) return 0;
    if (jpr_fs_read(strings_file, 0, head, STRINGS_HEADER, &got) || got != STRINGS_HEADER) return 0;
    if (head[0] != STRINGS_MAGIC || head[1] != STRINGS_VERSION || !head[2] || (head[2] & (head[2] - 1))) return 0;
    buckets = head[2];
    entries_at = head[4];
    strings_ok = 1;
    return 1;
}

/* Where a key's text is in the system text, and its English line's offset (0: none). 0 when it is not there. */
static int lookup(u32 hash, u16 *file, u16 *line, u32 *en) {
    static u8 bucket[BUCKET_READ * STRINGS_ENTRY];
    u32 span[2], got = 0, n, k;
    if (!hash) return 0;
    for (k = 0; k < CACHE; k++)
        if (cache[k].hash == hash) {
            if (cache[k].file == 0xFFFF) return 0;
            *file = cache[k].file;
            *line = cache[k].line;
            *en = cache[k].en;
            return 1;
        }
    if (!strings_ready()) return 0;
    k = cache_next;
    cache_next = (cache_next + 1) % CACHE;
    cache[k].hash = hash;
    cache[k].file = 0xFFFF;                      /* not there, until found */
    if (jpr_fs_read(strings_file, STRINGS_HEADER + 4 * (hash & (buckets - 1)), span, 8, &got) || got != 8) return 0;
    n = span[1] > span[0] ? span[1] - span[0] : 0;
    if (n > BUCKET_READ) n = BUCKET_READ;
    if (!n || jpr_fs_read(strings_file, entries_at + STRINGS_ENTRY * span[0], bucket, n * STRINGS_ENTRY, &got) ||
        got != n * STRINGS_ENTRY)
        return 0;
    for (n = got / STRINGS_ENTRY; n; n--) {
        const u8 *r = bucket + STRINGS_ENTRY * (n - 1);
        if (*(const u32 *)r != hash) continue;
        cache[k].file = *(const u16 *)(r + 4);
        cache[k].line = *(const u16 *)(r + 6);
        cache[k].en = *(const u32 *)(r + 8);
        *file = cache[k].file;
        *line = cache[k].line;
        *en = cache[k].en;
        return 1;
    }
    return 0;
}

/* At a strings.bin entry's offset: the English (which 0), then the literal (1). Into string_buf; its length. */
static u32 string_at(u32 offset, u32 which) {
    u16 len = 0;
    u32 got = 0;
    if (!offset || !strings_ready()) return 0;
    if (jpr_fs_read(strings_file, offset, &len, 2, &got) || got != 2) return 0;
    if (which) {
        offset += 2 + 2u * len;
        if (jpr_fs_read(strings_file, offset, &len, 2, &got) || got != 2) return 0;
    }
    if (len > EN_MAX) len = EN_MAX;
    if (jpr_fs_read(strings_file, offset + 2, string_buf, 2u * len, &got) || got != 2u * len) return 0;
    return len;
}

static void add_text(u32 box, u16 group) {
    const u16 *text = *(const u16 *const *)(box + TEXTBOX_TEXT);
    u32 len = *(const u16 *)(box + TEXTBOX_LEN), k;
    if (!len || seen_count >= SEEN || !jpr_plausible_ptr((u32)text)) return;
    len = jpr_text_cut(text, len, TEXT_MAX);
    if (!len) return;
    for (k = 0; k < seen_count; k++)
        if (seen[k].text == text) return;        /* a pane reached twice */
    seen[seen_count].text = text;
    seen[seen_count].len = (u16)len;
    seen[seen_count].group = group;
    seen[seen_count].hash = jpr_text_hash(text, len);
    seen_count++;
}

/* A pane and what is under it, if it shows: a TextBox's text; a Parts pane's own layout, as a group of its own. */
static void visit(u32 pane, u32 depth, u16 group) {
    u32 vt, anchor, node, k;
    if (!walk_left || depth > WALK_DEPTH || !jpr_plausible_ptr(pane)) return;
    walk_left--;
    if (!(*(const u8 *)(pane + PANE_FLAGS) & 1) || !*(const u8 *)(pane + PANE_ALPHA)) return;
    vt = *(const u32 *)pane;
    if (vt == JPR_TEXTBOX_VTABLE) {
        add_text(pane, group);
    } else if (vt == JPR_PARTS_VTABLE) {
        u32 layout = *(const u32 *)(pane + PARTS_LAYOUT);
        group = ++groups;
        if (jpr_plausible_ptr(layout) && *(const u32 *)(layout + LAYOUT_ROOT) != pane)
            visit(*(const u32 *)(layout + LAYOUT_ROOT), depth + 1, group);
    }
    anchor = pane + PANE_CHILDREN;
    node = *(const u32 *)anchor;
    for (k = 0; node != anchor && k < WALK_CHILDREN; k++) {
        if (!jpr_plausible_ptr(node)) return;
        visit(node - 4, depth + 1, group);
        node = *(const u32 *)node;
    }
}

/* Every text shown last frame, the lower screen's (the controls) first, then the upper's, each in the order its layouts
 * were drawn; returns a signature of them. */
static u32 walk(void) {
    const struct jpr_drawn_frame *f = jpr_drawn_last;
    u32 k, pass, sig = 0x811C9DC5u;
    seen_count = 0;
    groups = 0;
    walk_left = WALK_PANES;
    for (pass = 0; pass < 2 * (f ? f->n : 0); pass++) {
        u32 wk, base, layout;
        k = pass % f->n;
        if (f->display[k] != (pass < f->n ? LOWER : UPPER)) continue;
        wk = (u32)f->wk[k];
        if (!jpr_plausible_ptr(wk)) continue;
        base = *(const u32 *)(wk + WK_BASE);
        if (!jpr_plausible_ptr(base)) continue;
        layout = *(const u32 *)(base + BASE_LAYOUT);
        if (!jpr_plausible_ptr(layout)) continue;
        visit(*(const u32 *)(layout + LAYOUT_ROOT), 0, ++groups);
    }
    for (k = 0; k < seen_count; k++) sig = (sig ^ seen[k].hash ^ ((u32)seen[k].group << 16)) * 0x01000193u;
    jpr_dbg.screen_texts = seen_count;
    return sig;
}

static struct jpr_ident ident(s32 arc, u32 dat, u32 line) {
    struct jpr_ident id;
    jpr_memset(&id, 0, (u32)sizeof id);
    id.arc = arc;
    id.dat = dat;
    id.line = line;
    return id;
}

static struct jpr_ident plain(void) {
    return ident(JPR_ARC_UNKNOWN, JPR_NONE, JPR_NONE);
}

static u32 piece(struct jpr_piece *p, u32 n, const u16 *text, u32 len, u32 line, struct jpr_ident id) {
    if (n >= PIECES || !len) return n;
    p[n].text = text;
    p[n].len = (u16)len;
    p[n].line = (u16)line;
    p[n].id = id;
    return n + 1;
}

static const u16 SPACE[] = {0x0020};

/* A row's strings for its texts keep[from..to), each field filled in a pass of its own so it stays one run of the
 * pool: the literals, the English lines, the type of a move, then what is not Japanese (when the row has Japanese). */
static void row_strings(struct jpr_peek_row *row, const u16 *keep, u32 from, u32 to, int has_japanese) {
    u32 pass, k;
    for (pass = 0; pass < 4; pass++)
        for (k = from; k < to; k++) {
            const struct jpr_seen_text *t = &seen[keep[k]];
            int japanese = jpr_screen_japanese(t->text, t->len);
            u16 file, line;
            u32 en, len;
            if (pass == 3) {
                if (has_japanese && !japanese) {
                    u32 plain = jpr_plain_text(t->text, t->len, string_buf, EN_MAX);   /* no tags, no line breaks */
                    jpr_peek_append(&jpr_peek_shown, &row->meta, string_buf, plain, " ");
                }
                continue;
            }
            if (!japanese || !lookup(t->hash, &file, &line, &en)) continue;
            if (pass < 2 && (len = string_at(en, pass == 0)) != 0)
                jpr_peek_append(&jpr_peek_shown, pass == 0 ? &row->literal : &row->english, string_buf, len, "; ");
            if (pass == 2 && file == MOVE_NAMES && !row->pill.len &&
                (len = name(NAMES_TYPES_KANA, CALL_TYPE(line), string_buf)) != 0)
                jpr_peek_append(&jpr_peek_shown, &row->pill, string_buf, len, "");
        }
}

static void compose(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language) {
    static struct jpr_piece p[PIECES];
    static u16 keep[SEEN];
    static u8 starts[SEEN];
    static u8 first_piece[JPR_PEEK_ROWS], last_piece[JPR_PEEK_ROWS];
    s32 arc = (s32)(30 + language);
    u32 n = 0, kept, k, j, msg_hash = 0, matched = 0;
    jpr_memset(p, 0, (u32)sizeof p);
    jpr_peek_rows_clear(&jpr_peek_shown);
    /* the message: the window's, unless a talk message came after it */
    if (jpr_screen.msg_seq && jpr_screen.msg_after == jpr_cap.seq) {
        n = piece(p, n, jpr_screen.msg, jpr_screen.msg_len, 0, jpr_screen.msg_id);
        msg_hash = jpr_text_hash(jpr_screen.msg, jpr_screen.msg_len);
    } else if (msg) {
        n = piece(p, n, msg->text, msg->len, 0, plain());   /* its words come with it: units below */
        msg_hash = jpr_text_hash(msg->text, msg->len);
    }
    if (n) {
        u16 file, line;
        u32 en, len;
        jpr_peek_shown.header = 1;
        first_piece[0] = last_piece[0] = 0;
        if (lookup(msg_hash, &file, &line, &en) && (len = string_at(en, 0)) != 0)
            jpr_peek_append(&jpr_peek_shown, &jpr_peek_shown.row[0].english, string_buf, len, "");
        jpr_peek_shown.n = 1;
    }
    /* then a row a group: its Japanese on a line of its own (all its texts, when none is Japanese) */
    kept = jpr_screen_pick(seen, seen_count, msg_hash, keep, starts, SEEN);
    for (k = 0; k < kept && jpr_peek_shown.n < JPR_PEEK_ROWS; k = j) {
        u32 r = jpr_peek_shown.n, before = n;
        int has_japanese = 0;
        for (j = k; j < kept && (j == k || !starts[j]); j++) has_japanese |= jpr_screen_japanese(seen[keep[j]].text, seen[keep[j]].len);
        for (j = k; j < kept && (j == k || !starts[j]); j++) {
            const struct jpr_seen_text *t = &seen[keep[j]];
            u16 file, line;
            u32 en;
            if (has_japanese && !jpr_screen_japanese(t->text, t->len)) continue;
            if (n > before) n = piece(p, n, SPACE, 1, 0, plain());
            if (lookup(t->hash, &file, &line, &en)) {
                n = piece(p, n, t->text, t->len, n == before, ident(arc, file, line));
                matched++;
            } else {
                n = piece(p, n, t->text, t->len, n == before, plain());
            }
        }
        if (n == before) continue;
        first_piece[r] = (u8)before;
        last_piece[r] = (u8)(n - 1);
        row_strings(&jpr_peek_shown.row[r], keep, k, j, has_japanese);
        jpr_peek_shown.n++;
    }
    jpr_dbg.screen_matched = matched;
    peek_len = jpr_compose(p, n, peek_text, JPR_MSG_MAX - 1);
    peek_text[peek_len] = 0;                             /* for the e2e, which reads it */
    for (k = 0; k < jpr_peek_shown.n; k++) {                        /* each row's Japanese in the text */
        u32 i;
        for (i = first_piece[k]; i <= last_piece[k] && p[i].start != JPR_PIECE_NONE; i++) {}
        if (i <= last_piece[k]) {                        /* the text is full: no row from here on */
            jpr_peek_shown.n = k;
            break;
        }
        jpr_peek_shown.row[k].start = p[first_piece[k]].start;
        jpr_peek_shown.row[k].end = (u16)(p[last_piece[k]].start + p[last_piece[k]].len);
    }
    if (!jpr_peek_shown.n) {
        static const u16 nothing[] = {'N', 'o', 't', 'h', 'i', 'n', 'g', ' ', 't', 'o', ' ', 'r', 'e', 'a', 'd', ' ',
                                      'h', 'e', 'r', 'e', ' ', 'y', 'e', 't', '.'};
        jpr_peek_append(&jpr_peek_shown, &jpr_peek_shown.row[0].english, nothing, sizeof nothing / sizeof nothing[0], "");
        jpr_peek_shown.n = 1;
    }
    peek_count = 0;
    for (k = 0; k < n; k++) {
        u32 got;
        if (p[k].start == JPR_PIECE_NONE) continue;
        if (p[k].text == (msg ? msg->text : 0)) {          /* the talk message: its units as the capture found them */
            for (j = 0; j < count && peek_count < JPR_UNITS_MAX; j++) peek_units[peek_count++] = units[j];
            continue;
        }
        if (p[k].id.arc < 0) continue;
        got = JPR_UNITS_MAX - peek_count;
        got = jpr_data_units(&p[k].id, p[k].text, p[k].len, language, piece_units,
                             got < JPR_PIECE_UNITS ? got : JPR_PIECE_UNITS);
        for (j = 0; j < got; j++) {
            piece_units[j].start = (u16)(piece_units[j].start + p[k].start);
            peek_units[peek_count++] = piece_units[j];
        }
    }
}

void jpr_screen_peek(const struct jpr_msg *msg, const struct jpr_unit *units, u32 count, u32 language,
                     const u16 **text, u32 *len, const struct jpr_unit **out, u32 *out_count, u32 *key,
                     const struct jpr_peek_rows **out_rows) {
    u32 seq = msg ? msg->seq : 0, sig = walk();
    if (peek_version != jpr_screen.version || peek_seq != seq || peek_units_in != count || peek_sig != sig ||
        jpr_shown_owner != JPR_SHOWN_PEEK) {
        compose(msg, units, count, language);
        jpr_shown_owner = JPR_SHOWN_PEEK;
        peek_version = jpr_screen.version;
        peek_seq = seq;
        peek_units_in = count;
        peek_sig = sig;
        jpr_dbg.peek_composes++;
    }
    *text = peek_text;
    *len = peek_len;
    *out = peek_units;
    *out_count = peek_count;
    *out_rows = &jpr_peek_shown;
    *key = 0x80000000u | (jpr_dbg.peek_composes & 0x7FFFFFFFu);   /* a new key for every compose */
}
