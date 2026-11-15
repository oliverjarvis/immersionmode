#include <stdio.h>
#include <string.h>

#include "core/screen.h"
#include "core/util.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const u16 HATAKU[] = {0x306F, 0x305F, 0x304F};     /* はたく */
static const u16 MSG[] = {0x3069, 0x3046, 0x3059, 0x308B, 0xFF1F};   /* どうする？ */

static struct jpr_ident ident(s32 arc, u32 dat, u32 line) {
    struct jpr_ident id = {arc, dat, line, 0, 0, 0};
    return id;
}

static void test_the_latest_window_message_is_kept(void) {
    struct jpr_screen s;
    struct jpr_ident id = ident(30, 17, 78);
    u32 v;
    jpr_screen_init(&s);
    CHECK(s.msg_seq == 0);
    v = s.version;
    jpr_screen_message(&s, &id, MSG, 5, 12);
    CHECK(s.msg_seq == 1 && s.msg_after == 12 && s.msg_len == 5 && !memcmp(s.msg, MSG, 10));
    CHECK(s.msg_id.dat == 17 && s.msg_id.line == 78 && s.version != v);
}

static void test_compose(void) {
    static const u16 EN[] = {'P', 'o', 'u', 'n', 'd'};
    struct jpr_piece p[3];
    u16 out[16];
    u32 n;
    memset(p, 0, sizeof p);
    p[0].text = MSG; p[0].len = 5; p[0].id = ident(30, 17, 78);
    p[1].text = HATAKU; p[1].len = 3; p[1].line = 1; p[1].id = ident(30, 118, 1);
    p[2].text = EN; p[2].len = 5; p[2].id = ident(JPR_ARC_UNKNOWN, JPR_NONE, JPR_NONE);
    n = jpr_compose(p, 3, out, 16);
    CHECK(n == 5 + 1 + 3 + 5 && p[0].start == 0 && out[5] == 0x000A && p[1].start == 6 && p[2].start == 9);
    CHECK(!memcmp(out + 6, HATAKU, 6) && out[9] == 'P');
    n = jpr_compose(p, 3, out, 10);                          /* the English does not fit */
    CHECK(n == 9 && p[2].start == JPR_PIECE_NONE);
}

static void test_the_layouts_drawn_last_frame(void) {
    static struct jpr_drawn d;
    int wk[3];
    const struct jpr_drawn_frame *f;
    jpr_memset(&d, 0, sizeof d);
    jpr_drawn_add(&d, &wk[0], 0);
    jpr_drawn_add(&d, &wk[1], 1);
    jpr_drawn_add(&d, &wk[1], 1);                 /* drawn twice in a frame: listed once */
    jpr_drawn_add(&d, &wk[2], 1);
    f = jpr_drawn_flip(&d);
    CHECK(f->n == 3 && f->wk[0] == &wk[0] && f->display[0] == 0 && f->wk[1] == &wk[1] && f->display[1] == 1);
    jpr_drawn_add(&d, &wk[2], 1);                 /* the next frame starts empty */
    f = jpr_drawn_flip(&d);
    CHECK(f->n == 1 && f->wk[0] == &wk[2]);
    f = jpr_drawn_flip(&d);
    CHECK(f->n == 0);
}

static void test_a_full_frame_keeps_the_first(void) {
    static struct jpr_drawn d;
    static int wk[JPR_DRAWN + 4];
    u32 k;
    const struct jpr_drawn_frame *f;
    jpr_memset(&d, 0, sizeof d);
    for (k = 0; k < JPR_DRAWN + 4; k++) jpr_drawn_add(&d, &wk[k], 1);
    f = jpr_drawn_flip(&d);
    CHECK(f->n == JPR_DRAWN && f->wk[JPR_DRAWN - 1] == &wk[JPR_DRAWN - 1]);
}

static struct jpr_seen_text seen(u16 group, u32 hash) {
    struct jpr_seen_text t;
    t.text = HATAKU;
    t.len = 3;
    t.group = group;
    t.hash = hash;
    return t;
}

static void test_the_texts_kept_and_their_lines(void) {
    struct jpr_seen_text t[8];
    u16 keep[8];
    u8 starts[8];
    u32 n;
    t[0] = seen(1, 0x11);          /* a button: name, "PP", "35" */
    t[1] = seen(1, 0x12);
    t[2] = seen(1, 0x13);
    t[3] = seen(2, 0);             /* nothing to read: no key */
    t[4] = seen(2, 0x21);
    t[5] = seen(2, 0x21);          /* the same text twice in a group */
    t[6] = seen(3, 0x99);          /* the message again */
    t[7] = seen(4, 0x31);
    n = jpr_screen_pick(t, 8, 0x99, keep, starts, 8);
    CHECK(n == 5);
    CHECK(keep[0] == 0 && keep[1] == 1 && keep[2] == 2 && keep[3] == 4 && keep[4] == 7);
    CHECK(starts[0] == 1 && starts[1] == 0 && starts[2] == 0 && starts[3] == 1 && starts[4] == 1);
}

