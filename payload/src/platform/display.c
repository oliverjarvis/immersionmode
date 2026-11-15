#include "display.h"

#include "builtin_font.h"
#include "core/capture.h"
#include "core/config.h"
#include "core/grammar.h"
#include "core/replies.h"
#include "core/reply.h"
#include "core/status.h"
#include "core/voice.h"
#include "core/url.h"
#include "core/util.h"
#include "core/word.h"
#include "data.h"
#include "debug.h"
#include "voice.h"
#include "mix.h"
#include "choices.h"
#include "screen.h"
#include "fs.h"
#include "game.h"
#include "game_addrs.h"
#include "kokoro.h"
#include "svc.h"
#include "worker.h"

#define FONT_PATH "/jpreader/font.bin"
#define FONT_BYTES 0x34000 /* font.bin: all three faces (about 197 KB in use); jpreader_tools.font.FONT_MAX_BYTES */
#define CONFIG_PATH "/jpreader/config.txt"
#define STATUS_EVERY 30   /* frames between debug line updates */

struct jpr_panel jpr_panel;
const void *jpr_talk;

static struct jpr_font font;
static u8 font_blob[FONT_BYTES] __attribute__((aligned(4)));
static u32 touch_down, touch_x, touch_y;
static struct jpr_touch_gate touch_gate;
static struct jpr_config config;
static char config_text[JPR_CONFIG_MAX];
static u32 config_read, status_wait, gr_detail, fill_ticks, copy_ticks;   /* gr_detail: the 3DS result behind the last Grammar failure */
static struct jpr_grammar_job outgoing;

#define SYNC_NOT_YET 0   /* the session's word list: */
#define SYNC_POSTED 1
#define SYNC_DONE 2
#define SYNC_FAILED 3    /* tried again after the next request that succeeds */

static struct jpr_url kokoro_url;
static int kokoro_on;
static struct jpr_override overrides[JPR_OVERRIDES];
static u32 override_count, kokoro_job, kokoro_kind, kokoro_seq, sync_state, flush_ready, outbox_pending, add_entry;
static u32 point_id;   /* the grammar point the add job in flight is for (stage 17) */

static void succeeded(void);

/* config.txt, read when a request starts so an edited key takes effect (and once at the first message, for the debug line). */
static void read_config(void) {
    u32 len = 0;
    if (jpr_fs_read_file(CONFIG_PATH, config_text, sizeof config_text, &len)) len = 0;
    jpr_config_parse(&config, config_text, len);
    config_read = 1;
    kokoro_on = config.kokoro_token[0] && !jpr_url_parse(config.kokoro_url, &kokoro_url);
    jpr_panel.st.kokoro_on = (u32)kokoro_on;
}

static int japanese(void) {
    return *(const volatile u8 *)LANG_BYTE <= 1;
}

void jpr_display_init(void) {
    jpr_panel_init(&jpr_panel);
    jpr_font_init(&font);
    jpr_font_builtin(&font, jpr_builtin_font, sizeof jpr_builtin_font);
}

void jpr_display_ensure_font(void) {
    u32 len = 0;
    int err;
    if (jpr_panel.st.font_status != JPR_FONT_NONE || !japanese()) return;
    err = jpr_fs_read_file(FONT_PATH, font_blob, FONT_BYTES, &len);
    if (err) {
        jpr_panel.st.font_status = JPR_FONT_MISSING;
        jpr_panel.st.font_error = (u32)err;
        return;
    }
    if (jpr_font_load(&font, font_blob, len)) {
        jpr_panel.st.font_status = JPR_FONT_BAD;
        return;
    }
    jpr_panel.st.font_status = JPR_FONT_OK;
    jpr_panel.st.font_glyphs = font.faces[JPR_FACE_TEXT].count;
}

void jpr_display_committed(void) {
    u32 seq = jpr_cap.seq;
    if (!seq || !japanese()) return;
    jpr_display_ensure_font();
    if (!config_read) read_config();
    jpr_data_open();
    if (jpr_annot.seq != seq) jpr_data_message(&jpr_cap.ring[(seq - 1) & (JPR_RING - 1)], *(const volatile u8 *)LANG_BYTE);
}

