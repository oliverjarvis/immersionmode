#include "voice.h"

#include "core/capture.h"
#include "core/voice.h"
#include "data.h"
#include "debug.h"
#include "fs.h"
#include "snd.h"

static struct jpr_voice_player player;
static struct jpr_voice_index index_;
static u32 file, opened, fault;
static u32 part_file, part_size, part_open = 0xFFFFFFFFu;   /* the voice-N.bin being read */
static void *voice;
static volatile u32 voice_lost;          /* set on the sound thread when the game takes the voice back */
static struct jpr_wave waves[JPR_VOICE_SLOTS];
static s16 *pcm[JPR_VOICE_SLOTS];
static u32 must_play;
static u32 gain_bits = 0x3F800000u;      /* 1.0 */

static void lost(void *v, u32 arg) {
    (void)v;
    (void)arg;
    voice_lost = 1;                      /* the voice is already freed: never touch it again */
}

static int read_entry(void *ctx, u32 k, u8 *entry) {
    u32 len = 0;
    (void)ctx;
    return jpr_fs_read(file, JPR_VOICE_HEADER + JPR_VOICE_ENTRY * k, entry, JPR_VOICE_ENTRY, &len) ||
           len != JPR_VOICE_ENTRY;
}

/* voice.bin's header (the index stays on the SD card) and the ring's buffers; once. */
static void open_once(void) {
    u8 header[JPR_VOICE_HEADER];
    u32 len = 0, k;
    if (opened) return;
    opened = 1;
    jpr_voice_init(&player);
    if (jpr_fs_open(JPR_VOICE_PATH, &file)) {
        fault = JPR_VOICE_FAULT_FILE;
        return;
    }
    if (jpr_fs_read(file, 0, header, sizeof header, &len) ||
        jpr_voice_index_parse(&index_, header, len, read_entry, 0)) {
        fault = JPR_VOICE_FAULT_HEADER;
        index_.count = 0;
        return;
    }
    for (k = 0; k < JPR_VOICE_SLOTS; k++) pcm[k] = jpr_lin_alloc(2 * JPR_VOICE_CHUNK);
    jpr_dbg.voice_ring = (u32)pcm[0];
    if (!pcm[JPR_VOICE_SLOTS - 1]) {
        fault = JPR_VOICE_FAULT_MEMORY;
        index_.count = 0;
    }
}

/* voice-<part>.bin open for reading, and the clip about to play inside it. */
static int part_ready(u32 part, u32 at, u32 samples) {
    char path[JPR_VOICE_PATH_MAX];
    if (part != part_open) {
        if (part_open != 0xFFFFFFFFu) jpr_fs_close(part_file);
        part_open = 0xFFFFFFFFu;
        jpr_voice_part_path(path, part);
        if (jpr_fs_open(path, &part_file) || jpr_fs_size(part_file, &part_size)) return 0;
        part_open = part;
    }
    return at <= part_size && samples <= (part_size - at) / 2;
}

/* The game may take the voice back on its sound thread at any moment (the drop callback runs there, under
 * the sound lock): a voice is only used while that lock is held and voice_lost is still clear. */
static int ours(void) {
    jpr_snd_lock();
    if (voice && !voice_lost) return 1;
    jpr_snd_unlock();
    return 0;
}

static void stop_voice(void) {
    if (!ours()) return;
    jpr_snd_stop(voice);                     /* every queued buffer becomes DONE */
    jpr_snd_unlock();
}

static void release_voice(void) {
    if (ours()) {
        jpr_snd_voice_free(voice);
        jpr_snd_unlock();
    }
    voice = 0;
}

static int ensure_voice(void) {
    if (voice_lost) {
        voice = 0;
        voice_lost = 0;
    }
    if (voice) return 1;
    voice = jpr_snd_voice_alloc(lost, 0);
    if (!voice) return 0;
    jpr_snd_set_rate(voice, JPR_VOICE_RATE);
    if (ours()) {
        jpr_snd_set_volume(voice, gain_bits);
        jpr_snd_unlock();
    }
    return 1;
}

static void begin(void) {
    u32 k;
    stop_voice();
    if (!part_ready(player.part, player.at, player.total)) {
        fault = JPR_VOICE_FAULT_READ;          /* the part is missing or shorter than the index says */
        release_voice();
        jpr_voice_dropped(&player);
        return;
    }
    if (!ensure_voice()) {
        fault = JPR_VOICE_FAULT_VOICE;
        jpr_voice_dropped(&player);
        return;
    }
    for (k = 0; k < JPR_VOICE_SLOTS; k++) waves[k].status = JPR_VOICE_FREE;
    must_play = 1;
    if (fault == JPR_VOICE_FAULT_READ || fault == JPR_VOICE_FAULT_VOICE) fault = JPR_VOICE_FAULT_NONE;
    jpr_dbg.voice_starts++;
}

