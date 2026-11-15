#ifndef JPR_PANEL_H
#define JPR_PANEL_H

#include "annot.h"
#include "card.h"
#include "draw.h"
#include "font.h"
#include "grammar.h"
#include "kokoro.h"
#include "sentence.h"
#include "layout.h"
#include "mix.h"
#include "reply.h"
#include "screen.h"
#include "surface.h"
#include "word.h"

#define JPR_PANEL_MAGIC 0x5052504Au /* "JPRP" */

#define JPR_FONT_NONE 0    /* not read yet */
#define JPR_FONT_OK 1
#define JPR_FONT_MISSING 2 /* /jpreader/font.bin could not be read */
#define JPR_FONT_BAD 3     /* read, but not a font file this payload understands */

#define JPR_CARD_NONE 0       /* nothing selected, or its entry is still to be read */
#define JPR_CARD_OK 1
#define JPR_CARD_NO_ENTRY 2   /* the unit has no card entry */
#define JPR_CARD_UNREADABLE 3 /* dict.bin could not be read, or held something else */
#define JPR_DATA_NONE 0
#define JPR_DATA_OK 1
#define JPR_DATA_MISSING 2
#define JPR_DATA_BAD 3        /* not the version this payload reads */
#define JPR_DATA_MISMATCH 4   /* annot.bin and dict.bin come from different builds */
#define JPR_DATA_ANNOT 1      /* data_error >> 8 */
#define JPR_DATA_DICT 2
#define JPR_TAP_SLOP 6        /* pixels a press may move and still be a tap */
#define JPR_UNDERLINE 3       /* rows below the baseline */
#define JPR_UNDERLINE_PLAIN 1 /* thickness of a unit's underline */
#define JPR_UNDERLINE_MARK 2  /* ... of one kokoro knows or is learning */
#define JPR_UNDERLINE_SEL 3   /* ... of the selected unit */
#define JPR_CARD_PITCH 17
#define JPR_CARD_LABEL_PITCH 21
#define JPR_CARD_LAST (JPR_BAR_TOP - 5)
#define JPR_CARD_PAD 6        /* blank pixels below a scrolled card's last row */
#define JPR_COLOR_UNDERLINE 0xCFC5B2
#define JPR_COLOR_SELECT 0xF7E2A8
#define JPR_COLOR_ACCENT 0xB5532F

/* Bottom-screen layout in screen pixels (spec 8.1). */
#define JPR_SCREEN_W 320
#define JPR_SCREEN_H 240
#define JPR_TEXT_X 6
#define JPR_TEXT_W 308
#define JPR_TEXT_TOP 2         /* top of the first line box */
#define JPR_LINE_PITCH 24
#define JPR_TEXT_BASELINE 18   /* from the top of a line box */
#define JPR_VISIBLE_LINES 4
#define JPR_CARD_X 8
#define JPR_CARD_RIGHT 312
#define JPR_CARD_HEAD 20       /* baseline of the card's first row, below the card's top */
#define JPR_CARD_GAP 4         /* between the text and the card, and the card and the bar */
#define JPR_CARD_EDGE 4        /* the card panel's left edge */
#define JPR_CARD_BOTTOM (JPR_BAR_TOP - JPR_CARD_GAP)
#define JPR_BAR_TOP 208
#define JPR_BTN_Y 211
#define JPR_BTN_H 26
#define JPR_BTN_W 56             /* the grammar view's Back */
/* The reading view's bar (spec: voice 4): page arrows and counter, the Sound button (stage 11), voice ▶ and
 * 0.7x, Grammar. */
#define JPR_PREV_X 4
#define JPR_PAGE_W 36
#define JPR_COUNTER_X 42
#define JPR_COUNTER_W 40
#define JPR_NEXT_X 84
#define JPR_SOUND_X 124
#define JPR_SOUND_W 26
#define JPR_VOICE_X 154
#define JPR_SLOW_X 200
#define JPR_VOICE_W 44
#define JPR_COLOR_VOICE 0xF3E1C7  /* the voice buttons: warm, unlike the grey page arrows */
#define JPR_COUNTER_BASE (JPR_BTN_Y + 18)

