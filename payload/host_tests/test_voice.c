#include <stdio.h>
#include <string.h>

#include "core/voice.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 head[20 + 24 * 4];
static u32 reads;

static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

/* voice.bin's index with four clips: story (27,0) (27,1) in part 0, story (31,4) and system (38,1) in part 1:
 * normal 20,000 samples, slow 28,571. The platform reads entries one at a time from the SD card; here they come
 * from memory. */
static void make_head(void) {
    static const u32 keys[4][3] = {{27, 0, 0}, {27, 1, 0}, {31, 4, 1}, {JPR_VOICE_SYSTEM | 38, 1, 1}};
    u32 k, at = 0;
    memcpy(head, "JPRV", 4);
    put32(head + 4, 2);
    put32(head + 8, 4);
    put32(head + 12, 22050);
    put32(head + 16, 2);
    for (k = 0; k < 4; k++) {
        u8 *e = head + 20 + 24 * k;
        if (keys[k][2] == 1 && keys[k - 1][2] == 0) at = 0;
        put16(e, keys[k][0]);
        put16(e + 2, keys[k][1]);
        put16(e + 4, keys[k][2]);
        put16(e + 6, 0);
        put32(e + 8, at);
        put32(e + 12, 20000);
        at += 40000;
        put32(e + 16, at);
        put32(e + 20, 28571);
        at += 57152;
    }
}

static int read_entry(void *ctx, u32 k, u8 *entry) {
    (void)ctx;
    reads++;
    memcpy(entry, head + 20 + 24 * k, 24);
    return 0;
}

static int read_fails(void *ctx, u32 k, u8 *entry) {
    (void)ctx;
    (void)k;
    (void)entry;
    return 1;
}

static void test_index(void) {
    struct jpr_voice_index ix;
    struct jpr_voice_clip c;
    make_head();
    CHECK(jpr_voice_index_parse(&ix, head, 20, read_entry, 0) == 0 && ix.count == 4 && ix.parts == 2);
    reads = 0;
    CHECK(jpr_voice_find(&ix, 27, 1, &c) == 1 && c.part == 0 && c.at[0] == 40000 + 57152 && c.samples[1] == 28571);
    CHECK(reads <= 3);                                             /* a binary search, not the whole index */
    CHECK(jpr_voice_find(&ix, 31, 4, &c) == 2 && c.part == 1 && c.at[0] == 0);
    CHECK(jpr_voice_find(&ix, 27, 2, &c) == -1 && jpr_voice_find(&ix, 99, 0, &c) == -1);
    CHECK(jpr_voice_find(&ix, JPR_VOICE_SYSTEM | 38, 1, &c) == 3 && c.part == 1 && c.at[0] == 40000 + 57152);
    CHECK(jpr_voice_find(&ix, 38, 1, &c) == -1);                   /* story file 38 is another file */
    ix.read = read_fails;
    CHECK(jpr_voice_find(&ix, 27, 1, &c) == -1);                   /* the SD card failed: no clip */
    head[0] = 'X';
    CHECK(jpr_voice_index_parse(&ix, head, 20, read_entry, 0) == JPR_VOICE_BAD);
    make_head();
    put32(head + 4, 1);                                            /* the old single-file voice.bin */
    CHECK(jpr_voice_index_parse(&ix, head, 20, read_entry, 0) == JPR_VOICE_BAD);
    make_head();
    put32(head + 16, 0);                                           /* no parts */
    CHECK(jpr_voice_index_parse(&ix, head, 20, read_entry, 0) == JPR_VOICE_BAD);
    make_head();
    CHECK(jpr_voice_index_parse(&ix, head, 12, read_entry, 0) == JPR_VOICE_BAD);
    put32(head + 8, 14196);                                        /* the whole story: no limit any more */
    CHECK(jpr_voice_index_parse(&ix, head, 20, read_entry, 0) == 0 && ix.count == 14196);
}

static struct jpr_voice_index ix;
static struct jpr_voice_player p;

static void fresh(void) {
    make_head();
    jpr_voice_index_parse(&ix, head, 20, read_entry, 0);
    jpr_voice_init(&p);
}

