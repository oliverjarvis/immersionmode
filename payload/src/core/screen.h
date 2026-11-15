#ifndef JPR_CORE_SCREEN_H
#define JPR_CORE_SCREEN_H

#include "capture.h"
#include "types.h"

/* What the game has on its screens beyond the talk window, for the peek (stages 13 and 14; research:
 * docs/stage13-battle-findings.md): the latest message window's text (a battle's top-screen message, a bag or app
 * message) and the layouts drawn on each screen, whose text platform/screen.c reads. Pure logic. */

#define JPR_SCREEN_TEXT 256           /* UTF-16 units kept of a window message */
#define JPR_PIECE_NONE 0xFFFF

struct jpr_screen {
    u32 version;                      /* changes whenever anything here does */
    u32 msg_seq;                      /* window messages seen; 0 for none */
    u32 msg_after;                    /* the capture's talk message count when it came: a later talk message wins */
    struct jpr_ident msg_id;
    u16 msg_len;
    u16 msg[JPR_SCREEN_TEXT];
};

void jpr_screen_init(struct jpr_screen *s);
/* A message window (not the talk window) was given this text; talk_seq is the capture's message count now. */
void jpr_screen_message(struct jpr_screen *s, const struct jpr_ident *id, const u16 *text, u32 len, u32 talk_seq);

/* The layouts the game drew in a frame and on which display (0 upper, 1 lower, 2 upper right eye), from
 * gfl2::lyt's draw of a LytWk (stage 14): the peek walks the lower screen's for their text. */
#define JPR_DRAWN 48
struct jpr_drawn_frame {
    u32 n;
    const void *wk[JPR_DRAWN];
    u8 display[JPR_DRAWN];
};
struct jpr_drawn {
    struct jpr_drawn_frame frame[2];
    u32 cur;                          /* the frame being filled */
};
void jpr_drawn_add(struct jpr_drawn *d, const void *wk, u32 display);
/* Ends the frame being filled: returns it, and the next one starts empty. */
const struct jpr_drawn_frame *jpr_drawn_flip(struct jpr_drawn *d);

/* A text the walk of the lower screen found: its group is its nearest Parts pane (a button: name, "PP", "35"). */
struct jpr_seen_text {
    const u16 *text;
    u16 len;
    u16 group;
    u32 hash;                         /* jpr_key_hash of its key; 0 when it has none (nothing to read) */
};
/* The texts to list, in the walk's order: none without a key, none equal to the message (msg_hash), no text twice
 * in a group, and no group whose texts are an earlier group's again. keep[] gets their indexes, starts[] 1 where a
 * group begins. Returns how many, at most max. */
u32 jpr_screen_pick(const struct jpr_seen_text *t, u32 n, u32 msg_hash, u16 *keep, u8 *starts, u32 max);

/* Text built from pieces: each piece's text after a line break when `line` is set; its start in the result is kept in
 * start (JPR_PIECE_NONE when it did not fit), so the words of a piece with an origin can be found and moved there. */
struct jpr_piece {
    const u16 *text;
    u16 len;
    u16 line;                         /* begin a new line first (not before the first piece) */
    u16 start;                        /* set by jpr_compose */
    u16 pad;
    struct jpr_ident id;              /* arc JPR_ARC_UNKNOWN: plain text, no words */
};
u32 jpr_compose(struct jpr_piece *pieces, u32 n, u16 *out, u32 max);

/* The peek's rows (stage 15): the peek text holds each row's Japanese, separated by line breaks, so its words can be
 * tapped as any message's; the rest of a row is here, UTF-8 in pool: a literal translation and the official English
 * (strings.bin), what is not Japanese (PP, 30/30, ×3) and a move's type. Row 0 is the message when header is set. */
#define JPR_PEEK_ROWS 24
#define JPR_PEEK_POOL 2048
struct jpr_peek_str {
    u16 at, len;                      /* bytes of pool */
};
struct jpr_peek_row {
    u16 start, end;                   /* its Japanese in the peek text, half-open */
    struct jpr_peek_str literal, english, meta, pill;
};
struct jpr_peek_rows {
    u32 n;
    u32 header;
    u32 used;                         /* bytes of pool */
    struct jpr_peek_row row[JPR_PEEK_ROWS];
    u8 pool[JPR_PEEK_POOL];
};
void jpr_peek_rows_clear(struct jpr_peek_rows *r);
/* Appends text to s as UTF-8, after sep when s has some already. s must be empty or the pool's last string. 0, or -1
 * (nothing written) when it is not or the pool is full. */
int jpr_peek_append(struct jpr_peek_rows *r, struct jpr_peek_str *s, const u16 *text, u32 len, const char *sep);
/* Does the text hold kana or kanji (outside its tags)? A text without (PP, 30/30, ×3) is a row's meta. */
int jpr_screen_japanese(const u16 *text, u32 len);
/* The text without the game's tags, a line break as a space, into out (at most max units); its length. */
u32 jpr_plain_text(const u16 *text, u32 len, u16 *out, u32 max);
/* How much of text to keep for at most max units without cutting into a tag: max, or less. */
u32 jpr_text_cut(const u16 *text, u32 len, u32 max);

#endif