#define JPR_COLOR_BG 0xF4F0E6
#define JPR_COLOR_TEXT 0x202028
#define JPR_COLOR_RULE 0xC8C0B0
#define JPR_COLOR_BUTTON 0xE2DCCF
#define JPR_COLOR_BUTTON_OFF 0xEDE9E0
#define JPR_COLOR_ARROW 0x404048
#define JPR_COLOR_ARROW_OFF 0xBDB7AB
#define JPR_COLOR_ERROR 0xB03020
#define JPR_COLOR_MUTED 0x6E6A64
#define JPR_COLOR_CARD 0xFBF8F1         /* the card panel and the translation box */
#define JPR_COLOR_CARD_BORDER 0xE2DCCF
#define JPR_COLOR_PILL 0xE2DCCF         /* part of speech */
#define JPR_COLOR_PILL_TEXT 0x404048
#define JPR_COLOR_SHADE 0xEDE7D9        /* every other chunk of a Grammar reply */
#define JPR_COLOR_ON_ACCENT 0xFFFFFF
#define JPR_COLOR_KNOWN 0x3E7B4F        /* kokoro: in review */
#define JPR_COLOR_LEARNING 0xD9A23A     /* kokoro: learning */
#define JPR_KOKORO_ROW 18               /* the card's kokoro row, inside the panel above its bottom border */
#define JPR_KOKORO_TOP (JPR_CARD_BOTTOM - 1 - JPR_KOKORO_ROW)
#define JPR_ADD_W 128                   /* "+ Add to kokoro" */
#define JPR_ADD_X (JPR_CARD_RIGHT - JPR_ADD_W)
#define JPR_OVERRIDES 64                /* words, and grammar points, added (or saved for later) this session */

/* The peek's rows view (stage 15): every row's Japanese on a line of its own, its notes in the small face below, the
 * card over the lower rows only while a word is selected. Positions are from the top of the screen at scroll 0. */
#define JPR_PEEK_PAD 2           /* above and below a row's contents */
#define JPR_PEEK_LINE 21         /* a line of a row's Japanese */
#define JPR_PEEK_BASELINE 16     /* from the top of such a line */
#define JPR_PEEK_NOTE 16         /* a small line: the meta on its own, the literal, the English */
#define JPR_PEEK_NOTE_BASE 12
#define JPR_PEEK_ROW_MIN 40      /* rows below the message are at least this tall */
#define JPR_PEEK_CARD_TOP 92     /* the card, while a word is selected */
#define JPR_PEEK_SCROLL 40       /* the bar's up and down move the rows this far */
#define JPR_PEEK_GAP 8           /* between a row's Japanese and its meta */

struct jpr_row_geo {
    u16 top, height;
    u16 meta_top;                /* the meta's own small line, when it does not fit beside the Japanese */
    u16 note_top;
    u8 inline_meta;
    u8 pad[3];
};

#define JPR_WIN_WAITING 2 /* message window state: waiting for a button press */

/* The grammar view (spec 8.2). */
#define JPR_GR_IDLE 0
#define JPR_GR_ASKING 1
#define JPR_GR_DONE 2
#define JPR_GR_ERROR 3
#define JPR_GRAMMAR_X 248        /* Grammar button in the reading view's bar */
#define JPR_GRAMMAR_W 68
#define JPR_BACK_X 4             /* grammar view's bar: Back, Retry, up, down */
#define JPR_RETRY_X 64
#define JPR_RETRY_W 84
#define JPR_UP_X 216
#define JPR_DOWN_X 268
#define JPR_ARROW_W 48
#define JPR_GR_SENTENCE_BOTTOM 48
#define JPR_GR_TOP 50            /* the reply area */
#define JPR_GR_PITCH 18
#define JPR_GR_FIRST 14          /* first reply baseline, below JPR_GR_TOP */
#define JPR_SENTENCE_PITCH 22
#define JPR_STATUS_MAX 64
/* kokoro's grammar points in a reply (stage 17, spec: kokoro integration 4, mockup A): a row each, in its chunk's
 * block or the last block, "Other grammar". */
#define JPR_GR_POINTS JPR_ITEMS_MAX   /* point rows a reply's layout keeps, for taps: every row a reply can have */
#define JPR_GR_ADD_W 56          /* a row kokoro lacks the point of keeps this column at the right: "+ Add", then what became of it */
#define JPR_GR_DOT 7

/* The Sound view (stage 11): three rows of -, ten steps and +, then the duck's On/Off. */
#define JPR_MIX_TITLE_BASE 22
#define JPR_MIX_ROW_Y(k) (34 + (k) * 42)
#define JPR_MIX_ROW_H 30
#define JPR_MIX_LABEL_X 8
#define JPR_MIX_MINUS_X 76
#define JPR_MIX_BTN_W 34
#define JPR_MIX_BAR_X 116
#define JPR_MIX_STEP 14          /* each step: 12 pixels and a gap */
#define JPR_MIX_PLUS_X 262
#define JPR_MIX_DUCK_Y 164
#define JPR_MIX_TOGGLE_X 248
#define JPR_MIX_TOGGLE_W 64