static void feed(void) {
    u8 status[JPR_VOICE_SLOTS];
    struct jpr_voice_fill fills[JPR_VOICE_SLOTS];
    u32 n, k, len = 0;
    if (voice_lost) {
        voice = 0;
        voice_lost = 0;
        jpr_voice_dropped(&player);
        return;
    }
    if (!voice || player.clip < 0) return;
    for (k = 0; k < JPR_VOICE_SLOTS; k++) status[k] = waves[k].status;
    n = jpr_voice_refill(&player, status, fills);
    for (k = 0; k < n; k++) {
        struct jpr_wave *w = &waves[fills[k].slot];
        if (jpr_fs_read(part_file, fills[k].offset, pcm[fills[k].slot], 2 * fills[k].samples, &len) ||
            len != 2 * fills[k].samples) {
            fault = JPR_VOICE_FAULT_READ;
            stop_voice();
            release_voice();                 /* one of the game's 24 voices: never kept idle */
            jpr_voice_dropped(&player);
            return;
        }
        if (!ours()) return;                 /* taken back while the samples were read: the next frame resets */
        jpr_snd_wave_init(w);
        w->address = pcm[fills[k].slot];
        w->samples = (s32)fills[k].samples;
        jpr_snd_append(voice, w);
        jpr_snd_unlock();
    }
    if (must_play && n && ours()) {
        jpr_snd_play(voice);
        jpr_snd_unlock();
        must_play = 0;
    }
    for (k = 0; k < JPR_VOICE_SLOTS; k++) status[k] = waves[k].status;
    if (jpr_voice_finished(&player, status)) {
        release_voice();
        jpr_voice_dropped(&player);          /* nothing playing; the message's clip stays for the buttons */
    }
}

void jpr_voice_frame(u32 autoplay, u32 request) {
    u32 action = JPR_VOICE_NONE;
    if (!jpr_cap.seq) return;                /* nothing to voice yet; the game sets up its own memory first */
    open_once();
    if (index_.count && jpr_cap.seq) {
        const struct jpr_msg *msg = &jpr_cap.ring[(jpr_cap.seq - 1) & (JPR_RING - 1)];
        s32 arc = msg->id.arc;
        u32 dat = msg->id.dat, line = msg->id.line;
#if JPR_DEBUG
        if (jpr_dbg.voice_test) {           /* the e2e cannot reach the story quickly: any message stands in */
            arc = 41;
            dat = 27;
            line = jpr_dbg.voice_test - 1 + (jpr_cap.seq & 1);
        }
#endif
        /* A cutscene line is built in memory: its file is the one the underlines found its text in (looked up as
         * the message arrived, before this frame). */
        if (jpr_annot.seq == msg->seq) jpr_voice_origin(arc, dat, jpr_annot.arc, jpr_annot.dat, &arc, &dat);
        if (msg->seq == jpr_cap.seq)
            action = jpr_voice_message(&player, &index_, jpr_cap.seq, arc, dat, line, autoplay);
    }
    if (request && index_.count) action = jpr_voice_replay(&player, request - 1);
    if (action == JPR_VOICE_START) begin();
    feed();
    jpr_dbg.voice_ptr = (u32)voice;
    jpr_dbg.voice_clip = (u32)player.clip;
    jpr_dbg.voice_speed = player.speed;
    jpr_dbg.voice_next = player.next;
    jpr_dbg.voice_total = player.total;
    jpr_dbg.voice_fault = fault;
    if (voice && !voice_lost) {
        jpr_dbg.snd_pos = jpr_snd_position(voice);
        jpr_dbg.snd_active = jpr_snd_active(voice);
        jpr_dbg.snd_fed = jpr_snd_fed(voice);
    } else {
        jpr_dbg.snd_active = 0;
    }
}

u32 jpr_voice_available(void) {
    return player.message_clip >= 0;
}

u32 jpr_voice_playing(void) {
    return voice && !voice_lost && player.clip >= 0;
}

void jpr_voice_set_gain(u32 fbits) {
    gain_bits = fbits;
    if (ours()) {
        jpr_snd_set_volume(voice, gain_bits);
        jpr_snd_unlock();
    }
}

u32 jpr_voice_fault(void) {
    return fault;
}
