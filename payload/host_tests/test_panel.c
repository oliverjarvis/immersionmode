#include <stdio.h>
#include <string.h>

#include "core/panel.h"
#include "font_blob.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define A 0x3042 /* あ */
#define I 0x3044 /* い */

static struct jpr_panel panel;
static struct jpr_font font;
static u8 blob[16384] __attribute__((aligned(4)));
static u8 fb[320 * 240 * 3];
static u16 text[512];
static u32 len;
static struct jpr_unit units[16];
static u32 unit_count;

static void put(u16 u) { text[len++] = u; }
static void nl(void) { put(0x000A); }
static void clear_tag(void) { put(0x0010); put(1); put(JPR_TAG_CLEAR); }

static void unit(u32 start, u32 n, u32 entry, u32 label) {
    units[unit_count].start = (u16)start;
    units[unit_count].len = (u16)n;
    units[unit_count].entry = entry;
    units[unit_count].label = (u16)label;
    units[unit_count].kind = JPR_UNIT_WORD;
    units[unit_count].pad = 0;
    unit_count++;
}

static void show(void) {
    jpr_panel_init(&panel);
    jpr_panel_message(&panel, 7, text, len, &font, units, unit_count, unit_count ? JPR_ANNOT_OK : JPR_ANNOT_NO_RECORD);
}

/* A message of `blocks` blocks of `lines` one-glyph lines each, no units. */
static void message(u32 blocks, u32 lines) {
    u32 b, k;
    len = 0;
    unit_count = 0;
    for (b = 0; b < blocks; b++) {
        if (b) { clear_tag(); nl(); }
        for (k = 0; k < lines; k++) {
            if (k) nl();
            put(A);
        }
    }
    show();
}

/* "あいあい / あい": units あい (entry 5, label 1), あい (no entry), あい on line 2 (entry 6). */
static void words(void) {
    len = 0;
    unit_count = 0;
    put(A); put(I); put(A); put(I); nl(); put(A); put(I);
    unit(0, 2, 5, 1);
    unit(2, 2, JPR_NO_ENTRY, JPR_NO_LABEL);
    unit(5, 2, 6, JPR_NO_LABEL);
    show();
}

static void touch(u32 down, u32 x, u32 y) {
    jpr_panel_touch(&panel, down, x, y, &font, text, len);
}

static void tap(u32 x, u32 y) {
    touch(1, x, y);
    touch(0, 0, 0);
}

#define PREV_X (JPR_PREV_X + 10)
#define NEXT_X (JPR_NEXT_X + 10)
#define BTN_Y (JPR_BTN_Y + 10)
#define LINE_Y(k) (JPR_TEXT_TOP + (k) * JPR_LINE_PITCH + 10)
#define GLYPH_X(k) (JPR_TEXT_X + (k) * 17 + 8)

static void test_pages_step(void) {
    struct jpr_pages t = {0};
    CHECK(!jpr_pages_step(&t, 0, 0));
    CHECK(!jpr_pages_step(&t, JPR_WIN_WAITING, JPR_TAG_SCROLL));
    CHECK(!jpr_pages_step(&t, 3, JPR_TAG_SCROLL));
    CHECK(!jpr_pages_step(&t, JPR_WIN_WAITING, JPR_TAG_CLEAR));
    CHECK(!jpr_pages_step(&t, JPR_WIN_WAITING, JPR_TAG_CLEAR));
    CHECK(jpr_pages_step(&t, 3, JPR_TAG_CLEAR));
    CHECK(!jpr_pages_step(&t, 0, 0));
}

static void test_message_resets(void) {
    message(3, 1);
    CHECK(panel.st.magic == JPR_PANEL_MAGIC);
    CHECK(panel.st.msg_seq == 7 && panel.st.block_count == 3);
    CHECK(panel.st.live_block == 0 && panel.st.view_block == 0 && panel.st.scroll == 0);
    CHECK(panel.st.unit_count == 0 && panel.st.annot_status == JPR_ANNOT_NO_RECORD && panel.st.selected == 0);
    CHECK(panel.st.card_entry == JPR_NO_ENTRY);
}

static void test_box_moves_live_and_view_together(void) {
    message(3, 1);
    tap(NEXT_X, BTN_Y);
    CHECK(panel.st.view_block == 1 && panel.st.live_block == 0);
    jpr_panel_box(&panel, JPR_WIN_WAITING, JPR_TAG_CLEAR);
    jpr_panel_box(&panel, 3, JPR_TAG_CLEAR);
    CHECK(panel.st.live_block == 1 && panel.st.view_block == 1);
    jpr_panel_box(&panel, JPR_WIN_WAITING, JPR_TAG_CLEAR);
    jpr_panel_box(&panel, 3, 0);
    jpr_panel_box(&panel, JPR_WIN_WAITING, JPR_TAG_CLEAR);
    jpr_panel_box(&panel, 3, 0);
    CHECK(panel.st.live_block == 2);
}

static void test_buttons_page_within_the_message(void) {
    message(3, 1);
    tap(NEXT_X, BTN_Y);
    tap(NEXT_X, BTN_Y);
    CHECK(panel.st.view_block == 2 && panel.st.taps == 2);
    tap(PREV_X, BTN_Y);
    CHECK(panel.st.view_block == 1 && panel.st.taps == 3);
    message(1, 1);
    tap(PREV_X, BTN_Y);
    tap(NEXT_X, BTN_Y);
    CHECK(panel.st.view_block == 0 && panel.st.taps == 0);
}

static void test_holding_does_not_repeat(void) {
    message(3, 1);
    touch(1, NEXT_X, BTN_Y);
    touch(1, NEXT_X, BTN_Y);
    touch(1, NEXT_X, BTN_Y);
    CHECK(panel.st.view_block == 1);
    touch(0, 0, 0);
    tap(200, 150);                                 /* the card area does nothing */
    CHECK(panel.st.view_block == 1 && panel.st.taps == 1);
}

static void test_drag_scrolls_long_blocks_only(void) {
    message(1, 6);
    touch(1, 100, 80);
    touch(1, 100, 60);
    CHECK(panel.st.scroll == 20);
    touch(1, 100, 0);
    CHECK(panel.st.scroll == 2 * JPR_LINE_PITCH);
    touch(1, 100, 200);
    CHECK(panel.st.scroll == 0);
    touch(0, 0, 0);
    message(1, 3);
    touch(1, 100, 80);
    touch(1, 100, 10);
    CHECK(panel.st.scroll == 0);
    touch(0, 0, 0);
}

static void test_tap_selects_the_unit_under_the_finger(void) {
    words();
    tap(GLYPH_X(1), LINE_Y(0));                    /* い of the first unit */
    CHECK(panel.st.selected == 1 && panel.st.sel_start == 0 && panel.st.sel_len == 2);
    CHECK(panel.st.card_entry == 5 && panel.card_wanted && panel.st.card_status == JPR_CARD_NONE);
    tap(GLYPH_X(2), JPR_TEXT_TOP + 1);             /* top row of the line box: second unit */
    CHECK(panel.st.selected == 2 && panel.st.card_status == JPR_CARD_NO_ENTRY && !panel.card_wanted);
    tap(GLYPH_X(0), LINE_Y(1));                    /* second line */
    CHECK(panel.st.selected == 3 && panel.st.sel_start == 5 && panel.st.card_entry == 6);
    tap(200, LINE_Y(0));                           /* past the end of the line: nothing changes */
    tap(GLYPH_X(0), LINE_Y(3));                    /* below the last line */
    CHECK(panel.st.selected == 3);
}

static void test_a_drag_is_not_a_tap(void) {
    words();
    touch(1, GLYPH_X(0), LINE_Y(0));
    touch(1, GLYPH_X(0), LINE_Y(0) + JPR_TAP_SLOP + 1);
    touch(0, 0, 0);
    CHECK(panel.st.selected == 0);
    touch(1, GLYPH_X(0), LINE_Y(0));
    touch(1, GLYPH_X(0) + 3, LINE_Y(0) + 3);       /* a wobble is still a tap */
    touch(0, 0, 0);
    CHECK(panel.st.selected == 1);
}

static void test_taps_follow_the_scroll(void) {
    u32 k;
    len = 0;
    unit_count = 0;
    for (k = 0; k < 6; k++) {
        if (k) nl();
        unit(len, 2, 10 + k, JPR_NO_LABEL);
        put(A);
        put(I);
    }
    show();
    touch(1, 100, 80);
    touch(1, 100, 80 - JPR_LINE_PITCH);
    touch(0, 0, 0);
    CHECK(panel.st.scroll == JPR_LINE_PITCH && panel.st.selected == 0);
    tap(GLYPH_X(0), LINE_Y(0));                    /* the top line on screen is the second line */
    CHECK(panel.st.selected == 2 && panel.st.card_entry == 11);
}

static void test_selection_clears(void) {
    len = 0;
    unit_count = 0;
    put(A); put(I); clear_tag(); nl(); put(A);
    unit(0, 2, 5, JPR_NO_LABEL);
    show();
    tap(GLYPH_X(0), LINE_Y(0));
    CHECK(panel.st.selected == 1);
    tap(NEXT_X, BTN_Y);
    CHECK(panel.st.view_block == 1 && panel.st.selected == 0 && !panel.card_wanted);
    tap(PREV_X, BTN_Y);
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_box(&panel, JPR_WIN_WAITING, JPR_TAG_CLEAR);
    jpr_panel_box(&panel, 3, JPR_TAG_CLEAR);
    CHECK(panel.st.selected == 0);
    tap(GLYPH_X(0), LINE_Y(0));
    show();
    CHECK(panel.st.selected == 0);
}

static u32 put_string(u8 *out, const char *s) {
    u32 n = (u32)strlen(s);
    out[0] = (u8)n;
    out[1] = (u8)(n >> 8);
    memcpy(out + 2, s, n);
    return 2 + n;
}

