/* Atomic memory for the guest.

   Every real engine is built on these: reference counts, job queues, locks.
   Guest threads are host threads over shared memory, so these have to be
   genuinely atomic rather than merely look it, and the host's own atomics are
   used on the same bytes the guest sees.

   The exclusive pair, load exclusive then store exclusive, is emulated by
   remembering what was read and having the store succeed only if the memory
   still holds it. That is not quite an exclusive monitor, but it fails in the
   direction a monitor does: a store can spuriously fail, and the guest's
   retry loop handles that. */

#include "cpu.h"

#include <atomic>
#include <intrin.h>
#include <cstring>

namespace {

uint64_t masked(uint64_t value, int bytes) {
    if (bytes >= 8) return value;
    return value & ((1ull << (bytes * 8)) - 1);
}

/* The host's atomic operations, on the guest's own bytes. */
uint64_t atomic_load(uint8_t* at, int bytes) {
    switch (bytes) {
        case 1: return std::atomic_ref<uint8_t>(*at).load(std::memory_order_acquire);
        case 2: return std::atomic_ref<uint16_t>(*reinterpret_cast<uint16_t*>(at)).load(std::memory_order_acquire);
        case 4: return std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(at)).load(std::memory_order_acquire);
        default: return std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t*>(at)).load(std::memory_order_acquire);
    }
}

void atomic_store(uint8_t* at, uint64_t value, int bytes) {
    switch (bytes) {
        case 1: std::atomic_ref<uint8_t>(*at).store((uint8_t)value, std::memory_order_release); break;
        case 2:
            std::atomic_ref<uint16_t>(*reinterpret_cast<uint16_t*>(at))
                .store((uint16_t)value, std::memory_order_release);
            break;
        case 4:
            std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(at))
                .store((uint32_t)value, std::memory_order_release);
            break;
        default:
            std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t*>(at)).store(value, std::memory_order_release);
            break;
    }
}

bool atomic_swap_if(uint8_t* at, uint64_t expected, uint64_t wanted, int bytes, uint64_t* seen) {
    switch (bytes) {
        case 1: {
            uint8_t want = (uint8_t)expected;
            bool ok = std::atomic_ref<uint8_t>(*at).compare_exchange_strong(want, (uint8_t)wanted);
            *seen = want;
            return ok;
        }
        case 2: {
            uint16_t want = (uint16_t)expected;
            bool ok = std::atomic_ref<uint16_t>(*reinterpret_cast<uint16_t*>(at))
                          .compare_exchange_strong(want, (uint16_t)wanted);
            *seen = want;
            return ok;
        }
        case 4: {
            uint32_t want = (uint32_t)expected;
            bool ok = std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(at))
                          .compare_exchange_strong(want, (uint32_t)wanted);
            *seen = want;
            return ok;
        }
        default: {
            uint64_t want = expected;
            bool ok = std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t*>(at)).compare_exchange_strong(want, wanted);
            *seen = want;
            return ok;
        }
    }
}

uint64_t atomic_combine(uint8_t* at, uint64_t operand, int bytes, int operation) {
    /* Returns what was there before, which is what these instructions give. */
    /* The signed forms compare at the access size, not as 64-bit values. */
    auto widen = [bytes](uint64_t v) -> int64_t {
        int shift = 64 - bytes * 8;
        return shift ? (int64_t)(v << shift) >> shift : (int64_t)v;
    };
    for (;;) {
        uint64_t before = atomic_load(at, bytes);
        uint64_t after = before;
        switch (operation) {
            case 0: after = before + operand; break;              /* LDADD */
            case 1: after = before & ~operand; break;             /* LDCLR */
            case 2: after = before ^ operand; break;              /* LDEOR */
            case 3: after = before | operand; break;              /* LDSET */
            case 4: after = operand; break;                       /* SWP */
            case 5: after = widen(before) > widen(operand) ? before : operand; break; /* LDSMAX */
            case 6: after = widen(before) < widen(operand) ? before : operand; break; /* LDSMIN */
            case 7: after = before > operand ? before : operand; break;                   /* LDUMAX */
            case 8: after = before < operand ? before : operand; break;                   /* LDUMIN */
            default: return before;
        }
        uint64_t seen = 0;
        if (atomic_swap_if(at, before, masked(after, bytes), bytes, &seen)) return before;
    }
}

/* A pair of 4 or 8 byte values as one atomic unit: 8 bytes with the ordinary
   64-bit atomics, 16 with cmpxchg16b, which wants 16-byte alignment (as the
   Arm pair exclusives do for their atomicity). */
