#include <stdio.h>
#include <string.h>

#include "core/grammar.h"
#include "core/replies.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static u8 slot[JPR_REPLY_SLOT_BYTES];

int main(void) {
    const u8 s[] = "ポケモンを つかまえる", t[] = "ポケモンを つかまえた";
    const u8 r[] = "\"I catch a Pokemon.\"\nつかまえる: to catch";
    u32 k = jpr_reply_key("anthropic/claude-haiku-5.5", s, sizeof s - 1), n, rlen = 0;

    CHECK(k == jpr_reply_key("anthropic/claude-haiku-5.5", s, sizeof s - 1));
    CHECK(k != jpr_reply_key("anthropic/claude-opus-5.5", s, sizeof s - 1));   /* the model counts */
    CHECK(k != jpr_reply_key("anthropic/claude-haiku-5.5", t, sizeof t - 1));
    CHECK(jpr_reply_offset(k) % JPR_REPLY_SLOT_BYTES == 0);
    CHECK(jpr_reply_offset(k) < JPR_REPLY_SLOTS * JPR_REPLY_SLOT_BYTES);
    CHECK(JPR_REPLY_HEAD + JPR_SENTENCE_BYTES + JPR_REPLY_MAX <= JPR_REPLY_SLOT_BYTES);   /* any reply fits */

    n = jpr_reply_pack(slot, k, s, sizeof s - 1, r, sizeof r - 1);
    CHECK(n == JPR_REPLY_HEAD + (sizeof s - 1) + (sizeof r - 1));
    CHECK(jpr_reply_head(slot, n, k, s, sizeof s - 1, &rlen) && rlen == sizeof r - 1);
    CHECK(!memcmp(slot + JPR_REPLY_HEAD + sizeof s - 1, r, rlen));

    /* the slot asked about another sentence, or under another key, holds no reply for it */
    CHECK(!jpr_reply_head(slot, n, k, t, sizeof t - 1, &rlen));
    CHECK(!jpr_reply_head(slot, n, k ^ 1, s, sizeof s - 1, &rlen));
    CHECK(!jpr_reply_head(slot, JPR_REPLY_HEAD + 3, k, s, sizeof s - 1, &rlen));   /* a short read */
    memset(slot, 0, sizeof slot);                                                 /* never written */
    CHECK(!jpr_reply_head(slot, sizeof slot, k, s, sizeof s - 1, &rlen));

    CHECK(jpr_reply_pack(slot, k, s, sizeof s - 1, slot, JPR_REPLY_SLOT_BYTES) == 0);   /* too big */

    if (failures) {
        printf("replies: %d check(s) failed\n", failures);
        return 1;
    }
    printf("replies: all checks passed\n");
    return 0;
}