/* あ 【あ】 noun: "a word", label "-te iru" */
static u32 entry(u8 *out) {
    u32 n = 8;
    out[0] = JPR_ENTRY_DICTIONARY; out[1] = 1; out[2] = out[3] = 0;
    out[4] = 0x41; out[5] = 0x42; out[6] = 0x0F; out[7] = 0;      /* JMdict 1000001 */
    n += put_string(out + n, "\xE3\x81\x82");
    n += put_string(out + n, "\xE3\x81\x82");
    n += put_string(out + n, "noun");
    n += put_string(out + n, "a word");
    return n;
}

static void test_card_reading(void) {
    u32 n;
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    n = entry(panel.entry_bytes);
    jpr_panel_card(&panel, &font, text, 1, n, put_string(panel.label_bytes, "-te iru"));
    CHECK(panel.st.card_status == JPR_CARD_OK && !panel.card_wanted);
    CHECK(panel.card.senses == 1 && panel.label.len == 7);
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 0, 0, 0);               /* the read failed */
    CHECK(panel.st.card_status == JPR_CARD_UNREADABLE);
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, 3, 0);               /* not an entry */
    CHECK(panel.st.card_status == JPR_CARD_UNREADABLE);
}

static u32 colour(u32 x, u32 y) {
    const u8 *p = fb + (x * 240 + (239 - y)) * 3;
    return ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
}

static void draw(void) {
    struct jpr_surface s;
    memset(fb, 0, sizeof fb);
    jpr_surface_init(&s, fb, JPR_GL_RGB8, 320, 240);
    jpr_panel_draw(&panel, &s, &font, text);
}

static u32 count(jpr_rgb c, u32 top, u32 bottom) {
    u32 x, y, n = 0;
    for (x = 0; x < 320; x++)
        for (y = top; y < bottom; y++) n += colour(x, y) == c;
    return n;
}

#define BASE0 (JPR_TEXT_TOP + JPR_TEXT_BASELINE)
#define CT ((u32)jpr_panel_card_top(&panel))   /* the card's top for the message shown */

static void test_draw_frame(void) {
    message(2, 1);
    draw();
    CHECK(colour(319, 0) == JPR_COLOR_BG);
    CHECK(colour(JPR_TEXT_X + 1, BASE0 - 15) == JPR_COLOR_TEXT);
    CHECK(colour(4 + 8, CT) == JPR_COLOR_CARD_BORDER && colour(4 + 8, CT + 3) == JPR_COLOR_CARD);  /* the card panel */
    CHECK(colour(10, CT - 1) == JPR_COLOR_BG);                                   /* the gap above it */
    CHECK(colour(10, JPR_BAR_TOP - 1) == JPR_COLOR_RULE);
    CHECK(colour(JPR_PREV_X + (JPR_PAGE_W >> 1), JPR_BTN_Y + (JPR_BTN_H >> 1)) == JPR_COLOR_ARROW_OFF);
    CHECK(colour(JPR_NEXT_X + (JPR_PAGE_W >> 1), JPR_BTN_Y + (JPR_BTN_H >> 1)) == JPR_COLOR_ARROW);
    CHECK(count(JPR_COLOR_TEXT, JPR_BAR_TOP, 240) > 20);        /* the counter */
    CHECK(count(JPR_COLOR_MUTED, CT, JPR_BAR_TOP) == 0);  /* no units: no hint */
}

static void test_draw_underlines_and_selection(void) {
    words();
    draw();
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE);
    CHECK(colour(JPR_TEXT_X + 31, BASE0 + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE);
    CHECK(colour(JPR_TEXT_X + 33, BASE0 + JPR_UNDERLINE) == JPR_COLOR_BG);       /* the gap */
    CHECK(colour(JPR_TEXT_X + 35, BASE0 + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE); /* next unit */
    CHECK(count(JPR_COLOR_MUTED, CT, JPR_BAR_TOP) > 50);               /* "Tap a word" */
    tap(GLYPH_X(0), LINE_Y(0));
    draw();
    CHECK(colour(JPR_TEXT_X, JPR_TEXT_TOP + 2) == JPR_COLOR_SELECT);
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE) == JPR_COLOR_ACCENT);
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE + 2) == JPR_COLOR_ACCENT); /* 3 px */
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE + 3) == JPR_COLOR_BG);
    CHECK(colour(JPR_TEXT_X + 35, BASE0 + JPR_UNDERLINE + 1) == JPR_COLOR_BG);   /* plain: 1 px */
    CHECK(colour(JPR_TEXT_X + 1, BASE0 - 15) == JPR_COLOR_TEXT);
    CHECK(colour(JPR_TEXT_X + 40, JPR_TEXT_TOP + 2) == JPR_COLOR_BG);            /* only the selected unit */
}

static void test_draw_card(void) {
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), put_string(panel.label_bytes, "-te iru"));
    draw();
    CHECK(count(JPR_COLOR_TEXT, CT, CT + JPR_CARD_HEAD + 2) > 150);  /* headword */
    CHECK(count(JPR_COLOR_MUTED, CT, JPR_BAR_TOP) > 60);             /* reading, "in text", → */
    CHECK(count(JPR_COLOR_PILL, CT, CT + JPR_CARD_HEAD + 4) > 100);  /* the part-of-speech pill */
    CHECK(count(JPR_COLOR_SELECT, CT + JPR_CARD_HEAD, JPR_BAR_TOP) > 100);   /* the ending's chip */
    CHECK(count(JPR_COLOR_TEXT, CT + JPR_CARD_HEAD + 4, JPR_BAR_TOP) > 150);  /* in text, sense */
    tap(GLYPH_X(2), LINE_Y(0));                    /* no entry: the word and a note */
    draw();
    CHECK(count(JPR_COLOR_TEXT, CT, JPR_BAR_TOP) > 150 && count(JPR_COLOR_MUTED, CT, JPR_BAR_TOP) > 50);
}

/* A card with three long senses: more than fits between the card's top and the bar. */
static u32 long_entry(u8 *out) {
    static const char *const senses[] = {
        "to catch; to capture; to arrest; to seize; to restrain; to take hold of; to get hold of; to lay hands on; "
        "to take into custody",
        "to grab; to clutch; to grasp; to seize; to hold on to; to keep hold of; to cling to; to hang on to; "
        "to keep a firm grip on",
        "to catch hold of (someone); to stop (e.g. a stranger in the street); to hail (a taxi, waiter, etc.); "
        "to hold (someone) back; to detain"};
    u32 n = 8, k;
    out[0] = JPR_ENTRY_DICTIONARY; out[1] = 3; out[2] = out[3] = 0;
    out[4] = out[5] = out[6] = out[7] = 0;
    n += put_string(out + n, "\xE3\x81\x82\xE3\x81\x84");
    n += put_string(out + n, "\xE3\x81\x82\xE3\x81\x84");
    n += put_string(out + n, "verb");
    for (k = 0; k < 3; k++) n += put_string(out + n, senses[k]);
    return n;
}

#define CARD_AREA ((u32)jpr_panel_card_area(&panel))

static void test_long_card_scrolls_by_dragging(void) {
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, long_entry(panel.entry_bytes), 0);
    CHECK(panel.card_height > CARD_AREA);
    CHECK(panel.card_scroll == 0);
    touch(1, 200, 190);                            /* drag up inside the card */
    touch(1, 200, 150);
    CHECK(panel.card_scroll == 40 && panel.st.card_scroll == 40);
    CHECK(panel.st.scroll == 0);                   /* the text did not move */
    touch(1, 200, 0);                              /* far past the end: clamped */
    CHECK(panel.card_scroll == panel.card_height + JPR_CARD_PAD - CARD_AREA);
    touch(0, 200, 0);
    CHECK(panel.st.selected == 1);                 /* a card drag selects nothing */
    tap(GLYPH_X(0), LINE_Y(1));                    /* another unit: its card starts at the top */
    CHECK(panel.st.selected == 3 && panel.card_scroll == 0);
}

static void test_short_card_does_not_scroll(void) {
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), 0);
    CHECK(panel.card_height <= CARD_AREA);
    touch(1, 200, 190);
    touch(1, 200, 150);
    touch(0, 200, 150);
    CHECK(panel.card_scroll == 0);
}

static void test_scrolled_card_shows_other_rows(void) {
    u32 before, after;
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, long_entry(panel.entry_bytes), 0);
    draw();
    before = count(JPR_COLOR_TEXT, CT, JPR_BAR_TOP - 1);
    CHECK(count(JPR_COLOR_MUTED, JPR_BAR_TOP - 18, JPR_BAR_TOP - 1) > 0);   /* the more-below mark */
    touch(1, 200, 200);
    touch(1, 200, 100);
    touch(0, 200, 100);
    draw();
    after = count(JPR_COLOR_TEXT, CT, JPR_BAR_TOP - 1);
    CHECK(panel.card_scroll > 0 && after != before);
    CHECK(colour(10, CT - 1) == JPR_COLOR_BG);                              /* nothing drawn above the card */
    CHECK(count(JPR_COLOR_TEXT, 0, CT - 1) > 0);                            /* the text is still there */
}

static void test_text_area_fits_its_lines(void) {
    message(1, 1);
    CHECK(jpr_panel_card_top(&panel) == JPR_TEXT_TOP + 1 * JPR_LINE_PITCH + 8);
    message(1, 2);
    CHECK(jpr_panel_card_top(&panel) == JPR_TEXT_TOP + 2 * JPR_LINE_PITCH + 8);
    message(1, 4);
    CHECK(jpr_panel_card_top(&panel) == JPR_TEXT_TOP + 4 * JPR_LINE_PITCH + 8);
    message(1, 6);                                 /* a long block scrolls in four lines */
    CHECK(jpr_panel_card_top(&panel) == JPR_TEXT_TOP + 4 * JPR_LINE_PITCH + 8);
}

