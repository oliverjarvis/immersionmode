    .arm
    .text

    .global svcControlMemory
    .type svcControlMemory, %function
svcControlMemory:
    push  {r0, r4}
    ldr   r0, [sp, #8]
    ldr   r4, [sp, #12]
    svc   0x01
    ldr   r2, [sp], #4
    str   r1, [r2]
    ldr   r4, [sp], #4
    bx    lr

    .global svcGetSystemInfo
    .type svcGetSystemInfo, %function
svcGetSystemInfo:
    str   r0, [sp, #-4]!
    svc   0x2A
    ldr   r3, [sp], #4
    str   r1, [r3]
    str   r2, [r3, #4]
    bx    lr

    .global svcConnectToPort
    .type svcConnectToPort, %function
svcConnectToPort:              @ (u32 *out, const char *name)
    str   r0, [sp, #-4]!
    svc   0x2D
    ldr   r3, [sp], #4
    str   r1, [r3]
    bx    lr

    .global svcSendSyncRequest
    .type svcSendSyncRequest, %function
svcSendSyncRequest:            @ (u32 session)
    svc   0x32
    bx    lr

    .global svcCloseHandle
    .type svcCloseHandle, %function
svcCloseHandle:                @ (u32 handle)
    svc   0x23
    bx    lr

    .global jpr_cmdbuf
    .type jpr_cmdbuf, %function
jpr_cmdbuf:                    @ this thread's IPC command buffer
    mrc   p15, 0, r0, c13, c0, 3
    add   r0, r0, #0x80
    bx    lr

    .global svcGetSystemTick
    .type svcGetSystemTick, %function
svcGetSystemTick:              @ () -> u64 in r0:r1
    svc   0x28
    bx    lr

    .global svcCreateThread
    .type svcCreateThread, %function
svcCreateThread:               @ (u32 *thread, entry, arg, stack_top, [sp] priority, [sp+4] processor)
    push  {r0, r4}
    ldr   r0, [sp, #8]
    ldr   r4, [sp, #12]
    svc   0x08
    ldr   r2, [sp], #4
    str   r1, [r2]
    ldr   r4, [sp], #4
    bx    lr

    .global svcExitThread
    .type svcExitThread, %function
svcExitThread:
    svc   0x09
    bx    lr

    .global svcSleepThread
    .type svcSleepThread, %function
svcSleepThread:                @ (s64 ns in r0:r1)
    svc   0x0A
    bx    lr

    .global svcCreateEvent
    .type svcCreateEvent, %function
svcCreateEvent:                @ (u32 *event, u32 reset_type)
    str   r0, [sp, #-4]!
    svc   0x17
    ldr   r2, [sp], #4
    str   r1, [r2]
    bx    lr

    .global svcSignalEvent
    .type svcSignalEvent, %function
svcSignalEvent:
    svc   0x18
    bx    lr

    .global svcClearEvent
    .type svcClearEvent, %function
svcClearEvent:
    svc   0x19
    bx    lr

    .global svcWaitSynchronization
    .type svcWaitSynchronization, %function
svcWaitSynchronization:        @ (u32 handle, s64 ns in r2:r3)
    svc   0x24
    bx    lr

    .global svcCreateMemoryBlock
    .type svcCreateMemoryBlock, %function
svcCreateMemoryBlock:          @ (u32 *block, addr, size, perm, [sp] other_perm)
    str   r0, [sp, #-4]!
    ldr   r0, [sp, #4]
    svc   0x1E
    ldr   r2, [sp], #4
    str   r1, [r2]
    bx    lr

    .global jpr_staticbufs
    .type jpr_staticbufs, %function
jpr_staticbufs:
    mrc   p15, 0, r0, c13, c0, 3
    add   r0, r0, #0x180
    bx    lr