/* The message window of the talk window on screen, or null if any pointer on the way looks wrong. */
static const u8 *message_window(void) {
    u32 talk = (u32)jpr_talk, g2d, win;
    if (!jpr_plausible_ptr(talk)) return 0;
    g2d = *(const u32 *)(talk + TALK_G2D);
    if (!jpr_plausible_ptr(g2d)) return 0;
    win = *(const u32 *)(g2d + G2D_MSGWIN);
    if (!jpr_plausible_ptr(win)) return 0;
    if (*(const u32 *)win != JPR_MSGWIN_VTABLE) return 0;
    return (const u8 *)win;
}

/* A reply from replies.bin for the sentence asked about, handed to the panel as if the worker had answered;
 * 0 when there is none (no file, another sentence in the slot, a read error). */
static int remembered(const struct jpr_grammar_view *g) {
    static u8 head[JPR_REPLY_HEAD + JPR_SENTENCE_BYTES];
    u8 *reply = jpr_link.result.text;   /* the worker is idle: its result buffer is free */
    u32 key = jpr_reply_key(JPR_EXPLAIN_SOURCE, g->ex.sentence, g->ex.sentence_len), file = 0, got = 0, rlen = 0;
    u32 at = jpr_reply_offset(key);
    int ok = 0;
    if (jpr_fs_open(JPR_REPLIES_PATH, &file)) return 0;
    if (!jpr_fs_read(file, at, head, JPR_REPLY_HEAD + g->ex.sentence_len, &got) &&
        jpr_reply_head(head, got, key, g->ex.sentence, g->ex.sentence_len, &rlen) && rlen <= JPR_REPLY_MAX &&
        !jpr_fs_read(file, at + JPR_REPLY_HEAD + g->ex.sentence_len, reply, rlen, &got) && got == rlen)
        ok = 1;
    jpr_fs_close(file);
    if (ok) jpr_panel_grammar_result(&jpr_panel, &font, g->job, JPR_NET_OK, 0, 0, reply, rlen);
    return ok;
}

/* Every frame the panel is up: hands a waiting request to the worker, passes Back on, takes results. */
static void grammar_frame(void) {
    struct jpr_grammar_view *g = &jpr_panel.gr;
    if (g->cancel) {
        jpr_link.cancel = g->cancel;
        g->cancel = 0;
    }
    /* the link is free for Grammar only once any kokoro result is collected: it shares the result buffer */
    if (g->pending && jpr_link_idle(&jpr_link) && !kokoro_job) {
        g->pending = 0;
        read_config();
        if (!kokoro_on) {
            jpr_panel_grammar_result(&jpr_panel, &font, g->job, JPR_NET_NO_KEY, 0, 0, 0, 0);
            return;
        }
        if (remembered(g)) return;
        outgoing.id = g->job;
        outgoing.kind = JPR_JOB_GRAMMAR;
        outgoing.log = config.debug;
        jpr_memcpy(outgoing.key, config.kokoro_token, JPR_TOKEN_MAX);
        jpr_memcpy(&outgoing.kokoro, &kokoro_url, (u32)sizeof kokoro_url);
        jpr_memcpy(outgoing.sentence, g->ex.sentence, g->ex.sentence_len);
        outgoing.sentence_len = g->ex.sentence_len;
        jpr_memcpy(outgoing.context, g->ex.context, g->ex.context_len);
        outgoing.context_len = g->ex.context_len;
        if (jpr_worker_post(&outgoing)) {
            jpr_panel_grammar_result(&jpr_panel, &font, g->job, JPR_NET_UNREACHABLE, 7000, 0, 0, 0);
            return;
        }
        jpr_panel.st.gr_requests++;
    }
    if (g->state == JPR_GR_ASKING && !g->pending && jpr_link_result(&jpr_link, g->job)) {
        const struct jpr_grammar_result *r = &jpr_link.result;
        jpr_panel_grammar_result(&jpr_panel, &font, r->id, r->failure, r->code, r->http_status, r->text, r->text_len);
        gr_detail = r->detail;
        if (r->failure == JPR_NET_OK) succeeded();
    }
}

