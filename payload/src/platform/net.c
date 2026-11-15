#include "net.h"

#include "core/ipc.h"
#include "svc.h"

#define TIMEREF_CNT 0x1FF81000u
#define TIMEREF_0 0x1FF81020u   /* two copies, 0x20 bytes each: u64 ms since 1900, u64 tick, ... */

void jpr_deadline_set(struct jpr_deadline *d, u32 ms, const volatile u32 *cancel, u32 id) {
    d->at = svcGetSystemTick() + (s64)ms * JPR_TICKS_PER_MS;
    d->cancel = cancel;
    d->id = id;
}

int jpr_deadline_check(const struct jpr_deadline *d) {
    if (d->cancel && *d->cancel == d->id) return JPR_CANCELLED;
    return svcGetSystemTick() >= d->at ? JPR_EXPIRED : JPR_LIVE;
}

s32 jpr_call(u32 session) {
    s32 r = svcSendSyncRequest(session);
    return r ? r : (s32)jpr_cmdbuf()[1];
}

s32 jpr_soc_call(u32 soc) {
    s32 r = jpr_call(soc);
    return r ? (r < 0 ? r : -r) : (s32)jpr_cmdbuf()[2];
}

int jpr_service(const char *name, u32 *session) {
    u32 *cmd = jpr_cmdbuf(), srv = 0;
    int err = -1;
    if (svcConnectToPort(&srv, "srv:")) return -1;
    jpr_ipc_srv_register(cmd);
    if (!jpr_call(srv)) {
        jpr_ipc_srv_get_service(cmd, name);
        if (!jpr_call(srv)) {
            *session = cmd[3];
            err = 0;
        }
    }
    svcCloseHandle(srv);
    return err;
}

u64 jpr_time_ms(void) {
    volatile u32 *cnt = (volatile u32 *)TIMEREF_CNT;
    u32 before, after;
    u64 ms;
    do {
        before = *cnt;
        ms = *(volatile u64 *)(TIMEREF_0 + 0x20 * (before & 1));
        after = *cnt;
    } while (before != after);
    return ms;
}

int jpr_random(u8 *out, u32 n) {
    u32 ssl = 0, k;
    u32 *cmd = jpr_cmdbuf();
    s64 tick = svcGetSystemTick();
    int err = -1;
    for (k = 0; k < n; k++) out[k] = 0;
    if (!jpr_service("ssl:C", &ssl)) {
        jpr_ipc_ssl_initialize(cmd);
        if (!jpr_call(ssl)) {
            jpr_ipc_ssl_random(cmd, (u32)out, n);
            if (!jpr_call(ssl)) err = 0;
        }
        svcCloseHandle(ssl);
    }
    for (k = 0; k < n && k < 8; k++) out[k] ^= (u8)(tick >> (8 * k));
    return err;
}