static void test_card_follows_the_text(void) {
    u32 one_line;
    words();                                       /* two lines */
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, long_entry(panel.entry_bytes), 0);
    touch(1, 200, CT + 2);                         /* just inside the card: a card drag */
    touch(1, 200, CT + 2 + 30);
    touch(1, 200, CT + 2 - 30);
    touch(0, 200, CT + 2 - 30);
    CHECK(panel.card_scroll == 30);
    tap(GLYPH_X(0), CT - 2);                       /* the gap between text and card: nothing */
    CHECK(panel.st.selected == 1);
    one_line = 0;
    len = 0;                                       /* a one-line message gives the card more room */
    unit_count = 0;
    put(A); put(I);
    unit(0, 2, 5, 1);
    show();
    one_line = jpr_panel_card_area(&panel);
    words();
    CHECK(one_line == jpr_panel_card_area(&panel) + JPR_LINE_PITCH);
}

static void test_grammar_button_is_the_accent(void) {
    words();
    draw();
    CHECK(colour(JPR_GRAMMAR_X + (JPR_GRAMMAR_W >> 1), JPR_BTN_Y + 3) == JPR_COLOR_ACCENT);
    len = 0;                                       /* a block with no characters: the button is muted */
    unit_count = 0;
    nl();
    show();
    draw();
    CHECK(colour(JPR_GRAMMAR_X + (JPR_GRAMMAR_W >> 1), JPR_BTN_Y + 3) != JPR_COLOR_ACCENT);
}

static void test_badges_number_the_meanings(void) {
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, long_entry(panel.entry_bytes), 0);
    draw();
    CHECK(count(JPR_COLOR_ACCENT, CT + JPR_CARD_HEAD, JPR_BAR_TOP) > 60);   /* round badges with digits */
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), 0);    /* one meaning: no badge */
    draw();
    CHECK(count(JPR_COLOR_ACCENT, CT + JPR_CARD_HEAD + 6, JPR_BAR_TOP - 4) == 0);
}

static void test_draw_reports_problems(void) {
    words();
    panel.st.font_status = JPR_FONT_MISSING;
    draw();
    CHECK(count(JPR_COLOR_ERROR, CT, JPR_BAR_TOP) > 100);
    words();
    panel.st.data_status = JPR_DATA_BAD;
    panel.st.data_error = JPR_DATA_DICT << 8;
    draw();
    CHECK(count(JPR_COLOR_ERROR, CT, JPR_BAR_TOP) > 100);
    CHECK(count(JPR_COLOR_MUTED, CT, JPR_BAR_TOP) == 0);
    words();
    panel.st.data_status = JPR_DATA_MISMATCH;              /* annot.bin and dict.bin from different builds */
    draw();
    CHECK(count(JPR_COLOR_ERROR, CT, JPR_BAR_TOP) > 100);
}

#define BAR_Y (JPR_BTN_Y + 10)

static void grammar_tap(void) { tap(JPR_GRAMMAR_X + 20, BAR_Y); }

static void test_grammar_opens_and_asks(void) {
    words();
    grammar_tap();
    CHECK(panel.gr.open && panel.gr.state == JPR_GR_ASKING && panel.gr.pending && panel.gr.job == 1);
    CHECK(panel.st.gr_open == 1 && panel.st.gr_state == JPR_GR_ASKING);
    CHECK(panel.gr.ex.sentence_len == 6 * 3);          /* あいあい + あい: the whole message */
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)"\"Hi.\"\nline", 10);
    CHECK(panel.gr.state == JPR_GR_DONE && panel.gr.reply_len == 10 && panel.gr.height > 0);
    CHECK(panel.gr.answered_len == panel.gr.ex.sentence_len);
}

static void test_same_sentence_reuses_the_reply(void) {
    test_grammar_opens_and_asks();
    tap(JPR_BACK_X + 10, BAR_Y);
    CHECK(!panel.gr.open && panel.gr.state == JPR_GR_DONE);
    panel.gr.pending = 0;
    grammar_tap();
    CHECK(panel.gr.open && panel.gr.state == JPR_GR_DONE && !panel.gr.pending && panel.gr.job == 1);
}

static void test_back_cancels_and_stale_results_are_ignored(void) {
    words();
    grammar_tap();
    tap(JPR_BACK_X + 10, BAR_Y);
    CHECK(!panel.gr.open && panel.gr.cancel == 1 && panel.gr.state == JPR_GR_IDLE);
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)"late", 4);
    CHECK(panel.gr.state == JPR_GR_IDLE && panel.gr.reply_len == 0);
    grammar_tap();
    CHECK(panel.gr.job == 2 && panel.gr.state == JPR_GR_ASKING);
}

static void test_errors_and_retry(void) {
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_HTTP, 0, 401, (const u8 *)"No auth", 7);
    CHECK(panel.gr.state == JPR_GR_ERROR && !strcmp(panel.st.gr_message, "Token rejected: run jpreader kokoro-link"));
    CHECK(panel.st.gr_failure == JPR_NET_HTTP && panel.st.gr_http == 401);
    panel.gr.pending = 0;
    tap(JPR_RETRY_X + 10, BAR_Y);
    CHECK(panel.gr.state == JPR_GR_ASKING && panel.gr.pending && panel.gr.job == 2);
}

static void test_signature_follows_what_is_drawn(void) {
    u32 base;
    words();
    base = jpr_panel_signature(&panel);
    CHECK(jpr_panel_signature(&panel) == base);
    panel.st.frames += 5;                          /* counters that change every frame */
    panel.st.draw_ticks = 12345;
    panel.st.fb_addr = 0x33438480;
    panel.st.fb_format = JPR_GL_RGB8;
    CHECK(jpr_panel_signature(&panel) == base);
    tap(GLYPH_X(0), LINE_Y(0));                    /* a selection */
    CHECK(jpr_panel_signature(&panel) != base);
    base = jpr_panel_signature(&panel);
    panel.st.scroll = 3;
    CHECK(jpr_panel_signature(&panel) != base);
    base = jpr_panel_signature(&panel);
    panel.card_scroll = 9;
    CHECK(jpr_panel_signature(&panel) != base);
    base = jpr_panel_signature(&panel);
    panel.status[0] = 'm';                         /* the debug line */
    CHECK(jpr_panel_signature(&panel) != base);
    base = jpr_panel_signature(&panel);
    panel.gr.reply[10] ^= 1;                       /* the grammar reply */
    CHECK(jpr_panel_signature(&panel) != base);
}

static void test_reply_scrolls(void) {
    static u8 tall[400];
    u32 k;
    for (k = 0; k < sizeof tall; k++) tall[k] = (k % 20 == 19) ? '\n' : 'a';
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, tall, sizeof tall);
    CHECK(panel.gr.height == 20 * JPR_GR_PITCH);
    tap(JPR_DOWN_X + 10, BAR_Y);
    CHECK(panel.gr.scroll == 3 * JPR_GR_PITCH);
    tap(JPR_UP_X + 10, BAR_Y);
    tap(JPR_UP_X + 10, BAR_Y);
    CHECK(panel.gr.scroll == 0);
    touch(1, 100, 150);
    touch(1, 100, 50);
    touch(0, 0, 0);
    CHECK(panel.gr.scroll == 100);
    CHECK(panel.st.selected == 0);                   /* the reading view underneath is untouched */
}

static void test_draw_grammar(void) {
    words();
    grammar_tap();
    draw();
    CHECK(count(JPR_COLOR_MUTED, JPR_GR_TOP, JPR_BAR_TOP) > 50);             /* Asking… */
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_TIMEOUT, 0, 0, 0, 0);
    draw();
    CHECK(count(JPR_COLOR_ERROR, JPR_GR_TOP, JPR_BAR_TOP) > 50);             /* Timed out */
    CHECK(count(JPR_COLOR_TEXT, 0, JPR_GR_SENTENCE_BOTTOM) > 300);           /* the sentence */
}

static const char tagged[] = "T \"Setup is complete.\"\nC \xE3\x81\x82\xE3\x81\x84\nM settings + ga\nN ga marks the subject\n"
                             "C \xE3\x81\x84\nM completion\nN a noun used as a verb";

/* Rows of column x between y0 and y1 whose pixel is c. */
static u32 rows_of(u32 x, u32 y0, u32 y1, jpr_rgb c) {
    u32 y, n = 0;
    for (y = y0; y < y1; y++) n += colour(x, y) == c;
    return n;
}

static void test_tagged_reply_layout(void) {
    u32 y, first_shade = 0, bg_after_shade = 0;
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)tagged, sizeof tagged - 1);
    CHECK(panel.gr.height > 5 * JPR_GR_PITCH && panel.gr.height < 2 * (JPR_BAR_TOP - JPR_GR_TOP));
    draw();
    CHECK(rows_of(9, JPR_GR_TOP, JPR_GR_TOP + 30, JPR_COLOR_ACCENT) > 8);     /* the translation's accent bar */
    CHECK(rows_of(13, JPR_GR_TOP, JPR_GR_TOP + 30, JPR_COLOR_CARD) > 8);      /* its box */
    for (y = JPR_GR_TOP + 30; y < JPR_BAR_TOP - 1; y++) {                     /* down the left margin of the blocks */
        if (!first_shade && colour(6, y) == JPR_COLOR_SHADE) first_shade = y;
        if (first_shade && !bg_after_shade && colour(6, y) == JPR_COLOR_BG) bg_after_shade = y;
    }
    CHECK(first_shade && bg_after_shade > first_shade + 30);                  /* block 1 shaded, block 2 not */
    CHECK(count(JPR_COLOR_TEXT, JPR_GR_TOP, JPR_BAR_TOP - 1) > 300);         /* chunks and meanings */
    CHECK(count(JPR_COLOR_MUTED, JPR_GR_TOP, JPR_BAR_TOP - 1) > 100);        /* notes */
}

static void test_untagged_reply_keeps_every_line(void) {
    static const char plain[] = "\"Hi.\"\nfirst line\nsecond line\nthird line";
    u32 k;
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)plain, sizeof plain - 1);
    CHECK(panel.gr.height == 4 * JPR_GR_PITCH);
    draw();
    for (k = 0; k < 4; k++)                       /* text on each of the four rows */
        CHECK(count(JPR_COLOR_TEXT, JPR_GR_TOP + k * JPR_GR_PITCH + 2, JPR_GR_TOP + (k + 1) * JPR_GR_PITCH + 2) > 10);
}

