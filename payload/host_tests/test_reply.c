#include <stdio.h>
#include <string.h>

#include "core/grammar.h"
#include "core/reply.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static struct jpr_item items[JPR_ITEMS_MAX];

static u32 parse(const char *s) {
    return jpr_reply_items((const u8 *)s, (u32)strlen(s), items, JPR_ITEMS_MAX);
}

static int is(const char *reply, u32 k, u32 kind, u32 block, const char *text) {
    return items[k].kind == kind && items[k].block == block && items[k].len == strlen(text) &&
           !memcmp(reply + items[k].start, text, items[k].len);
}

static void test_tagged_reply(void) {
    const char *r = "T \"Setup is complete.\"\nC せっていが\nM settings + が\nN が marks the subject\n"
                    "C しゅうりょう\nM completion\nN a noun used as a verb with する";
    CHECK(parse(r) == 7);
    CHECK(is(r, 0, JPR_ITEM_TRANSLATION, 0, "\"Setup is complete.\""));
    CHECK(is(r, 1, JPR_ITEM_CHUNK, 1, "せっていが"));
    CHECK(is(r, 2, JPR_ITEM_MEANING, 1, "settings + が"));
    CHECK(is(r, 3, JPR_ITEM_NOTE, 1, "が marks the subject"));
    CHECK(is(r, 4, JPR_ITEM_CHUNK, 2, "しゅうりょう"));
    CHECK(is(r, 6, JPR_ITEM_NOTE, 2, "a noun used as a verb with する"));
}

static void test_untagged_lines_are_kept(void) {
    const char *r = "\"Setup is complete.\"\n\nせっていが: settings + が\nしました: polite past";
    CHECK(parse(r) == 3);                                    /* the empty line is skipped */
    CHECK(is(r, 0, JPR_ITEM_PLAIN, 0, "\"Setup is complete.\""));
    CHECK(is(r, 2, JPR_ITEM_PLAIN, 0, "しました: polite past"));
}

static void test_odd_tags(void) {
    const char *r = "C あ\nT \"late\"\nX not a tag\nM a\r\nTea";
    CHECK(parse(r) == 5);
    CHECK(is(r, 1, JPR_ITEM_PLAIN, 1, "\"late\""));            /* a translation that is not first */
    CHECK(is(r, 2, JPR_ITEM_PLAIN, 1, "X not a tag"));
    CHECK(is(r, 3, JPR_ITEM_MEANING, 1, "a"));                 /* CR trimmed */
    CHECK(is(r, 4, JPR_ITEM_PLAIN, 1, "Tea"));                 /* no space after the letter */
}

static void test_cap(void) {
    static char r[4096];
    struct jpr_item few[3];
    u32 k, at = 0;
    for (k = 0; k < 80; k++) at += (u32)sprintf(r + at, "C %u\n", k);
    CHECK(jpr_reply_items((const u8 *)r, at, few, 3) == 3);
    CHECK(parse(r) == JPR_ITEMS_MAX);
    CHECK(jpr_reply_items((const u8 *)"", 0, items, JPR_ITEMS_MAX) == 0);
}

static void test_empty_items_are_skipped(void) {
    const char *r = "C \nM alone\nC\r\nN x";
    CHECK(parse(r) == 3);
    CHECK(is(r, 0, JPR_ITEM_MEANING, 1, "alone"));      /* the empty chunk still starts block 1 */
    CHECK(is(r, 1, JPR_ITEM_PLAIN, 1, "C"));             /* no space: not a tag */
    CHECK(is(r, 2, JPR_ITEM_NOTE, 1, "x"));
    CHECK(JPR_ITEMS_MAX >= 64);                          /* room for a reply with many chunks */
}

/* "待って" and "ちょっと", for the tests below. */
#define MATTE "\xE5\xBE\x85\xE3\x81\xA3\xE3\x81\xA6"
#define CHOTTO "\xE3\x81\xA1\xE3\x82\x87\xE3\x81\xA3\xE3\x81\xA8"
#define TE "\xE3\x81\xA6"                       /* て */
#define WAVE "\xE3\x80\x9C"                     /* 〜 */
#define KUDASAI "\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84"