static void test_autoplay_rules(void) {
    fresh();
    CHECK(jpr_voice_message(&p, &ix, 1, 41, 27, 0, 1) == JPR_VOICE_START && p.clip == 0 && p.speed == 0);
    CHECK(p.message_clip == 0 && p.next == 0 && p.total == 20000);
    CHECK(jpr_voice_message(&p, &ix, 1, 41, 27, 0, 1) == JPR_VOICE_NONE);       /* the same message */
    CHECK(jpr_voice_message(&p, &ix, 2, 41, 27, 0, 1) == JPR_VOICE_NONE);       /* a later box of the same line */
    CHECK(jpr_voice_message(&p, &ix, 3, 30, 38, 2, 1) == JPR_VOICE_NONE);       /* a system line with no clip */
    CHECK(p.clip == 0 && p.message_clip == -1);                                   /* the playing clip carries on */
    CHECK(jpr_voice_message(&p, &ix, 33, 32, 27, 0, 1) == JPR_VOICE_NONE);      /* English text: never */
    CHECK(jpr_voice_message(&p, &ix, 4, 40, 27, 1, 1) == JPR_VOICE_START && p.clip == 1);   /* kana script too */
    CHECK(jpr_voice_message(&p, &ix, 5, 41, 31, 4, 0) == JPR_VOICE_NONE && p.message_clip == 2);  /* voice=0 */
    CHECK(jpr_voice_replay(&p, 1) == JPR_VOICE_START && p.clip == 2 && p.speed == 1 && p.total == 28571);
    CHECK(jpr_voice_replay(&p, 0) == JPR_VOICE_START && p.speed == 0 && p.next == 0);
    jpr_voice_message(&p, &ix, 6, 30, 38, 2, 1);
    CHECK(jpr_voice_replay(&p, 0) == JPR_VOICE_NONE);                             /* no clip for this message */
}

/* System text has its own file numbers: system file 38 (the opening video call) is not story file 38. Its clips
 * are filed under 38 + JPR_VOICE_SYSTEM, in either script. */
static void test_a_system_line_plays_its_own_clip(void) {
    fresh();
    CHECK(jpr_voice_file(41, 38) == 38 && jpr_voice_file(40, 38) == 38);
    CHECK(jpr_voice_file(31, 38) == (s32)(JPR_VOICE_SYSTEM | 38) && jpr_voice_file(30, 38) == (s32)(JPR_VOICE_SYSTEM | 38));
    CHECK(jpr_voice_file(-1, 0xFFFFFFFFu) == -1 && jpr_voice_file(32, 38) == -1 && jpr_voice_file(41, 0x8000) == -1);
    CHECK(jpr_voice_message(&p, &ix, 1, 31, 38, 1, 1) == JPR_VOICE_START && p.clip == 3 && p.message_clip == 3);
    CHECK(jpr_voice_message(&p, &ix, 2, 30, 38, 1, 1) == JPR_VOICE_START && p.clip == 3);    /* kana script too */
    CHECK(jpr_voice_replay(&p, 1) == JPR_VOICE_START && p.speed == 1 && p.part == 1);          /* 0.7x works */
    CHECK(jpr_voice_message(&p, &ix, 3, 41, 38, 1, 1) == JPR_VOICE_NONE && p.message_clip == -1);  /* story 38 */
}

static void test_refill_keeps_order_and_never_touches_a_queued_buffer(void) {
    u8 status[JPR_VOICE_SLOTS] = {0, 0, 0, 0};
    struct jpr_voice_fill fills[JPR_VOICE_SLOTS];
    u32 n;
    fresh();
    jpr_voice_message(&p, &ix, 1, 41, 27, 0, 1);
    jpr_voice_replay(&p, 1);                                     /* slow: 28,571 samples, more than the ring */
    n = jpr_voice_refill(&p, status, fills);
    CHECK(n == 4 && fills[0].slot == 0 && fills[3].slot == 3);
    CHECK(fills[0].offset == 40000 && fills[0].part == 0 && fills[0].samples == JPR_VOICE_CHUNK);
    CHECK(fills[1].offset == 40000 + 2 * JPR_VOICE_CHUNK && p.next == 4 * JPR_VOICE_CHUNK);
    status[0] = status[1] = status[2] = status[3] = JPR_VOICE_QUEUED;
    CHECK(jpr_voice_refill(&p, status, fills) == 0);            /* all queued: nothing to do */
    status[1] = JPR_VOICE_DONE;                                  /* slot 1 done, but slot 0 is next in order */
    CHECK(jpr_voice_refill(&p, status, fills) == 0);
    status[0] = JPR_VOICE_DONE;
    n = jpr_voice_refill(&p, status, fills);
    CHECK(n == 2 && fills[0].slot == 0 && fills[0].samples == JPR_VOICE_CHUNK);   /* slot 1 was done too */
    CHECK(fills[1].slot == 1 && fills[1].samples == 28571 - 5 * JPR_VOICE_CHUNK && p.next == 28571);
    status[0] = status[1] = JPR_VOICE_QUEUED;
    CHECK(jpr_voice_finished(&p, status) == 0);                  /* buffers still queued */
    status[0] = status[1] = status[2] = status[3] = JPR_VOICE_DONE;
    CHECK(jpr_voice_refill(&p, status, fills) == 0 && jpr_voice_finished(&p, status) == 1);
}