static void test_review_fixes(void) {
    static u8 tall[400];
    static const char wide[] = "T \"Wide wide wide wide wide wide wide wide wide wide wide wide wide wide.\"\nC \xE3\x81\x82";
    u32 k, y, gap = 0;
    /* the last row of a reply scrolled to the end is whole (drawing starts 2 px into the area) */
    for (k = 0; k < sizeof tall; k++) tall[k] = (k % 20 == 19) ? '\n' : 'a';
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, tall, sizeof tall);
    for (k = 0; k < 10; k++) tap(JPR_DOWN_X + 10, BAR_Y);   /* ▼ until it stops */
    CHECK(panel.gr.scroll == panel.gr.height - (JPR_BAR_TOP - 3 - JPR_GR_TOP));
    /* a fifth line never shows above the card */
    message(1, 6);
    draw();
    CHECK(count(JPR_COLOR_TEXT, JPR_TEXT_TOP + 4 * JPR_LINE_PITCH, CT) == 0);
    /* the card's drag zone starts at the card's top row */
    words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, long_entry(panel.entry_bytes), 0);
    touch(1, 200, CT);
    touch(1, 200, CT - 20 > 0 ? CT - 20 : 0);
    touch(0, 0, 0);
    CHECK(panel.card_scroll > 0);
    /* a scrolled-to-the-end card has no more-below mark */
    touch(1, 200, 200);
    touch(1, 200, 0);
    touch(0, 0, 0);
    draw();
    CHECK(count(JPR_COLOR_MUTED, JPR_CARD_BOTTOM - 12, JPR_CARD_BOTTOM - 4) < 5);
    /* the translation stays inside its box, 4 px from its right edge */
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)wide, sizeof wide - 1);
    draw();
    for (y = JPR_GR_TOP; y < JPR_GR_TOP + 40; y++)
        for (k = 309; k < 320; k++) CHECK(colour(k, y) != JPR_COLOR_TEXT);
    /* shading alternates: inside block 2 is the background */
    words();
    grammar_tap();
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)tagged, sizeof tagged - 1);
    draw();
    for (y = JPR_GR_TOP + 30; y < JPR_BAR_TOP - 1 && !gap; y++)
        if (colour(6, y) == JPR_COLOR_SHADE && colour(6, y + 1) == JPR_COLOR_BG) gap = y + 1;
    CHECK(gap && colour(6, gap + 10) == JPR_COLOR_BG && colour(2, gap + 10) == JPR_COLOR_BG);
}

static void test_status_line(void) {
    words();
    strcpy(panel.status, "mem 3412K");
    draw();
    CHECK(count(JPR_COLOR_MUTED, JPR_BAR_TOP - 20, JPR_BAR_TOP - 1) > 30);
}

static void test_cancelled_request_does_not_leave_a_blank_reply(void) {
    len = 0;
    unit_count = 0;
    put(A); put(0x3002); clear_tag(); nl(); put(I); put(0x3002);   /* block 0: あ。, block 1: い。: two sentences */
    show();
    grammar_tap();                                  /* A */
    jpr_panel_grammar_result(&panel, &font, 1, JPR_NET_OK, 0, 200, (const u8 *)"\"A.\"", 4);
    tap(JPR_BACK_X + 10, BAR_Y);
    tap(NEXT_X, BTN_Y);
    panel.gr.pending = 0;
    grammar_tap();                                  /* B, then Back while asking */
    tap(JPR_BACK_X + 10, BAR_Y);
    tap(PREV_X, BTN_Y);
    panel.gr.pending = 0;
    grammar_tap();                                  /* A again */
    CHECK(panel.gr.state == JPR_GR_ASKING || (panel.gr.state == JPR_GR_DONE && panel.gr.reply_len > 0));
}

static void test_a_press_on_the_panel_stays_hidden_until_it_lifts(void) {
    struct jpr_touch_gate g = {0, 0};
    CHECK(jpr_touch_hide(&g, 1, 1) == 1);           /* Back pressed on the panel */
    CHECK(jpr_touch_hide(&g, 1, 0) == 1);           /* the panel went away; the finger is still down */
    CHECK(jpr_touch_hide(&g, 0, 0) == 0);           /* lifted */
    CHECK(jpr_touch_hide(&g, 1, 0) == 0);           /* a new press with no panel reaches the game */
    CHECK(jpr_touch_hide(&g, 1, 1) == 0);           /* ...even if the panel comes up while it is held */
    CHECK(jpr_touch_hide(&g, 0, 1) == 0);
}

#define ROW_COUNT(c) count((c), JPR_KOKORO_TOP, JPR_KOKORO_TOP + JPR_KOKORO_ROW)
#define ADD_TAP_X (JPR_ADD_X + 20)
#define ROW_Y (JPR_KOKORO_TOP + 9)

static u8 marks_bits[8];

static void mark(u32 entry, u32 state) { marks_bits[entry >> 2] |= (u8)(state << ((entry & 3) * 2)); }

/* words() with kokoro on and no marks yet: units あい (entry 5), あい (no entry), あい on line 2 (entry 6). */
static void kokoro_words(void) {
    words();
    memset(marks_bits, 0, sizeof marks_bits);
    panel.st.kokoro_on = 1;
}

static void marks(void) { jpr_panel_marks(&panel, marks_bits, 32, 0, 0); }

/* Selects unit 0 (entry 5) with its card read, then marks. */
static void select_word(void) {
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), 0);
    marks();
}

static void test_marks_follow_kokoro(void) {
    kokoro_words();
    mark(5, JPR_KOKORO_KNOWN);
    mark(6, JPR_KOKORO_LEARNING);
    marks();
    CHECK(panel.marks[0] == JPR_KOKORO_KNOWN && panel.marks[1] == 0 && panel.marks[2] == JPR_KOKORO_LEARNING);
    CHECK(panel.st.marks_known == 1 && panel.st.marks_learning == 1);
    jpr_panel_marks(&panel, marks_bits, 6, 0, 0);           /* entry 6 is past the marks */
    CHECK(panel.marks[2] == 0 && panel.st.marks_learning == 0);
    panel.st.kokoro_on = 0;
    marks();
    CHECK(panel.marks[0] == 0 && panel.st.marks_known == 0);
}

static void test_overrides_fill_in_only(void) {
    struct jpr_override o[2] = {{5, JPR_KOKORO_NEW}, {6, JPR_KOKORO_QUEUED}};
    kokoro_words();
    mark(5, JPR_KOKORO_KNOWN);
    jpr_panel_marks(&panel, marks_bits, 32, o, 2);
    CHECK(panel.marks[0] == JPR_KOKORO_KNOWN && panel.marks[2] == JPR_KOKORO_QUEUED);
    jpr_panel_marks(&panel, 0, 0, o, 2);                    /* no list yet */
    CHECK(panel.marks[0] == JPR_KOKORO_NEW);
}

static void test_underlines_take_kokoro_colours(void) {
    kokoro_words();
    mark(5, JPR_KOKORO_KNOWN);
    mark(6, JPR_KOKORO_LEARNING);
    marks();
    draw();
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE) == JPR_COLOR_KNOWN);
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE + 1) == JPR_COLOR_KNOWN);                    /* 2 px */
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE + 2) == JPR_COLOR_BG);
    CHECK(colour(JPR_TEXT_X + 35, BASE0 + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE);                   /* no entry */
    CHECK(colour(JPR_TEXT_X + 35, BASE0 + JPR_UNDERLINE + 1) == JPR_COLOR_BG);                      /* 1 px */
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_LINE_PITCH + JPR_UNDERLINE) == JPR_COLOR_LEARNING);    /* line 2 */
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_LINE_PITCH + JPR_UNDERLINE + 1) == JPR_COLOR_LEARNING);
    tap(GLYPH_X(0), LINE_Y(0));
    marks();
    draw();
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE) == JPR_COLOR_ACCENT);                        /* selected */
    kokoro_words();
    mark(5, JPR_KOKORO_KNOWN);
    panel.st.kokoro_on = 0;                                                    /* not linked: as before */
    marks();
    draw();
    CHECK(colour(JPR_TEXT_X + 3, BASE0 + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE);
}

static void test_kokoro_row_and_add_button(void) {
    u32 area;
    words();
    select_word();                                         /* kokoro off */
    area = jpr_panel_card_area(&panel);
    draw();
    CHECK(count(JPR_COLOR_KNOWN, CT, JPR_BAR_TOP) == 0);
    kokoro_words();
    select_word();
    CHECK(panel.st.card_kokoro == JPR_KOKORO_NONE);
    CHECK(jpr_panel_card_area(&panel) == area - JPR_KOKORO_ROW);   /* the card scrolls above the row */
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_KNOWN) > 800);              /* the button */
    CHECK(ROW_COUNT(JPR_COLOR_ON_ACCENT) > 40);           /* its label */
    CHECK(ROW_COUNT(JPR_COLOR_MUTED) > 40);               /* "Not in kokoro" */
    tap(ADD_TAP_X, ROW_Y);
    CHECK(panel.add_wanted == 1 && panel.st.add_state == JPR_ADD_ASKING && panel.dragging == 0);
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_KNOWN) == 0);               /* "Adding…" in its place */
    tap(ADD_TAP_X, ROW_Y);
    CHECK(panel.st.add_state == JPR_ADD_ASKING);          /* one add at a time */
}

