; Windows x64 ABI boundary for Digitalis's private generated-code convention.
; RBP points at ThreadState; [RSP] is the translator's eight-byte scratch area.
; Generated regions contain no native calls and jump to digitalis_leave.
PUBLIC digitalis_enter
PUBLIC digitalis_leave
PUBLIC digitalis_dispatch
.code
digitalis_enter PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rdi
    .pushreg rdi
    push rsi
    .pushreg rsi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 184
    .allocstack 184
    movdqu [rsp+16], xmm6
    .savexmm128 xmm6, 16
    movdqu [rsp+32], xmm7
    .savexmm128 xmm7, 32
    movdqu [rsp+48], xmm8
    .savexmm128 xmm8, 48
    movdqu [rsp+64], xmm9
    .savexmm128 xmm9, 64
    movdqu [rsp+80], xmm10
    .savexmm128 xmm10, 80
    movdqu [rsp+96], xmm11
    .savexmm128 xmm11, 96
    movdqu [rsp+112], xmm12
    .savexmm128 xmm12, 112
    movdqu [rsp+128], xmm13
    .savexmm128 xmm13, 128
    movdqu [rsp+144], xmm14
    .savexmm128 xmm14, 144
    movdqu [rsp+160], xmm15
    .savexmm128 xmm15, 160
    .endprolog
    mov [rsp+8], r8
    mov rbp, rcx
    jmp rdx
; Chain cached regions without saving/restoring the Windows ABI registers or
; returning through a C++ hash lookup for every guest branch. All guest
; registers are already spilled by the region exit. The shared stack scratch
; is [rsp+0..7]; the dispatch context lives in the separate [rsp+8..15] slot.
digitalis_dispatch LABEL NEAR
    mov r10, [rsp+8]
    inc dword ptr [r10+28]
    dec dword ptr [r10+24]
    jz digitalis_leave
    ; Optimized loops exit here when their budget runs out; an interrupt
    ; sets it to one, so refill it and let the interrupt check decide.
    mov rcx, [r10+32]
    cmp dword ptr [rcx], 0
    jne digitalis_budget_ok
    mov dword ptr [rcx], 4096
digitalis_budget_ok LABEL NEAR
    mov rcx, [r10+16]
    cmp dword ptr [rcx], 0
    jne digitalis_leave
    mov rcx, [r10+8]
    cmp byte ptr [rcx], 0
    jne digitalis_leave
    mov rax, [rbp+784]
    ; Final blocks first (16-byte entries), then those still counting (32-byte).
    mov ecx, eax
    and ecx, dword ptr [r10+40]
    mov rdx, [r10+48]
    cmp [rdx+rcx*4], rax
    jne digitalis_counting
    jmp qword ptr [rdx+rcx*4+8]
digitalis_counting LABEL NEAR
    shl rcx, 3
    add rcx, [r10]
    cmp [rcx], rax
    jne digitalis_leave
    mov rdx, [rcx+8]
    test rdx, rdx
    jz digitalis_leave
    mov r8, [rcx+16]
    cmp dword ptr [r8], -1
    je digitalis_chain
    cmp dword ptr [r8], 0
    je digitalis_leave
    dec dword ptr [r8]
digitalis_chain LABEL NEAR
    jmp rdx
digitalis_leave LABEL NEAR
    movdqu xmm6, [rsp+16]
    movdqu xmm7, [rsp+32]
    movdqu xmm8, [rsp+48]
    movdqu xmm9, [rsp+64]
    movdqu xmm10, [rsp+80]
    movdqu xmm11, [rsp+96]
    movdqu xmm12, [rsp+112]
    movdqu xmm13, [rsp+128]
    movdqu xmm14, [rsp+144]
    movdqu xmm15, [rsp+160]
    add rsp, 184
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret
digitalis_enter ENDP
END