/* A request succeeded: a failed sync and the outbox get their next try. */
static void succeeded(void) {
    flush_ready = 1;
    if (sync_state == SYNC_FAILED) sync_state = SYNC_NOT_YET;
}

/* What the session knows of a word before kokoro's next list: an add kokoro answered, or one saved for later. */
static void remember(u32 entry, u32 state) {
    u32 k;
    for (k = 0; k < override_count; k++)
        if (overrides[k].entry == entry) {
            overrides[k].state = state;
            return;
        }
    if (override_count == JPR_OVERRIDES) {   /* full: the oldest goes */
        for (k = 1; k < JPR_OVERRIDES; k++) overrides[k - 1] = overrides[k];
        override_count--;
    }
    overrides[override_count].entry = entry;
    overrides[override_count++].state = state;
}

/* The kokoro job's result, once the worker has it. Runs before grammar_frame, which may post a new job. */
static void kokoro_collect(void) {
    const struct jpr_grammar_result *r = &jpr_link.result;
    u32 outcome, state = 0;
    if (!kokoro_job || !jpr_link_result(&jpr_link, kokoro_job)) return;
    kokoro_job = 0;
    outbox_pending = r->outbox;
    jpr_panel.st.outbox = r->outbox;
    jpr_panel.st.kokoro_failure = r->failure;
    jpr_panel.st.kokoro_http = r->http_status;
    if (kokoro_kind == JPR_JOB_SYNC) {
        sync_state = r->failure == JPR_NET_OK ? SYNC_DONE : SYNC_FAILED;
        if (r->failure == JPR_NET_OK) jpr_panel.st.kokoro_syncs++;
    } else if (kokoro_kind == JPR_JOB_ADD_WORD) {
        outcome = jpr_word_outcome(r->failure, r->http_status, r->queued, r->text, r->text_len, &state);
        if (state) remember(add_entry, state);
        jpr_panel_added(&jpr_panel, add_entry, outcome);
    } else if (kokoro_kind == JPR_JOB_ADD_GRAMMAR) {   /* kokoro answers a grammar add as it does a word add */
        outcome = jpr_word_outcome(r->failure, r->http_status, r->queued, r->text, r->text_len, &state);
        jpr_panel_point_added(&jpr_panel, point_id, outcome, state);
    } else if (r->failure == JPR_NET_OK) {
        sync_state = SYNC_NOT_YET;   /* the saved adds went: fetch the list again */
        jpr_overrides_sent(overrides, override_count);   /* kokoro has them, up next: v2 lists never do */
        jpr_overrides_sent(jpr_panel.gr.over, jpr_panel.gr.over_count);   /* and the grammar points saved */
    } else {
        flush_ready = 0;
    }
    if (r->failure == JPR_NET_OK) succeeded();
}

/* Hands a kokoro job to the worker. 0, or -1 when the worker cannot take it. */
static int kokoro_post(u32 kind) {
    outgoing.id = 0x80000000u | (++kokoro_seq & 0x7FFFFFFFu);   /* never a Grammar request's id */
    outgoing.kind = kind;
    outgoing.log = config.debug;
    jpr_memcpy(outgoing.key, config.kokoro_token, JPR_TOKEN_MAX);
    jpr_memcpy(&outgoing.kokoro, &kokoro_url, (u32)sizeof kokoro_url);
    if (jpr_worker_post(&outgoing)) return -1;
    kokoro_job = outgoing.id;
    kokoro_kind = kind;
    return 0;
}

/* Sends the add the panel built when Add was tapped. */
static void post_add(void) {
    jpr_panel.add_wanted = 0;
    read_config();   /* an edited token takes effect, as an edited key does */
    jpr_memcpy(outgoing.context, jpr_panel.add_body, jpr_panel.add_body_len);
    outgoing.context_len = jpr_panel.add_body_len;
    add_entry = jpr_panel.add_entry;
    if (!kokoro_on || kokoro_post(JPR_JOB_ADD_WORD)) jpr_panel_added(&jpr_panel, add_entry, JPR_ADD_FAILED);
}

