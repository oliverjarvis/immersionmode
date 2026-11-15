#include "kokoro.h"

#include "core/util.h"
#include "data.h"
#include "fs.h"
#include "game.h"

#define BITS_AT 0x8000       /* the id list first, then two sets of marks */
#define FALLBACK_AT 0xC000   /* then kokoro's cards that have no id, by spelling */

_Static_assert(JPR_KOKORO_WORDS * 4 <= BITS_AT && BITS_AT + 2 * JPR_KOKORO_BITS <= FALLBACK_AT &&
                   FALLBACK_AT + JPR_KOKORO_FALLBACK * 4 <= JPR_KOKORO_SIZE,
               "kokoro's lists and marks fit their memory block");

static u32 *table, *fallback;
static u8 *bits[2];
static volatile u32 active = 2;   /* the marks in use: 0 or 1; 2 for none yet */
static volatile u32 entries, kept, kept_fallback;
static u32 loaded;
static struct jpr_words words;

void jpr_kokoro_init(void *region) {
    if (!region) return;
    table = region;
    fallback = (u32 *)(void *)((u8 *)region + FALLBACK_AT);
    bits[0] = (u8 *)region + BITS_AT;
    bits[1] = bits[0] + JPR_KOKORO_BITS;
}

/* Marks from the lists (count ids, fb spellings) into the set not in use, read with ids.bin, then switches:
 * each entry by its id, then raised where a spelling of its word is one of kokoro's cards without an id. */
static void rebuild(u32 count, u32 fb, u8 *scratch, u32 size) {
    u8 head[JPR_IDS_HEAD];
    u32 next = active == 0 ? 1 : 0, file = 0, got = 0, n = 0, pairs = 0, first, per = size / 4;
    s32 total;
    jpr_memset(bits[next], 0, JPR_KOKORO_BITS);
    if (!jpr_fs_open(JPR_IDS_PATH, &file)) {
        if (!jpr_fs_read(file, 0, head, sizeof head, &got) &&
            (total = jpr_ids_count(head, got, jpr_data_stamp(), &pairs)) > 0) {
            u32 all = (u32)total;
            n = all < JPR_KOKORO_ENTRIES ? all : JPR_KOKORO_ENTRIES;
            for (first = 0; first < n; first += per) {
                u32 want = n - first < per ? n - first : per;
                if (jpr_fs_read(file, JPR_IDS_HEAD + first * 4, scratch, want * 4, &got) || got != want * 4) {
                    n = first;
                    pairs = 0;
                    break;
                }
                jpr_kokoro_mark_ids(bits[next], first, (const u32 *)(const void *)scratch, want, table, count);
            }
            per = size / 8;
            for (first = 0; fb && first < pairs; first += per) {
                u32 want = pairs - first < per ? pairs - first : per;
                if (jpr_fs_read(file, JPR_IDS_HEAD + all * 4 + first * 8, scratch, want * 8, &got) || got != want * 8)
                    break;
                jpr_kokoro_mark_forms(bits[next], (const u32 *)(const void *)scratch, want, fallback, fb);
            }
        }
        jpr_fs_close(file);
    }
    jpr_barrier();
    entries = n;
    active = next;
    kept = count;
    kept_fallback = fb;
}

void jpr_kokoro_load(u8 *scratch, u32 size) {
    u8 head[JPR_KOKORO_HEAD];
    u32 file = 0, got = 0, fb = 0;
    s32 count = -1;
    if (loaded || !table) return;
    loaded = 1;
    if (jpr_fs_open(JPR_KOKORO_PATH, &file)) return;   /* never synced */
    if (!jpr_fs_read(file, 0, head, sizeof head, &got))
        count = jpr_kokoro_head_counts(head, got, JPR_KOKORO_WORDS, JPR_KOKORO_FALLBACK, &fb);
    if (count > 0 && (jpr_fs_read(file, JPR_KOKORO_HEAD, table, (u32)count * 4, &got) || got != (u32)count * 4))
        count = -1;
    if (count >= 0 && fb &&
        (jpr_fs_read(file, JPR_KOKORO_HEAD + (u32)count * 4, fallback, fb * 4, &got) || got != fb * 4))
        count = -1;
    jpr_fs_close(file);
    if (count > 0 || (count == 0 && fb)) rebuild((u32)count, fb, scratch, size);
}

struct jpr_words *jpr_kokoro_words(void) {
    if (!table) return 0;
    loaded = 1;   /* this list replaces kokoro.bin's */
    jpr_words_init(&words, table, JPR_KOKORO_WORDS, fallback, JPR_KOKORO_FALLBACK);
    return &words;
}

s32 jpr_kokoro_keep(u8 *scratch, u32 size, u32 synced) {
    u8 head[JPR_KOKORO_HEAD];
    s32 count = jpr_words_finish(&words);
    if (count < 0) return -1;
    jpr_kokoro_head_pack(head, (u32)count, words.fb_count, synced);
    if (!jpr_fs_write_at(JPR_KOKORO_PATH, 0, head, sizeof head)) {
        if (count) jpr_fs_write_at(JPR_KOKORO_PATH, JPR_KOKORO_HEAD, table, (u32)count * 4);
        if (words.fb_count)
            jpr_fs_write_at(JPR_KOKORO_PATH, JPR_KOKORO_HEAD + (u32)count * 4, fallback, words.fb_count * 4);
    }
    rebuild((u32)count, words.fb_count, scratch, size);
    return count;
}

const u8 *jpr_kokoro_marks(u32 *n) {
    u32 a = active;
    if (a > 1) {
        *n = 0;
        return 0;
    }
    jpr_barrier();
    *n = entries;
    return bits[a];
}

u32 jpr_kokoro_count(void) {
    return kept + kept_fallback;
}
