"""Differential fuzzer: generates single-instruction tests for arm64.

Random instruction words are drawn per encoding class, run through LLVM's
disassembler to keep only real instructions, and filtered to what a user
process can execute safely and deterministically (no branches, system
registers, SP writes, PC-relative addressing, or memory tagging). Loads and
stores get their base register forced to x27, which the harness points at a
scratch buffer.

Each surviving word becomes an assembly function that loads a whole register
state from memory, executes the one instruction, and stores the state back.
The same harness then runs natively on a real device and inside the
interpreter, and the outputs are compared line by line.

    python tools/fuzz/gen.py <count> <seed> <out dir>
"""
import os
import random
import re
import subprocess
import sys
import tempfile

NDK = r'C:/Users/joshu/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'
CLANG = NDK + '/clang.exe'
OBJDUMP = NDK + '/llvm-objdump.exe'

count, seed, out_dir = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
rng = random.Random(seed)

# Top-level encoding classes, as (mask, value) on bits 28:25, and a weight.
CLASSES = [
    ('dp_imm', 0b1000, 3),   # 100x: data processing, immediate
    ('dp_reg', 0b0101, 4),   # x101: data processing, register
    ('simd', 0b0111, 6),     # x111: SIMD and floating point
    ('ldst', 0b0100, 4),     # x1x0: loads and stores
]

def candidate():
    total = sum(w for _, _, w in CLASSES)
    pick = rng.uniform(0, total)
    for name, op0, weight in CLASSES:
        if pick < weight:
            break
        pick -= weight
    word = rng.getrandbits(32)
    if name == 'dp_imm':
        word = (word & ~(0b1110 << 25)) | (0b1000 << 25)
    elif name == 'dp_reg':
        word = (word & ~(0b0111 << 25)) | (0b0101 << 25)
    elif name == 'simd':
        word = (word & ~(0b0111 << 25)) | (0b0111 << 25)
    else:
        word = (word & ~(0b0101 << 25)) | (0b0100 << 25)
        word = (word & ~(31 << 5)) | (27 << 5)  # base register x27
    return name, word

BANNED_MNEMONIC = re.compile(
    r'^(b|bl|br|blr|ret|cbz|cbnz|tbz|tbnz|b\..*|bc\..*|svc|hvc|smc|brk|hlt|dcps.*|eret.*|drps|msr|mrs|sys|sysl|'
    r'hint|yield|wfe|wfi|wfet|wfit|sev|sevl|pac.*|aut.*|xpac.*|bti|ldraa|ldrab|retaa|retab|stg|st2g|stzg|stz2g|'
    r'ldg|stgp|irg|addg|subg|gmi|subp|subps|ld64b|st64b.*|cpy.*|set.*|rcw.*|ldxr.*|stxr.*|ldaxr.*|stlxr.*|'
    r'ldxp|stxp|ldaxp|stlxp|adr|adrp|udf|tstart|tcommit|ttest|tcancel|chkfeat|gcs.*|at|dc|ic|tlbi|'
    r'axflag|xaflag|cfinv|rmif|setf8|setf16|prfm|prfum|isb|dsb|dmb|clrex|sb|ssbb|pssbb|csdb|esb|psb|tsb|'
    r'stllr.*|ldlar.*|casp.*|caspa.*|caspl.*|caspal.*|ldapr.*|stlur.*|ldapur.*|fcmla|fcadd|fjcvtzs|frint32.|frint64.|bf.*|.*mmla|sm3.*|sm4.*|sha512.*|eor3|rax1|xar|bcax|usdot|sudot)$')

def acceptable(name, text):
    parts = text.split(None, 1)
    mnemonic = parts[0]
    operands = parts[1] if len(parts) > 1 else ''
    if mnemonic in ('<unknown>', '.inst', 'udf') or BANNED_MNEMONIC.match(mnemonic):
        return False
    if re.search(r'\b(w?sp|z\d+|p\d+|za\w*|zt0)\b', operands):
        return False
    # Half precision, BFloat16 and friends are not claimed by the interpreter.
    if mnemonic.startswith(('f', 'bf', 'scvtf', 'ucvtf')) and re.search(r'\bh\d+\b|\.\d*h\b', operands):
        return False
    if re.search(r'\bh\d+\b', operands) and not mnemonic.startswith(('ldr', 'str', 'ldur', 'stur', 'dup', 'mov', 'ins', 'umov', 'smov')):
        return False
    if name == 'ldst':
        # A load pair into one register twice is architecturally
        # unpredictable, and real cores trap it.
        registers = re.findall(r'\b([wxsdq]\d+|[wx]zr)\b', operands.split('[')[0])
        if mnemonic.startswith('ld') and len(registers) >= 2 and registers[0][1:] == registers[1][1:]:
            return False
        # Only x27-based addressing, and x27 used nowhere else, so the address
        # stays inside the scratch buffer; nothing PC-relative.
        if '[x27' not in operands:
            return False
        if len(re.findall(r'\b[wx]27\b', operands)) != 1:
            return False
    return True

def disassemble(words):
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, 'w.s')
        obj = os.path.join(tmp, 'w.o')
        with open(source, 'w') as f:
            f.write('.text\n')
            for w in words:
                f.write('.inst 0x%08x\n' % w)
        subprocess.run([CLANG, '--target=aarch64-linux-android29', '-c', source, '-o', obj], check=True)
        out = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', '--mattr=+v8.2a,+lse,+rdm,+dotprod,+fp16,+crc,+aes,+sha2,+rcpc,+flagm',
                              obj], capture_output=True, text=True, check=True).stdout
    texts = []
    for line in out.splitlines():
        m = re.match(r'\s*[0-9a-f]+:\s+(.*)$', line)
        if m:
            texts.append(m.group(1).split('//')[0].strip())
    return texts