static void test_answers_go_to_their_own_word(void) {
    kokoro_words();
    select_word();
    tap(ADD_TAP_X, ROW_Y);
    jpr_panel_added(&panel, 6, JPR_ADD_FAILED);           /* another word's answer */
    CHECK(panel.st.add_state == JPR_ADD_ASKING);
    jpr_panel_added(&panel, 5, JPR_ADD_DONE);
    CHECK(panel.st.add_state == JPR_ADD_DONE);
    tap(GLYPH_X(0), LINE_Y(1));                           /* another word: its own state */
    CHECK(panel.st.add_state == JPR_ADD_NONE && panel.add_wanted == 0);
    jpr_panel_added(&panel, 5, JPR_ADD_FAILED);           /* the first word's late answer */
    CHECK(panel.st.add_state == JPR_ADD_NONE);
}

static void test_known_word_has_no_button(void) {
    u32 n;
    kokoro_words();
    mark(5, JPR_KOKORO_KNOWN);
    select_word();
    CHECK(panel.st.card_kokoro == JPR_KOKORO_KNOWN);
    draw();
    n = ROW_COUNT(JPR_COLOR_KNOWN);
    CHECK(n > 10 && n < 60);                              /* the dot only */
    tap(ADD_TAP_X, ROW_Y);
    CHECK(panel.add_wanted == 0);
}

static void test_add_failures(void) {
    kokoro_words();
    select_word();
    tap(ADD_TAP_X, ROW_Y);
    jpr_panel_added(&panel, 5, JPR_ADD_FAILED);
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_ERROR) > 20 && ROW_COUNT(JPR_COLOR_KNOWN) > 800);   /* "kokoro error", and try again */
    panel.add_wanted = 0;
    tap(ADD_TAP_X, ROW_Y);
    CHECK(panel.add_wanted == 1);
    jpr_panel_added(&panel, 5, JPR_ADD_REJECTED);
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_ERROR) > 20 && ROW_COUNT(JPR_COLOR_KNOWN) == 0);    /* a wrong token: no button */
}

static void test_queued_word(void) {
    struct jpr_override o = {5, JPR_KOKORO_QUEUED};
    kokoro_words();
    tap(GLYPH_X(0), LINE_Y(0));
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), 0);
    jpr_panel_marks(&panel, marks_bits, 32, &o, 1);
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_KNOWN) == 0 && ROW_COUNT(JPR_COLOR_MUTED) > 80);   /* "Saved: will send later" */
    tap(ADD_TAP_X, ROW_Y);
    CHECK(panel.add_wanted == 0);
}

/* A saved add that the flush delivered is kokoro's now, up next: the v2 list never lists it (unstudied). */
static void test_a_delivered_add_is_up_next(void) {
    struct jpr_override o[3] = {{5, JPR_KOKORO_QUEUED}, {6, JPR_KOKORO_KNOWN}, {7, JPR_KOKORO_QUEUED}};
    jpr_overrides_sent(o, 3);
    CHECK(o[0].state == JPR_KOKORO_NEW && o[1].state == JPR_KOKORO_KNOWN && o[2].state == JPR_KOKORO_NEW);
}

static void test_kokoro_row_texts(void) {
    const char *status, *after;
    panel.st.card_kokoro = JPR_KOKORO_NONE;
    panel.st.add_state = JPR_ADD_NONE;
    jpr_panel_kokoro_texts(&panel, &status, &after);
    CHECK(!strcmp(status, "Not in kokoro") && after == 0);
    panel.st.card_kokoro = JPR_KOKORO_NEW;
    panel.st.add_state = JPR_ADD_DONE;
    jpr_panel_kokoro_texts(&panel, &status, &after);
    CHECK(!strcmp(status, "Up next in kokoro") && !strcmp(after, "Added: up next"));
    panel.st.card_kokoro = JPR_KOKORO_KNOWN;                        /* the add found it known already */
    jpr_panel_kokoro_texts(&panel, &status, &after);
    CHECK(!strcmp(status, "Known") && !strcmp(after, "Already in kokoro"));
    panel.st.card_kokoro = JPR_KOKORO_QUEUED;
    panel.st.add_state = JPR_ADD_QUEUED;
    jpr_panel_kokoro_texts(&panel, &status, &after);
    CHECK(!strcmp(status, "Not in kokoro") && !strcmp(after, "Saved: will send later"));
    panel.st.add_state = JPR_ADD_REJECTED;
    jpr_panel_kokoro_texts(&panel, &status, &after);
    CHECK(!strcmp(status, "Token rejected: run jpreader kokoro-link"));
    panel.st.card_kokoro = JPR_KOKORO_NONE;
    panel.st.add_state = JPR_ADD_NONE;
}

static void test_no_row_for_particles(void) {
    u32 n;
    kokoro_words();
    tap(GLYPH_X(0), LINE_Y(0));
    n = entry(panel.entry_bytes);
    panel.entry_bytes[0] = JPR_ENTRY_PARTICLE;
    jpr_panel_card(&panel, &font, text, 1, n, 0);
    marks();
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_KNOWN) == 0 && ROW_COUNT(JPR_COLOR_RULE) == 0);
}

static void test_signature_follows_kokoro(void) {
    u32 before;
    kokoro_words();
    select_word();
    before = jpr_panel_signature(&panel);
    mark(5, JPR_KOKORO_LEARNING);
    marks();
    CHECK(jpr_panel_signature(&panel) != before);
    before = jpr_panel_signature(&panel);
    marks();
    CHECK(jpr_panel_signature(&panel) == before);         /* marking again, nothing changed: no redraw */
}

static void test_an_add_survives_moving_on(void) {
    kokoro_words();
    select_word();
    tap(ADD_TAP_X, ROW_Y);                                  /* the worker is busy: not sent yet */
    CHECK(panel.add_wanted == 1 && panel.add_entry == 5 &&
          strstr((const char *)panel.add_body, "front=\xE3\x81\x82\n") != 0);
    tap(GLYPH_X(0), LINE_Y(1));                             /* another word before it goes */
    CHECK(panel.add_wanted == 1 && panel.add_entry == 5 && panel.st.add_state == JPR_ADD_NONE);
    jpr_panel_card(&panel, &font, text, 1, entry(panel.entry_bytes), 0);
    marks();
    draw();
    CHECK(ROW_COUNT(JPR_COLOR_KNOWN) == 0);                 /* one add at a time */
    tap(GLYPH_X(0), LINE_Y(0));                             /* back to the word being added */
    CHECK(panel.st.add_state == JPR_ADD_ASKING);
    jpr_panel_added(&panel, 5, JPR_ADD_DONE);
    CHECK(panel.st.add_state == JPR_ADD_DONE && panel.add_entry == JPR_NO_ENTRY && panel.add_wanted == 0);
}

/* Stage 17: kokoro's grammar points. Chunk あ has a point kokoro has (learning); chunk い one it lacks (id 12, it
 * fits い), then three of no chunk: "-" (no Bunpro match), a known one, and one it lacks (id 40). */
static const char pointed[] = "T \"Wait.\"\nC \xE3\x81\x82\nM wait\nN request\nG bunpro:7\tlearning\tga\tsubject marker\n"
                              "C \xE3\x81\x84\nM did\nN polite past\nG bunpro:12\tnone\t\xE3\x81\x84\tpolite past ending\n"
                              "G -\tnone\tzz\tno match\nG bunpro:31\tknown\tkudasai\tplease\nG bunpro:40\tnone\tmashou\tlet us\n";

static void point_reply_text(const char *r, u32 n) {
    jpr_panel_grammar_result(&panel, &font, panel.gr.job, JPR_NET_OK, 0, 200, (const u8 *)r, n);
}

/* kokoro on, Grammar asked and answered with `pointed`. */
static void point_reply(void) {
    kokoro_words();
    grammar_tap();
    point_reply_text(pointed, sizeof pointed - 1);
}

/* Scrolls the reply so point row k is in view; its top on the screen. */
static u32 show_point(u32 k) {
    u32 area = JPR_BAR_TOP - 3 - JPR_GR_TOP, top = panel.gr.points[k].top;
    panel.gr.scroll = top < panel.gr.height - area ? top : panel.gr.height - area;
    touch(0, 0, 0);                                                     /* the state follows the scroll */
    return JPR_GR_TOP + 2 + top - panel.gr.scroll;
}

#define GR_X 12   /* panel.c: the reply's left edge */
#define COLUMN_X (JPR_CARD_RIGHT - 20)
#define COLUMN_COUNT(c, y) count_in((c), JPR_CARD_RIGHT - JPR_GR_ADD_W, JPR_CARD_RIGHT, (y), (y) + JPR_GR_PITCH)

static u32 count_in(jpr_rgb c, u32 x0, u32 x1, u32 y0, u32 y1) {
    u32 x, y, n = 0;
    for (x = x0; x < x1; x++)
        for (y = y0; y < y1; y++) n += colour(x, y) == c;
    return n;
}

static void test_points_are_rows(void) {
    point_reply();
    CHECK(panel.gr.point_count == 5 && panel.gr.other_count == 3);
    CHECK(panel.st.gr_points == 5 && panel.st.gr_other == 3 && panel.st.gr_buttons == 2);
    CHECK(panel.gr.points[0].id == 7 && panel.gr.points[0].status == JPR_POINT_LEARNING && !panel.gr.points[0].slot);
    CHECK(panel.gr.points[1].id == 12 && panel.gr.points[1].slot);
    CHECK(panel.gr.points[2].id == 0 && !panel.gr.points[2].slot);      /* "-": never a button */
    CHECK(panel.gr.points[4].id == 40 && panel.gr.points[4].slot);
    CHECK(panel.gr.points[2].top > panel.gr.points[1].top + JPR_GR_PITCH);   /* a new block, with its title */
    CHECK(panel.st.gr_button_y == JPR_GR_TOP + 2 + panel.gr.points[1].top);
    CHECK(panel.st.gr_add_state == JPR_ADD_NONE);
}

