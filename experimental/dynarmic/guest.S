// Fixed offsets are relative to the harness code base (0x1000).
.text
// 0x000: xorshift64*; x0=seed, x1=iterations, x2=multiplier.
integer:
    eor x0, x0, x0, lsr #12
    eor x0, x0, x0, lsl #25
    eor x0, x0, x0, lsr #27
    mul x0, x0, x2
    subs x1, x1, #1
    b.ne integer
    svc #0
.org 0x100
// x0=data base, x1=iterations, x2=checksum; 1024 uint64 elements.
memory:
    and x3, x1, #1023
    ldr x4, [x0, x3, lsl #3]
    add x4, x4, x1
    str x4, [x0, x3, lsl #3]
    eor x2, x2, x4
    subs x1, x1, #1
    b.ne memory
    svc #0
.org 0x200
// v0=accumulator, v1=increment, x1=iterations.
simd:
    add v0.4s, v0.4s, v1.4s
    subs x1, x1, #1
    b.ne simd
    svc #0
.org 0x300
// Sequential exclusive-access smoke test, not a multicore proof.
exclusive:
    ldaxr x2, [x0]
    add x2, x2, #1
    stlxr w3, x2, [x0]
    cbnz w3, exclusive
    subs x1, x1, #1
    b.ne exclusive
    svc #0
.org 0x400
    casal x1, x2, [x0]
    svc #0
.org 0x500
    ldaddal x1, x2, [x0]
    svc #0
.org 0x600
    swpal x1, x2, [x0]
    svc #0
.org 0x700
    fadd d0, d0, d1
    fmul d0, d0, d1
    svc #0
.org 0x800
    mov x0, #1
    svc #0
