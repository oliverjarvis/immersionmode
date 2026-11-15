@ The game is hard-float and the payload soft-float: these move floats between s0 and core registers
@ (stage 11; research: docs/stage11-sound-findings.md). The C side never touches VFP registers.
    .arm
    .fpu vfpv2
    .section .text.mix, "ax", %progbits

@ Replaces the game's "set player volume" (0x3ba324): (sys, label, s0 = v).
    .global jpr_h_player_vol_set_asm
    .type jpr_h_player_vol_set_asm, %function
jpr_h_player_vol_set_asm:
    vmov  r2, s0
    b     jpr_h_player_vol_set          @ void (void *sys, u32 label, u32 vbits)

@ Replaces the game's "get player volume" (0x4b7a90): (sys, label) -> s0.
    .global jpr_h_player_vol_get_asm
    .type jpr_h_player_vol_get_asm, %function
jpr_h_player_vol_get_asm:
    push  {r4, lr}
    bl    jpr_h_player_vol_get          @ u32 (void *sys, u32 label): the float's bits
    vmov  s0, r0
    pop   {r4, pc}

@ void jpr_call_f32(void *fn, u32 a0, u32 a1, u32 fbits): calls a game function taking (a0, a1, s0).
    .global jpr_call_f32
    .type jpr_call_f32, %function
jpr_call_f32:
    mov   ip, r0
    mov   r0, r1
    mov   r1, r2
    vmov  s0, r3
    bx    ip