static void test_a_group_shown_twice_is_listed_once(void) {
    struct jpr_seen_text t[4];
    u16 keep[4];
    u8 starts[4];
    t[0] = seen(1, 0x11);
    t[1] = seen(1, 0x12);
    t[2] = seen(5, 0x11);          /* the same button drawn again (another layout, the same labels) */
    t[3] = seen(5, 0x12);
    CHECK(jpr_screen_pick(t, 4, 0, keep, starts, 4) == 2);
}

static void test_the_pick_stops_at_max(void) {
    struct jpr_seen_text t[3];
    u16 keep[2];
    u8 starts[2];
    t[0] = seen(1, 0x1);
    t[1] = seen(2, 0x2);
    t[2] = seen(3, 0x3);
    CHECK(jpr_screen_pick(t, 3, 0, keep, starts, 2) == 2 && keep[1] == 1);
}

static int pool_is(const struct jpr_peek_rows *r, struct jpr_peek_str s, const char *want) {
    return s.len == strlen(want) && !memcmp(r->pool + s.at, want, s.len);
}

/* Stage 15: a row's strings go to the pool as UTF-8, each appended to while it is the pool's last. */
static void test_the_rows_pool(void) {
    static struct jpr_peek_rows r;
    static const u16 slap[] = {'s', 'l', 'a', 'p'}, poke[] = {'P', 'o', 'k', 0xE9}, cry[] = {'c', 'r', 'y'};
    struct jpr_peek_row *row;
    jpr_peek_rows_clear(&r);
    CHECK(r.n == 0 && r.used == 0);
    row = &r.row[r.n++];
    CHECK(jpr_peek_append(&r, &row->literal, slap, 4, "; ") == 0);
    CHECK(jpr_peek_append(&r, &row->literal, cry, 3, "; ") == 0);       /* a second text in the group */
    CHECK(pool_is(&r, row->literal, "slap; cry"));
    CHECK(jpr_peek_append(&r, &row->english, poke, 4, "; ") == 0);
    CHECK(pool_is(&r, row->english, "Pok\xC3\xA9"));                    /* é: two bytes */
    CHECK(jpr_peek_append(&r, &row->literal, cry, 3, "; ") == -1);      /* no longer the pool's last */
    CHECK(pool_is(&r, row->literal, "slap; cry"));
    r.used = JPR_PEEK_POOL - 3;
    CHECK(jpr_peek_append(&r, &row->meta, slap, 4, " ") == -1 && row->meta.len == 0);   /* full: nothing half-written */
    CHECK(r.used == JPR_PEEK_POOL - 3);
}

static void test_japanese_or_meta(void) {
    static const u16 pp[] = {'P', 'P'}, frac[] = {0xFF13, 0xFF10, '/', '3', '0'}, times[] = {0xD7, '3'},
                     kanji[] = {0x96FB, 0x6C17}, kata[] = {0x30D4, 0x30FC};
    CHECK(!jpr_screen_japanese(pp, 2) && !jpr_screen_japanese(frac, 5) && !jpr_screen_japanese(times, 2));
    CHECK(jpr_screen_japanese(HATAKU, 3) && jpr_screen_japanese(kanji, 2) && jpr_screen_japanese(kata, 2));
    CHECK(jpr_screen_japanese(MSG, 5));
}

/* Review fixes: tags and line breaks never reach a row's strings or decide what is Japanese; a cut never splits a tag. */
static void test_plain_text_and_safe_cuts(void) {
    static const u16 tagged[] = {'3', 0x0010, 2, 0x3042, 0x3044, '0', 0x000A, '/', '3'};   /* a tag whose parameters are kana */
    u16 out[16];
    u32 n = jpr_plain_text(tagged, 9, out, 16);
    CHECK(n == 5 && out[0] == '3' && out[1] == '0' && out[2] == ' ' && out[3] == '/' && out[4] == '3');
    CHECK(jpr_plain_text(tagged, 9, out, 2) == 2);
    CHECK(!jpr_screen_japanese(tagged, 9));
    CHECK(jpr_text_cut(tagged, 9, 9) == 9);
    CHECK(jpr_text_cut(tagged, 9, 3) == 1 && jpr_text_cut(tagged, 9, 4) == 1);       /* not inside the tag */
    CHECK(jpr_text_cut(tagged, 9, 5) == 5 && jpr_text_cut(tagged, 9, 1) == 1);
    {
        static const u16 short_tag[] = {0x3042, 0x0010};                                 /* already cut: the tag goes */
        CHECK(jpr_text_cut(short_tag, 2, 8) == 1);
    }
}

int main(void) {
    test_plain_text_and_safe_cuts();
    test_the_rows_pool();
    test_japanese_or_meta();
    test_the_texts_kept_and_their_lines();
    test_a_group_shown_twice_is_listed_once();
    test_the_pick_stops_at_max();
    test_the_layouts_drawn_last_frame();
    test_a_full_frame_keeps_the_first();
    test_the_latest_window_message_is_kept();
    test_compose();
    if (failures) {
        printf("screen: %d check(s) failed\n", failures);
        return 1;
    }
    printf("screen: all checks passed\n");
    return 0;
}
