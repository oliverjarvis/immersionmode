#include <stddef.h>
#include <stdio.h>

#include "core/capture.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* Distinct addresses standing in for game objects. */
static char objs[1024];
#define MSG(n) ((const void *)&objs[n])          /* n < 512 */
#define BUF(n) ((const void *)&objs[512 + (n)])  /* n < 256 */
#define TALK   ((const void *)&objs[1023])

static const u16 sample[] = {0x3053, 0x3093};

static const struct jpr_msg *last(void) {
    return &jpr_cap.ring[(jpr_cap.seq - 1) & (JPR_RING - 1)];
}

static void test_layout(void) {
    CHECK(sizeof(struct jpr_ident) == 20);
    CHECK(offsetof(struct jpr_msg, len) == 24);
    CHECK(offsetof(struct jpr_msg, text) == 28);
    CHECK(sizeof(struct jpr_msg) == 2076);
    CHECK(offsetof(struct jpr_capture, ring) == 16);
}

static void test_init(void) {
    jpr_cap_init();
    CHECK(jpr_cap.magic == JPR_CAP_MAGIC);
    CHECK(jpr_cap.seq == 0);
    CHECK(jpr_cap.open == 0);
}

static void test_string_overload_follows_expand(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(0), 41, 22);
    jpr_cap_fetch(MSG(0), 0, BUF(0), 18, 1200);
    jpr_cap_copy_ident(BUF(1), BUF(0));
    jpr_cap_begin_buf(TALK, BUF(1));
    jpr_cap_commit(BUF(2), sample, 2);
    CHECK(jpr_cap.seq == 1);
    CHECK(last()->seq == 1);
    CHECK(last()->id.arc == 41);
    CHECK(last()->id.dat == 22);
    CHECK(last()->id.line == 0);
    CHECK(last()->id.hdr_lines == 18);
    CHECK(last()->id.hdr_total == 1200);
    CHECK(last()->len == 2);
    CHECK(last()->full_len == 2);
    CHECK(last()->text[0] == 0x3053 && last()->text[1] == 0x3093);
}

static void test_id_overload_uses_final_buffer(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(1), 40, 7);
    jpr_cap_begin_id(TALK);
    jpr_cap_fetch(MSG(1), 5, BUF(0), 9, 300);
    jpr_cap_copy_ident(BUF(1), BUF(0));
    jpr_cap_commit(BUF(1), sample, 2);
    CHECK(last()->id.arc == 40);
    CHECK(last()->id.dat == 7);
    CHECK(last()->id.line == 5);
}

static void test_string_overload_ignores_stale_final_buffer(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(1), 40, 7);
    jpr_cap_fetch(MSG(1), 5, BUF(1), 9, 300);   /* left over from an earlier message */
    jpr_cap_begin_buf(TALK, BUF(9));            /* text that was never fetched */
    jpr_cap_commit(BUF(1), sample, 2);
    CHECK(last()->id.arc == JPR_ARC_UNKNOWN);
    CHECK(last()->id.line == JPR_NONE);
}

static void test_memory_built_message_data(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(2), JPR_ARC_MEMORY, JPR_NONE);
    jpr_cap_fetch(MSG(2), 3, BUF(0), 12, 640);
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), sample, 2);
    CHECK(last()->id.arc == JPR_ARC_MEMORY);
    CHECK(last()->id.dat == JPR_NONE);
    CHECK(last()->id.line == 3);
    CHECK(last()->id.hdr_lines == 12);
    CHECK(last()->id.hdr_total == 640);
}

static void test_deleted_message_data_is_forgotten(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(3), 41, 1);
    jpr_cap_msgdata_del(MSG(3));
    jpr_cap_fetch(MSG(3), 0, BUF(0), 1, 8);
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), sample, 2);
    CHECK(last()->id.arc == JPR_ARC_UNKNOWN);
    CHECK(last()->id.line == 0);
}

static void test_overlong_message_is_truncated(void) {
    static u16 big[3000];
    for (u32 i = 0; i < 3000; i++) big[i] = (u16)(0x3041 + (i & 63));
    jpr_cap_init();
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), big, 3000);
    CHECK(last()->len == JPR_MSG_MAX);
    CHECK(last()->full_len == 3000);
    CHECK(last()->text[JPR_MSG_MAX - 1] == big[JPR_MSG_MAX - 1]);
    CHECK(jpr_cap.ring[1].seq == 0);            /* the next slot is untouched */
    CHECK(jpr_cap.ring[1].id.arc == 0);
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), big, 70000);         /* only JPR_MSG_MAX units are read */
    CHECK(last()->len == JPR_MSG_MAX);
    CHECK(last()->full_len == 0xFFFF);
}

