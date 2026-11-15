#include "snd.h"

#include "game_addrs.h"
#include "debug.h"
#include "mix.h"
#include "svc.h"

#define LIN_SIZE 0x20000
#define VOICE_PRIORITY 0x7FFE        /* below the game's own 0x7FFF voices, which are never dropped */

typedef void *(*alloc_fn)(s32 priority, void (*dropped)(void *, u32), u32 arg);
typedef void (*voice_fn)(void *voice);
typedef void (*voice_int_fn)(void *voice, s32 value);
typedef void (*append_fn)(void *voice, struct jpr_wave *w);
typedef void (*wave_fn)(struct jpr_wave *w);
typedef void (*flush_fn)(u32 address, u32 bytes);
typedef void (*plain_fn)(void);

#define CALL(type, addr) ((type)(addr))

static u32 lin_base, lin_used;

void *jpr_lin_alloc(u32 bytes) {
    u32 at;
    if (!lin_base) {
        u32 va = 0;
        /* MEMOP_ALLOC | MEMOP_LINEAR, read and write: physically contiguous, so the DSP can read it */
        if (svcControlMemory(&va, 0, 0, LIN_SIZE, 0x10003, 3) || !va) return 0;
        lin_base = va;
        jpr_dbg.lin_addr = va;
    }
    at = (lin_used + 31) & ~31u;
    if (at + bytes > LIN_SIZE) return 0;
    lin_used = at + bytes;
    return (void *)(lin_base + at);
}

u32 jpr_dsp_address(u32 va, u32 bytes) {
    jpr_dbg.dsp_calls++;
    const volatile u32 *devmem = (const volatile u32 *)JPR_SND_DEVMEM;
    u32 base = devmem[0], size = devmem[1], end = va + bytes;
    int in_devmem = base <= va && end <= base + size && va < end;
    int in_ours = lin_base && lin_base <= va && end <= lin_base + LIN_SIZE && va < end;
    if (!in_devmem && !in_ours) return 0;
    return va + *(const volatile u32 *)JPR_SND_LINEAR_OFFSET;
}

void jpr_snd_set_volume(void *voice, u32 fbits) {
    jpr_call_f32(JPR_SND_SET_VOLUME, (u32)voice, 0, fbits);
}

static void lock(void) {
    CALL(plain_fn, JPR_SND_LOCK)();
}

static void unlock(void) {
    CALL(plain_fn, JPR_SND_UNLOCK)();
}

void jpr_snd_lock(void) {
    lock();
}

void jpr_snd_unlock(void) {
    unlock();
}

void *jpr_snd_voice_alloc(void (*dropped)(void *voice, u32 arg), u32 arg) {
    void *voice;
    lock();
    voice = CALL(alloc_fn, JPR_SND_ALLOC_VOICE)(VOICE_PRIORITY, dropped, arg);
    if (voice) {
        CALL(voice_int_fn, JPR_SND_SET_CHANNELS)(voice, 1);
        CALL(voice_int_fn, JPR_SND_SET_FORMAT)(voice, 1);   /* PCM16 */
    }
    unlock();
    return voice;
}

void jpr_snd_voice_free(void *voice) {
    lock();
    CALL(voice_fn, JPR_SND_FREE_VOICE)(voice);
    unlock();
}

void jpr_snd_set_rate(void *voice, u32 rate) {
    lock();
    CALL(voice_int_fn, JPR_SND_SET_RATE)(voice, (s32)rate);
    unlock();
}

void jpr_snd_wave_init(struct jpr_wave *w) {
    CALL(wave_fn, JPR_SND_INIT_WAVE)(w);
}

void jpr_snd_append(void *voice, struct jpr_wave *w) {
    CALL(flush_fn, JPR_SND_FLUSH)((u32)w->address, (u32)w->samples * 2);
    lock();
    CALL(append_fn, JPR_SND_APPEND)(voice, w);
    unlock();
}

void jpr_snd_play(void *voice) {
    lock();
    CALL(voice_int_fn, JPR_SND_SET_STATE)(voice, 0);
    unlock();
}

void jpr_snd_stop(void *voice) {
    lock();
    CALL(voice_int_fn, JPR_SND_SET_STATE)(voice, 1);
    unlock();
}

static const u8 *impl_of(const void *voice) {
    return *(const u8 *const *)((const u8 *)voice + 0x68);
}

u32 jpr_snd_position(const void *voice) {
    return *(const volatile u32 *)(impl_of(voice) + 0x08);
}

u32 jpr_snd_active(const void *voice) {
    return *(const volatile u8 *)(impl_of(voice) + 0x0C);
}

u32 jpr_snd_fed(const void *voice) {
    return (u32)*(const volatile s16 *)(impl_of(voice) + 0x34);
}
