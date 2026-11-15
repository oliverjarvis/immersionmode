@ Loader stub. Lives in the unused tail of the game's text segment, which is
@ already executable. The game's entry instruction is replaced by a branch here.
@
@ The payload image travels in the game's bss pages (STAGE_BASE), which the game
@ clears later in its own start-up. The stub moves it to fresh memory at
@ PAYLOAD_BASE first: nothing may live just past the game's image, because the
@ game maps its CRO modules there.
@ PAYLOAD_BASE, PAYLOAD_SIZE, STAGE_BASE, STAGE_SIZE, ENTRY_TARGET and
@ ENTRY_RETURN come from the Makefile.
    .arm
    .text
    .global jpr_stub
    .type jpr_stub, %function
jpr_stub:
    push  {r0-r12, lr}
    mov   r0, #3                @ MEMOP_ALLOC
    ldr   r1, =PAYLOAD_BASE
    mov   r2, #0
    ldr   r3, =PAYLOAD_SIZE
    mov   r4, #3                @ read, write
    svc   0x01                  @ svcControlMemory
    cmp   r0, #0
    bne   fail
    ldr   r0, =STAGE_BASE
    ldr   r1, =PAYLOAD_BASE
    ldr   r2, =STAGE_SIZE
copy:
    ldmia r0!, {r3, r4, r5, r7}
    stmia r1!, {r3, r4, r5, r7}
    subs  r2, r2, #16
    bne   copy
    ldr   r1, =0xFFFF8001       @ the current process's pseudo-handle, which svcControlProcessMemory
    svc   0x27                  @ refuses on a 3DS (0xD9001BF7): svcDuplicateHandle gives a real one
    cmp   r0, #0
    bne   no_handle
    mov   r8, r1                @ the real handle, for the flush and the protection change below
    mov   r0, r8
    ldr   r1, =PAYLOAD_BASE
    ldr   r2, =STAGE_SIZE
    svc   0x54                  @ svcFlushProcessDataCache: the copy must reach memory before it runs
    mov   r0, r8
    ldr   r1, =PAYLOAD_BASE
    mov   r2, #0
    ldr   r3, =PAYLOAD_SIZE
    mov   r4, #6                @ change protection
    mov   r5, #7                @ read, write, execute
    svc   0x70                  @ svcControlProcessMemory
    mov   r6, r0
    cmp   r0, #0                @ 0: done
    cmpne r0, r8                @ registers left untouched: Azahar, which does not implement 0x70
    bne   refused
    mov   r0, r6
    ldr   r2, =PAYLOAD_BASE     @ jpr_init is the first code in the payload
    blx   r2                    @ jpr_init(result of the protection call)
    pop   {r0-r12, lr}
    ldr   lr, =ENTRY_RETURN     @ carry out the entry instruction this stub replaced:
    ldr   pc, =ENTRY_TARGET     @ bl ENTRY_TARGET
fail:
    mov   r0, #0
    svc   0x3C                  @ svcBreak: the hooks would jump into unmapped memory
refused:
    mov   r0, #1
    svc   0x3C                  @ svcBreak: the payload could not be made executable
no_handle:
    mov   r0, #2
    svc   0x3C                  @ svcBreak: no handle for the current process
    .ltorg