void pair_load(uint8_t* at, int bytes, uint64_t* low, uint64_t* high) {
    if (bytes == 4) {
        uint64_t both = atomic_load(at, 8);
        *low = both & 0xffffffffull;
        *high = both >> 32;
        return;
    }
    if ((reinterpret_cast<uintptr_t>(at) & 15) == 0) {
        __int64 compare[2] = {0, 0};
        _InterlockedCompareExchange128(reinterpret_cast<volatile __int64*>(at), 0, 0, compare);
        *low = (uint64_t)compare[0];
        *high = (uint64_t)compare[1];
        return;
    }
    *low = atomic_load(at, 8);
    *high = atomic_load(at + 8, 8);
}

bool pair_swap_if(uint8_t* at, int bytes, uint64_t low, uint64_t high, uint64_t new_low, uint64_t new_high) {
    uint64_t seen = 0;
    if (bytes == 4)
        return atomic_swap_if(at, (low & 0xffffffffull) | (high << 32), (new_low & 0xffffffffull) | (new_high << 32), 8,
                              &seen);
    if ((reinterpret_cast<uintptr_t>(at) & 15) == 0) {
        __int64 compare[2] = {(__int64)low, (__int64)high};
        return _InterlockedCompareExchange128(reinterpret_cast<volatile __int64*>(at), (__int64)new_high,
                                              (__int64)new_low, compare) != 0;
    }
    /* Misaligned: not atomic on the device either. */
    if (atomic_load(at + 8, 8) != high || !atomic_swap_if(at, low, new_low, 8, &seen)) return false;
    atomic_store(at + 8, new_high, 8);
    return true;
}

/* What a load exclusive on this thread last saw. */
thread_local uint64_t g_watch_address = 0;
thread_local uint64_t g_watch_value = 0;
thread_local uint64_t g_watch_value_high = 0;
thread_local bool g_watching = false;

}  // namespace

bool step_atomic(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next) {
    auto reg = [&](int r) { return r == 31 ? 0ull : cpu.x[r]; };
    auto reg_or_sp = [&](int r) { return r == 31 ? cpu.sp : cpu.x[r]; };
    auto write = [&](int r, uint64_t v) {
        if (r != 31) cpu.x[r] = v;
    };

    /* Barriers. Order is already kept by the host atomics above. */
    if ((insn & 0xfffff01fu) == 0xd503301fu) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        cpu.pc = next;
        return true;
    }

    /* Load and store exclusive, and the plain acquire and release forms. */
    /* Not here: CAS (o2 and o1 both set) and CASP (o1 set, 32-bit size
       field), which share this prefix and have their own decoder. */
    bool compare_and_swap = ((insn >> 23) & 1) == 1 && ((insn >> 21) & 1) == 1;
    bool compare_and_swap_pair = ((insn >> 23) & 1) == 0 && ((insn >> 21) & 1) == 1 && ((insn >> 31) & 1) == 0;
    if (((insn >> 24) & 0x3f) == 0x08 && !compare_and_swap && !compare_and_swap_pair) {
        int size = (insn >> 30) & 3;
        int bytes = 1 << size;
        int o2 = (insn >> 23) & 1;
        int load = (insn >> 22) & 1;
        int pair = (insn >> 21) & 1;
        int rs = (insn >> 16) & 31;
        int o0 = (insn >> 15) & 1;
        int rt2 = (insn >> 10) & 31;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        uint64_t address = reg_or_sp(rn);
        uint8_t* at = guest_ptr(mem, address, pair ? (uint64_t)bytes * 2 : (uint64_t)bytes);
        if (!at) return false;

        if (o2 == 1) {
            /* Not exclusive: load acquire and store release. */
            if (load) write(rt, atomic_load(at, bytes));
            else atomic_store(at, masked(reg(rt), bytes), bytes);
            cpu.pc = next;
            return true;
        }
        if (load) {
            g_watch_address = address;
            if (pair) {
                /* Both halves in one atomic read, so they belong together. */
                pair_load(at, bytes, &g_watch_value, &g_watch_value_high);
                write(rt, g_watch_value);
                write(rt2, g_watch_value_high);
            } else {
                g_watch_value = atomic_load(at, bytes);
                write(rt, g_watch_value);
            }
            g_watching = true;
            (void)o0;
            cpu.pc = next;
            return true;
        }
        /* Store exclusive: only if nothing has touched it since the load. */
        bool ok = false;
        if (g_watching && g_watch_address == address) {
            uint64_t seen = 0;
            if (pair) {
                /* Both halves still holding what was read, and both replaced,
                   in one step. Checking one half and then swapping the other
                   leaves a window in which another thread's pair store lands
                   between them; a tagged-pointer queue (Unity's job queue)
                   then hands out one node twice or loses one, and a job group
                   never finishes. */
                ok = pair_swap_if(at, bytes, g_watch_value, g_watch_value_high, masked(reg(rt), bytes),
                                  masked(reg(rt2), bytes));
            } else {
                ok = atomic_swap_if(at, g_watch_value, masked(reg(rt), bytes), bytes, &seen);
            }
        }
        g_watching = false;
        write(rs, ok ? 0 : 1);
        cpu.pc = next;
        return true;
    }

    return false;
}