struct jpr_mix_view {
    u32 open;                                  /* the view covers the panel */
    u32 save;                                  /* closed with changes: the platform writes sound.txt, then saved() */
    struct jpr_mix_levels levels;              /* the platform applies these every frame */
    struct jpr_mix_levels opened;              /* as they were when the view opened */
};

/* A session's own knowledge of a word kokoro's list does not have yet (entry: a dict.bin entry), or of a grammar point
 * a reply says kokoro lacks (entry: its Bunpro id). */
struct jpr_override {
    u32 entry;
    u32 state;   /* JPR_KOKORO_* */
};

/* A point row of the reply, from its layout. */
struct jpr_gr_point {
    u32 id;      /* Bunpro id; 0 for none */
    u16 top;     /* the row's top, from the reply's top */
    u8 status;   /* JPR_POINT_*, as kokoro sent it */
    u8 slot;     /* 1: the row has the add column (kokoro lacks the point, and it has an id) */
};

struct jpr_grammar_view {
    u32 open;                                  /* the view covers the panel */
    u32 state;                                 /* JPR_GR_* */
    u32 job;                                   /* id of the request being waited for */
    u32 next_id;
    u32 pending;                               /* a request waits for the worker to be free */
    u32 cancel;                                /* a job id to abandon, for the platform to pass on */
    u32 scroll;                                /* pixels the reply is scrolled by */
    u32 height;                                /* pixels the reply takes */
    u32 failure, code, http_status;
    struct jpr_excerpt ex;                     /* the sentence asked about, and its context */
    char message[JPR_MESSAGE_MAX];
    u8 reply[JPR_REPLY_MAX];
    u32 reply_len;
    u8 answered[JPR_SENTENCE_BYTES];           /* the sentence the stored reply belongs to */
    u32 answered_len;
    struct jpr_gr_point points[JPR_GR_POINTS]; /* the reply's point rows */
    u32 point_count;
    u32 other_count;                           /* of them, in "Other grammar" */
    u32 add_id;                                /* Bunpro id of the add tapped and not yet answered; 0 for none */
    u32 add_wanted;                            /* the platform sends add_id's add when the worker is free */
    u32 last_id, last_outcome;                 /* the last add answered, and what became of it (JPR_ADD_*) */
    struct jpr_override over[JPR_OVERRIDES];   /* points added (or saved for later) this session */
    u32 over_count;
};

/* Read by the test driver through the jpr_panel symbol: keep it first in struct jpr_panel. */
struct jpr_panel_state {
    u32 magic;
    u32 visible;     /* 1 while the panel covers the bottom screen */
    u32 frames;      /* frames drawn */
    u32 block_count;
    u32 live_block;  /* block the top-screen box is showing */
    u32 view_block;  /* block the panel is showing */
    u32 scroll;      /* pixels the viewed block is scrolled by */
    u32 font_status; /* JPR_FONT_* */
    u32 font_error;  /* step that failed while reading font.bin (fs.h), 0 if none */
    u32 font_glyphs;
    u32 msg_seq;     /* capture sequence number of the message shown */
    u32 taps;        /* button taps acted on */
    u32 fb_addr;     /* last bottom-screen buffer drawn into */
    u32 fb_format;
    u32 draw_ticks;  /* CPU ticks the last frame's drawing and cache clean took */
    u32 unit_count;   /* units in the message shown */
    u32 annot_status; /* JPR_ANNOT_* for it */
    u32 selected;     /* 1 + index of the selected unit, 0 for none */
    u32 sel_start;    /* where the selected unit starts in the text */
    u32 sel_len;
    u32 card_entry;   /* its dict.bin entry, or JPR_NO_ENTRY */
    u32 card_status;  /* JPR_CARD_* */
    u32 data_status;  /* JPR_DATA_*: annot.bin and dict.bin */
    u32 data_error;   /* JPR_DATA_ANNOT or JPR_DATA_DICT << 8 | fs.h error; 0 if none */
    u32 gr_open;      /* the grammar view is showing */
    u32 gr_state;     /* JPR_GR_* */
    u32 gr_failure;   /* JPR_NET_* of the last result */
    u32 gr_http;      /* its HTTP status, 0 if none */
    u32 gr_requests;  /* requests handed to the worker */
    u32 gr_scroll;
    char gr_message[JPR_MESSAGE_MAX]; /* the failure message on screen, NUL-terminated */
    u32 card_scroll;  /* pixels the card is scrolled by */
    u32 kokoro_on;      /* config.txt has kokoro settings: marks, the kokoro row */
    u32 marks_known;    /* units of the message kokoro has in review */
    u32 marks_learning;
    u32 card_kokoro;    /* JPR_KOKORO_* of the selected unit */
    u32 add_state;      /* JPR_ADD_* of adding it */
    u32 kokoro_words;   /* words in the list the marks come from */
    u32 kokoro_syncs;   /* word lists fetched this session */
    u32 outbox;         /* adds waiting to be sent */
    u32 kokoro_failure; /* JPR_NET_* of the last kokoro request */
    u32 kokoro_http;    /* its HTTP status, 0 if none */
    u32 voice_available; /* the platform says the current message has a voice clip */
    u32 voice_requests;  /* ▶ and 0.7x taps taken */
    u32 mix_open;        /* the Sound view is showing (stage 11) */
    u32 mix_music;       /* its levels, 0..10 */
    u32 mix_effects;
    u32 mix_voice;
    u32 mix_duck;        /* 1: quieter music under a voice clip */
    u32 peek_open;       /* the peek is showing (stage 13) */
    u32 gr_points;       /* grammar point rows in the reply shown (stage 17) */
    u32 gr_other;        /* of them, in "Other grammar" */
    u32 gr_buttons;      /* rows with a "+ Add" (or "Retry") to tap */
    u32 gr_button_y;     /* the first such row's top on the screen at the current scroll; 0 for none */
    u32 gr_add_state;    /* JPR_ADD_* of the last grammar add: ASKING while one waits */
};