/* Sends the grammar point add the panel's "+ Add" asked for (stage 17). */
static void post_point(void) {
    u32 n;
    jpr_panel.gr.add_wanted = 0;
    read_config();
    point_id = jpr_panel.gr.add_id;
    n = jpr_point_body(outgoing.context, sizeof outgoing.context, point_id);
    outgoing.context_len = n;
    if (!n || !kokoro_on || kokoro_post(JPR_JOB_ADD_GRAMMAR)) jpr_panel_point_added(&jpr_panel, point_id, JPR_ADD_FAILED, 0);
}

/* One kokoro job at a time, after any Grammar request: a word add, a grammar add, the session's sync, then the
 * outbox. */
static void kokoro_schedule(void) {
    if (!kokoro_on) {
        if (jpr_panel.add_wanted) jpr_panel_added(&jpr_panel, jpr_panel.add_entry, JPR_ADD_FAILED);
        if (jpr_panel.gr.add_wanted) jpr_panel_point_added(&jpr_panel, jpr_panel.gr.add_id, JPR_ADD_FAILED, 0);
        return;
    }
    if (kokoro_job || jpr_panel.gr.pending || !jpr_link_idle(&jpr_link)) return;
    if (jpr_panel.add_wanted) {
        post_add();
    } else if (jpr_panel.gr.add_wanted) {
        post_point();
    } else if (sync_state == SYNC_NOT_YET) {
        sync_state = kokoro_post(JPR_JOB_SYNC) ? SYNC_FAILED : SYNC_POSTED;
    } else if (outbox_pending && flush_ready) {
        if (kokoro_post(JPR_JOB_FLUSH)) flush_ready = 0;
    }
}

/* Each unit's mark, every frame: kokoro's list, then this session's adds. */
static void kokoro_marks(void) {
    u32 entries = 0;
    const u8 *bits = jpr_kokoro_marks(&entries);
    jpr_panel_marks(&jpr_panel, bits, entries, overrides, override_count);
    jpr_panel.st.kokoro_words = jpr_kokoro_count();
}

/* The debug line (spec 8.3), refreshed every STATUS_EVERY frames while config.txt has debug=1. */
static void update_status(const struct jpr_msg *msg) {
    s64 used = 0;
    u32 total = *(const volatile u32 *)CFGMEM_APPMEMALLOC;
    if (!config.debug) {
        jpr_panel.status[0] = 0;
        return;
    }
    if (status_wait) {
        status_wait--;
        return;
    }
    status_wait = STATUS_EVERY;
    svcGetSystemInfo(&used, 0, 1);
    {
        s32 arc = msg->id.arc;
        u32 dat = msg->id.dat;
        /* a message built in memory still reads "mem", with the file its text was found in: mem/27/0 */
        if (jpr_annot.seq == msg->seq) jpr_voice_origin(msg->id.arc, msg->id.dat, jpr_annot.arc, jpr_annot.dat, &arc, &dat);
        jpr_status_line(jpr_panel.status, JPR_STATUS_MAX, (total - (u32)used) >> 10, msg->id.arc, dat, msg->id.line,
                        jpr_panel.gr.code, gr_detail, jpr_panel.st.draw_ticks, fill_ticks, copy_ticks);
    }
    jpr_status_voice(jpr_panel.status, JPR_STATUS_MAX, jpr_voice_fault());
}

/* Copies n bytes (a multiple of 32, word-aligned) a word at a time, as showing a ready image would. */
static void copy_words(u32 *d, const u32 *s, u32 n) {
    for (n >>= 5; n; n--, d += 8, s += 8) {
        u32 a = s[0], b = s[1], c = s[2], e = s[3], f = s[4], g = s[5], h = s[6], k = s[7];
        d[0] = a; d[1] = b; d[2] = c; d[3] = e; d[4] = f; d[5] = g; d[6] = h; d[7] = k;
    }
}