static void test_the_last_chunk_is_short(void) {
    u8 status[JPR_VOICE_SLOTS] = {0, 0, 0, 0};
    struct jpr_voice_fill fills[JPR_VOICE_SLOTS];
    u32 n, k, sum = 0;
    fresh();
    jpr_voice_message(&p, &ix, 1, 41, 27, 0, 1);
    n = jpr_voice_refill(&p, status, fills);
    for (k = 0; k < n; k++) sum += fills[k].samples;
    CHECK(n == 4 && sum == 20000 && fills[3].samples == 20000 - 3 * JPR_VOICE_CHUNK);
}

static void test_dropped_voice_is_not_touched(void) {
    u8 status[JPR_VOICE_SLOTS] = {JPR_VOICE_QUEUED, JPR_VOICE_QUEUED, 0, 0};
    struct jpr_voice_fill fills[JPR_VOICE_SLOTS];
    fresh();
    jpr_voice_message(&p, &ix, 1, 41, 27, 0, 1);
    jpr_voice_dropped(&p);
    CHECK(p.clip == -1 && jpr_voice_refill(&p, status, fills) == 0);
    CHECK(jpr_voice_replay(&p, 0) == JPR_VOICE_START && p.clip == 0 && p.next == 0);   /* a new voice is needed */
}

/* Cutscene lines come from message data the game builds in memory (arc -1): their story file is the one their
 * text was found in by header, as the underlines found it; the first scene's Mom line is story 27, line 0. */
static void test_a_memory_built_line_is_voiced_from_the_file_its_text_was_found_in(void) {
    s32 arc;
    u32 dat;
    jpr_voice_origin(-1, 0xFFFFFFFFu, 41, 27, &arc, &dat);
    CHECK(arc == 41 && dat == 27);
    fresh();
    CHECK(jpr_voice_message(&p, &ix, 1, arc, dat, 0, 1) == JPR_VOICE_START && p.clip == 0);
    jpr_voice_origin(-1, 0xFFFFFFFFu, -2, 0xFFFFFFFFu, &arc, &dat);   /* not found: no clip */
    CHECK(arc == -1 && dat == 0xFFFFFFFFu);
    jpr_voice_origin(41, 31, 41, 27, &arc, &dat);                     /* a line with its own file keeps it */
    CHECK(arc == 41 && dat == 31);
    jpr_voice_origin(-1, 0xFFFFFFFFu, 31, 38, &arc, &dat);            /* found in system text: its system clip */
    CHECK(arc == 31 && dat == 38);
    fresh();
    CHECK(jpr_voice_message(&p, &ix, 1, arc, dat, 1, 1) == JPR_VOICE_START && p.clip == 3);
}

/* The voice data passes 2 GB, so it comes in three or more parts: a clip in the last one is found and its file named. */
static void test_three_parts_and_their_files(void) {
    struct jpr_voice_index ix3;
    struct jpr_voice_clip c;
    char path[JPR_VOICE_PATH_MAX];
    make_head();
    put16(head + 20 + 24 * 3 + 4, 2);                             /* the system clip moves to part 2 */
    put32(head + 16, 3);
    CHECK(jpr_voice_index_parse(&ix3, head, 20, read_entry, 0) == 0 && ix3.parts == 3);
    CHECK(jpr_voice_find(&ix3, JPR_VOICE_SYSTEM | 38, 1, &c) == 3 && c.part == 2);
    put32(head + 16, 2);                                          /* an index naming only 2 parts: no such clip */
    jpr_voice_index_parse(&ix3, head, 20, read_entry, 0);
    CHECK(jpr_voice_find(&ix3, JPR_VOICE_SYSTEM | 38, 1, &c) == -1);
    jpr_voice_part_path(path, 0);
    CHECK(strcmp(path, "/jpreader/voice-0.bin") == 0);
    jpr_voice_part_path(path, 2);
    CHECK(strcmp(path, "/jpreader/voice-2.bin") == 0);
    jpr_voice_part_path(path, 12);
    CHECK(strcmp(path, "/jpreader/voice-12.bin") == 0);
    jpr_voice_part_path(path, JPR_VOICE_PARTS - 1);
    CHECK(strcmp(path, "/jpreader/voice-63.bin") == 0);
}

int main(void) {
    test_index();
    test_autoplay_rules();
    test_refill_keeps_order_and_never_touches_a_queued_buffer();
    test_the_last_chunk_is_short();
    test_dropped_voice_is_not_touched();
    test_a_system_line_plays_its_own_clip();
    test_three_parts_and_their_files();
    test_a_memory_built_line_is_voiced_from_the_file_its_text_was_found_in();
    if (failures) {
        printf("voice: %d check(s) failed\n", failures);
        return 1;
    }
    printf("voice: all checks passed\n");
    return 0;
}