def branch_tests(n):
    # Conditional branches that jump over one marker instruction when taken
    # (offset +8), so the marker register shows the decision.
    out = []
    for _ in range(n):
        kind = rng.randrange(3)
        rt = rng.choice([r for r in range(28) if r != 27])
        if kind == 0:
            word = 0x54000000 | (2 << 5) | rng.randrange(15)            # b.cond
        elif kind == 1:
            word = (rng.getrandbits(1) << 31) | 0x34000000 | (rng.getrandbits(1) << 24) | (2 << 5) | rt  # cbz/cbnz
        else:
            bit = rng.randrange(64)
            word = ((bit >> 5) << 31) | 0x36000000 | (rng.getrandbits(1) << 24) | ((bit & 31) << 19) | (2 << 5) | rt
        out.append(('branch', word, 'branch %08x' % word))
    return out

picked = []
seen = set()
# FUZZ_WORDS=file: test these instruction words (hex, one per line) instead
# of random ones, e.g. every distinct word of some mnemonics in a real library.
words_file = os.environ.get('FUZZ_WORDS')
if words_file:
    listed = [int(l.split()[0], 16) for l in open(words_file) if l.strip()]
    rng.shuffle(listed)
    listed = listed[:count]
    classed = []
    for w in listed:
        op0 = (w >> 25) & 0xf
        name = 'ldst' if (op0 & 0b0101) == 0b0100 else 'simd' if (op0 & 0b0111) == 0b0111 else 'dp_reg' if (op0 & 0b0111) == 0b0101 else 'dp_imm'
        if name == 'ldst':
            w = (w & ~(31 << 5)) | (27 << 5)
        classed.append((name, w))
    for (name, word), text in zip(classed, disassemble([w for _, w in classed])):
        if word in seen or not acceptable(name, text):
            continue
        seen.add(word)
        picked.append((name, word, text))
    count = len(picked)
while len(picked) < count:
    batch = [candidate() for _ in range(4000)]
    texts = disassemble([w for _, w in batch])
    for (name, word), text in zip(batch, texts):
        if word in seen or not acceptable(name, text):
            continue
        seen.add(word)
        picked.append((name, word, text))
        if len(picked) >= count:
            break

picked += branch_tests(max(1, count // 10))
os.makedirs(out_dir, exist_ok=True)
# State layout (bytes): x0..x30 at 0, nzcv at 248, v0..v31 at 256 (16 each).
with open(os.path.join(out_dir, 'fuzz_tests.S'), 'w', newline='\n') as f:
    f.write('.text\n')
    for i, (name, word, text) in enumerate(picked):
        f.write(f'''
    .globl fuzz_test_{i}
    .type fuzz_test_{i}, %function
    // {text}
fuzz_test_{i}:
    stp x29, x30, [sp, #-176]!
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    str x0, [sp, #160]
    mov x30, x0
''')
        for v in range(0, 32, 2):
            f.write(f'    ldp q{v}, q{v + 1}, [x30, #{256 + v * 16}]\n')
        f.write('    ldr x0, [x30, #248]\n    msr nzcv, x0\n')
        for r in range(0, 30, 2):
            f.write(f'    ldp x{r}, x{r + 1}, [x30, #{r * 8}]\n')
        f.write('    ldr x30, [x30, #240]\n')
        f.write(f'    .inst 0x{word:08x}\n')
        if name == 'branch':
            f.write('    movz x28, #0x1234\n')  # skipped when the branch is taken
        f.write('    str x30, [sp, #-16]!\n    ldr x30, [sp, #176]\n')
        for r in range(0, 30, 2):
            f.write(f'    stp x{r}, x{r + 1}, [x30, #{r * 8}]\n')
        f.write('    ldr x0, [sp], #16\n    str x0, [x30, #240]\n    mrs x0, nzcv\n    str x0, [x30, #248]\n')
        for v in range(0, 32, 2):
            f.write(f'    stp q{v}, q{v + 1}, [x30, #{256 + v * 16}]\n')
        f.write('    ldp x19, x20, [sp, #16]\n    ldp x21, x22, [sp, #32]\n    ldp x23, x24, [sp, #48]\n'
                '    ldp x25, x26, [sp, #64]\n    ldp x27, x28, [sp, #80]\n    ldp d8, d9, [sp, #96]\n'
                '    ldp d10, d11, [sp, #112]\n    ldp d12, d13, [sp, #128]\n    ldp d14, d15, [sp, #144]\n'
                '    ldp x29, x30, [sp], #176\n    ret\n')

with open(os.path.join(out_dir, 'fuzz_list.h'), 'w', newline='\n') as f:
    f.write('/* Generated by tools/fuzz/gen.py. */\n')
    f.write(f'#define FUZZ_COUNT {len(picked)}\n')
    for i in range(len(picked)):
        f.write(f'void fuzz_test_{i}(void* state);\n')
    f.write('static void (*const fuzz_tests[])(void*) = {\n')
    for i in range(len(picked)):
        f.write(f'    fuzz_test_{i},\n')
    f.write('};\n')
    f.write('static const unsigned char fuzz_memory[] = {\n')
    for name, _, _ in picked:
        f.write('1,' if name == 'ldst' else '0,')
    f.write('\n};\n')
    f.write('static const char* const fuzz_text[] = {\n')
    for _, word, text in picked:
        f.write('    "%08x %s",\n' % (word, text.replace('\\', '\\\\').replace('"', "'")))
    f.write('};\n')
print(len(picked), 'tests;', {n: sum(1 for p in picked if p[0] == n) for n, _, _ in CLASSES})