/* On the frame the debug line was refreshed, what a full-screen fill and a half-screen copy cost on
 * their own; the panel's drawing covers both straight after. */
static void price_drawing(const struct jpr_surface *s) {
    u32 half = (jpr_surface_bytes(s) / 2) & ~31u, start;
    if (!config.debug || status_wait != STATUS_EVERY || ((u32)s->base & 3)) return;
    start = (u32)svcGetSystemTick();
    jpr_fill(s, 0, 0, (s32)s->width, (s32)s->height, JPR_COLOR_BG);
    fill_ticks = (u32)svcGetSystemTick() - start;
    start = (u32)svcGetSystemTick();
    copy_words((u32 *)(void *)(s->base + half), (const u32 *)(const void *)s->base, half);
    copy_ticks = (u32)svcGetSystemTick() - start;
}

/* The panel's picture kept in its own buffer: redrawn only when something on it changed, then copied
 * into the game's buffer a word at a time. On a 3DS drawing the text took about 2.6 ms of every
 * frame and the copy takes about 1.8 ms. 0 when there is no image buffer or the game's buffer does
 * not suit a word copy; the caller then draws straight into it. */
static int draw_cached(const struct jpr_surface *fb, const u16 *text) {
    static u32 drawn_sig, drawn_format, drawn_height, valid;
    struct jpr_surface image;
    u32 bytes = jpr_surface_bytes(fb), sig;
    if (!jpr_dbg.panel_image_addr || ((u32)fb->base & 3) || (bytes & 31) || bytes > JPR_PANEL_IMAGE_SIZE) return 0;
    sig = jpr_panel_signature(&jpr_panel);
    if (!valid || sig != drawn_sig || fb->format != drawn_format || fb->height != drawn_height) {
        image = *fb;
        image.base = (u8 *)jpr_dbg.panel_image_addr;
        jpr_panel_draw(&jpr_panel, &image, &font, text);
        drawn_sig = sig;
        drawn_format = fb->format;
        drawn_height = fb->height;
        valid = 1;
    }
    copy_words((u32 *)(void *)fb->base, (const u32 *)jpr_dbg.panel_image_addr, bytes);
    return 1;
}

/* Reads the selected unit's entry (and ending label) for its card. */
static void load_card(const u16 *text) {
    const struct jpr_unit *u = &jpr_panel.units[jpr_panel.st.selected - 1];
    u32 entry_len = 0, label_len = 0;
    int ok = jpr_data_entry(u->entry, jpr_panel.entry_bytes, JPR_ENTRY_MAX, &entry_len) == 0;
    if (ok && u->label != JPR_NO_LABEL)
        ok = jpr_data_label(u->label, jpr_panel.label_bytes, JPR_LABEL_MAX, &label_len) == 0;
    jpr_panel_card(&jpr_panel, &font, text, ok, entry_len, label_len);
}

/* Engine display swap, before it runs: regs[0] display, regs[1] left buffer, regs[3] column
 * height, regs[14] GL format. The GPU has finished with the buffer by now. */

static u32 shown_key;       /* the choice menu composed into the panel's text: 0 for none */