static void test_draw_point_rows(void) {
    u32 y;
    point_reply();
    y = show_point(0);
    draw();
    CHECK(colour(GR_X + 3, y + 10) == JPR_COLOR_LEARNING);             /* the dot */
    CHECK(colour(6, y + 10) == JPR_COLOR_SHADE);                        /* in chunk 1's shaded block */
    CHECK(count_in(JPR_COLOR_TEXT, GR_X + 11, GR_X + 30, y, y + JPR_GR_PITCH) > 20);    /* "ga", bold */
    CHECK(count_in(JPR_COLOR_MUTED, GR_X + 30, 200, y, y + JPR_GR_PITCH) > 60);         /* the note, · learning */
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) == 0);                       /* kokoro has it: no button */
    y = show_point(1);
    draw();
    CHECK(colour(GR_X + 3, y + 10) == JPR_COLOR_UNDERLINE);            /* grey: not in kokoro */
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) > 300);                      /* "+ Add" */
    CHECK(COLUMN_COUNT(JPR_COLOR_ON_ACCENT, y) > 30);                   /* its label */
    y = show_point(3);                                                  /* "Other grammar": known */
    draw();
    CHECK(colour(GR_X + 3, y + 10) == JPR_COLOR_KNOWN);
    CHECK(colour(6, y + 10) == JPR_COLOR_BG && colour(6, y - JPR_GR_PITCH - 10) == JPR_COLOR_BG);   /* unshaded */
    CHECK(count_in(JPR_COLOR_MUTED, GR_X, GR_X + 90, JPR_GR_TOP + 2 + panel.gr.points[2].top - panel.gr.scroll - JPR_GR_PITCH,
                   JPR_GR_TOP + 2 + panel.gr.points[2].top - panel.gr.scroll) > 60);                   /* its title */
}

static void test_tap_point_adds(void) {
    u32 y, y4;
    point_reply();
    y = show_point(1);
    tap(COLUMN_X, y + 8);
    CHECK(panel.gr.add_wanted == 1 && panel.gr.add_id == 12 && panel.dragging == 0);
    CHECK(panel.st.gr_add_state == JPR_ADD_ASKING && panel.st.gr_buttons == 0);   /* one add at a time */
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) == 0 && COLUMN_COUNT(JPR_COLOR_MUTED, y) > 30);   /* "Adding…" */
    y4 = show_point(4);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_BUTTON_OFF, y4) > 300);               /* greyed while 12 waits */
    tap(COLUMN_X, y4 + 8);
    CHECK(panel.gr.add_id == 12);
    tap(COLUMN_X, JPR_GR_TOP + 2 + panel.gr.points[3].top - panel.gr.scroll + 8);   /* a known point: no button */
    CHECK(panel.gr.add_id == 12);
}

static void test_point_answers_go_to_their_own_point(void) {
    u32 y, y4;
    point_reply();
    y = show_point(1);
    tap(COLUMN_X, y + 8);
    jpr_panel_point_added(&panel, 99, JPR_ADD_FAILED, 0);              /* another point's answer */
    CHECK(panel.gr.add_id == 12 && panel.gr.add_wanted == 1);
    jpr_panel_point_added(&panel, 12, JPR_ADD_DONE, JPR_KOKORO_NEW);
    CHECK(panel.gr.add_id == 0 && panel.gr.add_wanted == 0 && panel.st.gr_add_state == JPR_ADD_DONE);
    CHECK(panel.st.gr_buttons == 1);                                    /* 40's is back */
    y = show_point(1);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) == 0 && COLUMN_COUNT(JPR_COLOR_MUTED, y) > 30);   /* "Added" */
    y4 = show_point(4);
    tap(COLUMN_X, y4 + 8);
    CHECK(panel.gr.add_id == 40);
    jpr_panel_point_added(&panel, 40, JPR_ADD_QUEUED, JPR_KOKORO_QUEUED);
    CHECK(panel.st.gr_buttons == 0 && panel.st.gr_add_state == JPR_ADD_QUEUED);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y4) == 0 && COLUMN_COUNT(JPR_COLOR_MUTED, y4) > 30);  /* "Queued" */
    jpr_overrides_sent(panel.gr.over, panel.gr.over_count);            /* the flush delivered it */
    CHECK(panel.gr.over[1].entry == 40 && panel.gr.over[1].state == JPR_KOKORO_NEW);
}

static void test_point_failures(void) {
    u32 y;
    point_reply();
    y = show_point(1);
    tap(COLUMN_X, y + 8);
    jpr_panel_point_added(&panel, 12, JPR_ADD_FAILED, 0);
    CHECK(panel.st.gr_buttons == 2 && panel.st.gr_add_state == JPR_ADD_FAILED);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_ERROR, y) > 300 && COLUMN_COUNT(JPR_COLOR_ON_ACCENT, y) > 100);   /* "Retry": a red pill */
    tap(COLUMN_X, y + 8);
    CHECK(panel.gr.add_wanted == 1 && panel.gr.add_id == 12);
    jpr_panel_point_added(&panel, 12, JPR_ADD_REJECTED, 0);
    CHECK(panel.st.gr_buttons == 1);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_ERROR, y) > 30 && COLUMN_COUNT(JPR_COLOR_ON_ACCENT, y) == 0);   /* "Rejected": text only */
    tap(COLUMN_X, y + 8);
    CHECK(panel.gr.add_wanted == 0);
}

static void test_overrides_on_a_remembered_reply(void) {
    static char known[sizeof pointed];
    char *at;
    point_reply();
    tap(COLUMN_X, show_point(1) + 8);
    jpr_panel_point_added(&panel, 12, JPR_ADD_DONE, JPR_KOKORO_NEW);
    tap(JPR_BACK_X + 10, BAR_Y);
    panel.gr.answered_len = 0;                                          /* a new conversation: asked again */
    panel.gr.pending = 0;
    grammar_tap();
    CHECK(panel.gr.state == JPR_GR_ASKING && panel.st.gr_points == 0);
    point_reply_text(pointed, sizeof pointed - 1);                      /* replies.bin's copy: 12 still "none" */
    CHECK(panel.st.gr_points == 5 && panel.st.gr_buttons == 1);         /* 12 shows added; 40 keeps its button */
    memcpy(known, pointed, sizeof pointed);                             /* kokoro's own word wins over the session's */
    at = strstr(known, "12\tnone");
    memcpy(at + 3, "new\t", 4);
    memmove(at + 7, at + 8, strlen(at + 8) + 1);
    tap(JPR_BACK_X + 10, BAR_Y);
    panel.gr.answered_len = 0;
    panel.gr.pending = 0;
    grammar_tap();
    point_reply_text(known, (u32)strlen(known));
    CHECK(panel.gr.points[1].status == JPR_POINT_NEW && !panel.gr.points[1].slot && panel.st.gr_buttons == 1);
}

static void test_point_rows_keep_their_place(void) {
    u32 y, before, after;
    point_reply();
    y = show_point(2);                                                  /* the row below 12's block */
    draw();
    before = count_in(JPR_COLOR_UNDERLINE, GR_X, GR_X + JPR_GR_DOT, y + 7, y + 7 + JPR_GR_DOT);
    tap(COLUMN_X, show_point(1) + 8);
    jpr_panel_point_added(&panel, 12, JPR_ADD_FAILED, 0);
    y = show_point(2);
    draw();
    after = count_in(JPR_COLOR_UNDERLINE, GR_X, GR_X + JPR_GR_DOT, y + 7, y + 7 + JPR_GR_DOT);
    CHECK(before > 30 && after == before);                              /* the "-" point's dot where it was */
}

static void test_no_point_buttons_without_kokoro(void) {
    u32 y;
    point_reply();
    panel.st.kokoro_on = 0;
    y = show_point(1);
    CHECK(panel.st.gr_buttons == 0);
    draw();
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) == 0);
    tap(COLUMN_X, y + 8);
    CHECK(panel.gr.add_wanted == 0);
}

static void test_every_point_row_can_be_tapped(void) {
    static char many[JPR_REPLY_MAX];
    u32 n = 0, k, y;
    n += (u32)sprintf(many + n, "C \xE3\x81\x82\n");
    for (k = 0; k < 40; k++) n += (u32)sprintf(many + n, "G bunpro:%u\tnone\tp%u\tn\n", 500 + k, k);   /* past the old 24 */
    kokoro_words();
    grammar_tap();
    point_reply_text(many, n);
    CHECK(panel.st.gr_points == 40 && panel.st.gr_buttons == 40);
    CHECK(JPR_GR_POINTS >= JPR_ITEMS_MAX);                              /* a row for every item a reply can have */
    y = show_point(39);
    tap(COLUMN_X, y + 8);
    CHECK(panel.gr.add_id == 539);
}

static void test_a_long_pattern_stops_before_the_button(void) {
    static const char wide[] = "C a\nG bunpro:9\tnone\tWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\tnote\n";
    u32 y;
    kokoro_words();
    grammar_tap();
    point_reply_text(wide, sizeof wide - 1);
    y = show_point(0);
    draw();
    CHECK(count_in(JPR_COLOR_TEXT, JPR_CARD_RIGHT - JPR_GR_ADD_W - 4, JPR_CARD_RIGHT, y, y + JPR_GR_PITCH) == 0);
    CHECK(count_in(JPR_COLOR_TEXT, GR_X + 20, GR_X + 100, y, y + JPR_GR_PITCH) > 50);   /* the pattern, cut */
    CHECK(COLUMN_COUNT(JPR_COLOR_KNOWN, y) > 300);                     /* and the button whole */
}

static void test_signature_follows_points(void) {
    u32 before;
    point_reply();
    before = jpr_panel_signature(&panel);
    jpr_panel_point_added(&panel, 12, JPR_ADD_DONE, JPR_KOKORO_NEW);
    CHECK(jpr_panel_signature(&panel) != before);
}

