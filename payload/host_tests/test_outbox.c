#include <stdio.h>
#include <string.h>

#include "core/outbox.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
    static u8 slot[JPR_OUTBOX_SLOT], big[JPR_OUTBOX_SLOT];
    u32 len = 0, seqs[4] = {0, 0, 0, 0};
    u32 n = jpr_outbox_pack(slot, 7, JPR_OUTBOX_WORD, (const u8 *)"front=x\n", 8);
    CHECK(n == JPR_OUTBOX_HEAD + 8 && !memcmp(slot + JPR_OUTBOX_HEAD, "front=x\n", 8));
    CHECK(jpr_outbox_head(slot, JPR_OUTBOX_HEAD, &len) == 7 && len == 8);
    CHECK(jpr_outbox_head(slot, 12, &len) == 0);                       /* short read */
    CHECK(jpr_outbox_kind(slot) == JPR_OUTBOX_WORD);
    CHECK(jpr_outbox_pack(slot, 8, JPR_OUTBOX_GRAMMAR, (const u8 *)"key=bunpro:1\n", 13) == JPR_OUTBOX_HEAD + 13);
    CHECK(jpr_outbox_head(slot, JPR_OUTBOX_HEAD, &len) == 8 && len == 13 && jpr_outbox_kind(slot) == JPR_OUTBOX_GRAMMAR);
    slot[12] = 0;                                                       /* a slot an 8B build wrote: reserved 0 */
    CHECK(jpr_outbox_kind(slot) == JPR_OUTBOX_WORD);
    slot[12] = 9;                                                       /* a kind this build does not know */
    CHECK(jpr_outbox_kind(slot) == JPR_OUTBOX_WORD);
    CHECK(jpr_outbox_pack(slot, 0, JPR_OUTBOX_WORD, (const u8 *)"x", 1) == 0);    /* seq 0 means empty */
    CHECK(jpr_outbox_pack(slot, 1, JPR_OUTBOX_WORD, big, JPR_OUTBOX_SLOT) == 0);  /* too long for a slot */
    memset(slot, 0, JPR_OUTBOX_HEAD);
    CHECK(jpr_outbox_head(slot, JPR_OUTBOX_HEAD, &len) == 0);          /* an empty slot */

    CHECK(jpr_outbox_slot(seqs, 4) == 0 && jpr_outbox_next(seqs, 4) == -1);
    seqs[0] = 5; seqs[1] = 3; seqs[3] = 9;
    CHECK(jpr_outbox_slot(seqs, 4) == 2);                               /* an empty slot first */
    CHECK(jpr_outbox_next(seqs, 4) == 1);                               /* the oldest add goes first */
    seqs[2] = 10;
    CHECK(jpr_outbox_slot(seqs, 4) == 1);                               /* full: the oldest is dropped */
    if (failures) {
        printf("outbox: %d check(s) failed\n", failures);
        return 1;
    }
    printf("outbox: all checks passed\n");
    return 0;
}