static void test_grammar_points_are_items_of_their_chunk(void) {
    static const char r[] = "T \"Wait.\"\nC " CHOTTO "\nM a little\nN adverb\nG -\tnone\t" CHOTTO "\tsoftens it\n"
                            "C " MATTE "\nM wait\nN request\nG bunpro:1082\tnew\t" WAVE TE "\tlinks on\n";
    u32 n = parse(r);
    struct jpr_point pt;
    CHECK(n == 9);
    CHECK(items[4].kind == JPR_ITEM_GRAMMAR && items[4].block == 1);
    CHECK(items[8].kind == JPR_ITEM_GRAMMAR && items[8].block == 2);       /* fits 待って: under it */
    jpr_reply_point((const u8 *)r, &items[4], &pt);
    CHECK(pt.id == 0 && pt.status == JPR_POINT_NONE);                     /* "-": no Bunpro match */
    CHECK(pt.pattern_len == 12 && !memcmp(r + pt.pattern, CHOTTO, 12));
    CHECK(pt.note_len == 10 && !memcmp(r + pt.note, "softens it", 10));
    jpr_reply_point((const u8 *)r, &items[8], &pt);
    CHECK(pt.id == 1082 && pt.status == JPR_POINT_NEW);
    CHECK(pt.note_len == 8 && !memcmp(r + pt.note, "links on", 8));
}

static void test_point_fields(void) {
    static const char r[] = "C a\nG bunpro:7\tknown\tx\t\nG bunpro:12\tlearning\ty\tz\nG bunpro:x1\tweird\tp\tq\n"
                            "G bunpro:5\tnone\nG bunpro:4294967296\tnone\tw\tv\nG bunpro:0\tnone\tw\tv\n";
    struct jpr_point pt;
    CHECK(parse(r) == 7);
    jpr_reply_point((const u8 *)r, &items[1], &pt);
    CHECK(pt.id == 7 && pt.status == JPR_POINT_KNOWN && pt.pattern_len == 1 && pt.note_len == 0);   /* empty note */
    jpr_reply_point((const u8 *)r, &items[2], &pt);
    CHECK(pt.id == 12 && pt.status == JPR_POINT_LEARNING && r[pt.note] == 'z' && pt.note_len == 1);
    jpr_reply_point((const u8 *)r, &items[3], &pt);
    CHECK(pt.id == 0 && pt.status == JPR_POINT_NONE && r[pt.pattern] == 'p');   /* not a Bunpro id; unknown status */
    jpr_reply_point((const u8 *)r, &items[4], &pt);
    CHECK(pt.id == 5 && pt.pattern_len == 0 && pt.note_len == 0);               /* a short line: empty texts */
    jpr_reply_point((const u8 *)r, &items[5], &pt);
    CHECK(pt.id == 0);                                                          /* too big for an id */
    jpr_reply_point((const u8 *)r, &items[6], &pt);
    CHECK(pt.id == 0);
}

static int block_of(const char *r, u32 k) {
    parse(r);
    return items[k].block;
}

static void test_points_after_the_last_chunk(void) {
    /* kokoro writes the last chunk's own points, then those that matched no chunk: told apart by its placement test */
    static const char own[] = "C " MATTE "\nG bunpro:1\tnone\t" WAVE TE "\tx\n";            /* て ends the chunk */
    static const char tail[] = "C " MATTE "\nG bunpro:1\tnone\t" WAVE TE "\tx\nG bunpro:2\tnone\t" KUDASAI "\ty\n"
                               "G bunpro:3\tnone\t" WAVE TE "\tz\n";
    static const char one[] = "C " MATTE "\nG bunpro:1\tnone\t" "\xE5\xBE\x85" "\tx\n";           /* 待: one character, not last */
    static const char inside[] = "C " MATTE "\nG bunpro:1\tnone\t" "\xE5\xBE\x85\xE3\x81\xA3" "\tx\n";  /* 待っ: inside */
    static const char alt[] = "C " MATTE "\nG bunpro:1\tnone\tA\xE3\x83\xBB" TE "\tx\n";          /* A・て: an alternative fits */
    static const char note[] = "C " MATTE "\nG bunpro:1\tnone\t" TE "\xEF\xBC\x88" KUDASAI "\xEF\xBC\x89\tx\n"; /* て（ください） */
    static const char paren[] = "C " MATTE "\nG bunpro:1\tnone\tq(" TE ")\tx\n";                 /* q(て): only q is left */
    static const char none[] = "T \"Hi.\"\nG bunpro:1\tnone\tx\ty\n";
    CHECK(block_of(own, 1) == 1);
    CHECK(block_of(tail, 1) == 1 && block_of(tail, 2) == JPR_BLOCK_OTHER);
    CHECK(block_of(tail, 3) == JPR_BLOCK_OTHER);          /* after an other one, the rest are others too */
    CHECK(block_of(one, 1) == JPR_BLOCK_OTHER);
    CHECK(block_of(inside, 1) == 1);
    CHECK(block_of(alt, 1) == 1);
    CHECK(block_of(note, 1) == 1);
    CHECK(block_of(paren, 1) == JPR_BLOCK_OTHER);
    CHECK(block_of(none, 1) == JPR_BLOCK_OTHER);          /* no chunks at all */
    CHECK(items[1].kind == JPR_ITEM_GRAMMAR);
}

