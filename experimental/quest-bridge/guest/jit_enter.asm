; qb_jit_enter(GuestCpu* cpu, BlockFn fn): runs translated code with the
; callee-saved registers free for it to use (jit.cpp keeps guest registers
; there). Their saving is described to the unwinder, so a guest fault that
; unwinds out of translated code through here restores them.
.code
qb_jit_enter PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 40
    .allocstack 40
    .endprolog
    call rdx
    add rsp, 40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
qb_jit_enter ENDP
END
