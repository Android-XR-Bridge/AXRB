// Instructions Dynarmic does not translate, executed one at a time on the
// thread's register state: the ARMv8.1 LSE atomics (CAS, CASP, LD<op>, SWP),
// RCPC loads/stores (LDAPR, LDAPUR, STLUR) and reads of the ID registers that
// Linux emulates for user space. All atomics act on the identity-mapped guest
// memory with host atomic instructions, so they interoperate with Dynarmic's
// exclusive-monitor implementation (which also compare-exchanges memory).

#include "cpu.h"
#include "linux.h"
#include "log.h"
#include "memory.h"

#include <intrin.h>
#include <windows.h>

#include <atomic>
#include <cstring>

namespace axrb {
using namespace linux_abi;

namespace {

struct Regs {
    GuestThread& t;
    uint64_t x(int r) const { return r == 31 ? 0 : get_reg(t, r); }
    uint64_t xsp(int r) const { return r == 31 ? get_sp(t) : get_reg(t, r); }
    void set(int r, uint64_t v) const {
        if (r != 31) set_reg(t, r, v);
    }
};

uint64_t mask_for(int bytes) { return bytes == 8 ? ~0ull : ((1ull << (bytes * 8)) - 1); }

int64_t sign_extend(uint64_t v, int bytes) {
    int shift = 64 - bytes * 8;
    return static_cast<int64_t>(v << shift) >> shift;
}

// Atomic compare-exchange of `bytes` at `address`. Returns the old value.
// Faults propagate as a structured exception to the caller's __try.
uint64_t cmpxchg(uint64_t address, uint64_t expected, uint64_t desired, int bytes) {
    switch (bytes) {
        case 1:
            return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(address),
                                                                     static_cast<char>(desired), static_cast<char>(expected)));
        case 2:
            return static_cast<uint16_t>(_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(address),
                                                                       static_cast<short>(desired), static_cast<short>(expected)));
        case 4:
            return static_cast<uint32_t>(_InterlockedCompareExchange(reinterpret_cast<volatile long*>(address),
                                                                     static_cast<long>(desired), static_cast<long>(expected)));
        default:
            return static_cast<uint64_t>(_InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(address),
                                                                       static_cast<long long>(desired),
                                                                       static_cast<long long>(expected)));
    }
}

uint64_t load(uint64_t address, int bytes) {
    switch (bytes) {
        case 1: return *reinterpret_cast<volatile uint8_t*>(address);
        case 2: return *reinterpret_cast<volatile uint16_t*>(address);
        case 4: return *reinterpret_cast<volatile uint32_t*>(address);
        default: return *reinterpret_cast<volatile uint64_t*>(address);
    }
}

void store(uint64_t address, uint64_t value, int bytes) {
    switch (bytes) {
        case 1: *reinterpret_cast<volatile uint8_t*>(address) = static_cast<uint8_t>(value); break;
        case 2: *reinterpret_cast<volatile uint16_t*>(address) = static_cast<uint16_t>(value); break;
        case 4: *reinterpret_cast<volatile uint32_t*>(address) = static_cast<uint32_t>(value); break;
        default: *reinterpret_cast<volatile uint64_t*>(address) = value; break;
    }
    _ReadWriteBarrier();
}

// LD<op>: returns the old value; `op` 0-7 as in the encoding.
uint64_t atomic_rmw(uint64_t address, uint64_t operand, int op, int bytes) {
    uint64_t m = mask_for(bytes);
    operand &= m;
    uint64_t old = load(address, bytes);
    for (;;) {
        uint64_t next;
        switch (op) {
            case 0: next = old + operand; break;                    // ADD
            case 1: next = old & ~operand; break;                   // CLR
            case 2: next = old ^ operand; break;                    // EOR
            case 3: next = old | operand; break;                    // SET
            case 4: next = sign_extend(old, bytes) > sign_extend(operand, bytes) ? old : operand; break;  // SMAX
            case 5: next = sign_extend(old, bytes) < sign_extend(operand, bytes) ? old : operand; break;  // SMIN
            case 6: next = old > operand ? old : operand; break;    // UMAX
            default: next = old < operand ? old : operand; break;   // UMIN
        }
        next &= m;
        uint64_t seen = cmpxchg(address, old, next, bytes);
        if (seen == old) return old;
        old = seen;
    }
}