void jpr_h_present(u32 *regs) {
    struct jpr_surface s;
    const struct jpr_msg *msg;
    const struct jpr_unit *units;
    const u16 *text;
    const u8 *win;
    const struct jpr_peek_rows *rows = 0;
    u32 start, len, count, key;
    int dialog, have;

    if (regs[0] != DISPLAY_BOTTOM) return;
    if (!config_read && jpr_cap.seq) read_config();   /* at the first message, as before: the e2e writes it first */
    jpr_voice_frame(config_read ? config.voice : 0, jpr_panel.voice_request);   /* every frame: a clip plays on after its dialog closes */
    jpr_panel_voice_taken(&jpr_panel);
    jpr_panel.st.voice_available = jpr_voice_available();
    jpr_mix_frame();
    jpr_choices_frame();
    jpr_screen_frame();
#if JPR_DEBUG
    if (jpr_dbg.forget_reply) {         /* as a new conversation does: only replies.bin can answer now */
        jpr_panel.gr.answered_len = 0;
        jpr_dbg.forget_reply = 0;
    }
#endif
    dialog = jpr_cap.open && jpr_cap.seq && japanese();
    /* The grammar view stays up after the dialog ends, until Back (spec 8.3); the peek, until SELECT (stage 13). */
    if (!(dialog || ((jpr_panel.gr.open || jpr_panel.peek) && jpr_cap.seq)) ||
        !jpr_surface_init(&s, (void *)regs[1], regs[14], BOTTOM_WIDTH, regs[3])) {
        jpr_panel.st.visible = 0;
        jpr_panel_mix_close(&jpr_panel);    /* the dialog ended: sound.txt is written next frame if it changed */
        return;
    }
    msg = &jpr_cap.ring[(jpr_cap.seq - 1) & (JPR_RING - 1)];
    have = jpr_annot.seq == jpr_cap.seq;
    /* With a choice menu under the message: the message and its options (stage 12) */
    if (jpr_panel.peek)                     /* SELECT: what the game's screens show (stage 13) */
        jpr_screen_peek(msg, have ? jpr_annot.units : 0, have ? jpr_annot.count : 0, *(const volatile u8 *)LANG_BYTE,
                        &text, &len, &units, &count, &key, &rows);
    else
        jpr_choices_shown(msg, have ? jpr_annot.units : 0, have ? jpr_annot.count : 0, *(const volatile u8 *)LANG_BYTE,
                          &text, &len, &units, &count, &key);
    if (jpr_panel.st.msg_seq != jpr_cap.seq || shown_key != key ||
        (have && jpr_panel.st.annot_status != jpr_annot.status)) {
        jpr_panel_rows(&jpr_panel, rows);
        jpr_panel_message(&jpr_panel, jpr_cap.seq, text, len, &font, units, count,
                          have ? jpr_annot.status : JPR_ANNOT_NONE);
        shown_key = key;
    }
    win = dialog ? message_window() : 0;
    if (win) jpr_panel_box(&jpr_panel, win[MSGWIN_STATE], *(const u16 *)(win + MSGWIN_TAG));
    jpr_panel_touch(&jpr_panel, touch_down, touch_x, touch_y, &font, text, len);
    if (jpr_panel.card_wanted) load_card(text);
    kokoro_collect();
    grammar_frame();
    kokoro_schedule();
    kokoro_marks();
    update_status(msg);
    jpr_panel.st.visible = 1;
    price_drawing(&s);
    start = (u32)svcGetSystemTick();
    /* No cache flush after drawing: on a 3DS, flushing this buffer every frame breaks the game's
     * Internet connection, and the panel shows correctly without it. */
    if (!draw_cached(&s, text)) jpr_panel_draw(&jpr_panel, &s, &font, text);
    jpr_panel.st.draw_ticks = (u32)svcGetSystemTick() - start;
    jpr_panel.st.frames++;
    jpr_panel.st.fb_addr = regs[1];
    jpr_panel.st.fb_format = regs[14];
}

/* End of the touch device's poll: r1 points at {u16 x, u16 y, u8 touching}. */
void jpr_h_touch(u32 *regs) {
    u16 *xy = (u16 *)regs[1];
    u8 *down = (u8 *)regs[1] + 4;
#if JPR_DEBUG
    if (jpr_dbg.touch_polls) {
        xy[0] = (u16)jpr_dbg.touch_x;
        xy[1] = (u16)jpr_dbg.touch_y;
        *down = 1;
        jpr_dbg.touch_polls--;
    }
#endif
    touch_down = *down;
    touch_x = xy[0];
    touch_y = xy[1];
    if (jpr_touch_hide(&touch_gate, *down, jpr_panel.st.visible)) {
        *down = 0; /* the panel has this press: the game sees no touch */
        jpr_dbg.touch_hidden++;
    } else if (*down) {
        jpr_dbg.touch_passed++;
    }
}