static void test_voice_buttons(void) {
    words();
    panel.st.voice_available = 1;
    draw();
    CHECK(colour(JPR_VOICE_X + 4, JPR_BTN_Y + 4) == JPR_COLOR_VOICE);
    CHECK(colour(JPR_SLOW_X + 4, JPR_BTN_Y + 4) == JPR_COLOR_VOICE);
    tap(JPR_VOICE_X + 22, BAR_Y);
    CHECK(panel.voice_request == 1 && panel.st.voice_requests == 1);
    jpr_panel_voice_taken(&panel);
    CHECK(panel.voice_request == 0);
    tap(JPR_SLOW_X + 22, BAR_Y);
    CHECK(panel.voice_request == 2 && panel.st.voice_requests == 2);
    jpr_panel_voice_taken(&panel);
    panel.st.voice_available = 0;                         /* this message has no clip */
    draw();
    CHECK(colour(JPR_VOICE_X + 4, JPR_BTN_Y + 4) == JPR_COLOR_BUTTON_OFF);
    tap(JPR_VOICE_X + 22, BAR_Y);
    CHECK(panel.voice_request == 0 && panel.st.voice_requests == 2);
    CHECK(JPR_PREV_X + JPR_PAGE_W < JPR_COUNTER_X && JPR_COUNTER_X + JPR_COUNTER_W < JPR_NEXT_X &&
          JPR_NEXT_X + JPR_PAGE_W < JPR_VOICE_X && JPR_VOICE_X + JPR_VOICE_W < JPR_SLOW_X &&
          JPR_SLOW_X + JPR_VOICE_W < JPR_GRAMMAR_X && JPR_GRAMMAR_X + JPR_GRAMMAR_W <= JPR_SCREEN_W - 4);
}

/* The Sound view (stage 11): the speaker button opens it; Music, Effects and Voice each have - and + and a bar of ten
 * steps (a step can be tapped), and the duck has an On/Off button. Back closes it and asks for sound.txt. */
#define ROW_TAP(k) (JPR_MIX_ROW_Y(k) + (JPR_MIX_ROW_H >> 1))

static void test_sound_view(void) {
    struct jpr_mix_levels l;
    words();
    jpr_mix_defaults(&l);
    jpr_panel_mix_levels(&panel, &l);                    /* what sound.txt said */
    draw();
    CHECK(colour(JPR_SOUND_X + 2, JPR_BTN_Y + 2) == JPR_COLOR_BUTTON);
    CHECK(JPR_NEXT_X + JPR_PAGE_W < JPR_SOUND_X && JPR_SOUND_X + JPR_SOUND_W < JPR_VOICE_X);
    tap(JPR_SOUND_X + (JPR_SOUND_W >> 1), BAR_Y);
    CHECK(panel.mix.open && panel.st.mix_open == 1 && panel.st.mix_music == 6);
    tap(JPR_MIX_PLUS_X + 8, ROW_TAP(0));
    CHECK(panel.mix.levels.music == 7 && panel.st.mix_music == 7);
    tap(JPR_MIX_MINUS_X + 8, ROW_TAP(1));
    CHECK(panel.mix.levels.effects == 9 && panel.st.mix_effects == 9);
    tap(JPR_MIX_PLUS_X + 8, ROW_TAP(2));
    CHECK(panel.mix.levels.voice == 10);                 /* already at the top */
    tap(JPR_MIX_BAR_X + 2 * JPR_MIX_STEP + 3, ROW_TAP(2));
    CHECK(panel.mix.levels.voice == 3 && panel.st.mix_voice == 3);   /* the third step */
    tap(JPR_MIX_BAR_X + 1, ROW_TAP(1));
    CHECK(panel.mix.levels.effects == 1);
    tap(JPR_MIX_TOGGLE_X + 8, JPR_MIX_DUCK_Y + 8);
    CHECK(panel.mix.levels.duck == 0 && panel.st.mix_duck == 0);
    tap(GLYPH_X(0), LINE_Y(0));                          /* nothing under the view is selected */
    CHECK(panel.st.selected == 0);
    draw();
    CHECK(colour(JPR_MIX_BAR_X + 3, ROW_TAP(0)) == JPR_COLOR_ACCENT);                     /* step 1 of 7 lit */
    CHECK(colour(JPR_MIX_BAR_X + 6 * JPR_MIX_STEP + 3, ROW_TAP(0)) == JPR_COLOR_ACCENT);  /* step 7 */
    CHECK(colour(JPR_MIX_BAR_X + 7 * JPR_MIX_STEP + 3, ROW_TAP(0)) != JPR_COLOR_ACCENT);  /* step 8 is not */
    jpr_panel_message(&panel, 8, text, len, &font, units, unit_count, JPR_ANNOT_OK);
    CHECK(panel.mix.open && panel.mix.levels.music == 7);     /* the next line leaves it open */
    tap(JPR_BACK_X + 8, BAR_Y);
    CHECK(!panel.mix.open && panel.st.mix_open == 0 && panel.mix.save == 1);
    jpr_panel_mix_saved(&panel);
    CHECK(panel.mix.save == 0);
    tap(JPR_SOUND_X + (JPR_SOUND_W >> 1), BAR_Y);
    tap(JPR_BACK_X + 8, BAR_Y);
    CHECK(panel.mix.save == 0);                          /* nothing changed: nothing to write */
    tap(JPR_SOUND_X + (JPR_SOUND_W >> 1), BAR_Y);
    jpr_panel_mix_close(&panel);                         /* the dialog ended while it was open */
    CHECK(!panel.mix.open && panel.st.mix_open == 0);
}

/* The peek (stage 13): SELECT opens the panel over any screen; it opens on its own content, never on a view left open. */
static void test_peek_toggles(void) {
    words();
    CHECK(panel.peek == 0 && panel.st.peek_open == 0);
    tap(JPR_SOUND_X + (JPR_SOUND_W >> 1), BAR_Y);                /* the Sound view, left open */
    jpr_panel_peek(&panel, 1);
    CHECK(panel.peek == 1 && panel.st.peek_open == 1 && !panel.mix.open);
    grammar_tap();
    CHECK(panel.gr.open);
    jpr_panel_peek(&panel, 0);
    CHECK(panel.peek == 0 && panel.st.peek_open == 0 && !panel.gr.open);
}

/* The peek's rows view (stage 15). "あい" (the message: a unit, entry 5) / "あいあい" (two units, entries 6 and 7; slap ·
 * Pound, 30/30, a type pill) / "い" (a unit, entry 8; Back), then `extra` rows of "あ" to make it scroll. */
static struct jpr_peek_rows rows;

static void pool_set(struct jpr_peek_str *s, const char *t) {
    u16 u[64];
    u32 n = 0;
    while (t[n]) { u[n] = (u8)t[n]; n++; }
    jpr_peek_append(&rows, s, u, n, "");
}

static void peek_rows(u32 extra) {
    static const u16 type[] = {A};
    u32 k;
    len = 0;
    unit_count = 0;
    jpr_peek_rows_clear(&rows);
    rows.header = 1;
    put(A); put(I); unit(0, 2, 5, JPR_NO_LABEL);
    rows.row[rows.n].start = 0; rows.row[rows.n++].end = 2;
    nl(); put(A); put(I); put(A); put(I); unit(3, 2, 6, JPR_NO_LABEL); unit(5, 2, 7, JPR_NO_LABEL);
    rows.row[rows.n].start = 3; rows.row[rows.n].end = 7;
    pool_set(&rows.row[rows.n].literal, "slap");
    pool_set(&rows.row[rows.n].english, "Pound");
    pool_set(&rows.row[rows.n].meta, "30/30");
    jpr_peek_append(&rows, &rows.row[rows.n].pill, type, 1, "");
    rows.n++;
    nl(); put(I); unit(8, 1, 8, JPR_NO_LABEL);
    rows.row[rows.n].start = 8; rows.row[rows.n].end = 9;
    pool_set(&rows.row[rows.n].english, "Back");
    rows.n++;
    for (k = 0; k < extra; k++) {
        nl();
        rows.row[rows.n].start = (u16)len;
        unit(len, 1, 20 + k, JPR_NO_LABEL);
        put(A);
        rows.row[rows.n++].end = (u16)len;
    }
    jpr_panel_init(&panel);
    jpr_panel_rows(&panel, &rows);
    jpr_panel_message(&panel, 7, text, len, &font, units, unit_count, JPR_ANNOT_OK);
}

#define PEEK_Y(r) ((u32)panel.line_top[r] - panel.st.scroll + 10)   /* inside layout line r, as shown */

static void test_rows_are_laid_out_down_the_screen(void) {
    peek_rows(0);
    CHECK(panel.rows == &rows && panel.layout.line_count == 3);
    CHECK(panel.geo[0].top == JPR_TEXT_TOP && panel.line_top[0] == JPR_TEXT_TOP + JPR_PEEK_PAD);
    CHECK(panel.geo[0].height == 2 * JPR_PEEK_PAD + JPR_PEEK_LINE);              /* the message: no notes, no minimum */
    CHECK(panel.geo[1].top == panel.geo[0].top + panel.geo[0].height);
    CHECK(panel.line_top[1] == panel.geo[1].top + JPR_PEEK_PAD);
    CHECK(panel.geo[1].inline_meta == 1);                                        /* 30/30 fits beside あいあい */
    CHECK(panel.geo[1].note_top == panel.line_top[1] + JPR_PEEK_LINE);
    CHECK(panel.geo[1].height >= JPR_PEEK_ROW_MIN);                              /* slap · Pound on one line */
    CHECK(panel.geo[1].height == 2 * JPR_PEEK_PAD + JPR_PEEK_LINE + JPR_PEEK_NOTE);
    CHECK(panel.geo[2].top == panel.geo[1].top + panel.geo[1].height);
    CHECK(panel.rows_height == (u32)(panel.geo[2].top + panel.geo[2].height - JPR_TEXT_TOP));
    jpr_panel_rows(&panel, 0);                                                   /* another message: no rows */
    words();
    CHECK(panel.rows == 0);
}

