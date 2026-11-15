#ifndef JPR_SVC_H
#define JPR_SVC_H

#include "core/types.h"

#define MEMOP_ALLOC   3
#define MEMPERM_READ  1
#define MEMPERM_WRITE 2

s32 svcControlMemory(u32 *addr_out, u32 addr0, u32 addr1, u32 size, u32 op, u32 perm);
s32 svcGetSystemInfo(s64 *out, u32 type, s32 param);

s32 svcConnectToPort(u32 *out, const char *name);
s32 svcSendSyncRequest(u32 session);
s32 svcCloseHandle(u32 handle);
u32 *jpr_cmdbuf(void);

s64 svcGetSystemTick(void);

/* Threads, events and memory blocks, for the grammar request (spec 9.3). */
s32 svcCreateThread(u32 *thread, void (*entry)(void *), u32 arg, u32 *stack_top, s32 priority, s32 processor);
void svcExitThread(void);
void svcSleepThread(s64 ns);
s32 svcCreateEvent(u32 *event, u32 reset_type);        /* 0 one-shot */
s32 svcSignalEvent(u32 event);
s32 svcClearEvent(u32 event);
s32 svcWaitSynchronization(u32 handle, s64 ns);         /* 0x09401BFE when it timed out */
s32 svcCreateMemoryBlock(u32 *block, u32 addr, u32 size, u32 perm, u32 other_perm);
u32 *jpr_staticbufs(void);                              /* this thread's receive buffer descriptors (TLS + 0x180) */
#define JPR_WAIT_TIMEOUT 0x09401BFEu

#endif