/* Notices the top-screen box clearing, from the message window's state and pending tag. */
struct jpr_pages {
    u32 waiting_clear;
};

struct jpr_panel {
    struct jpr_panel_state st;
    struct jpr_layout layout;
    struct jpr_pages pages;
    struct jpr_unit units[JPR_UNITS_MAX];
    struct jpr_card card;
    struct jpr_text label;            /* the ending label of the selected unit; len 0 when none */
    u8 entry_bytes[JPR_ENTRY_MAX];    /* filled by the platform while card_wanted is set */
    u8 label_bytes[JPR_LABEL_MAX];
    u32 card_wanted;                  /* the selected unit's entry is to be read: see jpr_panel_card */
    u32 prev_down;
    u32 dragging;                     /* 1 the text or reply, 2 the card */
    u32 moved;                        /* farthest the press has moved, in pixels */
    u32 press_x;
    u32 press_y;
    u32 press_scroll;
    u32 card_scroll;                  /* pixels the card is scrolled by */
    u32 card_height;                  /* pixels the card's content takes below the card's top */
    struct jpr_grammar_view gr;
    struct jpr_mix_view mix;
    u32 peek;                         /* SELECT opened the peek: the panel shows what is on the game's screens (stage 13) */
    char status[JPR_STATUS_MAX];      /* the debug line; empty when it is off */
    u8 marks[JPR_UNITS_MAX];          /* each unit's JPR_KOKORO_* state: see jpr_panel_marks */
    u32 add_wanted;                   /* Add was tapped: the platform sends add_body to kokoro when the worker is free */
    u32 voice_request;                /* 1 ▶ or 2 0.7x was tapped: the platform replays the clip, then calls voice_taken */
    u32 add_entry;                    /* dict.bin entry of the add tapped and not yet answered; JPR_NO_ENTRY for none */
    u8 add_body[JPR_CONTEXT_BYTES];   /* its request, built when Add was tapped: moving on cannot lose it */
    u32 add_body_len;
    const struct jpr_peek_rows *rows;  /* set: the peek shows these rows (jpr_panel_rows) */
    s16 line_top[JPR_LAYOUT_LINES];   /* rows view: each layout line's top, -1 for a blank one */
    struct jpr_row_geo geo[JPR_PEEK_ROWS];
    u32 rows_height;                  /* from JPR_TEXT_TOP to the last row's bottom */
    u32 messages;                     /* jpr_panel_message calls: new text or rows are drawn even when laid out alike */
};

/* Who owns a press: one that began while the panel was up stays the panel's until the finger lifts,
 * even if the panel goes away meanwhile (Back after the dialog ended must not tap the game). */
struct jpr_touch_gate {
    u32 held;
    u32 hiding;
};

