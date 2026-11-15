#include "data.h"

#include "core/util.h"
#include "display.h"
#include "fs.h"

#define ANNOT_PATH "/jpreader/annot.bin"
#define DICT_PATH "/jpreader/dict.bin"
#define ANNOT_MAGIC 0x4152504Au /* "JPRA" */
#define DICT_MAGIC 0x5752504Au  /* "JPRW" */
#define ANNOT_VERSION 2
#define DICT_VERSION 3      /* 3: each entry carries its JMdict id (stage 9C) */
#define DICT_HEADER 28 /* ... u32 stamp, the same in annot.bin's header */
#define ARCHIVES_MAX 8
#define FILE_ENTRY 12           /* u32 line table offset, u32 line count, u32 header total */
#define SCAN_FILES 128

struct jpr_annot_result jpr_annot;

static u32 annot_file, dict_file;
static u8 archives[ARCHIVES_MAX * 8] __attribute__((aligned(4)));
static u32 archive_count, stamp;
static u32 entry_count, label_count, entries_at, labels_at;
static u8 record[JPR_RECORD_MAX] __attribute__((aligned(4)));
static u8 scan[SCAN_FILES * FILE_ENTRY] __attribute__((aligned(4)));

static u32 rd16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 rd32(const u8 *p) {
    return rd16(p) | (rd16(p + 2) << 16);
}

static int read_exact(u32 file, u32 offset, void *buf, u32 size) {
    u32 got = 0;
    if (!size) return 0;
    return jpr_fs_read(file, offset, buf, size, &got) || got != size ? -1 : 0;
}

static void fail(u32 status, u32 which, int err) {
    jpr_panel.st.data_status = status;
    jpr_panel.st.data_error = (which << 8) | (u32)err;
}

void jpr_data_open(void) {
    u8 head[DICT_HEADER];
    int err;
    if (jpr_panel.st.data_status != JPR_DATA_NONE) return;
    err = jpr_fs_open(ANNOT_PATH, &annot_file);
    if (err) {
        fail(JPR_DATA_MISSING, JPR_DATA_ANNOT, err);
        return;
    }
    if (read_exact(annot_file, 0, head, 16) || rd32(head) != ANNOT_MAGIC || rd32(head + 4) != ANNOT_VERSION ||
        rd32(head + 8) > ARCHIVES_MAX || read_exact(annot_file, 16, archives, rd32(head + 8) * 8)) {
        fail(JPR_DATA_BAD, JPR_DATA_ANNOT, 0);
        return;
    }
    archive_count = rd32(head + 8);
    stamp = rd32(head + 12);
    err = jpr_fs_open(DICT_PATH, &dict_file);
    if (err) {
        fail(JPR_DATA_MISSING, JPR_DATA_DICT, err);
        return;
    }
    if (read_exact(dict_file, 0, head, DICT_HEADER) || rd32(head) != DICT_MAGIC || rd32(head + 4) != DICT_VERSION) {
        fail(JPR_DATA_BAD, JPR_DATA_DICT, 0);
        return;
    }
    if (rd32(head + 24) != stamp) {     /* unit entries index this build's dict.bin only */
        fail(JPR_DATA_MISMATCH, JPR_DATA_DICT, 0);
        return;
    }
    entry_count = rd32(head + 8);
    label_count = rd32(head + 12);
    entries_at = rd32(head + 16);
    labels_at = rd32(head + 20);
    jpr_panel.st.data_status = JPR_DATA_OK;
}

/* The archive table entry for archive `arc`, or null. */
static const u8 *archive(u32 arc) {
    u32 k;
    for (k = 0; k < archive_count; k++)
        if (rd16(archives + k * 8) == arc) return archives + k * 8;
    return 0;
}

/* What a lookup is for: a text, where it came from, and where its units go. */
struct want {
    const struct jpr_ident *id;
    const u16 *text;
    u32 len;
    struct jpr_unit *units;
    u32 max;
    u32 count;
    s32 arc;                 /* the file the units were found in */
    u32 dat;
};

/* Tries one file (its file table entry) for the text: JPR_ANNOT_OK with the units placed, or why not. */
static u32 try_file(const u8 *file, struct want *w) {
    u8 slot[4];
    u32 got = 0, at;
    s32 n;
    if (w->id->line >= rd32(file + 4)) return JPR_ANNOT_UNKNOWN_LINE;
    if (read_exact(annot_file, rd32(file) + w->id->line * 4, slot, 4)) return JPR_ANNOT_READ_ERROR;
    at = rd32(slot);
    if (!at) return JPR_ANNOT_NO_RECORD;
    if (jpr_fs_read(annot_file, at, record, JPR_RECORD_MAX, &got)) return JPR_ANNOT_READ_ERROR;
    n = jpr_annot_place(record, got, w->text, w->len, w->units, w->max);
    if (n < 0) return JPR_ANNOT_MISMATCH;
    w->count = (u32)n;
    return JPR_ANNOT_OK;
}