static void test_point_body(void) {
    u8 out[32];
    CHECK(jpr_point_body(out, sizeof out, 1082) == 16 && !memcmp(out, "key=bunpro:1082\n", 16));
    CHECK(jpr_point_body(out, 15, 1082) == 0);
    CHECK(jpr_point_body(out, sizeof out, 0) == 0);
}

static void test_kept_reply_is_whole_lines_with_grammar_points(void) {
    static u8 in[6000], out[4096];
    u32 n = 0, k, kept;
    const char *head = "T \"Hi.\"\nG bunpro:1\tnew\tx\ty\nC a\n";
    for (k = 0; head[k]; k++) in[n++] = (u8)head[k];
    while (n < 5900) {                                  /* a long reply: M lines of 2-byte characters */
        in[n++] = 'M'; in[n++] = ' ';
        for (k = 0; k < 40; k++) { in[n++] = 0xC3; in[n++] = 0xA9; }
        in[n++] = '\n';
    }
    kept = jpr_reply_keep(out, sizeof out, in, n);
    CHECK(kept > 0 && kept <= sizeof out);
    CHECK(out[kept - 1] == '\n');                       /* ends on a whole line */
    CHECK(!memcmp(out, head, strlen(head)));            /* the G line stays (stage 17) */
    CHECK(jpr_reply_keep(out, sizeof out, (const u8 *)"C x\nM y", 7) == 7);   /* a last line without \n stays */
}

/* A long sentence's reply as kokoro writes it: 15 chunks, each with a point and a 150-byte note. */
static u32 long_reply(u8 *out, u32 points) {
    u32 n = 0, k, j;
    n += (u32)sprintf((char *)out + n, "T \"A long line, translated in a fairly long sentence of English to be safe.\"\n");
    for (k = 0; k < 15; k++) {
        n += (u32)sprintf((char *)out + n, "C \xE3\x81\x82\xE3\x81\x84\xE3\x81\x86%u\nM a gloss of some length, as kokoro writes them\nN a role, often with a note in it too\n", k);
        if (k >= points) continue;
        n += (u32)sprintf((char *)out + n, "G bunpro:%u\tnone\t\xE3\x81\x86%u\t", 100 + k, k);
        for (j = 0; j < 150; j++) out[n++] = (u8)('a' + j % 26);
        out[n++] = '\n';
    }
    return n;
}

static void test_a_long_reply_fits_and_a_longer_one_is_cut(void) {
    static u8 in[12000], out[JPR_REPLY_MAX];
    u32 n = long_reply(in, 15), kept;
    CHECK(n > 4096 && n <= JPR_REPLY_MAX);                     /* more than the old 4 KB: kept whole */
    CHECK(jpr_reply_keep(out, JPR_REPLY_MAX, in, n) == n);
    n += long_reply(in + n, 15);                                /* twice as long: cut at the last whole line */
    kept = jpr_reply_keep(out, JPR_REPLY_MAX, in, n);
    CHECK(kept < n && kept <= JPR_REPLY_MAX && kept > JPR_REPLY_MAX - 200 && out[kept - 1] == '\n');
    CHECK(!memcmp(out, in, kept));                              /* the start, unchanged: the last lines are what goes */
}

int main(void) {
    test_tagged_reply();
    test_untagged_lines_are_kept();
    test_odd_tags();
    test_cap();
    test_empty_items_are_skipped();
    test_grammar_points_are_items_of_their_chunk();
    test_point_fields();
    test_points_after_the_last_chunk();
    test_point_body();
    test_kept_reply_is_whole_lines_with_grammar_points();
    test_a_long_reply_fits_and_a_longer_one_is_cut();
    if (failures) {
        printf("reply: %d check(s) failed\n", failures);
        return 1;
    }
    printf("reply: all checks passed\n");
    return 0;
}