/* One touch sample: 1 when the game must not see it. */
int jpr_touch_hide(struct jpr_touch_gate *g, u32 down, u32 visible);
int jpr_pages_step(struct jpr_pages *t, u32 state, u32 tag);
void jpr_panel_init(struct jpr_panel *p);
void jpr_panel_message(struct jpr_panel *p, u32 seq, const u16 *text, u32 len, const struct jpr_font *f,
                       const struct jpr_unit *units, u32 count, u32 annot_status);
void jpr_panel_box(struct jpr_panel *p, u32 state, u32 tag);
/* One touch sample. A press in the text area that moves at most JPR_TAP_SLOP pixels is a tap and
 * selects the unit under it on release; a longer move scrolls. text and len are the message shown. */
void jpr_panel_touch(struct jpr_panel *p, u32 down, u32 x, u32 y, const struct jpr_font *f, const u16 *text,
                     u32 len);
/* The worker's result for request id: a reply, or a failure (JPR_NET_*) with kokoro's message
 * in text. Results for any other request are ignored. */
void jpr_panel_grammar_result(struct jpr_panel *p, const struct jpr_font *f, u32 id, u32 failure, u32 code,
                              u32 http_status, const u8 *text, u32 len);
/* Index of the unit under (x, y) in the text area, or -1. */
s32 jpr_panel_hit(const struct jpr_panel *p, const struct jpr_font *f, const u16 *text, u32 x, u32 y);
/* After the platform has read the selected unit's entry (and label) into entry_bytes (label_bytes);
 * ok is 0 when a read failed. f and the message's text measure the card, which scrolls when it is
 * taller than its area. */
void jpr_panel_card(struct jpr_panel *p, const struct jpr_font *f, const u16 *text, int ok, u32 entry_len,
                    u32 label_len);
void jpr_panel_draw(const struct jpr_panel *p, const struct jpr_surface *s, const struct jpr_font *f,
                    const u16 *text);
/* Where the card starts: just below the text, which takes only the lines of the block shown (one to
 * four), so a short message leaves the card more room. */
s32 jpr_panel_card_top(const struct jpr_panel *p);
/* Rows inside the card panel, between its borders. */
u32 jpr_panel_card_area(const struct jpr_panel *p);
/* A hash of everything the panel's picture depends on: equal signatures (for the same message and
 * surface) draw the same pixels. Counters that change every frame are left out. */
u32 jpr_panel_signature(const struct jpr_panel *p);
/* Each unit's mark: kokoro's state for its entry from bits (two bits an entry, `entries` of them), or,
 * where kokoro has none, the session's override. Counts the known and learning units. Nothing is marked
 * while kokoro mode is off. */
void jpr_panel_marks(struct jpr_panel *p, const u8 *bits, u32 entries, const struct jpr_override *o, u32 n);
/* kokoro's answer to adding grammar point id (stage 17): outcome is JPR_ADD_*, state the JPR_KOKORO_* state it has from
 * now on (0: none), kept for the session so every reply that says kokoro lacks it shows it added. */
void jpr_panel_point_added(struct jpr_panel *p, u32 id, u32 outcome, u32 state);
/* After a flush delivered the saved adds: each is kokoro's now, up next (a v2 list never lists it). */
void jpr_overrides_sent(struct jpr_override *o, u32 n);
/* The card's kokoro row: its status text, and what became of an add (0 for none: the button, or nothing). */
void jpr_panel_kokoro_texts(const struct jpr_panel *p, const char **status, const char **after);
/* kokoro's answer (JPR_ADD_*) to adding entry's word: shown while that word is still the one selected. */
/* The platform has passed a ▶ or 0.7x tap on to the voice player. */
void jpr_panel_voice_taken(struct jpr_panel *p);
void jpr_panel_added(struct jpr_panel *p, u32 entry, u32 outcome);
/* The levels sound.txt holds (or the defaults), for the Sound view and the platform's mixer. */
void jpr_panel_mix_levels(struct jpr_panel *p, const struct jpr_mix_levels *l);
/* The platform has written sound.txt. */
void jpr_panel_mix_saved(struct jpr_panel *p);
/* The dialog has ended: the Sound view closes, as Back would. */
void jpr_panel_mix_close(struct jpr_panel *p);
/* The rows the next jpr_panel_message shows them in (stage 15); null for an ordinary message. */
void jpr_panel_rows(struct jpr_panel *p, const struct jpr_peek_rows *r);
/* SELECT opened (1) or closed (0) the peek; either way the Sound and Grammar views close. */
void jpr_panel_peek(struct jpr_panel *p, u32 open);

#endif
