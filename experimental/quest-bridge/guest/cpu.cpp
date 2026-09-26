#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <windows.h>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <algorithm>
#include <mutex>
#include "cpu.h"

#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Identity mapped, so translation is nothing at all. The first 64 kilobytes
   are never mapped on Windows either, which keeps a null pointer a null
   pointer; anything else that is wrong faults on the host and is caught by
   the guard around each step. */
uint8_t* guest_ptr(GuestMem& mem, uint64_t va, uint64_t bytes) {
    (void)mem;
    (void)bytes;
    if (va < 0x10000 || va >= 0x800000000000ull) return nullptr;
    return reinterpret_cast<uint8_t*>(va);
}

uint8_t* guest_reserve(uint64_t va, uint64_t size, bool commit) {
    void* got = VirtualAlloc(reinterpret_cast<void*>(va), (SIZE_T)size, MEM_RESERVE | (commit ? MEM_COMMIT : 0),
                             PAGE_READWRITE);
    if (got != reinterpret_cast<void*>(va)) {
        std::fprintf(stderr, "qb-guest: could not reserve guest memory at %llx (%llu bytes), error %lu\n",
                     (unsigned long long)va, (unsigned long long)size, GetLastError());
        /* Say what is in the way, which is the first thing anyone will ask. */
        for (uint64_t probe = va; probe < va + size;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(probe), &info, sizeof(info))) break;
            if (info.State != MEM_FREE) {
                char module[MAX_PATH] = "";
                GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), module, MAX_PATH);
                std::fprintf(stderr, "qb-guest:   %llx+%llx is taken (state %lx, type %lx) %s\n",
                             (unsigned long long)(uint64_t)info.BaseAddress, (unsigned long long)info.RegionSize,
                             info.State, info.Type, module);
            }
            probe = (uint64_t)info.BaseAddress + info.RegionSize;
        }
        std::fflush(stderr);
        std::abort();
    }
    return static_cast<uint8_t*>(got);
}

uint64_t guest_reserve_near(uint64_t preferred, uint64_t size, bool commit, uint64_t step) {
    for (uint64_t va = preferred; va + size < 0x7f0000000000ull; va += step) {
        void* got = VirtualAlloc(reinterpret_cast<void*>(va), (SIZE_T)size,
                                 MEM_RESERVE | (commit ? MEM_COMMIT : 0), PAGE_READWRITE);
        if (got == reinterpret_cast<void*>(va)) return va;
        if (got) VirtualFree(got, 0, MEM_RELEASE);
    }
    std::fprintf(stderr, "qb-guest: no room anywhere for %llu bytes of guest memory\n", (unsigned long long)size);
    std::abort();
}