/* Text from message data built in memory: every file of the language's archives whose header matches. */
static u32 by_header(struct want *w, u32 language) {
    u32 status = JPR_ANNOT_UNKNOWN_LINE, k, first, j;
    for (k = 0; k < 2; k++) {
        const u8 *a = archive((k ? 40 : 30) + language);
        u32 files, table;
        if (!a) continue;
        files = rd16(a + 2);
        table = rd32(a + 4);
        for (first = 0; first < files; first += SCAN_FILES) {
            u32 n = files - first < SCAN_FILES ? files - first : SCAN_FILES;
            if (read_exact(annot_file, table + first * FILE_ENTRY, scan, n * FILE_ENTRY)) return JPR_ANNOT_READ_ERROR;
            for (j = 0; j < n; j++) {
                const u8 *e = scan + j * FILE_ENTRY;
                u32 found;
                if (rd32(e + 4) != w->id->hdr_lines || rd32(e + 8) != w->id->hdr_total) continue;
                found = try_file(e, w);
                if (found == JPR_ANNOT_OK) {
                    w->arc = (s32)((k ? 40 : 30) + language);
                    w->dat = first + j;
                    return found;
                }
                if (found > status) status = found;   /* a line that did not fit says more */
            }
        }
    }
    return status;
}

static u32 lookup(struct want *w, u32 language) {
    u32 status = JPR_ANNOT_UNKNOWN_LINE;
    w->count = 0;
    w->arc = JPR_ARC_UNKNOWN;
    w->dat = JPR_NONE;
    if (jpr_panel.st.data_status != JPR_DATA_OK) {
        status = JPR_ANNOT_NONE;
    } else if (w->id->line == JPR_NONE) {
        status = JPR_ANNOT_UNKNOWN_LINE;
    } else if (w->id->arc >= 0 && w->id->dat != JPR_NONE) {
        const u8 *a = archive((u32)w->id->arc);
        u8 file[FILE_ENTRY];
        if (a && w->id->dat < rd16(a + 2)) {
            if (read_exact(annot_file, rd32(a + 4) + w->id->dat * FILE_ENTRY, file, FILE_ENTRY))
                status = JPR_ANNOT_READ_ERROR;
            else
                status = try_file(file, w);
            if (status == JPR_ANNOT_OK) {
                w->arc = w->id->arc;
                w->dat = w->id->dat;
            }
        }
    } else if (w->id->arc == JPR_ARC_MEMORY) {
        status = by_header(w, language);
    }
    if (status != JPR_ANNOT_OK) {
        w->count = 0;
        w->arc = JPR_ARC_UNKNOWN;
    }
    return status;
}

void jpr_data_message(const struct jpr_msg *msg, u32 language) {
    struct want w;
    u32 status;
    jpr_annot.seq = 0;
    w.id = &msg->id;
    w.text = msg->text;
    w.len = msg->len;
    w.units = jpr_annot.units;
    w.max = JPR_UNITS_MAX;
    status = lookup(&w, language);
    jpr_annot.count = w.count;
    jpr_annot.arc = w.arc;
    jpr_annot.dat = w.dat;
    jpr_annot.status = status;
    jpr_annot.seq = msg->seq;      /* last: the panel takes the units once this matches */
}

u32 jpr_data_units(const struct jpr_ident *id, const u16 *text, u32 len, u32 language, struct jpr_unit *units,
                   u32 max) {
    struct want w;
    w.id = id;
    w.text = text;
    w.len = len;
    w.units = units;
    w.max = max;
    return lookup(&w, language) == JPR_ANNOT_OK ? w.count : 0;
}

/* Entry or label n: its offset from the table, then up to `max` bytes from there. */
static int read_item(u32 file, u32 table, u32 count, u32 n, u8 *buf, u32 max, u32 *len) {
    u8 slot[4];
    if (jpr_panel.st.data_status != JPR_DATA_OK || n >= count) return -1;
    if (read_exact(file, table + n * 4, slot, 4)) return -1;
    return jpr_fs_read(file, rd32(slot), buf, max, len) ? -1 : 0;
}

int jpr_data_entry(u32 entry, u8 *buf, u32 max, u32 *len) {
    return read_item(dict_file, entries_at, entry_count, entry, buf, max, len);
}

int jpr_data_label(u32 label, u8 *buf, u32 max, u32 *len) {
    return read_item(dict_file, labels_at, label_count, label, buf, max, len);
}

u32 jpr_data_stamp(void) {
    return jpr_panel.st.data_status == JPR_DATA_OK ? stamp : 0;
}