uint64_t swap(uint64_t address, uint64_t value, int bytes) {
    switch (bytes) {
        case 1: return static_cast<uint8_t>(_InterlockedExchange8(reinterpret_cast<volatile char*>(address), static_cast<char>(value)));
        case 2: return static_cast<uint16_t>(_InterlockedExchange16(reinterpret_cast<volatile short*>(address), static_cast<short>(value)));
        case 4: return static_cast<uint32_t>(_InterlockedExchange(reinterpret_cast<volatile long*>(address), static_cast<long>(value)));
        default: return static_cast<uint64_t>(_InterlockedExchange64(reinterpret_cast<volatile long long*>(address), static_cast<long long>(value)));
    }
}

// The sanitized ID register values Linux reports to EL0 (arch/arm64/kernel/cpufeature.c),
// for a Cortex-A78C-class core without LSE (matching AT_HWCAP).
bool id_register(uint32_t sysreg, uint64_t& value) {
    uint32_t op0 = 2 + ((sysreg >> 14) & 1), op1 = (sysreg >> 11) & 7, crn = (sysreg >> 7) & 15, crm = (sysreg >> 3) & 15,
             op2 = sysreg & 7;
    if (op0 != 3 || op1 != 0 || crn != 0) return false;
    value = 0;
    switch ((crm << 3) | op2) {
        case (0 << 3) | 0: value = 0x410fd4b0; return true;          // MIDR_EL1: ARM Cortex-A78C r0p0
        case (0 << 3) | 5: value = 0x80000000; return true;          // MPIDR_EL1
        case (0 << 3) | 6: value = 0; return true;                   // REVIDR_EL1
        case (4 << 3) | 0: value = 0x0000000000000011ull; return true;  // ID_AA64PFR0: EL0/EL1 AArch64, FP, AdvSIMD
        case (6 << 3) | 0:                                           // ID_AA64ISAR0: AES+PMULL, SHA1, SHA2, CRC32
            value = (2ull << 4) | (1ull << 8) | (1ull << 12) | (1ull << 16);
            return true;
        default:
            if (crm >= 1 && crm <= 7) return true;  // other ID registers read as zero
            return false;
    }
}

