// Calling AXRB Direct's host from guest code.
//
// AXRB_HOST_FUNCTION(symbol, "host.name") defines a guest function `symbol`
// that traps to the host function registered under "host.name" (svc #3 with
// the name in x17). Arguments and results follow AAPCS64 unchanged: the host
// reads x0-x7/v0-v7/the stack and writes x0/v0, so a stub can stand in for any
// C function signature, variadic ones included.
#pragma once

#define AXRB_STR2(x) #x
#define AXRB_STR(x) AXRB_STR2(x)

#if defined(__x86_64__)
// Experimental ART platform bridge only: at most five integer/pointer args.
// General NDK signatures need typed wrappers; this is not a universal ABI shim.
#define AXRB_HOST_FUNCTION(symbol, host_name) \
    __asm__(".text\n.globl " #symbol "\n.type " #symbol ", @function\n" #symbol ":\n" \
            "mov %rcx, %r10\nlea 1f(%rip), %r9\nmov $0x41585244, %eax\nsyscall\nret\n" \
            ".size " #symbol ", . - " #symbol "\n.pushsection .rodata\n1: .asciz \"" host_name "\"\n.popsection\n")
#else
#define AXRB_HOST_FUNCTION(symbol, host_name)                     \
    __asm__(".text\n"                                             \
            ".balign 4\n"                                         \
            ".globl " #symbol "\n"                                \
            ".type " #symbol ", %function\n" #symbol ":\n"        \
            "    adr x17, 1f\n"                                   \
            "    svc #3\n"                                        \
            "    ret\n"                                           \
            "1:  .asciz \"" host_name "\"\n"                      \
            "    .balign 4\n"                                     \
            ".size " #symbol ", . - " #symbol "\n")
#endif