static void test_rows_tap_opens_and_closes_the_card(void) {
    peek_rows(0);
    draw();
    CHECK(colour(10, JPR_PEEK_CARD_TOP + 3) != JPR_COLOR_CARD);                 /* no card until a word is tapped */
    tap(GLYPH_X(2), PEEK_Y(1));                                                   /* the second あい of row 1 */
    CHECK(panel.st.selected == 3 && panel.st.card_entry == 7 && panel.card_wanted);
    CHECK(jpr_panel_card_top(&panel) == JPR_PEEK_CARD_TOP);
    draw();
    CHECK(colour(4 + 8, JPR_PEEK_CARD_TOP) == JPR_COLOR_CARD_BORDER && colour(4 + 8, JPR_PEEK_CARD_TOP + 3) == JPR_COLOR_CARD);
    tap(GLYPH_X(2), PEEK_Y(1));                                                   /* the same word: the card closes */
    CHECK(panel.st.selected == 0);
    tap(GLYPH_X(0), PEEK_Y(0));
    CHECK(panel.st.selected == 1);
    tap(300, PEEK_Y(1));                                                          /* beside the words: closes too */
    CHECK(panel.st.selected == 0);
}

static void test_rows_scroll_with_the_bar_and_keep_a_tapped_word_in_view(void) {
    u32 limit;
    peek_rows(8);
    limit = panel.rows_height - (JPR_BAR_TOP - 1 - JPR_TEXT_TOP);
    CHECK(panel.rows_height > JPR_BAR_TOP);
    tap(PREV_X, BTN_Y);
    CHECK(panel.st.scroll == 0);
    tap(NEXT_X, BTN_Y);
    CHECK(panel.st.scroll == JPR_PEEK_SCROLL);
    tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y);
    CHECK(panel.st.scroll == limit);
    tap(PREV_X, BTN_Y);
    CHECK(panel.st.scroll == limit - JPR_PEEK_SCROLL);
    panel.st.scroll = 0;
    tap(GLYPH_X(0), PEEK_Y(5));                                                   /* below where the card will be */
    CHECK(panel.st.selected == 7);                                               /* units: 4 above, then one a row */
    CHECK((s32)panel.line_top[5] - (s32)panel.st.scroll + JPR_PEEK_LINE <= JPR_PEEK_CARD_TOP - JPR_CARD_GAP);
    CHECK(panel.line_top[5] - panel.st.scroll > JPR_TEXT_TOP);
    tap(GLYPH_X(0), PEEK_Y(5));                                                   /* closed: the scroll stays put */
    CHECK(panel.st.selected == 0);
}

static void test_rows_draw_their_notes(void) {
    u32 note, line;
    peek_rows(0);
    draw();
    note = panel.geo[1].note_top;
    line = panel.line_top[1];
    CHECK(count(JPR_COLOR_TEXT, note, note + JPR_PEEK_NOTE) > 20);              /* slap */
    CHECK(count(JPR_COLOR_MUTED, note, note + JPR_PEEK_NOTE) > 20);             /* · Pound */
    CHECK(count(JPR_COLOR_MUTED, line, line + JPR_PEEK_LINE) > 20);             /* 30/30, at the right */
    CHECK(count(JPR_COLOR_PILL, line, line + JPR_PEEK_LINE) > 20);              /* the type */
    CHECK(colour(JPR_TEXT_X + 3, line + JPR_PEEK_BASELINE + JPR_UNDERLINE) == JPR_COLOR_UNDERLINE);
    CHECK(colour(20, panel.geo[2].top) == JPR_COLOR_CARD_BORDER);               /* a rule between rows */
    CHECK(count(JPR_COLOR_MUTED, panel.geo[2].note_top, panel.geo[2].note_top + JPR_PEEK_NOTE) > 10);   /* Back */
}

/* Review fixes. */
static void test_rows_review_fixes(void) {
    u32 before;
    peek_rows(0);
    before = jpr_panel_signature(&panel);
    jpr_panel_message(&panel, 7, text, len, &font, units, unit_count, JPR_ANNOT_OK);   /* new rows, same layout */
    CHECK(jpr_panel_signature(&panel) != before);                                     /* drawn again: PP 30/30 → 29/30 */

    panel.status[0] = 'x';                                                            /* the debug line's strip */
    CHECK(jpr_panel_card_top(&panel) == JPR_PEEK_CARD_TOP);
    peek_rows(8);
    {
        u32 plain_limit = panel.rows_height - (JPR_BAR_TOP - 1 - JPR_TEXT_TOP);
        panel.status[0] = 'x';
        panel.st.scroll = 0;
        tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y);
        tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y);
        CHECK(panel.st.scroll == plain_limit + 16);                                   /* the rows end above the strip */
        panel.status[0] = 0;
    }

    peek_rows(8);                                                                     /* Sound closes the card: */
    tap(GLYPH_X(0), PEEK_Y(4));                                                       /* the scroll fits again */
    CHECK(panel.st.selected && panel.st.scroll > 0);
    tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y);
    tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y); tap(NEXT_X, BTN_Y);                       /* as far down as it goes */
    tap(JPR_SOUND_X + (JPR_SOUND_W >> 1), BAR_Y);
    jpr_panel_mix_close(&panel);
    {
        u32 limit = panel.rows_height - (JPR_BAR_TOP - 1 - JPR_TEXT_TOP);
        CHECK(!panel.st.selected && panel.st.scroll <= limit);
    }

    peek_rows(0);                                                                     /* Grammar: the tapped word's row */
    tap(GLYPH_X(0), PEEK_Y(1));
    grammar_tap();
    CHECK(panel.gr.open && panel.gr.ex.sentence_len == 12);                           /* あいあい: 4 kana, 3 bytes each */
    jpr_panel_peek(&panel, 0);
    peek_rows(0);
    grammar_tap();                                                                    /* nothing tapped: the message */
    CHECK(panel.gr.open && panel.gr.ex.sentence_len == 6);
}

static void test_rows_with_nothing_to_read(void) {
    len = 0;
    unit_count = 0;
    jpr_peek_rows_clear(&rows);
    pool_set(&rows.row[0].english, "Nothing to read here yet.");
    rows.n = 1;
    jpr_panel_init(&panel);
    jpr_panel_rows(&panel, &rows);
    jpr_panel_message(&panel, 7, text, len, &font, units, 0, JPR_ANNOT_OK);
    draw();
    CHECK(colour(JPR_GRAMMAR_X + 3, JPR_BTN_Y + 3) == JPR_COLOR_BUTTON_OFF);           /* Grammar: nothing to ask */
    grammar_tap();
    CHECK(!panel.gr.open);
}

int main(void) {
    test_voice_buttons();
    static struct test_glyph text_glyphs[] = {{A, 17, 15, 15, 1, -15}, {I, 17, 15, 15, 1, -15}};
    static struct test_glyph small_glyphs[96];
    const struct test_glyph *g[2] = {text_glyphs, small_glyphs};
    u32 counts[2] = {2, 0};
    const u32 sizes[2] = {17, 13}, ascents[2] = {15, 10};
    u32 cp;
    for (cp = 0x20; cp < 0x7F; cp++) {
        struct test_glyph *s = &small_glyphs[counts[1]++];
        s->cp = (u16)cp; s->advance = 7; s->w = cp == 0x20 ? 0 : 5; s->h = cp == 0x20 ? 0 : 9; s->x = 1; s->y = -9;
    }
    small_glyphs[counts[1]++] = (struct test_glyph){0x2192, 12, 10, 5, 1, -7};
    test_font(blob, 2, g, counts, sizes, ascents);
    jpr_font_init(&font);
    jpr_font_load(&font, blob, sizeof blob);

    test_pages_step();
    test_message_resets();
    test_box_moves_live_and_view_together();
    test_buttons_page_within_the_message();
    test_holding_does_not_repeat();
    test_drag_scrolls_long_blocks_only();
    test_tap_selects_the_unit_under_the_finger();
    test_a_drag_is_not_a_tap();
    test_taps_follow_the_scroll();
    test_selection_clears();
    test_card_reading();
    test_draw_frame();
    test_draw_underlines_and_selection();
    test_draw_card();
    test_long_card_scrolls_by_dragging();
    test_short_card_does_not_scroll();
    test_scrolled_card_shows_other_rows();
    test_text_area_fits_its_lines();
    test_card_follows_the_text();
    test_grammar_button_is_the_accent();
    test_badges_number_the_meanings();
    test_draw_reports_problems();
    test_grammar_opens_and_asks();
    test_same_sentence_reuses_the_reply();
    test_back_cancels_and_stale_results_are_ignored();
    test_errors_and_retry();
    test_reply_scrolls();
    test_tagged_reply_layout();
    test_untagged_reply_keeps_every_line();
    test_review_fixes();
    test_signature_follows_what_is_drawn();
    test_draw_grammar();
    test_status_line();
    test_cancelled_request_does_not_leave_a_blank_reply();
    test_a_press_on_the_panel_stays_hidden_until_it_lifts();
    test_marks_follow_kokoro();
    test_overrides_fill_in_only();
    test_underlines_take_kokoro_colours();
    test_kokoro_row_and_add_button();
    test_answers_go_to_their_own_word();
    test_known_word_has_no_button();
    test_add_failures();
    test_queued_word();
    test_a_delivered_add_is_up_next();
    test_kokoro_row_texts();
    test_no_row_for_particles();
    test_signature_follows_kokoro();
    test_an_add_survives_moving_on();
    test_points_are_rows();
    test_draw_point_rows();
    test_tap_point_adds();
    test_point_answers_go_to_their_own_point();
    test_point_failures();
    test_overrides_on_a_remembered_reply();
    test_point_rows_keep_their_place();
    test_no_point_buttons_without_kokoro();
    test_signature_follows_points();
    test_every_point_row_can_be_tapped();
    test_a_long_pattern_stops_before_the_button();
    test_sound_view();
    test_peek_toggles();
    test_rows_are_laid_out_down_the_screen();
    test_rows_tap_opens_and_closes_the_card();
    test_rows_scroll_with_the_bar_and_keep_a_tapped_word_in_view();
    test_rows_draw_their_notes();
    test_rows_review_fixes();
    test_rows_with_nothing_to_read();
    if (failures) {
        printf("panel: %d check(s) failed\n", failures);
        return 1;
    }
    printf("panel: all checks passed\n");
    return 0;
}