static void test_missing_text(void) {
    jpr_cap_init();
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), 0, 50);
    CHECK(jpr_cap.seq == 1);
    CHECK(last()->len == 0);
    CHECK(last()->full_len == 0);
}

static void test_commit_without_begin_is_ignored(void) {
    jpr_cap_init();
    jpr_cap_commit(BUF(0), sample, 2);
    CHECK(jpr_cap.seq == 0);
    CHECK(jpr_cap_pending_talk() == 0);
    jpr_cap_begin_id(TALK);
    CHECK(jpr_cap_pending_talk() == TALK);
}

static void test_ring_wraps(void) {
    jpr_cap_init();
    for (int i = 0; i < 10; i++) {
        jpr_cap_begin_buf(TALK, BUF(0));
        jpr_cap_commit(BUF(0), sample, 2);
    }
    CHECK(jpr_cap.seq == 10);
    CHECK(jpr_cap.ring[1].seq == 10);
    CHECK(jpr_cap.ring[0].seq == 9);
    CHECK(jpr_cap.ring[2].seq == 3);
}

static void test_message_table_evicts_oldest(void) {
    jpr_cap_init();
    for (u32 i = 0; i < 200; i++) jpr_cap_msgdata_new(MSG(i), 41, i);
    jpr_cap_fetch(MSG(199), 0, BUF(0), 1, 8);
    jpr_cap_fetch(MSG(0), 0, BUF(1), 1, 8);
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), sample, 2);
    CHECK(last()->id.dat == 199);
    jpr_cap_begin_buf(TALK, BUF(1));
    jpr_cap_commit(BUF(1), sample, 2);
    CHECK(last()->id.arc == JPR_ARC_UNKNOWN);
}

static void test_buffer_table_evicts_oldest(void) {
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(0), 41, 0);
    for (u32 i = 0; i < 40; i++) jpr_cap_fetch(MSG(0), i, BUF(i), 1, 8);
    jpr_cap_begin_buf(TALK, BUF(39));
    jpr_cap_commit(BUF(39), sample, 2);
    CHECK(last()->id.line == 39);
    jpr_cap_begin_buf(TALK, BUF(0));
    jpr_cap_commit(BUF(0), sample, 2);
    CHECK(last()->id.arc == JPR_ARC_UNKNOWN);
}

static void test_open_and_end(void) {
    jpr_cap_init();
    jpr_cap_set_open(TALK, 1);
    CHECK(jpr_cap.open == 1);
    jpr_cap_set_open(TALK, 0);
    CHECK(jpr_cap.open == 0);
    CHECK(jpr_cap.end_count == 1);
}

/* A choice menu's option is copied from a buffer GetString filled (and Expand copied): its origin is that buffer's. */
static void test_ident_of_a_buffer(void) {
    struct jpr_ident id;
    jpr_cap_init();
    jpr_cap_msgdata_new(MSG(0), 41, 10);
    jpr_cap_fetch(MSG(0), 1, BUF(0), 18, 1200);
    jpr_cap_copy_ident(BUF(1), BUF(0));
    id = jpr_cap_ident_of(BUF(1));
    CHECK(id.arc == 41 && id.dat == 10 && id.line == 1 && id.hdr_lines == 18);
    id = jpr_cap_ident_of(BUF(2));                        /* never tagged */
    CHECK(id.arc == JPR_ARC_UNKNOWN && id.line == JPR_NONE);
}

int main(void) {
    test_layout();
    test_ident_of_a_buffer();
    test_init();
    test_string_overload_follows_expand();
    test_id_overload_uses_final_buffer();
    test_string_overload_ignores_stale_final_buffer();
    test_memory_built_message_data();
    test_deleted_message_data_is_forgotten();
    test_overlong_message_is_truncated();
    test_missing_text();
    test_commit_without_begin_is_ignored();
    test_ring_wraps();
    test_message_table_evicts_oldest();
    test_buffer_table_evicts_oldest();
    test_open_and_end();
    if (failures) {
        printf("capture: %d check(s) failed\n", failures);
        return 1;
    }
    printf("capture: all checks passed\n");
    return 0;
}