/* The compare and swap and read modify write forms, which sit in the ordinary
   load and store space with bit 21 set. */
bool step_atomic_memory(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next) {
    auto reg = [&](int r) { return r == 31 ? 0ull : cpu.x[r]; };
    auto reg_or_sp = [&](int r) { return r == 31 ? cpu.sp : cpu.x[r]; };
    auto write = [&](int r, uint64_t v) {
        if (r != 31) cpu.x[r] = v;
    };

    int size = (insn >> 30) & 3;
    int bytes = 1 << size;
    int rs = (insn >> 16) & 31;
    int rn = (insn >> 5) & 31;
    int rt = insn & 31;

    /* CAS and CASP: 001000 with bit 23 set and bit 21 set. */
    if (((insn >> 24) & 0x3f) == 0x08 && ((insn >> 23) & 1) == 1 && ((insn >> 21) & 1) == 1) {
        uint8_t* at = guest_ptr(mem, reg_or_sp(rn), bytes);
        if (!at) return false;
        uint64_t seen = 0;
        atomic_swap_if(at, masked(reg(rs), bytes), masked(reg(rt), bytes), bytes, &seen);
        write(rs, seen);
        cpu.pc = next;
        return true;
    }

    /* CASP: 0 sz 001000 0 L 1 Rs o0 11111 Rn Rt. Rs:Rs+1 is compared with
       the pair at [Xn]; if equal, Rt:Rt+1 is stored; Rs:Rs+1 get what was
       there. sz picks 4 or 8 bytes per element. */
    if (((insn >> 31) & 1) == 0 && ((insn >> 24) & 0x3f) == 0x08 && ((insn >> 23) & 1) == 0 &&
        ((insn >> 21) & 1) == 1 && ((insn >> 10) & 31) == 31 && !(rs & 1) && !(rt & 1)) {
        int element = ((insn >> 30) & 1) ? 8 : 4;
        uint8_t* at = guest_ptr(mem, reg_or_sp(rn), 2 * element);
        if (!at) return false;
        uint64_t low = masked(reg(rs), element), high = masked(reg(rs + 1), element);
        uint64_t seen_low = low, seen_high = high;
        if (!pair_swap_if(at, element, low, high, masked(reg(rt), element), masked(reg(rt + 1), element)))
            pair_load(at, element, &seen_low, &seen_high);
        write(rs, seen_low);
        write(rs + 1, seen_high);
        cpu.pc = next;
        return true;
    }

    /* The read modify write family: size 111000 A R 1 Rs 0 opc 00 Rn Rt. */
    if (((insn >> 27) & 7) == 7 && ((insn >> 26) & 1) == 0 && ((insn >> 24) & 3) == 0 && ((insn >> 21) & 1) == 1 &&
        ((insn >> 10) & 3) == 0) {
        int opc = (insn >> 12) & 7;
        int operation = opc == 0   ? 0   /* LDADD */
                        : opc == 1 ? 1   /* LDCLR */
                        : opc == 2 ? 2   /* LDEOR */
                        : opc == 3 ? 3   /* LDSET */
                        : opc == 4 ? 5   /* LDSMAX */
                        : opc == 5 ? 6   /* LDSMIN */
                        : opc == 6 ? 7   /* LDUMAX */
                                   : 8;  /* LDUMIN */
        if (((insn >> 15) & 1) == 1) operation = 4; /* SWP */
        uint8_t* at = guest_ptr(mem, reg_or_sp(rn), bytes);
        if (!at) return false;
        uint64_t before = atomic_combine(at, masked(reg(rs), bytes), bytes, operation);
        write(rt, before);
        cpu.pc = next;
        return true;
    }
    return false;
}