bool execute(GuestThread& t, uint64_t pc, uint32_t insn) {
    Regs r{t};
    // LD<op> / SWP / LDAPR:  size 111 0 00 A R 1 Rs o3 opc 00 Rn Rt
    if ((insn & 0x3f200c00u) == 0x38200000u) {
        int bytes = 1 << (insn >> 30);
        int rs = (insn >> 16) & 31, rn = (insn >> 5) & 31, rt = insn & 31;
        int o3 = (insn >> 15) & 1, opc = (insn >> 12) & 7;
        uint64_t address = untag(r.xsp(rn));
        uint64_t old;
        if (o3 == 0) {
            old = atomic_rmw(address, r.x(rs), opc, bytes);
        } else if (opc == 0) {
            old = swap(address, r.x(rs) & mask_for(bytes), bytes);
        } else if (opc == 4 && rs == 31) {  // LDAPR
            old = load(address, bytes);
            _ReadWriteBarrier();
        } else {
            return false;
        }
        r.set(rt, old);
        set_pc(t, pc + 4);
        return true;
    }
    // CAS{A}{L}{B,H}:  size 001000 1 L 1 Rs o0 11111 Rn Rt
    if ((insn & 0x3fa07c00u) == 0x08a07c00u) {
        int bytes = 1 << (insn >> 30);
        int rs = (insn >> 16) & 31, rn = (insn >> 5) & 31, rt = insn & 31;
        uint64_t m = mask_for(bytes);
        uint64_t old = cmpxchg(untag(r.xsp(rn)), r.x(rs) & m, r.x(rt) & m, bytes);
        r.set(rs, old);
        set_pc(t, pc + 4);
        return true;
    }
    // CASP{A}{L}:  0 sz 001000 0 L 1 Rs o0 11111 Rn Rt (register pairs)
    if ((insn & 0xbfa07c00u) == 0x08207c00u) {
        bool wide = (insn >> 30) & 1;
        int rs = (insn >> 16) & 31, rn = (insn >> 5) & 31, rt = insn & 31;
        if ((rs & 1) || (rt & 1)) return false;
        uint64_t address = untag(r.xsp(rn));
        if (wide) {
            long long compare[2] = {static_cast<long long>(r.x(rs)), static_cast<long long>(r.x(rs + 1))};
            _InterlockedCompareExchange128(reinterpret_cast<volatile long long*>(address), static_cast<long long>(r.x(rt + 1)),
                                           static_cast<long long>(r.x(rt)), compare);
            r.set(rs, static_cast<uint64_t>(compare[0]));
            r.set(rs + 1, static_cast<uint64_t>(compare[1]));
        } else {
            uint64_t expected = (r.x(rs) & 0xffffffffull) | (r.x(rs + 1) << 32);
            uint64_t desired = (r.x(rt) & 0xffffffffull) | (r.x(rt + 1) << 32);
            uint64_t old = cmpxchg(address, expected, desired, 8);
            r.set(rs, old & 0xffffffffull);
            r.set(rs + 1, old >> 32);
        }
        set_pc(t, pc + 4);
        return true;
    }
    // LDAPUR / LDAPURS / STLUR:  size 011001 opc 0 imm9 00 Rn Rt
    if ((insn & 0x3f200c00u) == 0x19000000u) {
        int bytes = 1 << (insn >> 30);
        int opc = (insn >> 22) & 3, rn = (insn >> 5) & 31, rt = insn & 31;
        int64_t imm = static_cast<int64_t>(static_cast<int32_t>(insn << 11) >> 23);
        uint64_t address = untag(r.xsp(rn)) + static_cast<uint64_t>(imm);
        if (opc == 0) {
            _ReadWriteBarrier();
            store(address, r.x(rt), bytes);
        } else {
            uint64_t v = load(address, bytes);
            if (opc == 2) v = static_cast<uint64_t>(sign_extend(v, bytes));
            else if (opc == 3) v = static_cast<uint32_t>(sign_extend(v, bytes));
            r.set(rt, v);
        }
        set_pc(t, pc + 4);
        return true;
    }
    // MRS of an ID register (Linux traps and emulates these for EL0).
    if ((insn & 0xfff00000u) == 0xd5300000u) {
        uint64_t value;
        const uint32_t sysreg = (insn >> 5) & 0x7fff;
        // CNTVCT_EL0 / CNTVCTSS_EL0 / CNTPCTSS_EL0: Linux user space has no
        // virtual offset, so all of them are the physical count Dynarmic has.
        constexpr uint32_t kCntvct = (1u << 14) | (3u << 11) | (14u << 7) | 2u;
        if (sysreg == kCntvct || sysreg == (kCntvct | 4u) || sysreg == ((kCntvct & ~7u) | 5u)) {
            value = cntpct();
        } else if (!id_register(sysreg, value)) {
            return false;
        }
        r.set(insn & 31, value);
        set_pc(t, pc + 4);
        return true;
    }
    return false;
}

int access_filter(EXCEPTION_POINTERS* info, uint64_t* address) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    *address = info->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}

bool execute_guarded(GuestThread& t, uint64_t pc, uint32_t insn, bool& faulted, uint64_t& fault_address) {
    __try {
        return execute(t, pc, insn);
    } __except (access_filter(GetExceptionInformation(), &fault_address)) {
        faulted = true;
        return true;
    }
}

}  // namespace

bool interpret_instruction(GuestThread& thread, uint64_t pc, uint32_t instruction) {
    bool faulted = false;
    uint64_t fault_address = 0;
    bool known = execute_guarded(thread, pc, instruction, faulted, fault_address);
    if (faulted) {
        cpu_raise(thread, SIGSEGV_, SEGV_MAPERR_, fault_address, pc);
        return true;
    }
    if (!known) {
        static std::atomic<int> reports{0};
        if (reports.fetch_add(1) < 50)
            LOGW("cpu", "undefined instruction %08x at %s", instruction, describe_address(pc).c_str());
    }
    return known;
}

}  // namespace axrb