bool guest_commit(uint64_t va, uint64_t size) {
    return VirtualAlloc(reinterpret_cast<void*>(va), (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

GuestBytes::~GuestBytes() {
    if (ptr && at) VirtualFree(ptr, 0, MEM_RELEASE);
    else delete[] ptr;
}

void GuestBytes::assign(size_t size, uint8_t fill) {
    if (ptr) {
        if (at) VirtualFree(ptr, 0, MEM_RELEASE);
        else delete[] ptr;
        ptr = nullptr;
    }
    n = size;
    if (!size) return;
    size_t rounded = (size + 0xffff) & ~(size_t)0xffff;
    ptr = at ? guest_reserve(at, rounded, true) : new uint8_t[size];
    /* Fresh commits are zero already; only another fill needs writing. */
    if (!at || fill) std::memset(ptr, fill, size);
}

static uint64_t xr(const GuestCpu& cpu, int r) { return r == 31 ? 0 : cpu.x[r]; }

static uint64_t x_or_sp(const GuestCpu& cpu, int r) { return r == 31 ? cpu.sp : cpu.x[r]; }

static void xw(GuestCpu& cpu, int r, uint64_t v) {
    if (r != 31) cpu.x[r] = v;
}

static void wsp(GuestCpu& cpu, int r, uint64_t v) {
    if (r == 31) cpu.sp = v;
    else cpu.x[r] = v;
}

static uint32_t load32(GuestMem& mem, uint64_t va) {
    uint8_t* p = guest_ptr(mem, va, 4);
    uint32_t w = 0;
    if (p) std::memcpy(&w, p, 4);
    return w;
}

static bool load(GuestMem& mem, uint64_t va, uint64_t* out, int bytes) {
    uint8_t* p = guest_ptr(mem, va, bytes);
    if (!p) return false;
    uint64_t v = 0;
    std::memcpy(&v, p, bytes);
    *out = v;
    return true;
}

uint64_t g_guest_watch = [] {
    const char* text = QB_ENV("QB_WATCH");
    return text ? std::strtoull(text, nullptr, 16) : 0ull;
}();
thread_local const GuestCpu* t_guest_cpu = nullptr;
std::atomic<uint64_t> g_guest_steps{0};

void guest_report_watch(uint64_t va, int bytes, uint64_t value) {
    const GuestCpu* cpu = t_guest_cpu;
    std::fprintf(stderr, "watch: %d bytes at %llx <- %llx by tp %llx at %s (lr %s)\n", bytes, (unsigned long long)va,
                 (unsigned long long)value, cpu ? (unsigned long long)cpu->tpidr : 0ull,
                 cpu ? guest_describe(cpu->pc).c_str() : "host", cpu ? guest_describe(cpu->x[30]).c_str() : "-");
}

static bool store(GuestMem& mem, uint64_t va, uint64_t v, int bytes) {
    uint8_t* p = guest_ptr(mem, va, bytes);
    if (!p) return false;
    if (g_guest_watch && g_guest_watch >= va && g_guest_watch < va + (uint64_t)bytes) guest_report_watch(va, bytes, v);
    std::memcpy(p, &v, bytes);
    return true;
}

static bool cond(const GuestCpu& cpu, int cc) {
    if (cc >= 14) return true;
    bool r = false;
    switch (cc & 0xe) {
        case 0: r = cpu.z; break;
        case 2: r = cpu.c; break;
        case 4: r = cpu.n; break;
        case 6: r = cpu.v; break;
        case 8: r = cpu.c && !cpu.z; break;
        case 10: r = cpu.n == cpu.v; break;
        case 12: r = !cpu.z && cpu.n == cpu.v; break;
        default: r = true; break;
    }
    if (cc & 1) r = !r;
    return r;
}

static uint64_t add_flags(GuestCpu& cpu, uint64_t a, uint64_t b, int bits, bool set) {
    uint64_t mask = bits == 64 ? ~0ull : 0xffffffffull;
    a &= mask;
    b &= mask;
    uint64_t r = (a + b) & mask;
    if (set) {
        cpu.z = r == 0;
        cpu.n = ((r >> (bits - 1)) & 1) != 0;
        cpu.c = r < a;
        int64_t sa = bits == 64 ? (int64_t)a : (int32_t)a;
        int64_t sb = bits == 64 ? (int64_t)b : (int32_t)b;
        int64_t sr = sa + sb;
        cpu.v = bits == 32 ? sr != (int32_t)sr : ((~(sa ^ sb) & (sa ^ sr)) < 0);
    }
    return r;
}

static uint64_t sub_flags(GuestCpu& cpu, uint64_t a, uint64_t b, int bits, bool set) {
    uint64_t mask = bits == 64 ? ~0ull : 0xffffffffull;
    a &= mask;
    b &= mask;
    uint64_t r = (a - b) & mask;
    if (set) {
        cpu.z = r == 0;
        cpu.n = ((r >> (bits - 1)) & 1) != 0;
        cpu.c = a >= b;
        int64_t sa = bits == 64 ? (int64_t)a : (int32_t)a;
        int64_t sb = bits == 64 ? (int64_t)b : (int32_t)b;
        int64_t sr = sa - sb;
        cpu.v = bits == 32 ? sr != (int32_t)sr : (((sa ^ sb) & (sa ^ sr)) < 0);
    }
    return r;
}

static uint64_t shift_reg(uint64_t v, int shift, int amount, int bits) {
    if (bits == 32) v &= 0xffffffffull;
    amount &= bits == 64 ? 63 : 31;
    switch (shift) {
        case 0: return v << amount;
        case 1: return bits == 64 ? v >> amount : (uint32_t)v >> amount;
        case 2: {
            int64_t s = bits == 64 ? (int64_t)v : (int32_t)v;
            return (uint64_t)(s >> amount);
        }
        default: {
            if (amount == 0) return v;
            if (bits == 32) {
                uint32_t x = (uint32_t)v;
                return (x >> amount) | (x << (32 - amount));
            }
            return (v >> amount) | (v << (64 - amount));
        }
    }
}

static thread_local unsigned long long g_steps = 0;

unsigned long long guest_steps() { return g_steps; }

static bool step(GuestCpu& cpu, GuestMem& mem) {
    ++g_steps;
    uint32_t insn = load32(mem, cpu.pc);
    uint64_t next = cpu.pc + 4;
    int sf = (insn >> 31) & 1;
    int bits = sf ? 64 : 32;

    /* The whole hint space is a no-op here: nop, yield, and the pointer
       authentication and branch target hints that shipped code is built with.
       We do not check signatures, so signing and authenticating are both
       nothing, and that is consistent as long as neither is enforced. */
    /* Prefetches, in all four addressing forms, only ever make things faster. */
    if ((insn & 0xffc00000u) == 0xf9800000u || (insn & 0xff000000u) == 0xd8000000u ||
        (insn & 0xffe00c00u) == 0xf8a00800u || (insn & 0xffe00c00u) == 0xf8800000u) {
        cpu.pc = next;
        return true;
    }
    if ((insn & 0xfffff01fu) == 0xd503201fu) {
        cpu.pc = next;
        return true;
    }

    /* The thread pointer. Thread local storage is all measured from here. */
    if ((insn & 0xffffffe0u) == 0xd53bd040u) { /* mrs Xt, tpidr_el0 */
        xw(cpu, insn & 31, cpu.tpidr);
        cpu.pc = next;
        return true;
    }
    if ((insn & 0xffffffe0u) == 0xd51bd040u) { /* msr tpidr_el0, Xt */
        cpu.tpidr = xr(cpu, insn & 31);
        cpu.pc = next;
        return true;
    }

    /* The other system registers user code reads, by their 15-bit encoding
       (op0 op1 CRn CRm op2). The ID registers are what Android's kernel
       answers when EL0 reads them: sanitised, and consistent with what
       HWCAP claims (FP, ASIMD, LSE atomics). */
    if ((insn & 0xfff00000u) == 0xd5300000u || (insn & 0xfff00000u) == 0xd5100000u) {
        bool read = (insn & 0x00200000u) != 0;
        int rt = insn & 31;
        uint32_t reg = (insn >> 5) & 0x7fff;
        const uint32_t kNzcv = 0x5a10, kFpcr = 0x5a20, kFpsr = 0x5a21, kCntfrq = 0x5f00, kCntpct = 0x5f01,
                       kCntvct = 0x5f02, kDczid = 0x5807, kTpidrro = 0x5e83, kMidr = 0x4000, kMpidr = 0x4005,
                       kRevidr = 0x4006, kPfr0 = 0x4020, kPfr1 = 0x4021, kIsar0 = 0x4030, kIsar1 = 0x4031,
                       kMmfr0 = 0x4038, kDfr0 = 0x4028;
        const uint64_t kCounterHz = 19200000; /* what the headset's generic timer runs at */
        if (read) {
            uint64_t value = 0;
            bool known = true;
            switch (reg) {
            case kNzcv:
                value = ((uint64_t)cpu.n << 31) | ((uint64_t)cpu.z << 30) | ((uint64_t)cpu.c << 29) |
                        ((uint64_t)cpu.v << 28);
                break;
            case kFpcr: value = cpu.fpcr; break;
            case kFpsr: value = cpu.fpsr; break;
            case kCntfrq: value = kCounterHz; break;
            case kCntpct:
            case kCntvct: {
                LARGE_INTEGER now, frequency;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&frequency);
                value = (uint64_t)((double)now.QuadPart * (double)kCounterHz / (double)frequency.QuadPart);
                break;
            }
            case kDczid: value = 4; break; /* DC ZVA allowed, on 64-byte blocks */
            case kTpidrro: value = 0; break;
            case kMidr: value = 0x411fd4b0; break; /* implementer 0x41, variant 1, part 0xd4b */
            case kMpidr: value = 0x80000000; break;
            case kRevidr: value = 0; break;
            case kPfr0: value = 0x0000000000000011ull; break; /* EL0 and EL1 AArch64; FP and AdvSIMD present */
            case kIsar0: value = 0x0000000000200000ull; break; /* LSE atomics */
            case kPfr1:
            case kIsar1:
            case kMmfr0:
            case kDfr0: value = 0; break;
            default: known = false;
            }
            if (!known) return false;
            xw(cpu, rt, value);
        } else {
            uint64_t value = xr(cpu, rt);
            switch (reg) {
            case kNzcv:
                cpu.n = (value >> 31) & 1;
                cpu.z = (value >> 30) & 1;
                cpu.c = (value >> 29) & 1;
                cpu.v = (value >> 28) & 1;
                break;
            case kFpcr: cpu.fpcr = value; break;
            case kFpsr: cpu.fpsr = value; break;
            default: return false;
            }
        }
        cpu.pc = next;
        return true;
    }

    /* Cache maintenance by address. DC ZVA zeroes a 64-byte block, which is
       what DCZID above promised; the rest keep caches coherent, and an
       interpreter's view of memory already is. */
    if ((insn & 0xffffffe0u) == 0xd50b7420u) { /* dc zva */
        uint64_t block = xr(cpu, insn & 31) & ~63ull;
        uint8_t* p = guest_ptr(mem, block, 64);
        if (!p) return false;
        std::memset(p, 0, 64);
        cpu.pc = next;
        return true;
    }
    if ((insn & 0xffffffe0u) == 0xd50b7e20u || (insn & 0xffffffe0u) == 0xd50b7b20u ||
        (insn & 0xffffffe0u) == 0xd50b7a20u || (insn & 0xffffffe0u) == 0xd50b7c20u ||
        (insn & 0xffffffe0u) == 0xd50b7520u || (insn & 0xffffffe0u) == 0xd50b7d20u) {
        cpu.pc = next; /* dc civac, cvau, cvac, cvap, cvadp; ic ivau */
        return true;
    }

    /* ADR / ADRP */
    if (((insn >> 24) & 0x1f) == 0x10) {
        int rd = insn & 31;
        uint32_t immhi = (insn >> 5) & 0x7ffff;
        uint32_t immlo = (insn >> 29) & 3;
        int64_t imm = (int64_t)((immhi << 2) | immlo);
        imm = (imm << 43) >> 43;
        uint64_t result = (insn & 0x80000000) ? ((cpu.pc & ~0xfffull) + ((uint64_t)imm << 12)) : (cpu.pc + imm);
        xw(cpu, rd, result);
        cpu.pc = next;
        return true;
    }

    /* ADD/SUB immediate */
    if (((insn >> 24) & 0x1f) == 0x11) {
        int op = (insn >> 30) & 1;
        int s = (insn >> 29) & 1;
        int shift = (insn >> 22) & 3;
        uint64_t imm = (insn >> 10) & 0xfff;
        if (shift == 1) imm <<= 12;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        /* Rn 31 is SP for every form here, flag-setting included (CMP SP,
           #imm); only the destination of ADDS/SUBS is the zero register. */
        uint64_t a = x_or_sp(cpu, rn);
        uint64_t r = op ? sub_flags(cpu, a, imm, bits, s) : add_flags(cpu, a, imm, bits, s);
        if (!s && bits == 32) r &= 0xffffffffull;
        if (s) xw(cpu, rd, bits == 32 ? (r & 0xffffffffull) : r);
        else wsp(cpu, rd, bits == 32 ? (uint32_t)r : r);
        cpu.pc = next;
        return true;
    }

    /* Wide move */
    if (((insn >> 23) & 0x3f) == 0x25) {
        int opc = (insn >> 29) & 3;
        int hw = (insn >> 21) & 3;
        int sh = hw * 16;
        uint64_t imm = (uint64_t)((insn >> 5) & 0xffff) << sh;
        int rd = insn & 31;
        uint64_t cur = xr(cpu, rd);
        uint64_t r = 0;
        if (opc == 2) r = imm;
        else if (opc == 3) r = (cur & ~(0xffffull << sh)) | imm;
        else if (opc == 0) r = ~imm;
        else return false;
        if (bits == 32) r &= 0xffffffffull;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Bitfield (LSL is UBFM) */
    /* EXTR, and ror by an immediate, which is EXTR with both sources the same. */
    if (((insn >> 23) & 0x3f) == 0x27 && ((insn >> 29) & 3) == 0 && !((insn >> 21) & 1)) {
        int lsb = (insn >> 10) & 63;
        uint64_t high = xr(cpu, (insn >> 5) & 31);
        uint64_t low = xr(cpu, (insn >> 16) & 31);
        uint64_t value;
        if (bits == 32) {
            uint64_t both = ((high & 0xffffffffull) << 32) | (low & 0xffffffffull);
            value = (both >> (lsb & 31)) & 0xffffffffull;
        } else {
            value = lsb == 0 ? low : (low >> lsb) | (high << (64 - lsb));
        }
        xw(cpu, insn & 31, value);
        cpu.pc = next;
        return true;
    }
    if (((insn >> 23) & 0x3f) == 0x26) {
        int opc = (insn >> 29) & 3;
        int immr = (insn >> 16) & 63;
        int imms = (insn >> 10) & 63;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t src = xr(cpu, rn);
        if (bits == 32) {
            src &= 0xffffffffull;
            immr &= 31;
            imms &= 31;
        }
        /* SBFM, which is how sxtw, asr, sbfx and sbfiz are all written. */
        if (opc == 0) {
            uint64_t mask = 0;
            int width = 0;
            uint64_t value = 0;
            if (imms >= immr) {
                width = imms - immr + 1;
                value = src >> immr;
            } else {
                width = imms + 1;
                value = src << (bits - immr);
                width += bits - immr;
            }
            mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
            value &= mask;
            /* Sign extend from the top bit of the field. */
            if (width < 64 && (value >> (width - 1)) & 1) value |= ~mask;
            if (bits == 32) value &= 0xffffffffull;
            xw(cpu, rd, value);
            cpu.pc = next;
            return true;
        }

        /* BFM, which inserts a field and leaves the rest of the register. */
        if (opc == 1) {
            uint64_t kept = xr(cpu, rd);
            int width = 0;
            uint64_t value = 0;
            if (imms >= immr) {
                width = imms - immr + 1;
                value = src >> immr;
            } else {
                width = imms + 1;
                value = src << (bits - immr);
            }
            uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
            if (imms < immr) mask <<= bits - immr;
            else mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
            uint64_t result = (kept & ~mask) | (value & mask);
            if (bits == 32) result &= 0xffffffffull;
            xw(cpu, rd, result);
            cpu.pc = next;
            return true;
        }

        /* UBFM: lsr, lsl, ubfx, ubfiz, uxtb and uxth. */
        if (opc == 2) {
            uint64_t value;
            if (imms >= immr) {
                int width = imms - immr + 1;
                uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
                value = (src >> immr) & mask;
            } else {
                int width = imms + 1;
                uint64_t mask = (1ull << width) - 1;
                value = (src & mask) << (bits - immr);
            }
            if (bits == 32) value &= 0xffffffffull;
            xw(cpu, rd, value);
            cpu.pc = next;
            return true;
        }
        return false;
    }

    /* Logical immediate, including MOV of a repeating bitmask. */
    if (((insn >> 23) & 0x3f) == 0x24) {
        int opc = (insn >> 29) & 3;
        int nbit = (insn >> 22) & 1;
        int immr = (insn >> 16) & 63;
        int imms = (insn >> 10) & 63;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        unsigned combined = (nbit << 6) | ((~imms) & 63);
        unsigned len = 0;
        for (int bit = 6; bit >= 0; --bit)
            if (combined & (1u << bit)) {
                len = (unsigned)bit;
                break;
            }
        if (len == 0) return false;
        unsigned size = 1u << len;
        unsigned levels = size - 1;
        unsigned s = imms & levels;
        unsigned r = immr & levels;
        uint64_t elem = (s == 63) ? ~0ull : ((1ull << (s + 1)) - 1);
        if (r != 0) elem = (elem >> r) | (elem << (size - r));
        uint64_t imm = 0;
        for (unsigned i = 0; i < 64; i += size) imm |= elem << i;
        if (bits == 32) imm &= 0xffffffffull;
        uint64_t a = xr(cpu, rn);
        uint64_t result = 0;
        if (opc == 1) result = a | imm;
        else if (opc == 0) result = a & imm;
        else if (opc == 3) {
            result = a & imm;
            cpu.z = (result & (bits == 32 ? 0xffffffffull : ~0ull)) == 0;
            cpu.n = ((result >> (bits - 1)) & 1) != 0;
            cpu.c = false;
            cpu.v = false;
        } else result = a ^ imm;
        if (bits == 32) result &= 0xffffffffull;
        /* AND/ORR/EOR write SP when rd is 31 (a stack realignment is
           "and sp, x9, #-64"); only ANDS writes the zero register. */
        if (opc == 3) xw(cpu, rd, result);
        else wsp(cpu, rd, result);
        cpu.pc = next;
        return true;
    }

    /* Return, with an authenticated link register. Since nothing was really
       signed, these are the ordinary return. */
    if (insn == 0xd65f0bffu || insn == 0xd65f0fffu) {
        cpu.pc = xr(cpu, 30);
        return true;
    }

    /* Two sources: dividing, and shifting by an amount held in a register. */
    if (((insn >> 21) & 0x3ff) == 0x0d6) {
        int opcode = (insn >> 10) & 0x3f;
        int rm = (insn >> 16) & 31;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t a = xr(cpu, rn);
        uint64_t b = xr(cpu, rm);
        if (bits == 32) {
            a &= 0xffffffffull;
            b &= 0xffffffffull;
        }
        uint64_t r = 0;
        if (opcode == 2) { /* UDIV, which gives zero rather than trapping */
            r = b == 0 ? 0 : a / b;
        } else if (opcode == 3) { /* SDIV */
            int64_t sa = bits == 64 ? (int64_t)a : (int32_t)a;
            int64_t sb = bits == 64 ? (int64_t)b : (int32_t)b;
            /* The most negative number divided by -1 overflows: ARM answers
               with the dividend, x86 raises a divide error, so it never gets
               there. */
            if (sb == -1) r = (uint64_t)(0 - (uint64_t)sa);
            else r = sb == 0 ? 0 : (uint64_t)(sa / sb);
        } else if (opcode >= 8 && opcode <= 11) {
            r = shift_reg(a, opcode - 8, (int)(b & (bits == 64 ? 63 : 31)), bits);
        } else if (opcode >= 0x10 && opcode <= 0x17) {
            /* CRC32 and CRC32C, of a byte up to a doubleword. */
            int size = opcode & 3;
            bool castagnoli = (opcode >> 2) & 1;
            uint32_t crc = (uint32_t)xr(cpu, rn);
            uint64_t data = xr(cpu, rm);
            uint32_t polynomial = castagnoli ? 0x82f63b78u : 0xedb88320u;
            for (int i = 0; i < (8 << size); ++i) {
                uint32_t bit = (crc ^ (uint32_t)(data >> i)) & 1;
                crc = (crc >> 1) ^ (bit ? polynomial : 0);
            }
            r = crc;
            bits = 32;
        } else
            return false;
        if (bits == 32) r &= 0xffffffffull;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* One source: bit and byte reversal, and counting leading bits. */
    if (((insn >> 21) & 0x3ff) == 0x2d6) {
        /* Signing and authenticating a pointer leave it as it was. */
        if (((insn >> 16) & 0x1f) == 1) {
            int rn = (insn >> 5) & 31;
            int rd = insn & 31;
            xw(cpu, rd, xr(cpu, rn == 31 ? rd : rn));
            cpu.pc = next;
            return true;
        }
        int opcode = (insn >> 10) & 0x3f;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t src = xr(cpu, rn);
        if (bits == 32) src &= 0xffffffffull;
        uint64_t r = 0;
        if (opcode == 0) { /* RBIT */
            for (int i = 0; i < bits; ++i)
                if ((src >> i) & 1) r |= 1ull << (bits - 1 - i);
        } else if (opcode == 1) { /* REV16, bytes swapped inside each halfword */
            for (int i = 0; i < bits / 8; i += 2) {
                r |= ((src >> (i * 8)) & 0xff) << ((i + 1) * 8);
                r |= ((src >> ((i + 1) * 8)) & 0xff) << (i * 8);
            }
        } else if (opcode == 2 || opcode == 3) {
            /* REV32 reverses inside each word; REV reverses the whole thing.
               On a 32-bit register opcode 2 is already the whole thing. */
            int span = (opcode == 2 && bits == 64) ? 32 : bits;
            for (int at = 0; at < bits; at += span)
                for (int i = 0; i < span / 8; ++i) {
                    uint64_t byte = (src >> (at + i * 8)) & 0xff;
                    r |= byte << (at + span - 8 - i * 8);
                }
        } else if (opcode == 4 || opcode == 5) { /* CLZ and CLS */
            uint64_t value = opcode == 5 ? (((src >> (bits - 1)) & 1) ? ~src : src) : src;
            if (opcode == 5) value <<= 1;
            int count = 0;
            for (int i = bits - 1; i >= 0; --i) {
                if ((value >> i) & 1) break;
                ++count;
            }
            r = opcode == 5 ? (uint64_t)(count > 0 ? count - 1 : 0) : (uint64_t)count;
            if (opcode == 5) {
                /* CLS counts the sign bits after the first, so recompute plainly. */
                uint64_t same = src;
                int matched = 0;
                int top = (int)((same >> (bits - 1)) & 1);
                for (int i = bits - 2; i >= 0; --i) {
                    if ((int)((same >> i) & 1) != top) break;
                    ++matched;
                }
                r = (uint64_t)matched;
            }
        } else
            return false;
        if (bits == 32) r &= 0xffffffffull;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Logical shifted register: MOV / ORR */
    if (((insn >> 24) & 0x1f) == 0x0a) {
        int opc = (insn >> 29) & 3;
        int shift = (insn >> 22) & 3;
        int rm = (insn >> 16) & 31;
        int amt = (insn >> 10) & 63;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t a = xr(cpu, rn);
        uint64_t b = shift_reg(xr(cpu, rm), shift, amt, bits);
        /* Bit 21 negates the second operand, which is what turns ORR into ORN
           and so gives mvn, and AND into BIC. Ignoring it means every one of
           those quietly computes the opposite of what was asked. */
        if ((insn >> 21) & 1) b = ~b;
        if (bits == 32) b &= 0xffffffffull;
        uint64_t r = 0;
        if (opc == 1) r = a | b;
        else if (opc == 0 || opc == 3) r = a & b;
        else if (opc == 2) r = a ^ b;
        else return false;
        if (bits == 32) r &= 0xffffffffull;
        if (opc == 3) { /* ANDS, which is how tst is written */
            cpu.z = r == 0;
            cpu.n = ((r >> (bits - 1)) & 1) != 0;
            cpu.c = false;
            cpu.v = false;
        }
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Add/sub extended register. Bit 21 marks it. The second operand is a
       register widened by sign or zero extension and shifted left by up to
       four, and register 31 is the stack pointer here, not zero. Decoding
       this as the shifted form got the arithmetic wrong and, where sp was
       named, walked the stack pointer somewhere nothing was mapped. */
    if (((insn >> 24) & 0x1f) == 0x0b && ((insn >> 21) & 1) == 1) {
        int op = (insn >> 30) & 1;
        int s = (insn >> 29) & 1;
        int rm = (insn >> 16) & 31;
        int option = (insn >> 13) & 7;
        int amount = (insn >> 10) & 7;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t raw = xr(cpu, rm);
        uint64_t b = 0;
        switch (option) {
            case 0: b = raw & 0xff; break;                          /* UXTB */
            case 1: b = raw & 0xffff; break;                        /* UXTH */
            case 2: b = raw & 0xffffffffull; break;                 /* UXTW */
            case 3: b = raw; break;                                 /* UXTX */
            case 4: b = (uint64_t)(int64_t)(int8_t)raw; break;      /* SXTB */
            case 5: b = (uint64_t)(int64_t)(int16_t)raw; break;     /* SXTH */
            case 6: b = (uint64_t)(int64_t)(int32_t)raw; break;     /* SXTW */
            default: b = raw; break;                                /* SXTX */
        }
        b <<= amount;
        uint64_t a = x_or_sp(cpu, rn);
        uint64_t r = op ? sub_flags(cpu, a, b, bits, s) : add_flags(cpu, a, b, bits, s);
        if (bits == 32) r &= 0xffffffffull;
        /* With flags set, register 31 is the zero register (cmp, cmn);
           without, it is the stack pointer. */
        if (s) xw(cpu, rd, r);
        else wsp(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Add/sub shifted register */
    if (((insn >> 24) & 0x1f) == 0x0b) {
        int op = (insn >> 30) & 1;
        int s = (insn >> 29) & 1;
        int shift = (insn >> 22) & 3;
        int rm = (insn >> 16) & 31;
        int amt = (insn >> 10) & 63;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t a = xr(cpu, rn);
        uint64_t b = shift_reg(xr(cpu, rm), shift, amt, bits);
        uint64_t r = op ? sub_flags(cpu, a, b, bits, s) : add_flags(cpu, a, b, bits, s);
        if (bits == 32) r &= 0xffffffffull;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Conditional compare: compare only when the condition holds, and take
       the flags straight from the instruction when it does not. */
    if (((insn >> 21) & 0x3ff) == 0x1d2 || ((insn >> 21) & 0x3ff) == 0x3d2) {
        int op = (insn >> 30) & 1;
        int immediate = (insn >> 11) & 1;
        int cc = (insn >> 12) & 15;
        int rn = (insn >> 5) & 31;
        uint64_t nzcv = insn & 15;
        if (cond(cpu, cc)) {
            uint64_t a = xr(cpu, rn);
            uint64_t b = immediate ? (uint64_t)((insn >> 16) & 31) : xr(cpu, (insn >> 16) & 31);
            if (op) sub_flags(cpu, a, b, bits, true);
            else add_flags(cpu, a, b, bits, true);
        } else {
            cpu.n = (nzcv >> 3) & 1;
            cpu.z = (nzcv >> 2) & 1;
            cpu.c = (nzcv >> 1) & 1;
            cpu.v = nzcv & 1;
        }
        cpu.pc = next;
        return true;
    }

    /* Add and subtract with carry: ADC, ADCS, SBC, SBCS (and NGC, NGCS). */
    if ((insn & 0x1fe0fc00u) == 0x1a000000u) {
        bool subtract = (insn >> 30) & 1;
        bool set = (insn >> 29) & 1;
        int bits = (insn >> 31) ? 64 : 32;
        uint64_t mask = bits == 64 ? ~0ull : 0xffffffffull;
        uint64_t a = xr(cpu, (insn >> 5) & 31) & mask;
        uint64_t b = xr(cpu, (insn >> 16) & 31) & mask;
        if (subtract) b = ~b & mask; /* a - b - !C is a + ~b + C */
        uint64_t carry = cpu.c ? 1 : 0;
        uint64_t r = (a + b + carry) & mask;
        if (set) {
            cpu.n = (r >> (bits - 1)) & 1;
            cpu.z = r == 0;
            if (bits == 64) {
                uint64_t partial = a + b;
                cpu.c = partial < a || (partial + carry) < partial;
            } else {
                cpu.c = (a + b + carry) > 0xffffffffull;
            }
            int64_t sa = bits == 64 ? (int64_t)a : (int32_t)(uint32_t)a;
            int64_t sb = bits == 64 ? (int64_t)b : (int32_t)(uint32_t)b;
            int64_t sr = bits == 64 ? (int64_t)r : (int32_t)(uint32_t)r;
            cpu.v = ((sa < 0) == (sb < 0)) && ((sr < 0) != (sa < 0));
        }
        xw(cpu, insn & 31, r);
        cpu.pc = next;
        return true;
    }

    /* Conditional select */
    /* Bit 30 picks CSINV/CSNEG over CSEL/CSINC, so it cannot be part of the
       match; only bit 29 (set flags) has to be clear. */
    if (((insn >> 21) & 0xff) == 0xd4 && ((insn >> 29) & 1) == 0 && ((insn >> 11) & 1) == 0) {
        int op = (insn >> 30) & 1;
        int rm = (insn >> 16) & 31;
        int cc = (insn >> 12) & 15;
        int op2 = (insn >> 10) & 1;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t a = xr(cpu, rn);
        uint64_t b = xr(cpu, rm);
        if (bits == 32) {
            a &= 0xffffffffull;
            b &= 0xffffffffull;
        }
        uint64_t r;
        if (cond(cpu, cc)) r = a;
        else if (!op && !op2) r = b;       /* CSEL */
        else if (!op && op2) r = b + 1;    /* CSINC */
        else if (op && !op2) r = ~b;       /* CSINV */
        else r = (uint64_t)(-(int64_t)b);  /* CSNEG */
        if (bits == 32) r &= 0xffffffffull;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* Three sources: multiply-add and multiply-subtract, the widening forms
       that take two 32-bit operands to a 64-bit result, and the high half of
       a 128-bit product. */
    if (((insn >> 24) & 0x1f) == 0x1b && ((insn >> 29) & 3) == 0) {
        int op31 = (insn >> 21) & 7;
        int o0 = (insn >> 15) & 1;
        int rm = (insn >> 16) & 31;
        int ra = (insn >> 10) & 31;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        uint64_t a = xr(cpu, rn), b = xr(cpu, rm), c = xr(cpu, ra);
        uint64_t r = 0;
        if (op31 == 0) {
            r = o0 ? c - a * b : c + a * b; /* MADD, MSUB */
            if (bits == 32) r &= 0xffffffffull;
        } else if (op31 == 1) { /* SMADDL, SMSUBL */
            int64_t p = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
            r = o0 ? c - (uint64_t)p : c + (uint64_t)p;
        } else if (op31 == 5) { /* UMADDL, UMSUBL */
            uint64_t p = (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
            r = o0 ? c - p : c + p;
        } else if (op31 == 2) { /* SMULH */
            r = (uint64_t)__mulh((int64_t)a, (int64_t)b);
        } else if (op31 == 6) { /* UMULH */
            r = __umulh(a, b);
        } else
            return false;
        xw(cpu, rd, r);
        cpu.pc = next;
        return true;
    }

    /* B / BL */
    if (((insn >> 26) & 0x1f) == 0x05) {
        int32_t imm = (int32_t)((insn & 0x03ffffff) << 6) >> 4;
        if (insn & 0x80000000) xw(cpu, 30, next);
        cpu.pc = cpu.pc + imm;
        return true;
    }

    /* B.cond */
    if (((insn >> 24) & 0xff) == 0x54 && ((insn >> 4) & 1) == 0) {
        int32_t imm = (int32_t)(((insn >> 5) & 0x7ffff) << 13) >> 11;
        int cc = insn & 15;
        cpu.pc = cond(cpu, cc) ? cpu.pc + imm : next;
        return true;
    }

    /* CBZ / CBNZ */
    if (((insn >> 25) & 0x3f) == 0x1a) {
        int op = (insn >> 24) & 1;
        int32_t imm = (int32_t)(((insn >> 5) & 0x7ffff) << 13) >> 11;
        int rt = insn & 31;
        uint64_t v = xr(cpu, rt);
        if (bits == 32) v &= 0xffffffffull;
        bool take = op ? v != 0 : v == 0;
        cpu.pc = take ? cpu.pc + imm : next;
        return true;
    }

    /* TBZ / TBNZ */
    if (((insn >> 25) & 0x3f) == 0x1b) {
        int op = (insn >> 24) & 1;
        int bit = ((insn >> 31) << 5) | ((insn >> 19) & 31);
        int32_t imm = (int32_t)(((insn >> 5) & 0x3fff) << 18) >> 16;
        int rt = insn & 31;
        bool set = ((xr(cpu, rt) >> bit) & 1) != 0;
        bool take = op ? set : !set;
        cpu.pc = take ? cpu.pc + imm : next;
        return true;
    }

    /* BR / BLR / RET */
    if (((insn >> 25) & 0x7f) == 0x6b) {
        int op = (insn >> 21) & 15;
        int rn = (insn >> 5) & 31;
        if (op == 0) cpu.pc = xr(cpu, rn);
        else if (op == 1) {
            uint64_t t = xr(cpu, rn);
            xw(cpu, 30, next);
            cpu.pc = t;
        } else if (op == 2) cpu.pc = xr(cpu, rn);
        else return false;
        return true;
    }

    /* Load/store pair */
    if (((insn >> 27) & 7) == 5 && ((insn >> 26) & 1) == 0) {
        int opc = (insn >> 30) & 3;
        int mode = (insn >> 23) & 7;
        int L = (insn >> 22) & 1;
        int imm7 = (insn >> 15) & 0x7f;
        int rt2 = (insn >> 10) & 31;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        int scale = opc == 2 ? 8 : 4;
        int32_t off = ((int32_t)(imm7 << 25)) >> 25;
        off *= scale;
        uint64_t base = x_or_sp(cpu, rn);
        uint64_t addr = base;
        /* Mode 0 is the non-temporal pair (LDNP/STNP) and 2 the plain
           offset form: both address base plus offset without writeback. 1 is
           post-index and 3 pre-index. */
        if (mode == 0 || mode == 2 || mode == 3) addr = base + off;
        int bytes = scale;
        if (L) {
            uint64_t a = 0, b = 0;
            if (!load(mem, addr, &a, bytes) || !load(mem, addr + bytes, &b, bytes)) return false;
            if (opc == 1) {
                /* LDPSW: each word sign extended to 64 bits. */
                xw(cpu, rt, (uint64_t)(int64_t)(int32_t)(uint32_t)a);
                xw(cpu, rt2, (uint64_t)(int64_t)(int32_t)(uint32_t)b);
            } else {
                xw(cpu, rt, bytes == 4 ? (uint32_t)a : a);
                xw(cpu, rt2, bytes == 4 ? (uint32_t)b : b);
            }
        } else {
            if (!store(mem, addr, xr(cpu, rt), bytes) || !store(mem, addr + bytes, xr(cpu, rt2), bytes)) return false;
        }
        if (mode == 1) wsp(cpu, rn, base + off);
        if (mode == 3) wsp(cpu, rn, base + off);
        cpu.pc = next;
        return true;
    }

    /* Load/store single integer. With bit 21 set, only the register offset
       form (bits 11:10 = 10) is ours; the rest of that space is the LSE
       atomics, which have to be left for their own decoder rather than
       refused here. */
    if (((insn >> 27) & 7) == 7 && ((insn >> 26) & 1) == 0 &&
        !(((insn >> 24) & 3) == 0 && ((insn >> 21) & 1) == 1 && ((insn >> 10) & 3) != 2)) {
        int size = (insn >> 30) & 3;
        int kind = (insn >> 24) & 3;
        int opc = (insn >> 22) & 3;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        int bytes = 1 << size;
        uint64_t base = x_or_sp(cpu, rn);
        uint64_t addr = base;
        bool writeback = false;
        uint64_t written = 0;
        if (kind == 1) {
            addr += ((insn >> 10) & 0xfff) * bytes;
        } else if (kind == 0 && ((insn >> 21) & 1) == 0) {
            int imm9 = (insn >> 12) & 0x1ff;
            int64_t off = (int64_t)((imm9 << 23) >> 23);
            int form = (insn >> 10) & 3;
            /* Form 2 is the unprivileged LDTR/STTR, which in user mode is
               the unscaled form. */
            if (form == 0 || form == 2) addr += off; /* unscaled, no writeback */
            else {
                /* post-index uses the base, pre-index uses the base plus the offset */
                writeback = true;
                written = base + off;
                if (form == 3) addr = written;
            }
        } else if (kind == 0 && ((insn >> 21) & 1) == 1 && ((insn >> 10) & 3) == 2) {
            /* Register offset, extended by the option field: UXTW (010) and
               SXTW (110) take the low 32 bits of the index, LSL (011) and
               SXTX (111) all 64. */
            int rm = (insn >> 16) & 31;
            int option = (insn >> 13) & 7;
            int shift = (insn >> 12) & 1;
            uint64_t index = xr(cpu, rm);
            if (option == 2) index = (uint32_t)index;
            else if (option == 6) index = (uint64_t)(int64_t)(int32_t)(uint32_t)index;
            else if (option != 3 && option != 7) return false;
            addr += index << (shift ? size : 0);
        } else return false;
        if (opc == 0) {
            if (!store(mem, addr, xr(cpu, rt), bytes)) return false;
        } else if (opc == 1) {
            uint64_t v = 0;
            if (!load(mem, addr, &v, bytes)) return false;
            xw(cpu, rt, bytes == 4 ? (uint32_t)v : v);
        } else if (bytes < 8) {
            /* LDRSB / LDRSH / LDRSW: opc 2 fills a 64-bit register, opc 3 a 32-bit one. */
            uint64_t v = 0;
            if (!load(mem, addr, &v, bytes)) return false;
            int64_t signed_value = bytes == 1   ? (int8_t)v
                                   : bytes == 2 ? (int16_t)v
                                                : (int32_t)v;
            xw(cpu, rt, opc == 3 ? (uint64_t)(uint32_t)signed_value : (uint64_t)signed_value);
        } else return false;
        if (writeback) wsp(cpu, rn, written);
        cpu.pc = next;
        return true;
    }

    if (step_atomic(cpu, mem, insn, next)) return true;
    if (step_atomic_memory(cpu, mem, insn, next)) return true;
    if (step_fp(cpu, mem, insn, next)) return true;
    if (step_crypto(cpu, insn, next)) return true;

    std::fprintf(stderr, "qb-guest: unknown arm64 %08x at %llx\n", insn, (unsigned long long)cpu.pc);
    return false;
}

namespace {

/* Every processor currently inside guest_run, so a watcher on another thread
   can see where each one is when something appears to hang. */
std::mutex g_running_lock;
std::vector<const GuestCpu*> g_running;

struct Running {
    const GuestCpu* cpu;
    explicit Running(const GuestCpu* c) : cpu(c) {
        std::lock_guard<std::mutex> held(g_running_lock);
        g_running.push_back(c);
    }
    ~Running() {
        std::lock_guard<std::mutex> held(g_running_lock);
        for (size_t i = g_running.size(); i-- > 0;)
            if (g_running[i] == cpu) {
                g_running.erase(g_running.begin() + (long long)i);
                break;
            }
    }
};

}  // namespace

namespace {

std::mutex g_signal_lock;
std::unordered_map<uint64_t, uint64_t> g_pending;
std::atomic<int> g_threads_with_signals{0};
GuestSignalHook g_signal_hook = nullptr;
GuestUndefinedHook g_undefined_hook = nullptr;

}  // namespace

void guest_set_signal_hook(GuestSignalHook hook) { g_signal_hook = hook; }
void guest_set_undefined_hook(GuestUndefinedHook hook) { g_undefined_hook = hook; }

static GuestDescribeHook g_describe_hook = nullptr;
void guest_set_describe_hook(GuestDescribeHook hook) { g_describe_hook = hook; }
const char* guest_package() {
    static const std::string package = QB_ENV("QB_PACKAGE") ? QB_ENV("QB_PACKAGE") : "org.axrb.bridge.test";
    return package.c_str();
}

std::string guest_describe(uint64_t address) {
    if (g_describe_hook) return g_describe_hook(address);
    char text[32];
    std::snprintf(text, sizeof(text), "%llx", (unsigned long long)address);
    return text;
}

bool guest_thread_running(uint64_t thread_pointer) {
    std::lock_guard<std::mutex> held(g_running_lock);
    for (const GuestCpu* cpu : g_running)
        if (cpu->tpidr == thread_pointer) return true;
    return false;
}

bool guest_raise(uint64_t thread_pointer, int signal) {
    if (!guest_thread_running(thread_pointer)) return false;
    std::lock_guard<std::mutex> held(g_signal_lock);
    uint64_t& mask = g_pending[thread_pointer];
    if (!mask) ++g_threads_with_signals;
    mask |= 1ull << (signal & 63);
    return true;
}

uint64_t guest_take_pending(uint64_t thread_pointer) {
    if (g_threads_with_signals.load(std::memory_order_relaxed) == 0) return 0;
    std::lock_guard<std::mutex> held(g_signal_lock);
    auto found = g_pending.find(thread_pointer);
    if (found == g_pending.end() || !found->second) return 0;
    uint64_t mask = found->second;
    found->second = 0;
    --g_threads_with_signals;
    return mask;
}

void guest_each_running(void (*visit)(const GuestCpu&)) {
    std::lock_guard<std::mutex> held(g_running_lock);
    for (const GuestCpu* cpu : g_running) visit(*cpu);
}

/* A step, or a host call, that touches memory the guest never had faults on
   the host. That is the guest's bug, not ours, so it is reported the way a
   decoder miss is instead of taking the whole process down. */
static int guarded_step(GuestCpu& cpu, GuestMem& mem) {
    __try {
        return step(cpu, mem) ? 1 : 0;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                  : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}

static int guarded_thunk(GuestThunk thunk, GuestCpu& cpu, GuestMem& mem, int index, void* user) {
    __try {
        thunk(cpu, mem, index, user);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                  : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}

/* QB_RET=pc,value[;pc,value]: a function entered at pc returns value at once,
   without running. For turning off a subsystem (audio) that the rest of the
   engine is prepared to find missing. The JIT ends its blocks before these
   addresses, so the check below sees every entry. */
/* QB_BREAK's addresses, for the run loop and for the JIT, which ends its
   blocks before them and never chains or jumps into them, so a breakpoint
   is seen at full speed. */
const std::vector<uint64_t>& guest_break_pcs() {
    static const std::vector<uint64_t> pcs = [] {
        std::vector<uint64_t> list;
        for (const char* text = QB_ENV("QB_BREAK"); text && *text;) {
            char* end = nullptr;
            list.push_back(std::strtoull(text, &end, 16));
            text = (end && *end == ',') ? end + 1 : "";
        }
        return list;
    }();
    return pcs;
}

/* One interpreted instruction: 1 done, 0 not decoded, -1 faulted. For
   checking other executors (the JIT) against the interpreter. */
int guest_step_once(GuestCpu& cpu, GuestMem& mem) {
    const GuestCpu* outer = t_guest_cpu;
    t_guest_cpu = &cpu;
    int result = guarded_step(cpu, mem);
    t_guest_cpu = outer;
    return result;
}

const std::vector<std::pair<uint64_t, uint64_t>>& guest_early_returns() {
    static const std::vector<std::pair<uint64_t, uint64_t>> all = [] {
        std::vector<std::pair<uint64_t, uint64_t>> list;
        for (const char* text = QB_ENV("QB_RET"); text && *text;) {
            char* end = nullptr;
            uint64_t pc = std::strtoull(text, &end, 16);
            uint64_t value = std::strtoull(end + 1, &end, 16);
            list.push_back({pc, value});
            text = (end && *end == ';') ? end + 1 : "";
        }
        return list;
    }();
    return all;
}

thread_local GuestThunk t_thunk = nullptr;
thread_local void* t_thunk_user = nullptr;

/* A host call from translated code: the thunk the run loop on this thread
   was given, under the same guard. 1 done, -1 faulted. */
int guest_jit_host_call(GuestCpu& cpu, GuestMem& mem, int index) {
    if (!t_thunk) return -1;
    return guarded_thunk(t_thunk, cpu, mem, index, t_thunk_user);
}

bool guest_run(GuestCpu& cpu, GuestMem& mem, GuestThunk thunk, void* user, int max_steps) {
    Running running(&cpu);
    struct ThunkScope {
        GuestThunk thunk;
        void* user;
        ~ThunkScope() {
            t_thunk = thunk;
            t_thunk_user = user;
        }
    } thunk_scope{t_thunk, t_thunk_user};
    t_thunk = thunk;
    t_thunk_user = user;
    const GuestCpu* outer = t_guest_cpu;
    t_guest_cpu = &cpu;
    struct Restore {
        const GuestCpu* value;
        ~Restore() { t_guest_cpu = value; }
    } restore{outer};
    /* A max_steps of zero runs the guest for as long as it wants to run. */
    long long i = 0;
    struct Count {
        long long& steps;
        ~Count() { g_guest_steps.fetch_add((uint64_t)steps, std::memory_order_relaxed); }
    } count{i};
    for (; max_steps <= 0 || i < max_steps; ++i) {
        if (cpu.pc == 1) return true;
        if (cpu.pc >= mem.thunk_va && cpu.pc < mem.thunk_va + (uint64_t)mem.thunk_count * mem.thunk_stride) {
            int index = (int)((cpu.pc - mem.thunk_va) / mem.thunk_stride);
            if (guarded_thunk(thunk, cpu, mem, index, user) < 0) {
                std::fprintf(stderr, "qb-guest: host call %d faulted on a guest pointer, lr %llx, x0 %llx x1 %llx\n",
                             index, (unsigned long long)cpu.x[30], (unsigned long long)cpu.x[0],
                             (unsigned long long)cpu.x[1]);
                return false;
            }
            cpu.pc = xr(cpu, 30);
            continue;
        }
        /* A signal is taken between instructions, the way hardware takes it. */
        if (g_threads_with_signals.load(std::memory_order_relaxed) && g_signal_hook) {
            uint64_t pending = guest_take_pending(cpu.tpidr);
            if (pending) g_signal_hook(cpu, pending);
        }
        {
            bool returned = false;
            for (const auto& one : guest_early_returns())
                if (cpu.pc == one.first) {
                    cpu.x[0] = one.second;
                    cpu.pc = xr(cpu, 30);
                    returned = true;
                    break;
                }
            if (returned) continue;
        }
        uint64_t at = cpu.pc;
        /* QB_BREAK=pc: prints x0-x3, lr and 0x80 bytes from x0+0x1f0 each
           time that pc is reached (the first 20 times), then carries on. */
        const std::vector<uint64_t>& break_pcs = guest_break_pcs();
        static std::atomic<int> break_hits{0};
        /* QB_BREAK_SKIP=n: stay quiet for the first n hits, to see later ones. */
        static const int break_skip = QB_ENV("QB_BREAK_SKIP") ? std::atoi(QB_ENV("QB_BREAK_SKIP")) : 0;
        int hit = -1;
        if (!break_pcs.empty()) {
            auto found = std::find(break_pcs.begin(), break_pcs.end(), at);
            /* Counted per address, so sampling one never starves the others. */
            static std::atomic<int> per_pc[64];
            if (found != break_pcs.end())
                hit = per_pc[(found - break_pcs.begin()) & 63].fetch_add(1);
        }
        /* QB_BREAK_EVERY=n: after the skip, report every nth hit for the whole run. */
        static const int break_every = QB_ENV("QB_BREAK_EVERY") ? std::atoi(QB_ENV("QB_BREAK_EVERY")) : 0;
        if (hit >= break_skip && (break_every > 0 ? (hit - break_skip) % break_every == 0 : hit < break_skip + 40)) {
            std::fprintf(stderr, "break %s: x8 %s x9 %s x21 %llx x19 %llx x0 %llx x1 %llx x2 %llx x3 %llx lr %s tp %llx\n",
                         guest_describe(at).c_str(), guest_describe(cpu.x[8]).c_str(), guest_describe(cpu.x[9]).c_str(), (unsigned long long)cpu.x[21], (unsigned long long)cpu.x[19],
                         (unsigned long long)cpu.x[0], (unsigned long long)cpu.x[1], (unsigned long long)cpu.x[2],
                         (unsigned long long)cpu.x[3], guest_describe(cpu.x[30]).c_str(),
                         (unsigned long long)cpu.tpidr);
            /* x0-x2 that point at readable text are shown as strings too. */
            for (int r = 0; r < 3; ++r) {
                uint64_t va = cpu.x[r];
                if (va < 0x10000 || va >= 0x800000000000ull) continue;
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(reinterpret_cast<void*>(va), &info, sizeof(info)) || info.State != MEM_COMMIT) continue;
                const char* text = reinterpret_cast<const char*>(va);
                int n = 0;
                while (n < 120 && (uint64_t)va + n < (uint64_t)info.BaseAddress + info.RegionSize && text[n] >= 32 &&
                       text[n] < 127)
                    ++n;
                if (n >= 3 && ((uint64_t)va + n >= (uint64_t)info.BaseAddress + info.RegionSize || text[n] == 0))
                    std::fprintf(stderr, "break:   x%d -> \"%.*s\"\n", r, n, text);
            }
            /* QB_BREAK_MEM=sp+8 (or x19+13d4): 32 bytes from there, as words. */
            if (const char* spec = QB_ENV("QB_BREAK_MEM")) {
                std::string text = spec;
                size_t plus = text.find('+');
                std::string reg = text.substr(0, plus);
                uint64_t base = reg == "sp" ? cpu.sp : (reg.size() > 1 ? cpu.x[std::atoi(reg.c_str() + 1) % 31] : 0);
                uint64_t va = base + (plus == std::string::npos ? 0 : std::strtoull(text.c_str() + plus + 1, nullptr, 16));
                if (const uint8_t* bytes = guest_ptr(mem, va, 32)) {
                    std::fprintf(stderr, "break:   [%s]", spec);
                    for (int i = 0; i < 8; ++i) {
                        uint32_t word = 0;
                        std::memcpy(&word, bytes + 4 * i, 4);
                        std::fprintf(stderr, " %08x", word);
                    }
                    std::fprintf(stderr, "\n");
                }
            }
            /* Words up the stack that point into a library: a rough backtrace. */
            std::fprintf(stderr, "break:   stack");
            for (uint64_t w = cpu.sp, shown = 0; w < cpu.sp + 0x10000 && shown < 40; w += 8) {
                uint64_t word = 0;
                if (!guest_ptr(mem, w, 8)) break;
                /* Stop at the end of the stack's committed memory. */
                if ((w & 0xfff) == 0 || w == cpu.sp) {
                    MEMORY_BASIC_INFORMATION info{};
                    if (!VirtualQuery(reinterpret_cast<void*>(w), &info, sizeof(info)) || info.State != MEM_COMMIT)
                        break;
                }
                std::memcpy(&word, reinterpret_cast<void*>(w), 8);
                if (word < 0x2000000000ull || word > 0x3000000000ull || (word & 3)) continue;
                /* Only a return address: the instruction before it is a call.
                   (Checked only inside a library, whose pages are mapped.) */
                if (guest_describe(word - 4).find('+') == std::string::npos) continue;
                uint32_t before = load32(mem, word - 4);
                bool call = (before & 0xfc000000u) == 0x94000000u || (before & 0xfffffc1fu) == 0xd63f0000u;
                if (!call) continue;
                std::string where = guest_describe(word);
                if (where.find('+') == std::string::npos) continue;
                std::fprintf(stderr, " %s", where.c_str());
                ++shown;
            }
            std::fprintf(stderr, "\n");
            /* The frame record chain, which code built with frame pointers
               (IL2CPP's output is) keeps exact: [fp] caller's fp, [fp+8] lr. */
            std::fprintf(stderr, "break:   frames %s", guest_describe(cpu.x[30]).c_str());
            for (uint64_t fp = cpu.x[29], depth = 0; fp && depth < 24; ++depth) {
                uint64_t record[2] = {};
                if (!guest_ptr(mem, fp, 16) || (fp & 7)) break;
                std::memcpy(record, reinterpret_cast<void*>(fp), 16);
                if (!record[1]) break;
                std::fprintf(stderr, " <- %s", guest_describe(record[1]).c_str());
                if (record[0] <= fp) break;
                fp = record[0];
            }
            std::fprintf(stderr, "\n");
            float s[4];
            for (int r = 0; r < 4; ++r) std::memcpy(&s[r], &cpu.q[r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 8 : 9].lo, 4);
            std::fprintf(stderr, "break:   s0 %g s1 %g s8 %g s9 %g\n", s[0], s[1], s[2], s[3]);
            /* (A raw dump at x0 used to follow; it faulted on non-pointer x0.) */
        }
        /* QB_SETBYTE=pc,reg,offset,value: each time pc is reached, store the
           byte value at x<reg>+offset. For flipping a setting just after the
           guest has read it in (Unity's AudioManager m_DisableAudio, say). */
        struct SetByte { uint64_t pc; int reg; uint64_t offset; uint8_t value; };
        static const std::vector<SetByte> set_bytes = [] {
            std::vector<SetByte> all;
            for (const char* text = QB_ENV("QB_SETBYTE"); text && *text;) {
                char* end = nullptr;
                SetByte one{};
                one.pc = std::strtoull(text, &end, 16);
                one.reg = (int)std::strtol(end + 1, &end, 10);
                one.offset = std::strtoull(end + 1, &end, 16);
                one.value = (uint8_t)std::strtoul(end + 1, &end, 16);
                all.push_back(one);
                text = (end && *end == ';') ? end + 1 : "";
            }
            return all;
        }();
        for (const SetByte& one : set_bytes)
            if (at == one.pc) {
                uint8_t* p = guest_ptr(mem, cpu.x[one.reg] + one.offset, 1);
                if (p) *p = one.value;
                std::fprintf(stderr, "setbyte: [x%d+%llx] = %u at %s\n", one.reg, (unsigned long long)one.offset,
                             one.value, guest_describe(at).c_str());
            }
        /* Translated code first; the interpreter below takes what it
           leaves. A bounded run (max_steps) stays in the interpreter so its
           count is exact. */
        static const bool jit = guest_jit_enabled();
        if (jit && max_steps <= 0) {
            int ran = guest_jit_step(cpu, mem);
            if (ran > 0) {
                i += ran - 1;
                continue;
            }
            guest_jit_note_interpreted(load32(mem, cpu.pc));
            if (cpu.pc != at) continue; /* a fault moved the pc onto the instruction to report */
        }
        int stepped = guarded_step(cpu, mem);
        /* QB_SKIP_UNKNOWN: for the fuzzer, an instruction the decoder does
           not know is reported and stepped over instead of ending the run. */
        static const bool skip_unknown = QB_ENV("QB_SKIP_UNKNOWN") != nullptr;
        if (stepped == 0 && skip_unknown) {
            std::fprintf(stderr, "skip: %08x\n", load32(mem, at));
            cpu.pc = at + 4;
            continue;
        }
        /* An undefined instruction is SIGILL, taken by the guest's own
           handler if it has one (feature probes rely on exactly that). */
        if (stepped == 0 && g_undefined_hook) {
            std::fprintf(stderr, "qb-guest: failed step before guest signal at %s instruction %08x x0=%llx x8=%llx lr=%s sp=%llx\n",
                guest_describe(at).c_str(), load32(mem, at), (unsigned long long)cpu.x[0], (unsigned long long)cpu.x[8],
                guest_describe(cpu.x[30]).c_str(), (unsigned long long)cpu.sp);
            if (g_undefined_hook(cpu)) continue;
        }
        if (stepped <= 0) {
            std::fprintf(stderr, "qb-guest: %s at %s on %08x, sp %llx, lr %s, fp %llx, tp %llx\n",
                         stepped < 0 ? "fault" : "stopped", guest_describe(at).c_str(), load32(mem, at),
                         (unsigned long long)cpu.sp, guest_describe(cpu.x[30]).c_str(),
                         (unsigned long long)cpu.x[29], (unsigned long long)cpu.tpidr);
            /* QB_STOPDUMP: the raw words above the stack pointer, for reading
               callers' saved registers by hand. */
            if (QB_ENV("QB_STOPDUMP"))
                for (uint64_t at = cpu.sp; at < cpu.sp + 0x200; at += 16) {
                    uint64_t pair[2] = {};
                    std::memcpy(pair, reinterpret_cast<void*>(at), 16);
                    std::fprintf(stderr, "qb-guest:   [sp+%03llx] %016llx %016llx\n", (unsigned long long)(at - cpu.sp),
                                 (unsigned long long)pair[0], (unsigned long long)pair[1]);
                }
            /* Return addresses on the stack: a backtrace that does not need
               frame pointers. */
            for (uint64_t at = cpu.sp, shown = 0; at < cpu.sp + 0x800 && shown < 16; at += 8) {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT) break;
                uint64_t word = 0;
                std::memcpy(&word, reinterpret_cast<void*>(at), 8);
                if (word < 0x2000000000ull || word > 0x3000000000ull) continue;
                std::string where = guest_describe(word);
                if (where.find('+') == std::string::npos) continue;
                std::fprintf(stderr, "qb-guest:   stack+%llx %s\n", (unsigned long long)(at - cpu.sp), where.c_str());
                ++shown;
            }
            for (int r = 0; r < 31; r += 4) {
                std::fprintf(stderr, "qb-guest:   x%-2d", r);
                for (int k = r; k < r + 4 && k < 31; ++k)
                    std::fprintf(stderr, " %016llx", (unsigned long long)cpu.x[k]);
                std::fprintf(stderr, "\n");
            }
            return false;
        }
    }
    std::fprintf(stderr, "qb-guest: hit the step limit at %llx\n", (unsigned long long)cpu.pc);
    return false;
}
