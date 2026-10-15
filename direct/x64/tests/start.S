.text
.globl _start
.type _start,@function
_start:
    mov %rsp, %rdi
    and $-16, %rsp
    call probe_main
    mov %eax, %edi
    mov $60, %eax
    syscall
    ud2
.size _start,.-_start
.globl immediate_probe
.type immediate_probe,@function
immediate_probe:
    mov $0x050f, %eax
    ret
.size immediate_probe,.-immediate_probe

.globl red_zone_probe
.type red_zone_probe,@function
red_zone_probe:
    # Diagnostic only: deliberately exercise SysV's 128-byte red zone across
    # the exception gateway. Production support must not assume its survival.
    mov $16, %r8
    lea -128(%rsp), %r9
1:
    movq $0x12345678, (%r9)
    add $8, %r9
    dec %r8
    jnz 1b
    mov $39, %eax
    syscall
    mov $16, %r8
    lea -128(%rsp), %r9
2:
    cmpq $0x12345678, (%r9)
    jne 3f
    add $8, %r9
    dec %r8
    jnz 2b
    mov $1, %eax
    ret
3:
    xor %eax, %eax
    ret
.size red_zone_probe,.-red_zone_probe
.section .note.GNU-stack,"",@progbits
.text
.globl gs_bridge_probe
.type gs_bridge_probe,@function
gs_bridge_probe:
    mov %rsp,%rsi
    mov $0x12345678,%r11
    mov $0x23456789,%r10
    xor %eax,%eax
    cmp %eax,%eax
    pushq %gs:8
    jne 4f
    pop %rdx
    cmp 8(%rdi),%rdx
    jne 4f
    cmp %rsp,%rsi
    jne 4f
    cmp $0x12345678,%r11
    jne 4f
    cmp $0x23456789,%r10
    jne 4f
    mov %rsp,%gs:16
    cmp 16(%rdi),%rsp
    jne 4f
    mov $8,%eax
    mov %gs:(%eax),%rdx
    cmp 8(%rdi),%rdx
    jne 4f
    xor %eax,%eax
    ret
4:
    mov %rsi,%rsp
    mov $1,%eax
    ret
.size gs_bridge_probe,.-gs_bridge_probe
