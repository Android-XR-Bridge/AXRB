/* A block JIT: arm64 basic blocks become x64 code.

   The interpreter decodes every instruction every time it runs. Here a run of
   the common integer instructions (arithmetic, logic, moves, loads and
   stores, address generation) up to and including the branch that ends it is
   translated once into x64 that works directly on the GuestCpu structure, and
   after that runs at host speed. Anything the translator does not know ends
   the block before it, and the interpreter steps that one instruction as it
   always has; so coverage can grow an instruction class at a time, and a
   wrong translation can be found by switching the JIT off (QB_JIT=0).

   Guest registers live in memory (cpu.x[], cpu.sp, the flag bytes), so a
   block needs no register allocation and leaves nothing to write back: every
   guest instruction's effects are in the structure by the time the next one
   starts. That is also what makes faults exact. Guest memory is identity
   mapped, so a guest load is a host load; if one faults, the block's table
   of host memory instructions names the guest instruction it came from, the
   pc is set there, and the interpreter re-runs that single instruction and
   reports the fault as it would have without the JIT. Every translated
   instruction does its loads before any of its register writes, so re-running
   it from the start is always right.

   Host registers: rcx holds the GuestCpu pointer throughout; rax, rdx, r8 and
   r9 are scratch. All four are volatile in the Windows x64 convention, and
   the block calls nothing, so it needs no prologue. A block returns the
   number of guest instructions it ran in eax, with cpu.pc already set to the
   next one. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <windows.h>

#include "cpu.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <thread>
#include <chrono>

namespace {

using BlockFn = uint32_t (*)(GuestCpu*);
/* jit_enter.asm: calls fn with the callee-saved registers saved around it. */
extern "C" uint32_t qb_jit_enter(GuestCpu* cpu, BlockFn fn);
/* Where translated code hands back to the run loop, sampled: the pcs
   that keep leaving are what to chain or inline next. */
std::mutex g_exit_lock;
std::unordered_map<uint64_t, uint64_t> g_exits;
void guest_jit_note_exit(uint64_t pc) {
    thread_local uint32_t batch = 0;
    if ((++batch & 63) != 0) return;
    std::lock_guard<std::mutex> held(g_exit_lock);
    g_exits[pc] += 64;
}
void print_exits() {
    std::vector<std::pair<uint64_t, uint64_t>> sorted;
    {
        std::lock_guard<std::mutex> held(g_exit_lock);
        for (auto& entry : g_exits) sorted.push_back({entry.second, entry.first});
        g_exits.clear();
    }
    std::sort(sorted.rbegin(), sorted.rend());
    for (size_t i = 0; i < sorted.size() && i < 15; ++i)
        std::fprintf(stderr, "jit: exit %10llu at %s\n", (unsigned long long)sorted[i].first,
                     guest_describe(sorted[i].second).c_str());
}


struct FaultSite {
    uint32_t host_start, host_end; /* offsets into the block's code */
    uint64_t guest_pc;
};

struct Block {
    uint64_t pc = 0;
    BlockFn fn = nullptr; /* null: the first instruction is not translated */
    uint8_t* code = nullptr;
    uint32_t size = 0;
    uint32_t instructions = 0;
    std::vector<FaultSite> faults;
    /* Chaining: where each exit's jump offset sits in the code, and the
       guest pc it leaves for. */
    std::vector<std::pair<uint32_t, uint64_t>> slots;
};

/* The interpreter, called from inside a block (defined with the code cache). */
int jit_interpret(GuestCpu* cpu, uint64_t pc);
int jit_host_call(GuestCpu* cpu, uint64_t index);
int jit_atomic(GuestCpu* cpu, uint64_t pc);
bool fold_constant(uint32_t insn, uint64_t pc, int* rd, GuestVec* value);

/* ---- Where things are in GuestCpu -------------------------------------- */

int32_t off_x(int r) { return (int32_t)(offsetof(GuestCpu, x) + 8 * (size_t)r); }
const int32_t kOffSp = (int32_t)offsetof(GuestCpu, sp);
const int32_t kOffPc = (int32_t)offsetof(GuestCpu, pc);
const int32_t kOffN = (int32_t)offsetof(GuestCpu, n);
const int32_t kOffZ = (int32_t)offsetof(GuestCpu, z);
const int32_t kOffC = (int32_t)offsetof(GuestCpu, c);
const int32_t kOffV = (int32_t)offsetof(GuestCpu, v);

/* Register 31 means the stack pointer in some encodings and the zero
   register in the rest. */
enum class R31 { Zero, Sp };

/* ---- x64 emitter -------------------------------------------------------- */

enum Host { RAX = 0, RCX = 1, RDX = 2, RBX = 3, RBP = 5, RSI = 6, RDI = 7, R8 = 8, R9 = 9,
            R12 = 12, R13 = 13, R14 = 14, R15 = 15 };

struct Emitter {
    std::vector<uint8_t> out;
    /* Bumped at every jump target: code after one can be reached by more
       than one path, so the translator's register cache starts over. */
    uint32_t epoch = 0;

    void b(uint8_t v) { out.push_back(v); }
    void d32(int32_t v) {
        for (int i = 0; i < 4; ++i) b((uint8_t)((uint32_t)v >> (8 * i)));
    }
    void d64(uint64_t v) {
        for (int i = 0; i < 8; ++i) b((uint8_t)(v >> (8 * i)));
    }
    uint32_t here() const { return (uint32_t)out.size(); }
    /* A short forward jump (jz 74, jne 75, jmp EB) and where it lands. */
    uint32_t jump(uint8_t opcode) {
        b(opcode);
        b(0);
        ++open_short;
        return here() - 1;
    }
    /* Short jumps not yet landed: code inside one must not grow past what
       a rel8 reaches, so the register cache adds nothing there. */
    int open_short = 0;
    bool overflowed = false;
    void land(uint32_t from) {
        if (open_short > 0) --open_short;
        if (here() - (from + 1) > 127) overflowed = true; /* the block is thrown away */
        out[from] = (uint8_t)(here() - (from + 1));
        last_store.end = ~0u;
        ++epoch;
    }
    /* rel32 jumps: jmp E9, or a jcc given as its second opcode byte (0F xx). */
    uint32_t jump32(uint8_t jcc_or_0) {
        if (jcc_or_0) {
            b(0x0F);
            b(jcc_or_0);
        } else {
            b(0xE9);
        }
        d32(0);
        return here() - 4;
    }
    void land32(uint32_t from) {
        int32_t rel = (int32_t)(here() - (from + 4));
        std::memcpy(&out[from], &rel, 4);
        last_store.end = ~0u;
        ++epoch;
    }
    /* SSE op with a [rcx + disp32] operand: prefix (0 for none), 0F, opcode, xmm register. */
    void sse_mem(uint8_t prefix, uint8_t opcode, int xmm, int32_t disp) {
        if (prefix) b(prefix);
        b(0x0F);
        b(opcode);
        b((uint8_t)(0x80 | (xmm << 3) | 1));
        d32(disp);
    }
    /* 66 0F 38 op, register to register (SSSE3/SSE4.1). */
    void sse38(uint8_t opcode, int dst, int src) {
        b(0x66);
        b(0x0F);
        b(0x38);
        b(opcode);
        b((uint8_t)(0xC0 | (dst << 3) | src));
    }
    /* VEX.128.66.0F38 FMA form (B8 vfmadd231, BC vfnmadd231): xmm0 = +-(xmm1 * xmm2) + xmm0. */
    void fma231(uint8_t opcode, bool is_double) {
        b(0xC4);
        b(0xE2);
        b(is_double ? 0xF1 : 0x71); /* W, vvvv = ~xmm1, L=0, pp=66 */
        b(opcode);
        b(0xC2); /* dst xmm0, rm xmm2 */
    }
    /* SSE op between registers: prefix, 0F, opcode, then dst (reg) and src (rm). */
    void sse_rr(uint8_t prefix, uint8_t opcode, int dst, int src) {
        if (prefix) b(prefix);
        b(0x0F);
        b(opcode);
        b((uint8_t)(0xC0 | (dst << 3) | src));
    }

    /* The store just emitted, if nothing has come after it: a load of the
       same slot straight after it takes the value from the register instead
       (a jump landing in between clears it, since that path may not have
       stored). */
    struct LastStore {
        uint32_t end = ~0u;
        Host host = RAX;
        int32_t disp = 0;
    } last_store;
    /* mov host, [rcx + disp] (64-bit) */
    void load_struct(Host h, int32_t disp) {
        if (last_store.end == here() && last_store.disp == disp) {
            if (h != last_store.host) mov_rr(h, last_store.host, true);
            return;
        }
        b(0x48 | (h >= 8 ? 4 : 0));
        b(0x8B);
        b((uint8_t)(0x80 | ((h & 7) << 3) | 1));
        d32(disp);
    }
    /* mov [rcx + disp], host (64-bit) */
    void store_struct(Host h, int32_t disp) {
        b(0x48 | (h >= 8 ? 4 : 0));
        b(0x89);
        b((uint8_t)(0x80 | ((h & 7) << 3) | 1));
        d32(disp);
        last_store = {here(), h, disp};
    }
    /* mov host, imm64 */
    void mov_imm(Host h, uint64_t v) {
        if (v <= 0xffffffffull) { /* mov r32, imm32 zero-extends */
            if (h >= 8) b(0x41);
            b((uint8_t)(0xB8 + (h & 7)));
            d32((int32_t)(uint32_t)v);
            return;
        }
        b(0x48 | (h >= 8 ? 1 : 0));
        b((uint8_t)(0xB8 + (h & 7)));
        d64(v);
    }
    void zero(Host h) { /* xor r32, r32 */
        if (h >= 8) b(0x45);
        b(0x31);
        b((uint8_t)(0xC0 | ((h & 7) << 3) | (h & 7)));
    }
    /* op dst, src for the classic ALU opcodes (01 add, 29 sub, 21 and, 09 or, 31 xor) */
    void alu(uint8_t opcode, Host dst, Host src, bool wide) {
        uint8_t rex = (uint8_t)((wide ? 0x48 : 0x40) | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(opcode);
        b((uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7)));
    }
    void mov_rr(Host dst, Host src, bool wide) { alu(0x89, dst, src, wide); }
    /* Group-2 shift by immediate: /4 shl, /5 shr, /7 sar, /1 ror */
    void shift(int ext, Host h, int count, bool wide) {
        uint8_t rex = (uint8_t)((wide ? 0x48 : 0x40) | (h >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(0xC1);
        b((uint8_t)(0xC0 | (ext << 3) | (h & 7)));
        b((uint8_t)count);
    }
    /* Group-3 unary: /2 not, /3 neg */
    void unary(int ext, Host h, bool wide) {
        uint8_t rex = (uint8_t)((wide ? 0x48 : 0x40) | (h >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(0xF7);
        b((uint8_t)(0xC0 | (ext << 3) | (h & 7)));
    }
    void imul(Host dst, Host src, bool wide) {
        uint8_t rex = (uint8_t)((wide ? 0x48 : 0x40) | (dst >= 8 ? 4 : 0) | (src >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(0x0F);
        b(0xAF);
        b((uint8_t)(0xC0 | ((dst & 7) << 3) | (src & 7)));
    }
    /* setcc byte [rcx + disp] */
    void setcc_struct(uint8_t cc, int32_t disp) {
        b(0x0F);
        b((uint8_t)(0x90 | cc));
        b(0x81);
        d32(disp);
    }
    /* movzx r32, byte [rcx + disp] */
    void load_flag(Host h, int32_t disp) {
        if (h >= 8) b(0x44);
        b(0x0F);
        b(0xB6);
        b((uint8_t)(0x80 | ((h & 7) << 3) | 1));
        d32(disp);
    }
    void xor1(Host h) { /* xor r32, 1 */
        if (h >= 8) b(0x41);
        b(0x83);
        b((uint8_t)(0xF0 | (h & 7)));
        b(1);
    }
    void test32(Host a, Host c) {
        uint8_t rex = (uint8_t)(0x40 | (c >= 8 ? 4 : 0) | (a >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(0x85);
        b((uint8_t)(0xC0 | ((c & 7) << 3) | (a & 7)));
    }
    /* cmovcc dst, src */
    void cmov(uint8_t cc, Host dst, Host src, bool wide) {
        uint8_t rex = (uint8_t)((wide ? 0x48 : 0x40) | (dst >= 8 ? 4 : 0) | (src >= 8 ? 1 : 0));
        if (rex != 0x40) b(rex);
        b(0x0F);
        b((uint8_t)(0x40 | cc));
        b((uint8_t)(0xC0 | ((dst & 7) << 3) | (src & 7)));
    }
    /* Memory access at [r8 + disp]. kind: 0 store, 1 load zero-extend, 2 load sign-extend to 64, 3 sign-extend to 32. */
    void mem(Host data, int bytes, int kind, int32_t disp) {
        const uint8_t modrm = (uint8_t)(0x80 | ((data & 7) << 3) | 0); /* rm = r8 & 7 = 0 */
        const uint8_t rex_r = data >= 8 ? 4 : 0;
        if (kind == 0) {
            if (bytes == 2) b(0x66);
            b((uint8_t)((bytes == 8 ? 0x48 : 0x40) | rex_r | 1));
            b(bytes == 1 ? 0x88 : 0x89);
        } else if (kind == 1) {
            if (bytes == 8 || bytes == 4) {
                b((uint8_t)((bytes == 8 ? 0x48 : 0x40) | rex_r | 1));
                b(0x8B);
            } else {
                b((uint8_t)(0x40 | rex_r | 1));
                b(0x0F);
                b(bytes == 1 ? 0xB6 : 0xB7);
            }
        } else {
            bool to64 = kind == 2;
            if (bytes == 4) { /* movsxd */
                b((uint8_t)(0x48 | rex_r | 1));
                b(0x63);
            } else {
                b((uint8_t)((to64 ? 0x48 : 0x40) | rex_r | 1));
                b(0x0F);
                b(bytes == 1 ? 0xBE : 0xBF);
            }
        }
        b(modrm);
        d32(disp);
    }
    void ret_count(uint32_t n) {
        b(0xB8);
        d32((int32_t)n);
        b(0xC3);
    }
};

/* ---- Translation -------------------------------------------------------- */

struct Translator {
    Emitter e;
    std::vector<FaultSite> faults;

    /* A write-through cache of guest registers in the host's callee-saved
       registers (qb_jit_enter saves them): the GuestCpu copy is always
       current, so faults, helpers and the interpreter see what they always
       did, and a read of a register this block already has is a register
       move instead of a load that waits on the store before it. Emptied at
       every jump target (Emitter::epoch) and after every helper call, which
       may change any register. Index 31 is SP. */
    static constexpr Host kCacheRegs[8] = {RBX, RSI, RDI, RBP, R12, R13, R14, R15};
    int cache_slot[32];
    int cache_reg[8];
    uint32_t cache_used[8];
    uint32_t cache_tick = 0, cache_epoch = ~0u;
    static bool cache_on() {
        static const bool on = QB_ENV("QB_JIT_NOCACHE") == nullptr;
        return on;
    }
    /* KeepCache: a stretch with internal jumps whose paths touch no general
       register (a NaN guard's interpreter fallback, a divide's special
       cases) keeps the cache across its jump targets instead of losing it.
       Anything written to a register inside undoes that. */
    int cache_frozen = 0;
    bool frozen_written = false;
    struct KeepCache {
        Translator& t;
        uint32_t epoch;
        int slot[32], reg[8];
        bool on = QB_ENV("QB_JIT_NOKEEP") == nullptr;
        explicit KeepCache(Translator& owner) : t(owner) {
            if (!on) return;
            t.cache_sync();
            epoch = t.e.epoch;
            std::memcpy(slot, t.cache_slot, sizeof(slot));
            std::memcpy(reg, t.cache_reg, sizeof(reg));
            if (!t.cache_frozen++) t.frozen_written = false;
        }
        ~KeepCache() {
            if (!on) return;
            if (--t.cache_frozen || t.frozen_written) return; /* leave the flush */
            std::memcpy(t.cache_slot, slot, sizeof(slot));
            std::memcpy(t.cache_reg, reg, sizeof(reg));
            t.e.epoch = epoch;
            t.cache_epoch = epoch;
        }
    };
    void cache_sync() {
        if (cache_epoch == e.epoch) return;
        cache_epoch = e.epoch;
        for (int& slot : cache_slot) slot = -1;
        for (int& reg : cache_reg) reg = -1;
    }
    void cache_keep(int index, Host h) {
        int slot = cache_slot[index];
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < 8; ++i)
                if (cache_reg[i] < 0 || (cache_reg[slot] >= 0 && cache_used[i] < cache_used[slot])) slot = i;
            if (cache_reg[slot] >= 0) cache_slot[cache_reg[slot]] = -1;
            cache_reg[slot] = index;
            cache_slot[index] = slot;
        }
        cache_used[slot] = ++cache_tick;
        e.mov_rr(kCacheRegs[slot], h, true);
    }
    void get(Host h, int r, R31 mode) {
        if (r == 31 && mode != R31::Sp) {
            e.zero(h);
            return;
        }
        int32_t off = r == 31 ? kOffSp : off_x(r);
        if (!cache_on() || e.open_short || cache_frozen) {
            if (cache_on() && !e.open_short && cache_frozen) {
                cache_sync();
                int slot = cache_slot[r];
                if (slot >= 0) { /* reading a kept copy is safe; filling one is not */
                    e.mov_rr(h, kCacheRegs[slot], true);
                    return;
                }
            }
            e.load_struct(h, off);
            return;
        }
        cache_sync();
        int slot = cache_slot[r];
        if (slot >= 0) {
            cache_used[slot] = ++cache_tick;
            e.mov_rr(h, kCacheRegs[slot], true);
            return;
        }
        e.load_struct(h, off);
        cache_keep(r, h);
    }
    void put(Host h, int r, R31 mode) {
        if (r == 31 && mode != R31::Sp) return; /* the zero register discards */
        e.store_struct(h, r == 31 ? kOffSp : off_x(r));
        if (!cache_on()) return;
        cache_sync();
        if (cache_frozen) frozen_written = true; /* the kept state is no longer true */
        if (e.open_short || cache_frozen) { /* no room to keep it: forget any older copy */
            if (cache_slot[r] >= 0) {
                cache_reg[cache_slot[r]] = -1;
                cache_slot[r] = -1;
            }
            return;
        }
        cache_keep(r, h);
    }
    /* A 32-bit result is zero-extended before it is stored. */
    void narrow(Host h, bool wide) {
        if (!wide) e.mov_rr(h, h, false);
    }
    /* The extend of an extended-register operand, on rdx. */
    void extend(Host h, int option) {
        (void)h; /* always rdx */
        switch (option) {
            case 0: e.b(0x0F); e.b(0xB6); e.b(0xD2); break;             /* movzx edx, dl */
            case 1: e.b(0x0F); e.b(0xB7); e.b(0xD2); break;             /* movzx edx, dx */
            case 2: e.mov_rr(RDX, RDX, false); break;                   /* mov edx, edx */
            case 4: e.b(0x48); e.b(0x0F); e.b(0xBE); e.b(0xD2); break; /* movsx rdx, dl */
            case 5: e.b(0x48); e.b(0x0F); e.b(0xBF); e.b(0xD2); break; /* movsx rdx, dx */
            case 6: e.b(0x48); e.b(0x63); e.b(0xD2); break;             /* movsxd rdx, edx */
            default: break;                                             /* UXTX, SXTX */
        }
    }
    /* Which kind of instruction last set the flags (0 none, 1 subtract, 2
       add, 3 logical), and where the emitted code stood when that
       instruction finished: if a B.cond comes with nothing emitted since,
       the x86 flags are still that instruction's and it can branch on them. */
    int pending_flags = 0, live_kind = 0;
    uint32_t flags_live_at = ~0u;
    bool flags_still_live = false; /* this instruction left the live flags intact */
    /* The x86 condition for Arm's cc on the live flags, given what set them.
       Arm's C is not-borrow after a subtract and carry after an add or
       logical op; HI and LS have no single x86 test in the second case. */
    bool live_x86_cc(int cc, uint8_t* out) const {
        static const uint8_t after_subtract[14] = {0x4, 0x5, 0x3, 0x2, 0x8, 0x9, 0x0,
                                                   0x1, 0x7, 0x6, 0xD, 0xC, 0xF, 0xE};
        if (!live_kind || cc >= 14) return false;
        uint8_t x86 = after_subtract[cc];
        if (live_kind != 1) {
            if (cc == 2) x86 = 0x2;
            else if (cc == 3) x86 = 0x3;
            else if (cc == 8 || cc == 9) return false;
        }
        *out = x86;
        return true;
    }
    void flags_after(bool subtract, bool logical) {
        pending_flags = logical ? 3 : subtract ? 1 : 2;
        e.setcc_struct(0x8, kOffN);                              /* sets */
        e.setcc_struct(0x4, kOffZ);                              /* sete */
        e.setcc_struct(logical ? 0x2 : (subtract ? 0x3 : 0x2), kOffC); /* setb / setae (Arm C is not-borrow) */
        e.setcc_struct(0x0, kOffV);                              /* seto; logical ops leave OF clear */
    }
    void memory_op(uint64_t pc, Host data, int bytes, int kind, int32_t disp) {
        FaultSite site{e.here(), 0, pc};
        e.mem(data, bytes, kind, disp);
        site.host_end = e.here();
        faults.push_back(site);
    }
    /* Arm condition cc into eax as 0 or 1. Clobbers rdx. */
    void condition(int cc) {
        switch (cc & 0xe) {
            case 0: e.load_flag(RAX, kOffZ); break;
            case 2: e.load_flag(RAX, kOffC); break;
            case 4: e.load_flag(RAX, kOffN); break;
            case 6: e.load_flag(RAX, kOffV); break;
            case 8:
                e.load_flag(RAX, kOffZ);
                e.xor1(RAX);
                e.load_flag(RDX, kOffC);
                e.alu(0x21, RAX, RDX, false);
                break;
            case 10:
                e.load_flag(RAX, kOffN);
                e.load_flag(RDX, kOffV);
                e.alu(0x31, RAX, RDX, false);
                e.xor1(RAX);
                break;
            case 12:
                e.load_flag(RAX, kOffN);
                e.load_flag(RDX, kOffV);
                e.alu(0x31, RAX, RDX, false);
                e.xor1(RAX);
                e.load_flag(RDX, kOffZ);
                e.xor1(RDX);
                e.alu(0x21, RAX, RDX, false);
                break;
        }
        if ((cc & 1) && cc < 14) e.xor1(RAX);
    }
    void set_pc(uint64_t target) {
        e.mov_imm(RAX, target);
        e.store_struct(RAX, kOffPc);
    }
    static int32_t v_off(int r) { return (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)r); }
    /* A lane of bytes 1, 2, 4 or 8 at [rcx + disp] into rax (zero-extended),
       or from rax (or rdx) into it. */
    void load_lane(int32_t disp, int bytes) {
        if (bytes == 8) { e.load_struct(RAX, disp); return; }
        if (bytes == 4) { e.b(0x8B); e.b(0x81); e.d32(disp); return; }       /* mov eax, [rcx+d] */
        e.b(0x0F); e.b(bytes == 1 ? 0xB6 : 0xB7); e.b(0x81); e.d32(disp);    /* movzx eax, [rcx+d] */
    }
    void store_lane(int32_t disp, int bytes) {
        if (bytes == 8) { e.store_struct(RAX, disp); return; }
        if (bytes == 2) e.b(0x66);
        e.b(bytes == 1 ? 0x88 : 0x89); e.b(0x81); e.d32(disp);               /* mov [rcx+d], al/ax/eax */
    }
    void zero_high(int r) {
        e.zero(RDX);
        e.store_struct(RDX, v_off(r) + 8);
    }
    void store_constant_vector(int r, const GuestVec& value) {
        e.mov_imm(RAX, value.lo);
        e.store_struct(RAX, v_off(r));
        e.mov_imm(RAX, value.hi);
        e.store_struct(RAX, v_off(r) + 8);
    }

    /* Loads vector register r into xmm: 4 bytes (s), 8 (d, or a 64-bit
       vector), or all 16. Unused lanes come in as zero. */
    void load_v(int xmm, int r, int bytes) {
        if (bytes == 4) e.sse_mem(0xF3, 0x10, xmm, v_off(r));      /* movss */
        else if (bytes == 8) e.sse_mem(0xF3, 0x7E, xmm, v_off(r)); /* movq */
        else e.sse_mem(0, 0x10, xmm, v_off(r));                     /* movups */
    }
    /* Stores xmm0 to vector register r as Arm writes it: the low 4 or 8 bytes
       with the rest of the register zeroed, or all 16. */
    void store_v0(int r, int bytes) {
        if (bytes == 16) {
            e.sse_mem(0, 0x11, 0, v_off(r)); /* movups [q], xmm0 */
            return;
        }
        if (bytes == 4) {
            e.b(0x66); e.b(0x0F); e.b(0x7E); e.b(0xC0);             /* movd eax, xmm0 */
        } else {
            e.b(0x66); e.b(0x48); e.b(0x0F); e.b(0x7E); e.b(0xC0); /* movq rax, xmm0 */
        }
        e.store_struct(RAX, v_off(r));
        e.zero(RDX);
        e.store_struct(RDX, v_off(r) + 8);
    }
    /* A float result that came out NaN (any lane) goes to the interpreter
       instead: Arm and x86 choose different NaNs, and the interpreter is the
       reference. The inputs are untouched until the store, so re-running the
       instruction from the start is exact. */
    void nan_guarded_store(uint64_t pc, int rd, int bytes, bool is_double) {
        KeepCache keep(*this); /* the fallback redoes this FP instruction only */
        e.sse_rr(0, 0x28, 2, 0);                                   /* movaps xmm2, xmm0 */
        e.sse_rr(is_double ? 0x66 : 0, 0xC2, 2, 2);                /* cmpunordps/pd xmm2, xmm2 */
        e.b(0x03);
        e.sse_rr(is_double ? 0x66 : 0, 0x50, 0, 2);                /* movmskps/pd eax, xmm2 */
        /* Only the lanes this instruction owns: a 64-bit vector's upper
           lanes are zeros that may divide into NaN. */
        int lanes = is_double ? (bytes == 16 ? 2 : 1) : bytes / 4;
        e.b(0x83); e.b(0xE0); e.b((uint8_t)((1 << lanes) - 1));   /* and eax, lanes */
        e.test32(RAX, RAX);
        uint32_t slow = e.jump32(0x85);                            /* jnz -> interpreter */
        store_v0(rd, bytes);
        uint32_t done = e.jump32(0);
        e.land32(slow);
        interpret_inline(pc, block_count);
        e.land32(done);
    }

    /* xmm = 16 constant bytes. */
    void load_constant_xmm(int xmm, const uint8_t* bytes16) {
        uint64_t lo, hi;
        std::memcpy(&lo, bytes16, 8);
        std::memcpy(&hi, bytes16 + 8, 8);
        e.mov_imm(RAX, lo);
        e.b(0x66); e.b(0x48); e.b(0x0F); e.b(0x6E); e.b((uint8_t)(0xC0 | (xmm << 3)));               /* movq xmm, rax */
        e.mov_imm(RAX, hi);
        e.b(0x66); e.b(0x48); e.b(0x0F); e.b(0x3A); e.b(0x22); e.b((uint8_t)(0xC0 | (xmm << 3))); e.b(1); /* pinsrq xmm, rax, 1 */
    }
    /* d = bytes picked from n (index 0-15) and m (16-31); -1 gives zero. */
    void permute_two(int rd, int rn, int rm, const int* index, int bytes) {
        uint8_t from_n[16], from_m[16];
        for (int i = 0; i < 16; ++i) {
            from_n[i] = index[i] >= 0 && index[i] < 16 ? (uint8_t)index[i] : 0x80;
            from_m[i] = index[i] >= 16 ? (uint8_t)(index[i] - 16) : 0x80;
        }
        load_v(0, rn, 16);
        load_v(1, rm, 16);
        load_constant_xmm(2, from_n);
        e.sse38(0x00, 0, 2);                         /* pshufb xmm0, xmm2 */
        load_constant_xmm(2, from_m);
        e.sse38(0x00, 1, 2);                         /* pshufb xmm1, xmm2 */
        e.b(0x66); e.b(0x0F); e.b(0xEB); e.b(0xC1);  /* por xmm0, xmm1 */
        store_v0(rd, bytes);
    }

    int translate_simd(uint32_t insn, uint64_t pc) {
        const int rd = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
        const bool q = (insn >> 30) & 1;
        /* Scalar FADD, FSUB, FMUL, FDIV, FNMUL (single and double). */
        if ((insn & 0xff200c00u) == 0x1e200800u || (insn & 0xff200c00u) == 0x1e600800u) {
            bool is_double = (insn >> 22) & 1;
            int opcode = (insn >> 12) & 15;
            uint8_t op = opcode == 0 ? 0x59 : opcode == 1 ? 0x5E : opcode == 2 ? 0x58 : opcode == 3 ? 0x5C
                         : opcode == 8 ? 0x59 : 0;
            if (!op) return 0;
            int bytes = is_double ? 8 : 4;
            uint8_t prefix = is_double ? 0xF2 : 0xF3;
            load_v(0, rn, bytes);
            load_v(1, rm, bytes);
            e.sse_rr(prefix, op, 0, 1);
            if (opcode == 8) { /* FNMUL: the product negated */
                e.mov_imm(RAX, is_double ? 0x8000000000000000ull : 0x80000000ull);
                e.b(0x66); e.b(0x48); e.b(0x0F); e.b(0x6E); e.b(0xC8); /* movq xmm1, rax */
                e.sse_rr(0, 0x57, 0, 1);                               /* xorps xmm0, xmm1 */
            }
            nan_guarded_store(pc, rd, bytes, is_double);
            return 1;
        }
        /* Vector FADD, FSUB, FMUL, FDIV (4s, 2s, 2d). */
        if ((insn & 0x9f200400u) == 0x0e200400u) {
            int opcode = (insn >> 11) & 0x1f;
            bool u = (insn >> 29) & 1, a = (insn >> 23) & 1, is_double = (insn >> 22) & 1;
            uint8_t op = 0;
            if (!u && opcode == 0x1a) op = a ? 0x5C : 0x58;
            else if (u && !a && opcode == 0x1b) op = 0x59;
            else if (u && !a && opcode == 0x1f) op = 0x5E;
            if (op && !(is_double && !q)) {
                int bytes = q ? 16 : 8;
                load_v(0, rn, bytes);
                load_v(1, rm, bytes);
                e.sse_rr(is_double ? 0x66 : 0, op, 0, 1);
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            /* FMLA, FMLS (vector): fused, as FMA3 is. */
            if (!u && opcode == 0x19 && !(is_double && !q)) {
                int bytes = q ? 16 : 8;
                load_v(0, rd, bytes);
                load_v(1, rn, bytes);
                load_v(2, rm, bytes);
                e.fma231(a ? 0xBC : 0xB8, is_double);
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            /* FADDP (vector): adjacent pairs of n, then of m. */
            if (u && !a && opcode == 0x1a && !(is_double && !q)) {
                int bytes = q ? 16 : 8;
                load_v(0, rn, bytes);
                load_v(1, rm, bytes);
                e.sse_rr(is_double ? 0x66 : 0xF2, 0x7C, 0, 1); /* haddps/pd */
                if (!q) { e.sse_rr(0, 0xC6, 0, 0); e.b(0x08); } /* 2s: lanes 0 and 2 */
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            /* FCMEQ, FCMGE, FCMGT (vector, register): ordered compares give
               all-ones or zero lanes, false for NaN, as Arm's do. */
            if (opcode == 0x1c && !(is_double && !q) && !(!u && a)) {
                int bytes = q ? 16 : 8;
                uint8_t prefix = is_double ? 0x66 : 0;
                if (!u) { /* FCMEQ: n == m */
                    load_v(0, rn, bytes);
                    load_v(1, rm, bytes);
                    e.sse_rr(prefix, 0xC2, 0, 1); e.b(0x00);
                } else {  /* FCMGE: m <= n; FCMGT: m < n */
                    load_v(0, rm, bytes);
                    load_v(1, rn, bytes);
                    e.sse_rr(prefix, 0xC2, 0, 1); e.b(a ? 0x01 : 0x02);
                }
                store_v0(rd, bytes);
                return 1;
            }
            /* FABD: |n - m|. */
            if (u && a && opcode == 0x1a && !(is_double && !q)) {
                int bytes = q ? 16 : 8;
                load_v(0, rn, bytes);
                load_v(1, rm, bytes);
                e.sse_rr(is_double ? 0x66 : 0, 0x5C, 0, 1); /* subps/pd */
                e.mov_imm(RAX, is_double ? 0x7fffffffffffffffull : 0x7fffffff7fffffffull);
                e.b(0x66); e.b(0x48); e.b(0x0F); e.b(0x6E); e.b(0xC8); /* movq xmm1, rax */
                e.b(0x66); e.b(0x0F); e.b(0x6C); e.b(0xC9);            /* punpcklqdq xmm1, xmm1 */
                e.sse_rr(0, 0x54, 0, 1);                               /* andps */
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            /* FMAX, FMIN (vector). x86 answers the second operand for equal
               inputs, so both orders are taken and combined: AND for max
               (+0 over -0), OR for min (-0 over +0). NaN goes to the
               interpreter. */
            if (!u && opcode == 0x1e && !(is_double && !q)) {
                int bytes = q ? 16 : 8;
                uint8_t prefix = is_double ? 0x66 : 0, op = a ? 0x5D : 0x5F;
                load_v(0, rn, bytes);
                load_v(1, rm, bytes);
                /* A NaN in either input must reach the guard, which looks at
                   the result only: unordered lanes become all-ones, a NaN. */
                e.sse_rr(0, 0x28, 3, 0);                 /* movaps xmm3, xmm0 */
                e.sse_rr(prefix, 0xC2, 3, 1); e.b(0x03); /* cmpunord xmm3, xmm1 */
                e.sse_rr(0, 0x28, 2, 0);                 /* movaps xmm2, xmm0 */
                e.sse_rr(prefix, op, 0, 1);              /* xmm0 = op(n, m) */
                e.sse_rr(prefix, op, 1, 2);              /* xmm1 = op(m, n) */
                e.sse_rr(0, a ? 0x56 : 0x54, 0, 1);
                e.sse_rr(0, 0x56, 0, 3);                 /* orps xmm0, xmm3 */
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            /* ADDP (vector, integer): halves and words. */
            if (!u && opcode == 0x17 && (((insn >> 22) & 3) == 1 || ((insn >> 22) & 3) == 2)) {
                int size = (insn >> 22) & 3, bytes = q ? 16 : 8;
                load_v(0, rn, bytes);
                load_v(1, rm, bytes);
                if (q) {
                    e.sse38(size == 1 ? 0x01 : 0x02, 0, 1); /* phaddw/phaddd */
                } else {
                    e.b(0x66); e.b(0x0F); e.b(0x6C); e.b(0xC1); /* punpcklqdq xmm0, xmm1: n | m */
                    e.sse_rr(0, 0x57, 1, 1);                    /* xorps xmm1, xmm1 */
                    e.sse38(size == 1 ? 0x01 : 0x02, 0, 1);     /* sums of n, then of m, in the low half */
                }
                store_v0(rd, bytes);
                return 1;
            }
            /* Integer ADD, SUB, MUL, CMEQ, CMGT, CMGE, CMTST, and the signed and
               unsigned MAX/MIN, lane by lane. */
            {
                int size = (insn >> 22) & 3;
                if (size == 3 && !q && opcode != 0x03) return 0; /* the logical ops use size as an opcode */
                int bytes = q ? 16 : 8;
                bool done = true;
                auto three = [&](uint8_t op0f, uint8_t op38) { /* one of the two is used */
                    load_v(0, rn, bytes);
                    load_v(1, rm, bytes);
                    if (op38) e.sse38(op38, 0, 1);
                    else e.sse_rr(0x66, op0f, 0, 1);
                };
                if (opcode == 0x10) { /* ADD, SUB */
                    static const uint8_t add[4] = {0xFC, 0xFD, 0xFE, 0xD4}, sub_[4] = {0xF8, 0xF9, 0xFA, 0xFB};
                    three(u ? sub_[size] : add[size], 0);
                } else if (opcode == 0x13 && !u && (size == 1 || size == 2)) { /* MUL */
                    three(size == 1 ? 0xD5 : 0, size == 2 ? 0x40 : 0);
                } else if (opcode == 0x11 && u) { /* CMEQ */
                    three(size == 0 ? 0x74 : size == 1 ? 0x75 : size == 2 ? 0x76 : 0, size == 3 ? 0x29 : 0);
                } else if (opcode == 0x06 && !u) { /* CMGT */
                    three(size == 0 ? 0x64 : size == 1 ? 0x65 : size == 2 ? 0x66 : 0, size == 3 ? 0x37 : 0);
                } else if (opcode == 0x07 && !u) { /* CMGE: not (m > n) */
                    load_v(0, rm, bytes);
                    load_v(1, rn, bytes);
                    if (size == 3) e.sse38(0x37, 0, 1);
                    else e.sse_rr(0x66, (uint8_t)(0x64 + size), 0, 1);
                    e.sse_rr(0x66, 0x76, 1, 1); /* pcmpeqd xmm1, xmm1: all ones */
                    e.sse_rr(0x66, 0xEF, 0, 1); /* pxor */
                } else if (opcode == 0x11 && !u) { /* CMTST: (n & m) != 0 */
                    three(0xDB, 0);                                          /* pand */
                    e.sse_rr(0x66, 0xEF, 1, 1);                              /* pxor xmm1, xmm1 */
                    if (size == 3) e.sse38(0x29, 0, 1);
                    else e.sse_rr(0x66, (uint8_t)(0x74 + size), 0, 1);       /* == 0 */
                    e.sse_rr(0x66, 0x76, 1, 1);
                    e.sse_rr(0x66, 0xEF, 0, 1);                              /* not */
                } else if ((opcode == 0x0c || opcode == 0x0d) && size < 3) { /* MAX, MIN */
                    bool max = opcode == 0x0c;
                    uint8_t op0f = 0, op38 = 0;
                    if (!u) {
                        if (size == 1) op0f = max ? 0xEE : 0xEA;
                        else op38 = size == 0 ? (max ? 0x3C : 0x38) : (max ? 0x3D : 0x39);
                    } else {
                        if (size == 0) op0f = max ? 0xDE : 0xDA;
                        else op38 = size == 1 ? (max ? 0x3E : 0x3A) : (max ? 0x3F : 0x3B);
                    }
                    three(op0f, op38);
                } else {
                    done = false;
                }
                if (done) {
                    store_v0(rd, bytes);
                    return 1;
                }
            }
            /* Vector AND, BIC, ORR, ORN, EOR, BSL, BIT, BIF: bitwise, on the
               two 64-bit halves in general registers. */
            if (opcode == 0x03) {
                int kind = (u ? 4 : 0) | ((insn >> 22) & 3);
                for (int half = 0; half < (q ? 2 : 1); ++half) {
                    int32_t o = 8 * half;
                    e.load_struct(RAX, v_off(rn) + o);
                    e.load_struct(RDX, v_off(rm) + o);
                    switch (kind) {
                        case 0: e.alu(0x21, RAX, RDX, true); break;                            /* AND */
                        case 1: e.unary(2, RDX, true); e.alu(0x21, RAX, RDX, true); break;     /* BIC */
                        case 2: e.alu(0x09, RAX, RDX, true); break;                            /* ORR */
                        case 3: e.unary(2, RDX, true); e.alu(0x09, RAX, RDX, true); break;     /* ORN */
                        case 4: e.alu(0x31, RAX, RDX, true); break;                            /* EOR */
                        default: {
                            /* BSL: d = (d & n) | (~d & m); BIT: d = (n & m) | (d & ~m);
                               BIF: d = (d & m) | (n & ~m). As d ^ ((d ^ x) & mask). */
                            e.load_struct(R8, v_off(rd) + o);
                            if (kind == 5) {        /* BSL: m ^ ((m ^ n) & d) */
                                e.alu(0x31, RAX, RDX, true);
                                e.alu(0x21, RAX, R8, true);
                                e.alu(0x31, RAX, RDX, true);
                            } else if (kind == 6) { /* BIT: d ^ ((d ^ n) & m) */
                                e.alu(0x31, RAX, R8, true);
                                e.alu(0x21, RAX, RDX, true);
                                e.alu(0x31, RAX, R8, true);
                            } else {                /* BIF: d ^ ((d ^ n) & ~m) */
                                e.alu(0x31, RAX, R8, true);
                                e.unary(2, RDX, true);
                                e.alu(0x21, RAX, RDX, true);
                                e.alu(0x31, RAX, R8, true);
                            }
                            break;
                        }
                    }
                    e.store_struct(RAX, v_off(rd) + o);
                }
                if (!q) {
                    e.zero(RDX);
                    e.store_struct(RDX, v_off(rd) + 8);
                }
                return 1;
            }
            return 0;
        }
        /* DUP (element) and DUP (general): one value into every lane. The
           lane is widened to 64 bits by multiplying with a repeating 1. */
        if ((insn & 0xbfe0fc00u) == 0x0e000400u || (insn & 0xbfe0fc00u) == 0x0e000c00u) {
            bool from_general = ((insn >> 11) & 1) == 1;
            int imm5 = (insn >> 16) & 31;
            int size = imm5 & 1 ? 0 : imm5 & 2 ? 1 : imm5 & 4 ? 2 : imm5 & 8 ? 3 : -1;
            if (size < 0 || (size == 3 && !q)) return 0;
            int bytes = 1 << size;
            if (from_general) {
                get(RAX, rn, R31::Zero);
            } else {
                int index = imm5 >> (size + 1);
                e.load_struct(RAX, v_off(rn) + index * bytes - (index * bytes) % 8);
                int shift = ((index * bytes) % 8) * 8;
                if (shift) e.shift(5, RAX, shift, true);
            }
            if (size < 3) {
                e.mov_imm(RDX, size == 0 ? 0xffull : size == 1 ? 0xffffull : 0xffffffffull);
                e.alu(0x21, RAX, RDX, true);
                e.mov_imm(RDX, size == 0 ? 0x0101010101010101ull : size == 1 ? 0x0001000100010001ull
                                                                             : 0x0000000100000001ull);
                e.imul(RAX, RDX, true);
            }
            e.store_struct(RAX, v_off(rd));
            if (q) e.store_struct(RAX, v_off(rd) + 8);
            else {
                e.zero(RDX);
                e.store_struct(RDX, v_off(rd) + 8);
            }
            return 1;
        }
        /* CNT (bytes) and UADDLV (bytes), in 64-bit halves with the usual
           bit tricks: a bitset popcount loop is hot in Unity's main thread. */
        if ((insn & 0xbffffc00u) == 0x0e205800u || (insn & 0xbffffc00u) == 0x2e303800u) {
            bool cnt = (insn & 0xbffffc00u) == 0x0e205800u;
            auto masked = [&](Host dst, uint64_t mask) { /* dst &= mask, using RDX */
                e.mov_imm(RDX, mask);
                e.alu(0x21, dst, RDX, true);
            };
            if (cnt) {
                for (int half = 0; half < (q ? 2 : 1); ++half) {
                    e.load_struct(RAX, v_off(rn) + 8 * half);
                    e.mov_rr(R8, RAX, true);
                    e.shift(5, R8, 1, true);
                    masked(R8, 0x5555555555555555ull);
                    e.alu(0x29, RAX, R8, true); /* x - ((x >> 1) & 0x55..) */
                    e.mov_rr(R8, RAX, true);
                    e.shift(5, R8, 2, true);
                    masked(R8, 0x3333333333333333ull);
                    masked(RAX, 0x3333333333333333ull);
                    e.alu(0x01, RAX, R8, true);
                    e.mov_rr(R8, RAX, true);
                    e.shift(5, R8, 4, true);
                    e.alu(0x01, RAX, R8, true);
                    masked(RAX, 0x0f0f0f0f0f0f0f0full);
                    e.store_struct(RAX, v_off(rd) + 8 * half);
                }
                if (!q) {
                    e.zero(RAX);
                    e.store_struct(RAX, v_off(rd) + 8);
                }
                return 1;
            }
            /* UADDLV Hd: bytes paired into 16-bit lanes, then the lanes
               summed by a multiply; at most 16 * 255 fits. */
            e.zero(R9);
            for (int half = 0; half < (q ? 2 : 1); ++half) {
                e.load_struct(RAX, v_off(rn) + 8 * half);
                e.mov_rr(R8, RAX, true);
                e.shift(5, R8, 8, true);
                masked(R8, 0x00ff00ff00ff00ffull);
                masked(RAX, 0x00ff00ff00ff00ffull);
                e.alu(0x01, RAX, R8, true);
                e.mov_imm(RDX, 0x0001000100010001ull);
                e.imul(RAX, RDX, true);
                e.shift(5, RAX, 48, true);
                e.alu(0x01, R9, RAX, true);
            }
            e.store_struct(R9, v_off(rd));
            e.zero(RAX);
            e.store_struct(RAX, v_off(rd) + 8);
            return 1;
        }
        /* Vector FABS, FNEG: the sign bits cleared or flipped. */
        if ((insn & 0x9fbffc00u) == 0x0ea0f800u) {
            bool negate = (insn >> 29) & 1, is_double = (insn >> 22) & 1;
            if (is_double && !q) return 0;
            uint64_t sign = is_double ? 0x8000000000000000ull : 0x8000000080000000ull;
            e.mov_imm(RDX, negate ? sign : ~sign);
            for (int half = 0; half < (q ? 2 : 1); ++half) {
                e.load_struct(RAX, v_off(rn) + 8 * half);
                e.alu(negate ? 0x31 : 0x21, RAX, RDX, true);
                e.store_struct(RAX, v_off(rd) + 8 * half);
            }
            if (!q) {
                e.zero(RAX);
                e.store_struct(RAX, v_off(rd) + 8);
            }
            return 1;
        }
        /* FMUL (by element), single precision. */
        if ((insn & 0xbfc0f400u) == 0x0f809000u) {
            int index = (((insn >> 11) & 1) << 1) | ((insn >> 21) & 1);
            int m = (insn >> 16) & 31; /* M:Rm, all five bits for singles */
            int bytes = q ? 16 : 8;
            load_v(0, rn, bytes);
            e.sse_mem(0xF3, 0x10, 1, v_off(m) + 4 * index); /* movss xmm1, lane */
            e.sse_rr(0, 0xC6, 1, 1);                        /* shufps xmm1, xmm1, 0 */
            e.b(0x00);
            e.sse_rr(0, 0x59, 0, 1);                        /* mulps */
            nan_guarded_store(pc, rd, bytes, false);
            return 1;
        }
        /* Scalar FMOV, FABS, FNEG (bits), FSQRT, FCVT between single and double. */
        if ((insn & 0xff207c00u) == 0x1e204000u || (insn & 0xff207c00u) == 0x1e604000u) {
            bool is_double = (insn >> 22) & 1;
            int opcode = (insn >> 15) & 0x3f;
            int bytes = is_double ? 8 : 4;
            if (opcode <= 2) {
                e.load_struct(RAX, v_off(rn));
                if (!is_double) e.mov_rr(RAX, RAX, false);
                if (opcode) {
                    e.mov_imm(RDX, is_double ? 0x8000000000000000ull : 0x80000000ull);
                    if (opcode == 1) { e.unary(2, RDX, true); e.alu(0x21, RAX, RDX, true); }
                    else e.alu(0x31, RAX, RDX, true);
                }
                e.store_struct(RAX, v_off(rd));
                zero_high(rd);
                return 1;
            }
            if (opcode == 3) {
                load_v(0, rn, bytes);
                e.sse_rr(is_double ? 0xF2 : 0xF3, 0x51, 0, 0); /* sqrtss/sd */
                nan_guarded_store(pc, rd, bytes, is_double);
                return 1;
            }
            if ((opcode == 5 && !is_double) || (opcode == 4 && is_double)) {
                load_v(0, rn, bytes);
                e.sse_rr(is_double ? 0xF2 : 0xF3, 0x5A, 0, 0); /* cvtsd2ss / cvtss2sd */
                nan_guarded_store(pc, rd, is_double ? 4 : 8, !is_double);
                return 1;
            }
            return 0;
        }
        /* FCMP, FCMPE (with a register or with zero): Arm's NZCV from x86's
           unordered compare. less: N; equal: ZC; greater: C; unordered: CV. */
        if (((insn & 0xff20fc07u) | 0x10u) == 0x1e202010u || ((insn & 0xff20fc07u) | 0x10u) == 0x1e202018u ||
            ((insn & 0xff20fc07u) | 0x10u) == 0x1e602010u || ((insn & 0xff20fc07u) | 0x10u) == 0x1e602018u) {
            bool is_double = (insn >> 22) & 1, with_zero = (insn >> 3) & 1;
            int bytes = is_double ? 8 : 4;
            load_v(0, rn, bytes);
            if (with_zero) e.sse_rr(0, 0x57, 1, 1); /* xorps xmm1, xmm1 */
            else load_v(1, rm, bytes);
            e.sse_rr(is_double ? 0x66 : 0, 0x2E, 0, 1); /* ucomiss/sd xmm0, xmm1 */
            /* Every flag read before any instruction that would change them. */
            e.setcc_struct(0xA, kOffV);                               /* V = PF */
            e.b(0x0F); e.b(0x9B); e.b(0xC2);                          /* setnp dl */
            e.b(0x0F); e.b(0x94); e.b(0xC0);                          /* sete al */
            e.b(0x41); e.b(0x0F); e.b(0x92); e.b(0xC0);               /* setb r8b */
            e.b(0x41); e.b(0x0F); e.b(0x93); e.b(0xC1);               /* setae r9b */
            e.b(0x41); e.b(0x0F); e.b(0x9A); e.b(0xC3);               /* setp r11b */
            e.b(0x20); e.b(0xD0);                                     /* and al, dl */
            e.b(0x88); e.b(0x81); e.d32(kOffZ);                       /* Z = ZF and not PF */
            e.b(0x41); e.b(0x20); e.b(0xD0);                          /* and r8b, dl */
            e.b(0x44); e.b(0x88); e.b(0x81); e.d32(kOffN);            /* N = CF and not PF */
            e.b(0x45); e.b(0x08); e.b(0xD9);                          /* or r9b, r11b */
            e.b(0x44); e.b(0x88); e.b(0x89); e.d32(kOffC);            /* C = not CF, or PF */
            return 1;
        }
        /* FCSEL */
        if ((insn & 0xff200c00u) == 0x1e200c00u || (insn & 0xff200c00u) == 0x1e600c00u) {
            bool is_double = (insn >> 22) & 1;
            int cc = (insn >> 12) & 15;
            condition(cc);
            e.mov_rr(R9, RAX, false);
            e.load_struct(RAX, v_off(rn));
            e.load_struct(RDX, v_off(rm));
            if (cc < 14) {
                e.test32(R9, R9);
                e.cmov(0x4, RAX, RDX, true); /* condition false takes rm */
            }
            if (!is_double) e.mov_rr(RAX, RAX, false);
            e.store_struct(RAX, v_off(rd));
            zero_high(rd);
            return 1;
        }
        /* FMOV between general and FP registers. */
        {
            uint32_t form = insn & 0xfffffc00u;
            if (form == 0x1e260000u || form == 0x9e660000u) { /* FMOV Wd, Sn / Xd, Dn */
                e.load_struct(RAX, v_off(rn));
                if (form == 0x1e260000u) e.mov_rr(RAX, RAX, false);
                put(RAX, rd, R31::Zero);
                return 1;
            }
            if (form == 0x1e270000u || form == 0x9e670000u) { /* FMOV Sd, Wn / Dd, Xn */
                get(RAX, rn, R31::Zero);
                if (form == 0x1e270000u) e.mov_rr(RAX, RAX, false);
                e.store_struct(RAX, v_off(rd));
                zero_high(rd);
                return 1;
            }
            /* UCVTF (scalar) from a W register: zero-extended, then the
               64-bit signed conversion, exact for every 32-bit value. */
            if (form == 0x1e230000u || form == 0x1e630000u) {
                bool to_double = (insn >> 22) & 1;
                get(RAX, rn, R31::Zero);
                e.mov_rr(RAX, RAX, false);
                e.sse_rr(0, 0x57, 0, 0); /* xorps xmm0, xmm0 */
                e.b(to_double ? 0xF2 : 0xF3);
                e.b(0x48);
                e.b(0x0F); e.b(0x2A); e.b(0xC0); /* cvtsi2ss/sd xmm0, rax */
                store_v0(rd, to_double ? 8 : 4);
                return 1;
            }
            /* SCVTF (scalar, signed): exact with round-to-nearest, as Arm's default. */
            if (form == 0x1e220000u || form == 0x9e220000u || form == 0x1e620000u || form == 0x9e620000u) {
                bool from64 = insn >> 31, to_double = (insn >> 22) & 1;
                get(RAX, rn, R31::Zero);
                e.sse_rr(0, 0x57, 0, 0); /* xorps xmm0, xmm0 */
                e.b(to_double ? 0xF2 : 0xF3);
                if (from64) e.b(0x48);
                e.b(0x0F); e.b(0x2A); e.b(0xC0); /* cvtsi2ss/sd xmm0, eax/rax */
                store_v0(rd, to_double ? 8 : 4);
                return 1;
            }
            /* FCVTZS (scalar): x86 gives the "integer indefinite" for NaN and
               out of range, where Arm saturates; that value goes to the
               interpreter. */
            if (form == 0x1e380000u || form == 0x9e380000u || form == 0x1e780000u || form == 0x9e780000u) {
                bool to64 = insn >> 31, from_double = (insn >> 22) & 1;
                load_v(0, rn, from_double ? 8 : 4);
                e.b(from_double ? 0xF2 : 0xF3);
                if (to64) e.b(0x48);
                e.b(0x0F); e.b(0x2C); e.b(0xC0); /* cvttss/sd2si eax/rax, xmm0 */
                e.mov_imm(RDX, to64 ? 0x8000000000000000ull : 0x80000000ull);
                if (to64) { e.b(0x48); e.b(0x39); e.b(0xD0); } /* cmp rax, rdx */
                else { e.b(0x39); e.b(0xD0); }                 /* cmp eax, edx */
                uint32_t slow = e.jump32(0x84);                /* je -> interpreter */
                put(RAX, rd, R31::Zero);
                uint32_t done = e.jump32(0);
                e.land32(slow);
                interpret_inline(pc, block_count);
                e.land32(done);
                return 1;
            }
        }
        /* INS (element): one lane copied into another register's lane. */
        if ((insn & 0xffe08400u) == 0x6e000400u) {
            int imm5 = (insn >> 16) & 31, imm4 = (insn >> 11) & 15;
            int size = imm5 & 1 ? 0 : imm5 & 2 ? 1 : imm5 & 4 ? 2 : imm5 & 8 ? 3 : -1;
            if (size < 0) return 0;
            int bytes = 1 << size;
            load_lane(v_off(rn) + (imm4 >> size) * bytes, bytes);
            store_lane(v_off(rd) + (imm5 >> (size + 1)) * bytes, bytes);
            return 1;
        }
        /* INS (general): a general register into a lane. */
        if ((insn & 0xffe0fc00u) == 0x4e001c00u) {
            int imm5 = (insn >> 16) & 31;
            int size = imm5 & 1 ? 0 : imm5 & 2 ? 1 : imm5 & 4 ? 2 : imm5 & 8 ? 3 : -1;
            if (size < 0) return 0;
            int bytes = 1 << size;
            get(RAX, rn, R31::Zero);
            store_lane(v_off(rd) + (imm5 >> (size + 1)) * bytes, bytes);
            return 1;
        }
        /* DUP (element, scalar), the MOV Sd, Vn.S[i] alias. */
        if ((insn & 0xffe0fc00u) == 0x5e000400u) {
            int imm5 = (insn >> 16) & 31;
            int size = imm5 & 1 ? 0 : imm5 & 2 ? 1 : imm5 & 4 ? 2 : imm5 & 8 ? 3 : -1;
            if (size < 0) return 0;
            int bytes = 1 << size;
            load_lane(v_off(rn) + (imm5 >> (size + 1)) * bytes, bytes);
            e.store_struct(RAX, v_off(rd));
            zero_high(rd);
            return 1;
        }
        /* UMOV: a lane, zero-extended, into a general register. */
        if ((insn & 0xbfe0fc00u) == 0x0e003c00u) {
            int imm5 = (insn >> 16) & 31;
            int size = imm5 & 1 ? 0 : imm5 & 2 ? 1 : imm5 & 4 ? 2 : imm5 & 8 ? 3 : -1;
            if (size < 0 || (size == 3) != q) return 0;
            int bytes = 1 << size;
            load_lane(v_off(rn) + (imm5 >> (size + 1)) * bytes, bytes);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* EXT: bytes from the pair Vm:Vn. */
        if ((insn & 0xbfe08400u) == 0x2e000000u) {
            int imm4 = (insn >> 11) & 15;
            if (q) {
                load_v(0, rn, 16);
                load_v(1, rm, 16);
                e.b(0x66); e.b(0x0F); e.b(0x3A); e.b(0x0F); e.b(0xC8); e.b((uint8_t)imm4); /* palignr xmm1, xmm0 */
                e.sse_mem(0, 0x11, 1, v_off(rd));                                        /* movups [q], xmm1 */
                return 1;
            }
            if (imm4 >= 8) return 0;
            e.load_struct(RAX, v_off(rn));
            e.load_struct(RDX, v_off(rm));
            if (imm4) { e.b(0x48); e.b(0x0F); e.b(0xAC); e.b(0xD0); e.b((uint8_t)(imm4 * 8)); } /* shrd rax, rdx */
            e.store_struct(RAX, v_off(rd));
            zero_high(rd);
            return 1;
        }
        /* ZIP, UZP, TRN. */
        if ((insn & 0xbf208c00u) == 0x0e000800u) {
            int opcode = (insn >> 12) & 7, size = (insn >> 22) & 3;
            bool second = opcode >= 5;
            int kind = opcode & 3; /* 1 UZP, 2 TRN, 3 ZIP */
            if (kind == 0 || (size == 3 && !q)) return 0;
            if (!q && size == 2) kind = 3;         /* for 2s all three are the same pairs */
            if (size == 3) kind = 3;                /* and for 2d */
            if (!q && kind != 3) return 0;
            if ((size <= 1) && kind != 3) {
                /* UZP, TRN of bytes and halves: a byte permutation of n and m. */
                int esize = 1 << size, bytes = q ? 16 : 8, elements = bytes / esize;
                int index[16];
                for (int i = 0; i < 16; ++i) index[i] = -1;
                for (int el = 0; el < elements; ++el) {
                    int from; /* element of the concatenation n, m */
                    if (kind == 1) from = 2 * el + (second ? 1 : 0);
                    else from = (el & 1) ? elements + (el & ~1) + (second ? 1 : 0) : el + (second ? 1 : 0);
                    for (int b = 0; b < esize; ++b) {
                        int source = from < elements ? from * esize + b : 16 + (from - elements) * esize + b;
                        index[el * esize + b] = source;
                    }
                }
                permute_two(rd, rn, rm, index, bytes);
                return 1;
            }
            load_v(0, rn, q ? 16 : 8);
            load_v(1, rm, q ? 16 : 8);
            if (kind == 3) {
                uint8_t low = size == 0 ? 0x60 : size == 1 ? 0x61 : size == 2 ? 0x14 : 0x6C;
                uint8_t high = size == 0 ? 0x68 : size == 1 ? 0x69 : size == 2 ? 0x15 : 0x6D;
                bool packed_single = size == 2;
                if (q) {
                    e.sse_rr(packed_single ? 0 : 0x66, second ? high : low, 0, 1);
                } else {
                    e.sse_rr(packed_single ? 0 : 0x66, low, 0, 1);
                    if (second) { e.b(0x66); e.b(0x0F); e.b(0x73); e.b(0xD8); e.b(8); } /* psrldq xmm0, 8 */
                }
            } else { /* 4s UZP or TRN */
                e.sse_rr(0, 0xC6, 0, 1);
                e.b(second ? 0xDD : 0x88); /* shufps: lanes 0,2 (or 1,3) of each */
                if (kind == 2) { e.sse_rr(0, 0xC6, 0, 0); e.b(0xD8); } /* then interleave for TRN */
            }
            store_v0(rd, q ? 16 : 8);
            return 1;
        }
        /* SCVTF (vector, signed, singles) and FCVTZS (vector, singles). */
        if ((insn & 0xbffffc00u) == 0x0e21d800u) {
            load_v(0, rn, q ? 16 : 8);
            e.sse_rr(0, 0x5B, 0, 0); /* cvtdq2ps */
            store_v0(rd, q ? 16 : 8);
            return 1;
        }
        if ((insn & 0xbffffc00u) == 0x0ea1b800u) {
            load_v(0, rn, q ? 16 : 8);
            KeepCache keep(*this);
            e.sse_rr(0xF3, 0x5B, 0, 0);                          /* cvttps2dq */
            e.sse_rr(0, 0x28, 2, 0);                             /* movaps xmm2, xmm0 */
            e.mov_imm(RAX, 0x80000000ull);
            e.b(0x66); e.b(0x0F); e.b(0x6E); e.b(0xC8);          /* movd xmm1, eax */
            e.b(0x66); e.b(0x0F); e.b(0x70); e.b(0xC9); e.b(0);  /* pshufd xmm1, xmm1, 0 */
            e.b(0x66); e.b(0x0F); e.b(0x76); e.b(0xD1);          /* pcmpeqd xmm2, xmm1 */
            e.sse_rr(0, 0x50, 0, 2);                             /* movmskps eax, xmm2 */
            e.b(0x83); e.b(0xE0); e.b(q ? 15 : 3);               /* and eax, lanes */
            e.test32(RAX, RAX);
            uint32_t slow = e.jump32(0x85);
            store_v0(rd, q ? 16 : 8);
            uint32_t done = e.jump32(0);
            e.land32(slow);
            interpret_inline(pc, block_count);
            e.land32(done);
            return 1;
        }
        /* SSHLL, USHLL (and SXTL, UXTL): widen the low or high half, then
           shift left. */
        if ((insn & 0x9f80fc00u) == 0x0f00a400u && ((insn >> 19) & 15) != 0 && ((insn >> 22) & 1) == 0) {
            bool unsigned_ = (insn >> 29) & 1;
            int immh = (insn >> 19) & 15, shift_imm = (insn >> 16) & 127;
            int size = immh >= 4 ? 2 : immh >= 2 ? 1 : 0;
            int shift = shift_imm - (8 << size);
            e.sse_mem(0xF3, 0x7E, 0, v_off(rn) + (q ? 8 : 0)); /* movq xmm0, the half */
            static const uint8_t sx[3] = {0x20, 0x23, 0x25}, zx[3] = {0x30, 0x33, 0x35};
            e.sse38(unsigned_ ? zx[size] : sx[size], 0, 0);
            if (shift) {
                e.b(0x66); e.b(0x0F); e.b((uint8_t)(0x71 + size)); e.b(0xF0); e.b((uint8_t)shift); /* psllw/d/q */
            }
            store_v0(rd, 16);
            return 1;
        }
        /* XTN, XTN2: each lane's low half, packed, in general registers. */
        if ((insn & 0xbf3ffc00u) == 0x0e212800u && ((insn >> 22) & 3) != 3) {
            int size = (insn >> 22) & 3, width = 8 << size; /* output lane width */
            uint64_t mask = width == 32 ? 0xffffffffull : (1ull << width) - 1;
            e.zero(R9);
            for (int half = 0; half < 2; ++half) {
                e.load_struct(RAX, v_off(rn) + 8 * half);
                for (int lane = 0; lane < 64 / (2 * width); ++lane) {
                    e.mov_rr(RDX, RAX, true);
                    if (lane) e.shift(5, RDX, 2 * width * lane, true);
                    e.mov_imm(R8, mask);
                    e.alu(0x21, RDX, R8, true);
                    int to = width * lane + 32 * half;
                    if (to) e.shift(4, RDX, to, true);
                    e.alu(0x09, R9, RDX, true);
                }
            }
            if (q) {
                e.store_struct(R9, v_off(rd) + 8);
            } else {
                e.store_struct(R9, v_off(rd));
                e.zero(RDX);
                e.store_struct(RDX, v_off(rd) + 8);
            }
            return 1;
        }
        /* REV64: lanes reversed within each 64-bit half. */
        if ((insn & 0xbf3ffc00u) == 0x0e200800u && ((insn >> 22) & 3) != 3) {
            int size = (insn >> 22) & 3;
            for (int half = 0; half < (q ? 2 : 1); ++half) {
                e.load_struct(RAX, v_off(rn) + 8 * half);
                if (size == 2) {
                    e.shift(0, RAX, 32, true); /* rol rax, 32 */
                } else {
                    e.b(0x48); e.b(0x0F); e.b(0xC8); /* bswap rax */
                    if (size == 1) {                /* bytes back in order within each half-word */
                        e.mov_rr(RDX, RAX, true);
                        e.shift(5, RDX, 8, true);
                        e.mov_imm(R8, 0x00ff00ff00ff00ffull);
                        e.alu(0x21, RDX, R8, true);
                        e.alu(0x21, RAX, R8, true);
                        e.shift(4, RAX, 8, true);
                        e.alu(0x09, RAX, RDX, true);
                    }
                }
                e.store_struct(RAX, v_off(rd) + 8 * half);
            }
            if (!q) {
                e.zero(RDX);
                e.store_struct(RDX, v_off(rd) + 8);
            }
            return 1;
        }
        /* UCVTF (vector, singles), and SCVTF/UCVTF (scalar, singles, from a
           SIMD register). Unsigned: the top and bottom 16 bits converted
           apart, both exact, so the one rounding is in the add. */
        if ((insn & 0xbffffc00u) == 0x2e21d800u || (insn & 0xfffffc00u) == 0x5e21d800u ||
            (insn & 0xfffffc00u) == 0x7e21d800u) {
            bool scalar = ((insn >> 28) & 1) == 1, unsigned_ = (insn >> 29) & 1;
            int bytes = scalar ? 4 : q ? 16 : 8;
            load_v(0, rn, bytes);
            if (!unsigned_) {
                e.sse_rr(0, 0x5B, 0, 0); /* cvtdq2ps */
            } else {
                e.sse_rr(0, 0x28, 1, 0);                                    /* movaps xmm1, xmm0 */
                e.b(0x66); e.b(0x0F); e.b(0x72); e.b(0xD1); e.b(16);        /* psrld xmm1, 16 */
                e.b(0x66); e.b(0x0F); e.b(0x72); e.b(0xF0); e.b(16);        /* pslld xmm0, 16 */
                e.b(0x66); e.b(0x0F); e.b(0x72); e.b(0xD0); e.b(16);        /* psrld xmm0, 16 */
                e.sse_rr(0, 0x5B, 1, 1);                                    /* cvtdq2ps xmm1 */
                e.sse_rr(0, 0x5B, 0, 0);                                    /* cvtdq2ps xmm0 */
                e.mov_imm(RAX, 0x47800000ull);                              /* 65536.0f */
                e.b(0x66); e.b(0x0F); e.b(0x6E); e.b(0xD0);                 /* movd xmm2, eax */
                e.b(0x66); e.b(0x0F); e.b(0x70); e.b(0xD2); e.b(0);         /* pshufd xmm2, xmm2, 0 */
                e.sse_rr(0, 0x59, 1, 2);                                    /* mulps xmm1, xmm2 */
                e.sse_rr(0, 0x58, 0, 1);                                    /* addps xmm0, xmm1 */
            }
            store_v0(rd, bytes);
            return 1;
        }
        /* TBL, one table register: pshufb, with indexes pushed to 0x70-0x7f
           when in range and to 0x80 or more (a zero) when not. */
        if ((insn & 0xbfe0fc00u) == 0x0e000000u) {
            load_v(0, rn, 16);
            load_v(2, rm, q ? 16 : 8);
            e.mov_imm(RAX, 0x70707070ull);
            e.b(0x66); e.b(0x0F); e.b(0x6E); e.b(0xD8);             /* movd xmm3, eax */
            e.b(0x66); e.b(0x0F); e.b(0x70); e.b(0xDB); e.b(0);     /* pshufd xmm3, xmm3, 0 */
            e.b(0x66); e.b(0x0F); e.b(0xDC); e.b(0xD3);             /* paddusb xmm2, xmm3 */
            e.sse38(0x00, 0, 2);                                    /* pshufb xmm0, xmm2 */
            store_v0(rd, q ? 16 : 8);
            return 1;
        }
        /* FMUL (scalar, by element), singles. */
        if ((insn & 0xffc0f400u) == 0x5f809000u) {
            int index = (((insn >> 11) & 1) << 1) | ((insn >> 21) & 1);
            int m = (insn >> 16) & 31;
            e.sse_mem(0xF3, 0x10, 0, v_off(rn));             /* movss xmm0, n */
            e.sse_mem(0xF3, 0x10, 1, v_off(m) + 4 * index); /* movss xmm1, the lane */
            e.sse_rr(0xF3, 0x59, 0, 1);                     /* mulss */
            nan_guarded_store(pc, rd, 4, false);
            return 1;
        }
        /* NOT (vector) */
        if ((insn & 0xbffffc00u) == 0x2e205800u) {
            for (int half = 0; half < (q ? 2 : 1); ++half) {
                e.load_struct(RAX, v_off(rn) + 8 * half);
                e.unary(2, RAX, true);
                e.store_struct(RAX, v_off(rd) + 8 * half);
            }
            if (!q) zero_high(rd);
            return 1;
        }
        /* FMLA, FMLS (by element), singles. */
        if ((insn & 0xbfc0b400u) == 0x0f801000u) {
            bool subtract = (insn >> 14) & 1;
            int index = (((insn >> 11) & 1) << 1) | ((insn >> 21) & 1);
            int m = (insn >> 16) & 31;
            int bytes = q ? 16 : 8;
            load_v(0, rd, bytes);
            load_v(1, rn, bytes);
            e.sse_mem(0xF3, 0x10, 2, v_off(m) + 4 * index); /* movss xmm2, lane */
            e.sse_rr(0, 0xC6, 2, 2);
            e.b(0x00);                                      /* broadcast */
            e.fma231(subtract ? 0xBC : 0xB8, false);
            nan_guarded_store(pc, rd, bytes, false);
            return 1;
        }
        (void)rm;
        return 0;
    }

    /* Calls jit_interpret(cpu, pc) for one instruction. Blocks are entered
       with rsp 8 past 16-byte alignment; pushing rcx and r10 and reserving
       the 32-byte shadow space plus 8 lines the call up. If it did not fall
       through, the block returns the count of what it had finished. */
    void interpret_inline(uint64_t pc, uint32_t done_before) {
        call_helper((uint64_t)(uintptr_t)&jit_interpret, pc, done_before);
    }
    /* A host call (BL into the thunk page): x30 set as the BL would, then
       the host function, then on with the block. */
    uint64_t thunk_va = 0, thunk_end = 0;
    int thunk_stride = 16;
    bool host_call(uint64_t pc, uint64_t target, uint32_t done_before) {
        if (target < thunk_va || target >= thunk_end || (target - thunk_va) % (uint64_t)thunk_stride) return false;
        e.mov_imm(RAX, pc + 4);
        put(RAX, 30, R31::Zero);
        call_helper((uint64_t)(uintptr_t)&jit_host_call, (target - thunk_va) / (uint64_t)thunk_stride, done_before);
        return true;
    }
    void call_helper(uint64_t helper, uint64_t argument, uint32_t done_before) {
        e.b(0x41); e.b(0x52);                                   /* push r10 */
        e.b(0x51);                                              /* push rcx */
        e.b(0x48); e.b(0x83); e.b(0xEC); e.b(40);               /* sub rsp, 40 */
        e.b(0x48); e.b(0xBA); e.d64(argument);                  /* mov rdx, argument */
        e.b(0x48); e.b(0xB8); e.d64(helper);                    /* mov rax, helper */
        e.b(0xFF); e.b(0xD0);                                   /* call rax */
        e.b(0x48); e.b(0x83); e.b(0xC4); e.b(40);               /* add rsp, 40 */
        e.b(0x59);                                              /* pop rcx */
        e.b(0x41); e.b(0x5A);                                   /* pop r10 */
        e.test32(RAX, RAX);
        uint32_t fell_through = e.jump(0x75);                   /* jnz -> carry on */
        e.b(0x41); e.b(0x81); e.b(0xC2); e.d32((int32_t)done_before); /* add r10d, done */
        e.b(0x44); e.b(0x89); e.b(0xD0); e.b(0xC3);             /* mov eax, r10d; ret */
        e.land(fell_through);
    }

    /* How the block ends, which compile() turns into exits that can chain:
       straight to a known pc, to one of two on the condition in eax, or to a
       pc already stored (an indirect branch). */
    enum class Exit { None, Direct, Conditional, Indirect, ConditionalFlags };
    uint8_t exit_x86_cc = 0;
    Exit exit = Exit::None;
    uint64_t exit_taken = 0, exit_fallthrough = 0;
    std::vector<std::pair<uint32_t, uint64_t>> slots;
    void exit_to(uint64_t target) {
        exit = Exit::Direct;
        exit_taken = target;
    }
    /* pc = eax ? taken : fallthrough */
    void set_pc_if(uint64_t taken, uint64_t fallthrough) {
        exit = Exit::Conditional;
        exit_taken = taken;
        exit_fallthrough = fallthrough;
    }

    static uint64_t bit_mask(int n, int immr, int imms, int width, bool* ok) {
        unsigned combined = ((unsigned)n << 6) | ((~(unsigned)imms) & 63);
        int len = -1;
        for (int bit = 6; bit >= 0; --bit)
            if (combined & (1u << bit)) {
                len = bit;
                break;
            }
        if (len < 1) {
            *ok = false;
            return 0;
        }
        unsigned size = 1u << len, levels = size - 1, s = (unsigned)imms & levels, r = (unsigned)immr & levels;
        if (s == levels) {
            *ok = false;
            return 0;
        }
        uint64_t elem = (s + 1 == 64) ? ~0ull : ((1ull << (s + 1)) - 1);
        if (size < 64) elem &= (1ull << size) - 1;
        if (r) elem = ((elem >> r) | (elem << (size - r))) & (size == 64 ? ~0ull : ((1ull << size) - 1));
        uint64_t imm = 0;
        for (unsigned i = 0; i < 64; i += size) imm |= elem << i;
        if (width == 32) imm &= 0xffffffffull;
        *ok = true;
        return imm;
    }

    /* Returns 1 if translated and the block goes on, 2 if translated and it
       ends the block (a branch), 0 if not translated. */
    uint32_t block_count = 0; /* instructions finished before this one */
    int translate(uint32_t insn, uint64_t pc) {
        const bool wide = (insn >> 31) & 1;
        const int rd = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
        const uint64_t next = pc + 4;

        /* NOP and the other hints. */
        if ((insn & 0xfffff01fu) == 0xd503201fu) return 1;

        /* ADD/SUB (immediate) */
        if ((insn & 0x1f000000u) == 0x11000000u) {
            bool sub = (insn >> 30) & 1, setflags = (insn >> 29) & 1;
            if ((insn >> 23) & 1) return 0;
            uint32_t imm = (insn >> 10) & 0xfff;
            if ((insn >> 22) & 1) imm <<= 12;
            get(RAX, rn, R31::Sp);
            if (setflags || !wide || imm) {
                if (setflags) {
                    e.mov_imm(RDX, imm);
                    e.alu(sub ? 0x29 : 0x01, RAX, RDX, wide);
                    flags_after(sub, false);
                } else if (imm) {
                    e.mov_imm(RDX, imm);
                    e.alu(sub ? 0x29 : 0x01, RAX, RDX, wide);
                }
            }
            narrow(RAX, wide);
            put(RAX, rd, setflags ? R31::Zero : R31::Sp);
            return 1;
        }
        /* ADD/SUB (shifted register) */
        if ((insn & 0x1f200000u) == 0x0b000000u) {
            bool sub = (insn >> 30) & 1, setflags = (insn >> 29) & 1;
            int type = (insn >> 22) & 3, amount = (insn >> 10) & 63;
            if (type == 3 || (!wide && amount > 31)) return 0;
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            if (amount) e.shift(type == 0 ? 4 : type == 1 ? 5 : 7, RDX, amount, wide);
            e.alu(sub ? 0x29 : 0x01, RAX, RDX, wide);
            if (setflags) flags_after(sub, false);
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* Logical (shifted register): AND, BIC, ORR, ORN, EOR, EON, ANDS, BICS */
        if ((insn & 0x1f000000u) == 0x0a000000u) {
            int opc = (insn >> 29) & 3, type = (insn >> 22) & 3, amount = (insn >> 10) & 63;
            bool invert = (insn >> 21) & 1;
            if (!wide && amount > 31) return 0;
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            if (amount) e.shift(type == 0 ? 4 : type == 1 ? 5 : type == 2 ? 7 : 1, RDX, amount, wide);
            if (invert) e.unary(2, RDX, wide);
            e.alu(opc == 1 ? 0x09 : opc == 2 ? 0x31 : 0x21, RAX, RDX, wide);
            if (opc == 3) flags_after(false, true);
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* Logical (immediate): AND, ORR, EOR write SP for rd 31; ANDS the zero register. */
        if ((insn & 0x1f800000u) == 0x12000000u) {
            int opc = (insn >> 29) & 3, n = (insn >> 22) & 1, immr = (insn >> 16) & 63, imms = (insn >> 10) & 63;
            if (!wide && n) return 0;
            bool ok = false;
            uint64_t imm = bit_mask(n, immr, imms, wide ? 64 : 32, &ok);
            if (!ok) return 0;
            get(RAX, rn, R31::Zero);
            e.mov_imm(RDX, imm);
            e.alu(opc == 1 ? 0x09 : opc == 2 ? 0x31 : 0x21, RAX, RDX, wide);
            if (opc == 3) flags_after(false, true);
            narrow(RAX, wide);
            put(RAX, rd, opc == 3 ? R31::Zero : R31::Sp);
            return 1;
        }
        /* MOVN, MOVZ, MOVK */
        if ((insn & 0x1f800000u) == 0x12800000u) {
            int opc = (insn >> 29) & 3, hw = (insn >> 21) & 3;
            if (opc == 1 || (!wide && hw > 1)) return 0;
            uint64_t imm = (uint64_t)((insn >> 5) & 0xffff) << (16 * hw);
            if (opc == 3) {
                get(RAX, rd, R31::Zero);
                e.mov_imm(RDX, ~(0xffffull << (16 * hw)));
                e.alu(0x21, RAX, RDX, true);
                e.mov_imm(RDX, imm);
                e.alu(0x09, RAX, RDX, true);
            } else {
                uint64_t value = opc == 0 ? ~imm : imm;
                if (!wide) value &= 0xffffffffull;
                e.mov_imm(RAX, value);
            }
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* ADR, ADRP */
        if ((insn & 0x1f000000u) == 0x10000000u) {
            int64_t imm = (int64_t)((((insn >> 5) & 0x7ffff) << 2) | ((insn >> 29) & 3));
            imm = (imm << 43) >> 43; /* 21-bit signed */
            uint64_t value = (insn >> 31) ? ((pc & ~0xfffull) + (uint64_t)(imm << 12)) : pc + (uint64_t)imm;
            e.mov_imm(RAX, value);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* UBFM, SBFM (LSL, LSR, ASR, UXTB, SXTW and friends) */
        if ((insn & 0x1f800000u) == 0x13000000u) {
            int opc = (insn >> 29) & 3, n = (insn >> 22) & 1, immr = (insn >> 16) & 63, imms = (insn >> 10) & 63;
            int width = wide ? 64 : 32;
            if (opc == 1) goto not_sbfm_ubfm; /* BFM: its own case below */
            if (n != (int)wide || (!wide && (immr > 31 || imms > 31))) return 0;
            get(RAX, rn, R31::Zero);
            if (opc == 2) { /* UBFM */
                if (imms >= immr) {
                    int len = imms - immr + 1;
                    if (immr) e.shift(5, RAX, immr, wide);
                    if (len < width) {
                        e.mov_imm(RDX, (1ull << len) - 1);
                        e.alu(0x21, RAX, RDX, wide);
                    }
                } else {
                    int len = imms + 1;
                    e.mov_imm(RDX, (1ull << len) - 1);
                    e.alu(0x21, RAX, RDX, wide);
                    e.shift(4, RAX, width - immr, wide);
                }
            } else { /* SBFM */
                if (imms >= immr) {
                    e.shift(4, RAX, width - 1 - imms, wide);
                    e.shift(7, RAX, width - 1 - imms + immr, wide);
                } else {
                    e.shift(4, RAX, width - 1 - imms, wide);
                    e.shift(7, RAX, width - 1 - imms, wide);
                    e.shift(4, RAX, width - immr, wide);
                }
            }
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
    not_sbfm_ubfm:
        /* CCMP, CCMN (register and immediate): the compare's flags if the
           condition holds, the instruction's own NZCV if not. */
        if ((insn & 0x3fe00410u) == 0x3a400000u) {
            bool subtract = (insn >> 30) & 1, immediate = (insn >> 11) & 1;
            int cc = (insn >> 12) & 15, nzcv = insn & 15, m = (insn >> 16) & 31;
            uint32_t otherwise = 0;
            if (cc < 14) {
                condition(cc);
                e.test32(RAX, RAX);
                otherwise = e.jump32(0x84); /* jz */
            }
            get(RAX, rn, R31::Zero);
            if (immediate) e.mov_imm(RDX, (uint64_t)m);
            else get(RDX, m, R31::Zero);
            e.alu(subtract ? 0x29 : 0x01, RAX, RDX, wide);
            flags_after(subtract, false);
            if (cc < 14) {
                uint32_t done = e.jump32(0);
                e.land32(otherwise);
                const int32_t offs[4] = {kOffN, kOffZ, kOffC, kOffV};
                for (int i = 0; i < 4; ++i) {
                    e.b(0xC6); e.b(0x81); e.d32(offs[i]); e.b((uint8_t)((nzcv >> (3 - i)) & 1)); /* mov byte [rcx+f], imm */
                }
                e.land32(done);
                pending_flags = 0; /* two paths meet: the x86 flags are not the Arm ones */
            }
            return 1;
        }
        /* LDADD, LDCLR, LDEOR, LDSET (and their ST aliases), every size and
           ordering: x86 locked operations are full barriers. Add is one
           lock xadd; the others a lock cmpxchg loop. */
        if ((insn & 0x3f208c00u) == 0x38200000u && ((insn >> 12) & 7) <= 3) {
            int size = insn >> 30, bytes = 1 << size, opc = (insn >> 12) & 7;
            int rs = (insn >> 16) & 31, rt = insn & 31;
            uint8_t rex = (uint8_t)(size == 3 ? 0x49 : 0x41); /* W for 64 bits, B for [r8] */
            get(R8, rn, R31::Sp);
            get(RAX, rs, R31::Zero);
            FaultSite site{e.here(), 0, pc};
            if (opc == 0) {
                e.b(0xF0);
                if (size == 1) e.b(0x66);
                e.b(rex); e.b(0x0F); e.b(size == 0 ? 0xC0 : 0xC1); e.b(0x00); /* lock xadd [r8], rax */
            } else {
                e.mov_rr(R9, RAX, true);
                if (opc == 1) e.unary(2, R9, true); /* CLR: and with the complement */
                e.mem(RAX, bytes, 1, 0);          /* the old value */
                uint32_t top = e.here();
                e.mov_rr(RDX, RAX, true);
                e.alu(opc == 1 ? 0x21 : opc == 2 ? 0x31 : 0x09, RDX, R9, true);
                e.b(0xF0);
                if (size == 1) e.b(0x66);
                e.b((uint8_t)(rex)); e.b(0x0F); e.b(size == 0 ? 0xB0 : 0xB1); e.b(0x10); /* lock cmpxchg [r8], rdx */
                e.b(0x75); e.b((uint8_t)(int8_t)((int32_t)top - (int32_t)(e.here() + 1))); /* jnz top */
            }
            site.host_end = e.here();
            faults.push_back(site);
            if (size == 0) { e.b(0x0F); e.b(0xB6); e.b(0xC0); }      /* movzx eax, al */
            else if (size == 1) { e.b(0x0F); e.b(0xB7); e.b(0xC0); } /* movzx eax, ax */
            else if (size == 2) e.mov_rr(RAX, RAX, false);
            put(RAX, rt, R31::Zero);
            return 1;
        }
        /* CSEL, CSINC, CSINV, CSNEG */
        if ((insn & 0x1fe00000u) == 0x1a800000u && ((insn >> 29) & 1) == 0 && ((insn >> 11) & 1) == 0) {
            int op = (insn >> 30) & 1, o2 = (insn >> 10) & 1, cc = (insn >> 12) & 15;
            /* Straight after the compare, on its live x86 flags: only
               instructions that leave the flags alone (mov, lea, not, cmov),
               and the flags stay live for whatever reads them next. */
            uint8_t x86 = 0;
            if (cc < 14 && !(op == 1 && o2 == 1) && flags_live_at == e.here() && live_x86_cc(cc, &x86)) {
                auto operand = [&](Host h, int r) {
                    if (r == 31) e.mov_imm(h, 0); /* not xor: that sets flags */
                    else get(h, r, R31::Zero);
                };
                operand(RAX, rn);
                operand(RDX, rm);
                if (op == 0 && o2 == 1) { e.b(0x48); e.b(0x8D); e.b(0x52); e.b(0x01); } /* lea rdx, [rdx+1] */
                else if (op == 1) e.unary(2, RDX, wide);                                 /* not */
                e.cmov((uint8_t)(x86 ^ 1), RAX, RDX, wide); /* condition false takes rm */
                if (!wide) { e.b(0x89); e.b(0xC0); }      /* mov eax, eax: no flags */
                if (rd != 31) {
                    e.store_struct(RAX, off_x(rd));
                    /* put() would do the same plus the cache, but its cache
                       reset must not run between here and a flags reader;
                       the store and a register move are both flag-free. */
                    if (cache_on() && !e.open_short) {
                        cache_sync();
                        cache_keep(rd, RAX);
                    } else if (cache_on()) {
                        cache_sync();
                        if (cache_slot[rd] >= 0) { cache_reg[cache_slot[rd]] = -1; cache_slot[rd] = -1; }
                    }
                }
                flags_still_live = true;
                return 1;
            }
            condition(cc);
            e.mov_rr(R9, RAX, false);
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            if (op == 0 && o2 == 1) { /* inc */
                e.mov_imm(R8, 1);
                e.alu(0x01, RDX, R8, wide);
            } else if (op == 1 && o2 == 0) {
                e.unary(2, RDX, wide);
            } else if (op == 1 && o2 == 1) {
                e.unary(3, RDX, wide);
            }
            if (cc < 14) {
                e.test32(R9, R9);
                e.cmov(0x4, RAX, RDX, wide); /* cmovz: condition false takes rm */
            }
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* MADD, MSUB */
        if ((insn & 0x7fe00000u) == 0x1b000000u) {
            int ra = (insn >> 10) & 31;
            bool subtract = (insn >> 15) & 1;
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            e.imul(RAX, RDX, wide);
            get(RDX, ra, R31::Zero);
            if (subtract) {
                e.alu(0x29, RDX, RAX, wide);
                e.mov_rr(RAX, RDX, true);
            } else {
                e.alu(0x01, RAX, RDX, wide);
            }
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }

        /* LSLV, LSRV, ASRV, RORV: the count goes in cl, so rcx (the GuestCpu
           pointer) is parked in r9 around the shift. Arm takes the count
           modulo the width, as x64 does. */
        if ((insn & 0x7fe0f000u) == 0x1ac02000u) {
            int op = (insn >> 10) & 3;
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            e.mov_rr(R9, RCX, true);
            e.mov_rr(RCX, RDX, false);
            if (wide) e.b(0x48);
            e.b(0xD3);
            e.b((uint8_t)(0xC0 | ((op == 0 ? 4 : op == 1 ? 5 : op == 2 ? 7 : 1) << 3)));
            e.mov_rr(RCX, R9, true);
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* UDIV, SDIV: Arm gives 0 for a zero divisor and INT_MIN for
           INT_MIN / -1, where x64 would trap; both are checked first. */
        if ((insn & 0x7fe0f800u) == 0x1ac00800u) {
            bool is_signed = (insn >> 10) & 1;
            get(RAX, rn, R31::Zero);
            get(R8, rm, R31::Zero);
            std::optional<KeepCache> keep;
            keep.emplace(*this);
            if (wide) e.b(0x4D); else e.b(0x45); /* test r8, r8 */
            e.b(0x85);
            e.b(0xC0);
            uint32_t to_zero = e.jump(0x74); /* jz */
            uint32_t to_done_neg = 0;
            if (is_signed) {
                e.b(wide ? 0x49 : 0x41); /* cmp r8, -1 */
                e.b(0x83);
                e.b(0xF8);
                e.b(0xFF);
                uint32_t to_divide = e.jump(0x75); /* jne */
                e.unary(3, RAX, wide);             /* x / -1 = -x, and INT_MIN stays */
                to_done_neg = e.jump(0xEB);
                e.land(to_divide);
                if (wide) e.b(0x48);
                e.b(0x99); /* cqo / cdq */
                e.b(wide ? 0x49 : 0x41);
                e.b(0xF7);
                e.b(0xF8); /* idiv r8 */
            } else {
                e.zero(RDX);
                e.b(wide ? 0x49 : 0x41);
                e.b(0xF7);
                e.b(0xF0); /* div r8 */
            }
            uint32_t to_done = e.jump(0xEB);
            e.land(to_zero);
            e.zero(RAX);
            e.land(to_done);
            if (is_signed) e.land(to_done_neg);
            keep.reset();
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* BFM (BFI, BFXIL): a field of rn into rd, the rest of rd kept */
        if ((insn & 0x7f800000u) == 0x33000000u) {
            int n = (insn >> 22) & 1, immr = (insn >> 16) & 63, imms = (insn >> 10) & 63;
            int width = wide ? 64 : 32;
            if (n != (int)wide || (!wide && (immr > 31 || imms > 31))) return 0;
            int len, pos;
            get(RAX, rn, R31::Zero);
            if (imms >= immr) {
                len = imms - immr + 1;
                pos = 0;
                if (immr) e.shift(5, RAX, immr, true);
            } else {
                len = imms + 1;
                pos = width - immr;
                e.shift(4, RAX, pos, true);
            }
            uint64_t mask = (len >= 64 ? ~0ull : ((1ull << len) - 1)) << pos;
            if (!wide) mask &= 0xffffffffull;
            e.mov_imm(R8, mask);
            e.alu(0x21, RAX, R8, true);
            get(RDX, rd, R31::Zero);
            e.mov_imm(R8, ~mask);
            e.alu(0x21, RDX, R8, true);
            e.alu(0x09, RAX, RDX, true);
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* EXTR (and ROR by immediate) */
        if ((insn & 0x7fa00000u) == 0x13800000u) {
            int n = (insn >> 22) & 1, lsb = (insn >> 10) & 63;
            int width = wide ? 64 : 32;
            if (n != (int)wide || lsb >= width) return 0;
            get(RAX, rm, R31::Zero);
            if (lsb) {
                get(RDX, rn, R31::Zero);
                e.shift(5, RAX, lsb, wide);
                e.shift(4, RDX, width - lsb, wide);
                e.alu(0x09, RAX, RDX, wide);
            }
            narrow(RAX, wide);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* ADD/SUB (extended register) */
        if ((insn & 0x1fe00000u) == 0x0b200000u) {
            bool sub = (insn >> 30) & 1, setflags = (insn >> 29) & 1;
            int option = (insn >> 13) & 7, amount = (insn >> 10) & 7;
            if (amount > 4) return 0;
            get(RAX, rn, R31::Sp);
            get(RDX, rm, R31::Zero);
            extend(RDX, option);
            if (amount) e.shift(4, RDX, amount, true);
            e.alu(sub ? 0x29 : 0x01, RAX, RDX, wide);
            if (setflags) flags_after(sub, false);
            narrow(RAX, wide);
            put(RAX, rd, setflags ? R31::Zero : R31::Sp);
            return 1;
        }
        /* DMB, DSB: x86 keeps every order but store-then-load, which mfence
           covers. ISB has nothing to do here. */
        if ((insn & 0xfffff0dfu) == 0xd503309fu) {
            e.b(0x0F); e.b(0xAE); e.b(0xF0);
            return 1;
        }
        if ((insn & 0xfffff0ffu) == 0xd50330dfu) return 1;
        /* REV (32 and 64): bswap. */
        if ((insn & 0xfffffc00u) == 0xdac00c00u || (insn & 0xfffffc00u) == 0x5ac00800u) {
            get(RAX, rn, R31::Zero);
            if (wide) e.b(0x48);
            e.b(0x0F); e.b(0xC8);
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* The exclusives: the interpreter's atomic decoder, from inside the
           block (LDXR, LDAXR, STXR, STLXR and the pairs). */
        if (((insn >> 24) & 0x3f) == 0x08 && ((insn >> 23) & 1) == 0) {
            call_helper((uint64_t)(uintptr_t)&jit_atomic, pc, block_count);
            return 1;
        }
        /* CLZ: lzcnt counts the width for zero, as Arm does. */
        if ((insn & 0x7ffffc00u) == 0x5ac01000u) {
            get(RAX, rn, R31::Zero);
            e.b(0xF3);
            if (wide) e.b(0x48);
            e.b(0x0F); e.b(0xBD); e.b(0xC0); /* lzcnt rax, rax */
            put(RAX, rd, R31::Zero);
            return 1;
        }
        if (((insn >> 25) & 7) == 7) {
            int simd = translate_simd(insn, pc);
            if (simd) return simd;
        }
        /* MRS Xt, TPIDR_EL0 */
        if ((insn & 0xffffffe0u) == 0xd53bd040u) {
            e.load_struct(RAX, (int32_t)offsetof(GuestCpu, tpidr));
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* LDAR, STLR: x64 loads already acquire; a store-release is made
           sequentially consistent with xchg (or a fence for the narrow
           sizes), since Arm's release store never passes a later acquire. */
        if ((insn & 0x3ffffc00u) == 0x08dffc00u || (insn & 0x3ffffc00u) == 0x089ffc00u) {
            bool load = (insn >> 22) & 1;
            int bytes = 1 << ((insn >> 30) & 3);
            get(R8, rn, R31::Sp);
            if (load) {
                memory_op(pc, RAX, bytes, 1, 0);
                put(RAX, rd, R31::Zero);
            } else {
                get(RDX, rd, R31::Zero);
                FaultSite site{e.here(), 0, pc};
                if (bytes >= 4) {
                    e.b(bytes == 8 ? 0x49 : 0x41); /* xchg [r8+0], rdx */
                    e.b(0x87);
                    e.b(0x90);
                    e.d32(0);
                } else {
                    e.mem(RDX, bytes, 0, 0);
                }
                site.host_end = e.here();
                faults.push_back(site);
                if (bytes < 4) { /* mfence */
                    e.b(0x0F);
                    e.b(0xAE);
                    e.b(0xF0);
                }
            }
            return 1;
        }

        /* SMADDL, SMSUBL, UMADDL, UMSUBL: 32x32 -> 64, plus a 64-bit addend */
        if ((insn & 0xff600000u) == 0x9b200000u) {
            bool is_unsigned = (insn >> 23) & 1, subtract = (insn >> 15) & 1;
            int ra = (insn >> 10) & 31;
            get(RAX, rn, R31::Zero);
            get(RDX, rm, R31::Zero);
            if (is_unsigned) {
                e.mov_rr(RAX, RAX, false);
                e.mov_rr(RDX, RDX, false);
            } else { /* movsxd rax, eax / movsxd rdx, edx */
                e.b(0x48); e.b(0x63); e.b(0xC0);
                e.b(0x48); e.b(0x63); e.b(0xD2);
            }
            e.imul(RAX, RDX, true);
            get(RDX, ra, R31::Zero);
            if (subtract) {
                e.alu(0x29, RDX, RAX, true);
                e.mov_rr(RAX, RDX, true);
            } else {
                e.alu(0x01, RAX, RDX, true);
            }
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* SMULH, UMULH: the high 64 bits of a 128-bit product */
        if ((insn & 0xff60fc00u) == 0x9b407c00u) {
            bool is_unsigned = (insn >> 23) & 1;
            get(RAX, rn, R31::Zero);
            get(R8, rm, R31::Zero);
            /* mul r8 / imul r8: rdx:rax = rax * r8 */
            e.b(0x49);
            e.b(0xF7);
            e.b(is_unsigned ? 0xE0 : 0xE8);
            put(RDX, rd, R31::Zero);
            return 1;
        }

        /* LD1R: one element, into every lane (no offset, or post-indexed). */
        if ((insn & 0xbffff000u) == 0x0d40c000u || (insn & 0xbfe0f000u) == 0x0dc0c000u) {
            bool q = (insn >> 30) & 1, post = (insn >> 23) & 1;
            int size = (insn >> 10) & 3, rt = insn & 31, rm = (insn >> 16) & 31;
            int bytes = 1 << size;
            if (size == 3 && !q) return 0;
            get(R8, rn, R31::Sp);
            memory_op(pc, RAX, bytes, 1, 0);
            if (size < 3) {
                e.mov_imm(RDX, size == 0 ? 0x0101010101010101ull : size == 1 ? 0x0001000100010001ull
                                                                             : 0x0000000100000001ull);
                e.imul(RAX, RDX, true);
            }
            e.store_struct(RAX, (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)rt));
            if (q) e.store_struct(RAX, (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)rt) + 8);
            else {
                e.zero(RDX);
                e.store_struct(RDX, (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)rt) + 8);
            }
            if (post) {
                if (rm == 31) e.mov_imm(RDX, (uint64_t)bytes);
                else get(RDX, rm, R31::Zero);
                e.alu(0x01, R8, RDX, true);
                put(R8, rn, R31::Sp);
            }
            return 1;
        }
        /* LD1, ST1 (multiple structures, one register), no offset or post-indexed. */
        if ((insn & 0xbfbff000u) == 0x0c007000u || (insn & 0xbfa0f000u) == 0x0c807000u) {
            bool q = (insn >> 30) & 1, load = (insn >> 22) & 1, post = (insn >> 23) & 1;
            int rt = insn & 31, rm = (insn >> 16) & 31, bytes = q ? 16 : 8;
            get(R8, rn, R31::Sp);
            vector_access(pc, rt, bytes, load, 0);
            if (post) {
                if (rm == 31) e.mov_imm(RDX, (uint64_t)bytes);
                else get(RDX, rm, R31::Zero);
                e.alu(0x01, R8, RDX, true);
                put(R8, rn, R31::Sp);
            }
            return 1;
        }
        /* LD1, ST1 (single lane) of a b or h lane, no offset or post-indexed. */
        if (((insn & 0xbfbfe000u) == 0x0d000000u || (insn & 0xbfbfe000u) == 0x0d004000u ||
             (insn & 0xbfa0e000u) == 0x0d800000u || (insn & 0xbfa0e000u) == 0x0d804000u)) {
            bool q = (insn >> 30) & 1, load = (insn >> 22) & 1, post = (insn >> 23) & 1;
            int size = (insn >> 10) & 3, s_bit = (insn >> 12) & 1, rt = insn & 31, rm = (insn >> 16) & 31;
            bool half = ((insn >> 14) & 1) == 1;
            if (half && (size & 1)) return 0;
            int bytes = half ? 2 : 1;
            int index = half ? (((int)q << 2) | (s_bit << 1) | (size >> 1)) : (((int)q << 3) | (s_bit << 2) | size);
            int32_t lane = (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)rt) + index * bytes;
            get(R8, rn, R31::Sp);
            if (load) {
                memory_op(pc, RAX, bytes, 1, 0);
                if (half) e.b(0x66);
                e.b(half ? 0x89 : 0x88); e.b(0x81); e.d32(lane); /* mov [rcx+lane], al/ax */
            } else {
                e.b(0x0F); e.b(half ? 0xB7 : 0xB6); e.b(0x91); e.d32(lane); /* movzx edx, [rcx+lane] */
                memory_op(pc, RDX, bytes, 0, 0);
            }
            if (post) {
                if (rm == 31) e.mov_imm(RDX, (uint64_t)bytes);
                else get(RDX, rm, R31::Zero);
                e.alu(0x01, R8, RDX, true);
                put(R8, rn, R31::Sp);
            }
            return 1;
        }
        /* LD1, ST1 (single lane) of an s or d lane, no offset or post-indexed. */
        if ((insn & 0xbfbfe000u) == 0x0d008000u || (insn & 0xbf80e000u) == 0x0d808000u) {
            bool q = (insn >> 30) & 1, load = (insn >> 22) & 1, post = (insn >> 23) & 1;
            int size = (insn >> 10) & 3, s_bit = (insn >> 12) & 1, rt = insn & 31, rm = (insn >> 16) & 31;
            int bytes, index;
            if (size == 0) { bytes = 4; index = ((int)q << 1) | s_bit; }
            else if (size == 1 && !s_bit) { bytes = 8; index = q; }
            else return 0;
            if (post && ((insn >> 21) & 1)) return 0;
            int32_t lane = (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)rt) + index * bytes;
            get(R8, rn, R31::Sp);
            if (load) {
                memory_op(pc, RAX, bytes, 1, 0);
                if (bytes == 8) e.store_struct(RAX, lane);
                else { e.b(0x89); e.b(0x81); e.d32(lane); } /* mov [rcx+lane], eax */
            } else {
                if (bytes == 8) e.load_struct(RDX, lane);
                else { e.b(0x8B); e.b(0x91); e.d32(lane); } /* mov edx, [rcx+lane] */
                memory_op(pc, RDX, bytes, 0, 0);
            }
            if (post) {
                if (rm == 31) e.mov_imm(RDX, (uint64_t)bytes);
                else get(RDX, rm, R31::Zero);
                e.alu(0x01, R8, RDX, true);
                put(R8, rn, R31::Sp);
            }
            return 1;
        }
        /* RBIT: bswap, then nibbles, pairs and bits swapped. */
        if ((insn & 0x7ffffc00u) == 0x5ac00000u) {
            get(RAX, rn, R31::Zero);
            if (wide) e.b(0x48);
            e.b(0x0F); e.b(0xC8);
            const uint64_t masks[3] = {0x0F0F0F0F0F0F0F0Full, 0x3333333333333333ull, 0x5555555555555555ull};
            const int shifts[3] = {4, 2, 1};
            for (int k = 0; k < 3; ++k) {
                e.mov_imm(R8, wide ? masks[k] : (masks[k] & 0xffffffffull));
                e.mov_rr(RDX, RAX, wide);
                e.shift(5, RDX, shifts[k], wide);
                e.alu(0x21, RDX, R8, wide);
                e.alu(0x21, RAX, R8, wide);
                e.shift(4, RAX, shifts[k], wide);
                e.alu(0x09, RAX, RDX, wide);
            }
            put(RAX, rd, R31::Zero);
            return 1;
        }
        /* SIMD and FP register loads and stores. */
        if ((insn & 0x3f000000u) == 0x3d000000u) return vector_load_store(insn, pc, 0, 0);
        if (((insn >> 26) & 1) == 1 && (insn & 0x3b200000u) == 0x38000000u && ((insn >> 10) & 3) != 2) {
            int mode = (insn >> 10) & 3;
            int32_t imm = (int32_t)(((int32_t)((insn >> 12) & 0x1ff) << 23) >> 23);
            return vector_load_store(insn, pc, mode == 0 ? 0 : mode == 1 ? 1 : 2, imm);
        }
        if (((insn >> 26) & 1) == 1 && (insn & 0x3a000000u) == 0x28000000u && ((insn >> 23) & 7) != 0)
            return vector_pair(insn, pc);
        if (((insn >> 26) & 1) == 1 && (insn & 0x3b200c00u) == 0x38200800u) return vector_register_offset(insn, pc);

        /* PRFM (unsigned offset, register offset, unscaled): only ever a hint. */
        if (((insn >> 30) & 3) == 3 && ((insn >> 22) & 3) == 2 && ((insn >> 26) & 1) == 0 &&
            ((insn & 0x3b000000u) == 0x39000000u || (insn & 0x3b200c00u) == 0x38200800u ||
             (insn & 0x3b200c00u) == 0x38000000u))
            return 1;

        /* Loads and stores, general registers only. */
        if (((insn >> 26) & 1) == 0) {
            /* LDR/STR (unsigned immediate) */
            if ((insn & 0x3b000000u) == 0x39000000u)
                return load_store(insn, pc, (int32_t)(((insn >> 10) & 0xfff) << ((insn >> 30) & 3)), 0);
            /* unscaled, post-index, pre-index (imm9) */
            if ((insn & 0x3b200000u) == 0x38000000u && ((insn >> 10) & 3) != 2) {
                int32_t imm = (int32_t)(((int32_t)((insn >> 12) & 0x1ff) << 23) >> 23);
                int mode = (insn >> 10) & 3; /* 0 unscaled, 1 post, 3 pre */
                return load_store(insn, pc, imm, mode == 0 ? 0 : mode == 1 ? 1 : 2);
            }
            /* register offset */
            if ((insn & 0x3b200c00u) == 0x38200800u) return load_store_register(insn, pc);
            /* LDP/STP: post, offset, pre */
            if ((insn & 0x3a000000u) == 0x28000000u && ((insn >> 23) & 7) != 0) return pair(insn, pc);
            /* LDR (literal) */
            if ((insn & 0x3b000000u) == 0x18000000u) {
                int opc = (insn >> 30) & 3;
                if (opc == 3) return 1; /* PRFM literal */
                int64_t imm = (int64_t)(((int32_t)((insn >> 5) & 0x7ffff) << 13) >> 11);
                e.mov_imm(R8, pc + (uint64_t)imm);
                memory_op(pc, RAX, opc == 0 ? 4 : opc == 1 ? 8 : 4, opc == 2 ? 2 : 1, 0);
                put(RAX, rd, R31::Zero);
                return 1;
            }
        }

        /* Branches end the block. */
        if ((insn & 0x7c000000u) == 0x14000000u) { /* B, BL */
            int64_t imm = (int64_t)((int32_t)(insn << 6) >> 4);
            if ((insn >> 31) && host_call(pc, pc + (uint64_t)imm, block_count)) return 1;
            if (insn >> 31) {
                e.mov_imm(RAX, next);
                put(RAX, 30, R31::Zero);
            }
            exit_to(pc + (uint64_t)imm);
            return 2;
        }
        if ((insn & 0xff000010u) == 0x54000000u) { /* B.cond */
            int64_t imm = (int64_t)((int32_t)((insn >> 5) << 13) >> 11);
            int cc = insn & 15;
            if (cc >= 14) {
                exit_to(pc + (uint64_t)imm);
            } else if (flags_live_at == e.here() && live_kind) {
                /* The x86 condition for the Arm one, given what set the flags.
                   Arm's C is not-borrow after a subtract and carry after an
                   add or logical op; HI and LS have no single x86 test in the
                   second case. */
                static const uint8_t after_subtract[14] = {0x4, 0x5, 0x3, 0x2, 0x8, 0x9, 0x0,
                                                           0x1, 0x7, 0x6, 0xD, 0xC, 0xF, 0xE};
                uint8_t x86 = after_subtract[cc];
                bool ok = true;
                if (live_kind != 1) {
                    if (cc == 2) x86 = 0x2;
                    else if (cc == 3) x86 = 0x3;
                    else if (cc == 8 || cc == 9) ok = false;
                }
                if (ok) {
                    exit = Exit::ConditionalFlags;
                    exit_x86_cc = x86;
                    exit_taken = pc + (uint64_t)imm;
                    exit_fallthrough = next;
                } else {
                    condition(cc);
                    set_pc_if(pc + (uint64_t)imm, next);
                }
            } else {
                condition(cc);
                set_pc_if(pc + (uint64_t)imm, next);
            }
            return 2;
        }
        if ((insn & 0x7e000000u) == 0x34000000u) { /* CBZ, CBNZ */
            int64_t imm = (int64_t)((int32_t)((insn >> 5) << 13) >> 11);
            bool nonzero = (insn >> 24) & 1;
            get(RDX, rd, R31::Zero);
            e.zero(RAX);
            e.test32(RDX, RDX);
            if (wide) { /* test rdx, rdx */
                e.out.pop_back();
                e.out.pop_back();
                e.b(0x48);
                e.b(0x85);
                e.b(0xD2);
            }
            e.b(0x0F);
            e.b(nonzero ? 0x95 : 0x94); /* setnz / setz al */
            e.b(0xC0);
            set_pc_if(pc + (uint64_t)imm, next);
            return 2;
        }
        if ((insn & 0x7e000000u) == 0x36000000u) { /* TBZ, TBNZ */
            int bit = (int)(((insn >> 31) << 5) | ((insn >> 19) & 31));
            int64_t imm = (int64_t)((int32_t)(((insn >> 5) & 0x3fff) << 18) >> 16);
            bool nonzero = (insn >> 24) & 1;
            get(RDX, rd, R31::Zero);
            e.shift(5, RDX, bit, true);
            e.mov_imm(RAX, 1);
            e.alu(0x21, RAX, RDX, false);
            if (!nonzero) e.xor1(RAX);
            set_pc_if(pc + (uint64_t)imm, next);
            return 2;
        }
        if ((insn & 0xfffffc1fu) == 0xd65f0000u || (insn & 0xfffffc1fu) == 0xd61f0000u ||
            (insn & 0xfffffc1fu) == 0xd63f0000u) { /* RET, BR, BLR */
            get(RDX, rn, R31::Zero);
            if ((insn & 0xfffffc1fu) == 0xd63f0000u) {
                e.mov_imm(RAX, next);
                put(RAX, 30, R31::Zero);
            }
            e.store_struct(RDX, kOffPc);
            exit = Exit::Indirect;
            return 2;
        }
        return 0;
    }

    /* size opc: the access, from the load/store encodings' size and opc fields.
       Returns false for what is not a general-register access handled here. */
    static bool access(uint32_t insn, int* bytes, int* kind) {
        int size = (insn >> 30) & 3, opc = (insn >> 22) & 3;
        *bytes = 1 << size;
        if (opc == 0) *kind = 0;
        else if (opc == 1) *kind = 1;
        else if (opc == 2) {
            if (size == 3) return false; /* PRFM */
            *kind = 2;
        } else {
            if (size >= 2) return false;
            *kind = 3;
        }
        return true;
    }
    void finish_load(int rt, int kind, bool wide_dest) {
        (void)kind;
        (void)wide_dest;
        put(RAX, rt, R31::Zero);
    }

    /* mode 0: [base + imm]; 1: post-index; 2: pre-index. */
    int load_store(uint32_t insn, uint64_t pc, int32_t imm, int mode) {
        int bytes = 0, kind = 0;
        if (!access(insn, &bytes, &kind)) return 0;
        int rt = insn & 31, rn = (insn >> 5) & 31;
        if (mode != 0 && rt == rn && rt != 31) return 0; /* unpredictable writeback */
        get(R8, rn, R31::Sp);
        if (kind == 0) get(RDX, rt, R31::Zero);
        int32_t disp = mode == 1 ? 0 : imm;
        if (kind == 0) memory_op(pc, RDX, bytes, 0, disp);
        else memory_op(pc, RAX, bytes, kind, disp);
        if (kind != 0) put(RAX, rt, R31::Zero);
        if (mode != 0) {
            e.mov_imm(RDX, (uint64_t)(int64_t)imm);
            e.alu(0x01, R8, RDX, true);
            put(R8, rn, R31::Sp);
        }
        return 1;
    }

    int load_store_register(uint32_t insn, uint64_t pc) {
        int bytes = 0, kind = 0;
        if (!access(insn, &bytes, &kind)) return 0;
        int rt = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
        int option = (insn >> 13) & 7;
        bool scaled = (insn >> 12) & 1;
        if (option != 2 && option != 3 && option != 6 && option != 7) return 0;
        get(R8, rn, R31::Sp);
        get(RDX, rm, R31::Zero);
        if (option == 2) e.mov_rr(RDX, RDX, false); /* UXTW */
        if (option == 6) {                         /* SXTW: movsxd rdx, edx */
            e.b(0x48);
            e.b(0x63);
            e.b(0xD2);
        }
        int shift = scaled ? ((insn >> 30) & 3) : 0;
        if (shift) e.shift(4, RDX, shift, true);
        e.alu(0x01, R8, RDX, true);
        if (kind == 0) {
            get(RDX, rt, R31::Zero);
            memory_op(pc, RDX, bytes, 0, 0);
        } else {
            memory_op(pc, RAX, bytes, kind, 0);
            put(RAX, rt, R31::Zero);
        }
        return 1;
    }

    static int32_t q_off(int r) { return (int32_t)(offsetof(GuestCpu, q) + 16 * (size_t)r); }

    /* One vector register's worth: a load zero-fills the rest of the
       register, as Arm does; 16 bytes go as two 8-byte halves. */
    void vector_access(uint64_t pc, int rt, int bytes, bool load, int32_t disp) {
        if (load) {
            if (bytes == 16) {
                memory_op(pc, RAX, 8, 1, disp);
                memory_op(pc, RDX, 8, 1, disp + 8);
                e.store_struct(RAX, q_off(rt));
                e.store_struct(RDX, q_off(rt) + 8);
            } else {
                memory_op(pc, RAX, bytes, 1, disp);
                e.store_struct(RAX, q_off(rt));
                e.zero(RDX);
                e.store_struct(RDX, q_off(rt) + 8);
            }
        } else {
            if (bytes == 16) {
                e.load_struct(RAX, q_off(rt));
                e.load_struct(RDX, q_off(rt) + 8);
                memory_op(pc, RAX, 8, 0, disp);
                memory_op(pc, RDX, 8, 0, disp + 8);
            } else {
                e.load_struct(RDX, q_off(rt));
                memory_op(pc, RDX, bytes, 0, disp);
            }
        }
    }

    /* LDR/STR of b, h, s, d, q registers: mode 0 is [base + imm] (the
       unsigned-offset form's scaled immediate, or the unscaled one), 1 post,
       2 pre. */
    int vector_load_store(uint32_t insn, uint64_t pc, int mode, int32_t imm9) {
        int size = (insn >> 30) & 3, opc = (insn >> 22) & 3;
        bool load = opc & 1;
        int bytes = (opc & 2) ? (size == 0 ? 16 : 0) : (1 << size);
        if (bytes == 0) return 0;
        int rt = insn & 31, rn = (insn >> 5) & 31;
        int32_t imm = imm9;
        bool unsigned_offset = ((insn >> 24) & 1) == 1;
        if (unsigned_offset) {
            int scale = bytes == 16 ? 4 : size;
            imm = (int32_t)(((insn >> 10) & 0xfff) << scale);
            mode = 0;
        }
        get(R8, rn, R31::Sp);
        vector_access(pc, rt, bytes, load, mode == 1 ? 0 : imm);
        if (mode != 0) {
            e.mov_imm(RDX, (uint64_t)(int64_t)imm);
            e.alu(0x01, R8, RDX, true);
            put(R8, rn, R31::Sp);
        }
        return 1;
    }

    /* LDR/STR of a vector register at [base, index {, extend/shift}] */
    int vector_register_offset(uint32_t insn, uint64_t pc) {
        int size = (insn >> 30) & 3, opc = (insn >> 22) & 3;
        bool load = opc & 1;
        int bytes = (opc & 2) ? (size == 0 ? 16 : 0) : (1 << size);
        if (bytes == 0) return 0;
        int rt = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
        int option = (insn >> 13) & 7;
        bool scaled = (insn >> 12) & 1;
        if (option != 2 && option != 3 && option != 6 && option != 7) return 0;
        get(R8, rn, R31::Sp);
        get(RDX, rm, R31::Zero);
        extend(RDX, option);
        int shift = scaled ? (bytes == 16 ? 4 : size) : 0;
        if (shift) e.shift(4, RDX, shift, true);
        e.alu(0x01, R8, RDX, true);
        vector_access(pc, rt, bytes, load, 0);
        return 1;
    }

    int vector_pair(uint32_t insn, uint64_t pc) {
        int opc = (insn >> 30) & 3, index = (insn >> 23) & 7;
        bool load = (insn >> 22) & 1;
        if (opc == 3) return 0;
        int bytes = 4 << opc; /* s, d, q */
        int rt = insn & 31, rt2 = (insn >> 10) & 31, rn = (insn >> 5) & 31;
        int32_t imm = (int32_t)(((int32_t)((insn >> 15) & 0x7f) << 25) >> 25) * bytes;
        int mode = index == 1 ? 1 : index == 3 ? 2 : 0;
        get(R8, rn, R31::Sp);
        int32_t disp = mode == 1 ? 0 : imm;
        /* Loads all happen before any register write, so a fault part way
           leaves the registers for the interpreter to redo from scratch. */
        if (load && bytes == 16) {
            out_pair_load16(pc, rt, rt2, disp);
        } else if (load) {
            memory_op(pc, RAX, bytes, 1, disp);
            memory_op(pc, RDX, bytes, 1, disp + bytes);
            e.store_struct(RAX, q_off(rt));
            e.store_struct(RDX, q_off(rt2));
            e.zero(RAX);
            e.store_struct(RAX, q_off(rt) + 8);
            e.store_struct(RAX, q_off(rt2) + 8);
        } else {
            vector_access(pc, rt, bytes, false, disp);
            vector_access(pc, rt2, bytes, false, disp + bytes);
        }
        if (mode != 0) {
            e.mov_imm(RDX, (uint64_t)(int64_t)imm);
            e.alu(0x01, R8, RDX, true);
            put(R8, rn, R31::Sp);
        }
        return 1;
    }

    /* LDP q: four 8-byte loads, parked in rax, rdx, r9 and the upper half
       read last, then all four written. */
    void out_pair_load16(uint64_t pc, int rt, int rt2, int32_t disp) {
        memory_op(pc, RAX, 8, 1, disp);
        memory_op(pc, RDX, 8, 1, disp + 8);
        memory_op(pc, R9, 8, 1, disp + 16);
        e.store_struct(RAX, q_off(rt));
        e.store_struct(RDX, q_off(rt) + 8);
        e.store_struct(R9, q_off(rt2));
        memory_op(pc, RAX, 8, 1, disp + 24);
        e.store_struct(RAX, q_off(rt2) + 8);
    }

    int pair(uint32_t insn, uint64_t pc) {
        int opc = (insn >> 30) & 3, index = (insn >> 23) & 7;
        bool load = (insn >> 22) & 1;
        if ((insn >> 26) & 1) return 0; /* SIMD pairs: interpreter */
        if (opc == 3 || (opc == 1 && !load) || index == 0) return 0;
        int bytes = opc == 2 ? 8 : 4;
        int kind = opc == 1 ? 2 : 1; /* LDPSW sign-extends */
        int rt = insn & 31, rt2 = (insn >> 10) & 31, rn = (insn >> 5) & 31;
        int32_t imm = (int32_t)(((int32_t)((insn >> 15) & 0x7f) << 25) >> 25) * bytes;
        int mode = index == 1 ? 1 : index == 3 ? 2 : 0; /* post, pre, offset (2 is offset, 3 pre) */
        if (index == 2) mode = 0;
        if (mode != 0 && rn != 31 && (rt == rn || rt2 == rn)) return 0;
        get(R8, rn, R31::Sp);
        int32_t disp = mode == 1 ? 0 : imm;
        if (load) {
            memory_op(pc, RAX, bytes, kind, disp);
            memory_op(pc, RDX, bytes, kind, disp + bytes);
            put(RAX, rt, R31::Zero);
            put(RDX, rt2, R31::Zero);
        } else {
            get(RAX, rt, R31::Zero);
            get(RDX, rt2, R31::Zero);
            memory_op(pc, RAX, bytes, 0, disp);
            memory_op(pc, RDX, bytes, 0, disp + bytes);
        }
        if (mode != 0) {
            e.mov_imm(RDX, (uint64_t)(int64_t)imm);
            e.alu(0x01, R8, RDX, true);
            put(R8, rn, R31::Sp);
        }
        return 1;
    }
};

/* ---- Code cache --------------------------------------------------------- */

std::shared_mutex g_blocks_lock;
std::unordered_map<uint64_t, Block*> g_blocks;
std::mutex g_code_lock;
uint8_t* g_code = nullptr;
size_t g_code_used = 0, g_code_size = 0;
std::atomic<uint64_t> g_jit_instructions{0};
std::atomic<uint64_t> g_entries{0}; /* times the run loop entered translated code */
/* Bumped when translations are thrown away, so per-thread tables follow. */
std::atomic<uint64_t> g_generation{1};

uint8_t* code_alloc(size_t bytes) {
    std::lock_guard<std::mutex> held(g_code_lock);
    bytes = (bytes + 15) & ~(size_t)15;
    if (!g_code || g_code_used + bytes > g_code_size) {
        g_code_size = 64u << 20;
        g_code = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, g_code_size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_EXECUTE_READWRITE));
        g_code_used = 0;
        if (!g_code) return nullptr;
    }
    uint8_t* at = g_code + g_code_used;
    g_code_used += bytes;
    return at;
}

const int kMaxBlock = 256;

/* An instruction the translator does not know, run by the interpreter from
   inside a block, so the block (and its chain) carries on past it instead of
   going back to the run loop. Returns 1 if it ran and fell through to the
   next instruction; anything else (a fault, an undecoded instruction) leaves
   the pc on it for the ordinary path to deal with. */
GuestMem* g_jit_mem = nullptr;

/* A BL into the thunk page: the host function, then back to the
   instruction after the call, as the run loop would. Returns 1 if it
   returned there; otherwise the pc is left for the run loop. */
int jit_host_call(GuestCpu* cpu, uint64_t index) {
    uint64_t back = cpu->x[30];
    cpu->pc = g_jit_mem->thunk_va + index * (uint64_t)g_jit_mem->thunk_stride;
    if (guest_jit_host_call(*cpu, *g_jit_mem, (int)index) < 0) return 0; /* pc on the thunk: the loop reports it */
    cpu->pc = cpu->x[30];
    return cpu->x[30] == back ? 1 : 0;
}

/* The exclusive loads and stores (LDXR, STXR, LDAXR, STLXR and the pairs):
   the interpreter's atomic decoder, which is the only place step() decodes
   them, called directly. The exclusive monitor it keeps is per thread, and
   so is this. 1 done (and fell through), otherwise the pc is left on it. */
int jit_atomic(GuestCpu* cpu, uint64_t pc) {
    uint32_t insn = 0;
    std::memcpy(&insn, reinterpret_cast<const void*>(pc), 4);
    cpu->pc = pc;
    int result = 0;
    __try {
        result = step_atomic(*cpu, *g_jit_mem, insn, pc + 4) ? 1 : 0;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                  : EXCEPTION_CONTINUE_SEARCH) {
        result = -1;
    }
    if (result == 1 && cpu->pc == pc + 4) return 1;
    cpu->pc = pc;
    return 0;
}

/* For a SIMD/FP data-processing instruction the translator does not know:
   if running it from two unrelated states leaves everything alone except
   one vector register, and gives that register the same value both times,
   it is a constant (MOVI and friends), and the block can just store it. */
bool fold_constant(uint32_t insn, uint64_t pc, int* rd, GuestVec* value) {
    static const bool off = QB_ENV("QB_JIT_NOFOLD") != nullptr;
    /* Only encodings that are immediates by definition: sampling alone
       cannot prove an instruction ignores its inputs (two random negative
       inputs make FSQRT look like it always gives NaN). The SIMD modified
       immediate class (MOVI, MVNI, ORR/BIC immediate, FMOV vector) and scalar
       FMOV #imm; ORR/BIC read their destination, which the sampling below
       catches. */
    bool immediate_class = (insn & 0x9ff80400u) == 0x0f000400u || (insn & 0xff201fe0u) == 0x1e201000u ||
                           (insn & 0xff201fe0u) == 0x1e601000u;
    if (off || !immediate_class || !g_jit_mem) return false;
    GuestCpu states[4];
    uint64_t seed = 0x2545f4914f6cdd1dull ^ insn;
    auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    int which = 0;
    for (GuestCpu& state : states) {
        /* All zeros, all ones, and two random states. */
        auto fill = [&]() { return which == 0 ? 0ull : which == 1 ? ~0ull : next(); };
        state = GuestCpu{};
        for (uint64_t& r : state.x) r = fill();
        for (GuestVec& v : state.q) v = {fill(), fill()};
        ++which;
        state.sp = next();
        state.tpidr = next();
        state.n = next() & 1;
        state.z = next() & 1;
        state.c = next() & 1;
        state.v = next() & 1;
        state.pc = pc;
    }
    GuestCpu before[4] = {states[0], states[1], states[2], states[3]};
    const GuestCpu* outer = t_guest_cpu;
    for (GuestCpu& state : states) {
        int result = guest_step_once(state, *g_jit_mem);
        if (result != 1 || state.pc != pc + 4) return false;
    }
    (void)outer;
    int changed = -1;
    for (int r = 0; r < 32; ++r) {
        bool differs = false;
        for (int k = 0; k < 4; ++k) differs = differs || std::memcmp(&states[k].q[r], &before[k].q[r], sizeof(GuestVec)) != 0;
        if (differs) {
            if (changed >= 0) return false;
            changed = r;
        }
    }
    if (changed < 0) return false;
    for (int k = 1; k < 4; ++k)
        if (std::memcmp(&states[0].q[changed], &states[k].q[changed], sizeof(GuestVec)) != 0) return false;
    for (int k = 0; k < 4; ++k) {
        if (std::memcmp(states[k].x, before[k].x, sizeof(before[k].x)) || states[k].sp != before[k].sp ||
            states[k].n != before[k].n || states[k].z != before[k].z || states[k].c != before[k].c ||
            states[k].v != before[k].v || states[k].fpsr != before[k].fpsr || states[k].fpcr != before[k].fpcr)
            return false;
    }
    if (std::memcmp(&states[0].q[changed], &states[1].q[changed], sizeof(GuestVec)) != 0) return false;
    *rd = changed;
    *value = states[0].q[changed];
    if (QB_ENV("QB_JIT_FOLDLOG"))
        std::printf("fold: %08x at %llx -> q%d = %016llx %016llx\n", insn, (unsigned long long)pc, changed,
                    (unsigned long long)value->hi, (unsigned long long)value->lo);
    return true;
}

int jit_interpret(GuestCpu* cpu, uint64_t pc) {
    /* Counted with the run loop's misses, for QB_JIT_STATS. */
    uint32_t insn = 0;
    std::memcpy(&insn, reinterpret_cast<const void*>(pc), 4);
    guest_jit_note_interpreted(insn);
    cpu->pc = pc;
    /* The full step, always: step() decodes a number of SIMD forms itself
       before it reaches the FP decoder, so going to that decoder directly
       is not the same instruction set. */
    int result = guest_step_once(*cpu, *g_jit_mem);
    if (result == 1 && cpu->pc == pc + 4) return 1;
    if (result != 1) cpu->pc = pc;
    return 0;
}

/* Indirect branches (RET, BR, BLR) look their target up here, inline: a
   direct-mapped table of blocks by guest pc. A slot holds one Block pointer,
   written whole, so a racing reader sees an old block or a new one, never a
   mix; the code checks the block's pc before jumping into it. Blocks are
   never freed, so a stale pointer is still a valid one. */
const uint32_t kIndirectSlots = 1u << 20;
std::atomic<Block*> g_indirect[kIndirectSlots];

bool is_early_return(uint64_t pc);
void remember_indirect(Block* block) {
    if (block && block->fn && !is_early_return(block->pc)) g_indirect[(block->pc >> 2) & (kIndirectSlots - 1)].store(block, std::memory_order_release);
}

Block* compile(GuestMem& mem, uint64_t pc) {
    auto* block = new Block;
    block->pc = pc;
    Translator t;
    t.thunk_va = mem.thunk_va;
    t.thunk_end = mem.thunk_va + (uint64_t)mem.thunk_count * mem.thunk_stride;
    t.thunk_stride = mem.thunk_stride;
    uint32_t count = 0;
    uint64_t at = pc;
    bool ended = false;
    while (count < (uint32_t)kMaxBlock) {
        uint8_t* p = guest_ptr(mem, at, 4);
        if (!p) break;
        /* The thunk page is host calls, never code. */
        if (at >= mem.thunk_va && at < mem.thunk_va + (uint64_t)mem.thunk_count * mem.thunk_stride) break;
        /* Nor does a block run into an address QB_RET short-circuits. */
        if (at != pc && is_early_return(at)) break; /* QB_RET and QB_BREAK addresses start their own block */
        uint32_t insn = 0;
        std::memcpy(&insn, p, 4);
        t.block_count = count;
        t.pending_flags = 0;
        t.flags_still_live = false;
        int result = t.translate(insn, at);
        /* A flag-setting instruction just finished: its x86 flags are live
           until anything else is emitted. */
        if (result == 1 && t.pending_flags) {
            t.flags_live_at = t.e.here();
            t.live_kind = t.pending_flags;
        } else if (result == 1 && t.flags_still_live) {
            t.flags_live_at = t.e.here();
        }
        /* A SIMD instruction whose result is a constant: stored as one. */
        int folded_rd = 0;
        GuestVec folded{};
        if (result == 0 && fold_constant(insn, at, &folded_rd, &folded)) {
            t.store_constant_vector(folded_rd, folded);
            result = 1;
        }
        /* Not translated, and not a branch (op0 x101, apart from the system
           instructions, which fall through): the interpreter runs it here. */
        bool system = (insn & 0xffc00000u) == 0xd5000000u;
        if (result == 0 && (((insn >> 26) & 7) != 5 || system)) {
            t.interpret_inline(at, count);
            result = 1;
        }
        if (result == 0) break;
        ++count;
        at += 4;
        if (result == 2) {
            ended = true;
            break;
        }
    }
    if (count == 0) return block; /* fn stays null: the interpreter's */
    /* Cut off only by length: the next instruction is another block,
       chained to like any known target instead of via the run loop. */
    if (!ended && count == (uint32_t)kMaxBlock && t.exit == Translator::Exit::None) t.exit_to(at);
    Emitter& e = t.e;
    /* The instruction count of a chained run builds up in r10d: each block
       adds its own, then either chains or returns the total. */
    auto add_count = [&]() { e.b(0x41); e.b(0x81); e.b(0xC2); e.d32((int32_t)count); }; /* add r10d, imm32 */
    auto leave = [&]() { e.b(0x44); e.b(0x89); e.b(0xD0); e.b(0xC3); };                /* mov eax, r10d; ret */
    /* Chained, the next block runs without the pc being stored: nothing
       inside translated code reads it (helpers and fault handling set it
       themselves), and every way out of translated code stores it. */
    auto exit_known = [&](uint64_t target) {
        add_count();
        e.b(0x41); e.b(0x81); e.b(0xFA); e.d32(4096); /* cmp r10d, 4096: back to the loop now and then */
        uint32_t over = e.jump(0x73);                   /* jae -> leave */
        while ((e.here() + 1) % 4 != 0) e.b(0x90);      /* 4-aligned offset, so patching is one write */
        e.b(0xE9);
        t.slots.push_back({e.here(), target});
        e.d32(0);                                       /* to the next instruction (leave) until patched */
        e.land(over);
        t.set_pc(target);
        leave();
    };
    switch (t.exit) {
        case Translator::Exit::Direct: exit_known(t.exit_taken); break;
        case Translator::Exit::Conditional: {
            e.test32(RAX, RAX);
            e.b(0x0F);
            e.b(0x85); /* jnz rel32 -> taken */
            uint32_t taken_at = e.here();
            e.d32(0);
            exit_known(t.exit_fallthrough);
            int32_t rel = (int32_t)(e.here() - (taken_at + 4));
            std::memcpy(&e.out[taken_at], &rel, 4);
            exit_known(t.exit_taken);
            break;
        }
        case Translator::Exit::ConditionalFlags: {
            e.b(0x0F);
            e.b((uint8_t)(0x80 | t.exit_x86_cc)); /* jcc rel32 -> taken, on the live flags */
            uint32_t taken_at = e.here();
            e.d32(0);
            exit_known(t.exit_fallthrough);
            int32_t rel = (int32_t)(e.here() - (taken_at + 4));
            std::memcpy(&e.out[taken_at], &rel, 4);
            exit_known(t.exit_taken);
            break;
        }
        case Translator::Exit::Indirect: {
            /* The target is in rdx and already stored as the pc. */
            add_count();
            uint32_t top = e.here();
            e.b(0x41); e.b(0x81); e.b(0xFA); e.d32(4096);              /* cmp r10d, 4096 */
            uint32_t budget = e.jump32(0x83);                             /* jae -> leave */
            e.b(0x48); e.b(0x89); e.b(0xD0);                              /* mov rax, rdx */
            /* A branch into the thunk page (a PLT stub's br x16) is a host
               call: made here, then on to x30 as its return would, without
               going back to the run loop. */
            uint32_t host_failed = 0;
            bool inline_host = t.thunk_end > t.thunk_va && t.thunk_stride == 16 &&
                               QB_ENV("QB_JIT_NO_INDIRECT_HOST") == nullptr;
            if (inline_host) {
                e.b(0x49); e.b(0x89); e.b(0xC0);                          /* mov r8, rax */
                e.b(0x49); e.b(0xB9); e.d64(t.thunk_va);                  /* mov r9, thunk_va */
                e.b(0x4D); e.b(0x29); e.b(0xC8);                          /* sub r8, r9 */
                e.b(0x49); e.b(0xB9); e.d64(t.thunk_end - t.thunk_va);    /* mov r9, span */
                e.b(0x4D); e.b(0x39); e.b(0xC8);                          /* cmp r8, r9 */
                uint32_t not_host = e.jump32(0x83);                       /* jae -> the lookup */
                e.b(0x49); e.b(0xC1); e.b(0xE8); e.b(0x04);               /* shr r8, 4: the import */
                e.b(0x41); e.b(0x52);                                     /* push r10 */
                e.b(0x51);                                                /* push rcx */
                e.b(0x48); e.b(0x83); e.b(0xEC); e.b(40);                 /* sub rsp, 40 */
                e.b(0x4C); e.b(0x89); e.b(0xC2);                          /* mov rdx, r8 */
                e.b(0x48); e.b(0xB8); e.d64((uint64_t)(uintptr_t)&jit_host_call); /* mov rax, helper */
                e.b(0xFF); e.b(0xD0);                                     /* call rax */
                e.b(0x48); e.b(0x83); e.b(0xC4); e.b(40);                 /* add rsp, 40 */
                e.b(0x59);                                                /* pop rcx */
                e.b(0x41); e.b(0x5A);                                     /* pop r10 */
                e.b(0x41); e.b(0xFF); e.b(0xC2);                          /* inc r10d: the call counts */
                e.test32(RAX, RAX);
                host_failed = e.jump32(0x84);                             /* jz -> leave, pc as the helper left it */
                e.b(0x48); e.b(0x8B); e.b(0x91); e.d32(kOffPc);           /* mov rdx, [rcx + pc] */
                e.b(0xE9); e.d32((int32_t)top - (int32_t)(e.here() + 4)); /* jmp top */
                e.land32(not_host);
            }
            e.b(0x49); e.b(0x89); e.b(0xC0);                              /* mov r8, rax */
            e.b(0x49); e.b(0xC1); e.b(0xE8); e.b(0x02);                   /* shr r8, 2 */
            e.b(0x41); e.b(0x81); e.b(0xE0); e.d32((int32_t)(kIndirectSlots - 1)); /* and r8d, mask */
            e.b(0x49); e.b(0xB9); e.d64((uint64_t)(uintptr_t)&g_indirect[0]);     /* mov r9, table */
            e.b(0x4F); e.b(0x8B); e.b(0x04); e.b(0xC1);                   /* mov r8, [r9 + r8*8] */
            e.b(0x4D); e.b(0x85); e.b(0xC0);                              /* test r8, r8 */
            uint32_t empty = e.jump(0x74);                                /* jz -> leave */
            e.b(0x49); e.b(0x39); e.b(0x80); e.d32((int32_t)offsetof(Block, pc));   /* cmp [r8 + pc], rax */
            uint32_t other = e.jump(0x75);                                /* jne -> leave */
            e.b(0x4D); e.b(0x8B); e.b(0x80); e.d32((int32_t)offsetof(Block, code)); /* mov r8, [r8 + code] */
            e.b(0x49); e.b(0x83); e.b(0xC0); e.b(0x04);                   /* add r8, 4: past the entry prologue */
            e.b(0x41); e.b(0xFF); e.b(0xE0);                              /* jmp r8 */
            e.land32(budget);
            e.land(empty);
            e.land(other);
            if (host_failed) e.land32(host_failed);
            leave();
            break;
        }
        case Translator::Exit::None:
            /* Ran into something untranslated: back to the interpreter. */
            t.set_pc(at);
            add_count();
            leave();
            break;
    }
    /* Entry from outside a chain zeroes the count (xor r10d, r10d, then a nop
       so the body keeps its 4-byte alignment); chained jumps land after it. */
    std::vector<uint8_t> body = {0x45, 0x31, 0xD2, 0x90};
    body.insert(body.end(), e.out.begin(), e.out.end());
    for (auto& slot : t.slots) slot.first += 4;
    for (FaultSite& site : t.faults) {
        site.host_start += 4;
        site.host_end += 4;
    }
    e.out.swap(body);
    if (t.e.overflowed) { /* a short jump could not reach: leave it to the interpreter */
        static std::atomic<int> told{0};
        if (told++ < 5) std::fprintf(stderr, "jit: block %llx has a short jump out of range\n", (unsigned long long)pc);
        return block;
    }
    uint8_t* code = code_alloc(t.e.out.size());
    if (!code) return block;
    std::memcpy(code, t.e.out.data(), t.e.out.size());
    FlushInstructionCache(GetCurrentProcess(), code, t.e.out.size());
    block->code = code;
    block->size = (uint32_t)t.e.out.size();
    block->instructions = count;
    block->faults = std::move(t.faults);
    block->slots = std::move(t.slots);
    block->fn = reinterpret_cast<BlockFn>(code);
    /* QB_JIT_DUMP=<lo hex>-<hi hex>: each block starting in that guest range
       written out as jitdump/<pc>.arm and .x86, for objdump. */
    static const std::pair<uint64_t, uint64_t> dump_range = [] {
        const char* text = QB_ENV("QB_JIT_DUMP");
        if (!text) return std::pair<uint64_t, uint64_t>{0, 0};
        char* end = nullptr;
        uint64_t lo = std::strtoull(text, &end, 16);
        uint64_t hi = end && *end == '-' ? std::strtoull(end + 1, nullptr, 16) : lo + 1;
        CreateDirectoryA("jitdump", nullptr);
        return std::pair<uint64_t, uint64_t>{lo, hi};
    }();
    if (pc >= dump_range.first && pc < dump_range.second) {
        char path[64];
        std::snprintf(path, sizeof(path), "jitdump/%llx.x86", (unsigned long long)pc);
        if (FILE* f = std::fopen(path, "wb")) { std::fwrite(code, 1, t.e.out.size(), f); std::fclose(f); }
        std::snprintf(path, sizeof(path), "jitdump/%llx.arm", (unsigned long long)pc);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(reinterpret_cast<const void*>(pc), 4, count, f);
            std::fclose(f);
        }
    }
    return block;
}

/* Chaining state, under g_blocks_lock: exits waiting for a block that is
   not translated yet, and every block by where its code is, for faults
   (a fault can now land in any block of a chained run). */
std::unordered_map<uint64_t, std::vector<uint8_t*>> g_waiting;
std::map<uintptr_t, Block*> g_by_code;

bool is_early_return(uint64_t pc) {
    for (const auto& one : guest_early_returns())
        if (one.first == pc) return true;
    for (uint64_t one : guest_break_pcs())
        if (one == pc) return true;
    return false;
}

/* Points one exit's jump at a block's chained entry. The offset is 4-aligned,
   so the write is atomic for any thread running past it. */
void patch(uint8_t* slot, const Block* to) {
    int64_t rel = (int64_t)(uintptr_t)(to->code + 4) - (int64_t)(uintptr_t)(slot + 4);
    if (rel < INT32_MIN || rel > INT32_MAX) return; /* too far to chain: stays an exit */
    reinterpret_cast<std::atomic<int32_t>*>(slot)->store((int32_t)rel, std::memory_order_release);
    FlushInstructionCache(GetCurrentProcess(), slot, 4);
}

/* A new block: its exits to translated blocks are chained now, the rest
   wait; exits that were waiting for it are chained to it. */
void link(Block* made) {
    g_by_code[(uintptr_t)made->code] = made;
    for (const auto& slot : made->slots) {
        if (is_early_return(slot.second)) continue;
        uint8_t* at = made->code + slot.first;
        auto target = g_blocks.find(slot.second);
        if (target != g_blocks.end() && target->second->fn) patch(at, target->second);
        else if (target == g_blocks.end()) g_waiting[slot.second].push_back(at);
    }
    auto waiting = g_waiting.find(made->pc);
    if (waiting != g_waiting.end()) {
        for (uint8_t* at : waiting->second) patch(at, made);
        g_waiting.erase(waiting);
    }
}

Block* lookup(GuestMem& mem, uint64_t pc) {
    /* A small per-thread table in front of the shared map. */
    struct Entry {
        uint64_t pc;
        Block* block;
    };
    thread_local Entry recent[4096] = {};
    thread_local uint64_t seen_generation = 0;
    uint64_t generation = g_generation.load(std::memory_order_acquire);
    if (seen_generation != generation) {
        std::memset(recent, 0, sizeof(recent));
        seen_generation = generation;
    }
    Entry& slot = recent[(pc >> 2) & 4095];
    if (slot.pc == pc && slot.block) return slot.block;
    Block* block = nullptr;
    {
        std::shared_lock<std::shared_mutex> held(g_blocks_lock);
        auto found = g_blocks.find(pc);
        if (found != g_blocks.end()) block = found->second;
    }
    if (!block) {
        Block* made = compile(mem, pc);
        std::unique_lock<std::shared_mutex> held(g_blocks_lock);
        auto inserted = g_blocks.emplace(pc, made);
        if (!inserted.second) {
            delete made;
        } else if (made->fn) {
            link(made);
        } else {
            g_waiting.erase(pc); /* nothing will ever chain there */
        }
        block = inserted.first->second;
    }
    slot = {pc, block};
    remember_indirect(block);
    return block;
}

int fault_filter(EXCEPTION_POINTERS* info, uint64_t* rip) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    *rip = info->ContextRecord->Rip;
    return EXCEPTION_EXECUTE_HANDLER;
}

int run_guarded(Block* block, GuestCpu* cpu, uint64_t* rip) {
    __try {
        return (int)qb_jit_enter(cpu, block->fn);
    } __except (fault_filter(GetExceptionInformation(), rip)) {
        return -1;
    }
}

}  // namespace

bool guest_jit_enabled() {
    static const bool on = [] {
        const char* setting = QB_ENV("QB_JIT");
        if (setting && setting[0] == '0') return false;
        /* The per-instruction debugging aids live in the interpreter. */
        for (const char* name : {"QB_WATCH", "QB_SETBYTE", "QB_SKIP_UNKNOWN"})
            if (std::getenv(name)) return false;
        return true;
    }();
    return on;
}

/* Runs one translated block at cpu.pc. Returns how many guest instructions it
   ran; 0 means the interpreter should step the instruction at cpu.pc (not
   translated, or it faulted and the pc now names it). */
int guest_jit_step(GuestCpu& cpu, GuestMem& mem) {
    g_jit_mem = &mem;
    Block* block = lookup(mem, cpu.pc);
    if (!block->fn) return 0;
    uint64_t rip = 0;
    static const bool counting = QB_ENV("QB_JIT_STATS") != nullptr;
    if (counting) g_entries.fetch_add(1, std::memory_order_relaxed);
    int ran = run_guarded(block, &cpu, &rip);
    if (counting) guest_jit_note_exit(cpu.pc);
    if (ran >= 0) {
        g_jit_instructions.fetch_add((uint64_t)ran, std::memory_order_relaxed);
        return ran;
    }
    {
        std::shared_lock<std::shared_mutex> held(g_blocks_lock);
        auto after = g_by_code.upper_bound((uintptr_t)rip);
        if (after != g_by_code.begin()) {
            Block* owner = std::prev(after)->second;
            uint64_t inside = rip - (uint64_t)owner->code;
            if (inside < owner->size) {
                for (const FaultSite& site : owner->faults)
                    if (inside >= site.host_start && inside < site.host_end) {
                        cpu.pc = site.guest_pc;
                        return 0;
                    }
                block = owner;
            }
        }
    }
    uint64_t offset = rip - (uint64_t)block->code;
    /* A fault outside any memory instruction would be a translator bug. */
    std::fprintf(stderr, "jit: fault at block %llx +%llx that no guest instruction owns\n",
                 (unsigned long long)block->pc, (unsigned long long)offset);
    std::abort();
}

unsigned long long guest_jit_instructions() { return g_jit_instructions.load(); }

/* Forgets every translation (the code memory is not reused). For code that
   changes under the same address, which in practice is only the fuzzer. */
void guest_jit_invalidate() {
    std::unique_lock<std::shared_mutex> held(g_blocks_lock);
    g_blocks.clear();
    g_waiting.clear();
    g_by_code.clear();
    for (auto& entry : g_indirect) entry.store(nullptr, std::memory_order_relaxed);
    g_generation.fetch_add(1, std::memory_order_release);
}

/* QB_JIT_STATS: which instructions still fall back to the interpreter, by
   the top ten bits of the encoding with one example each, printed at exit.
   That is the list of what to translate next. */
namespace {
std::mutex g_miss_lock;
std::unordered_map<uint32_t, std::pair<uint64_t, uint32_t>> g_misses;
std::atomic<uint64_t> g_interpreted{0};

void print_misses() {
    std::vector<std::pair<uint64_t, std::pair<uint32_t, uint32_t>>> sorted;
    {
        std::lock_guard<std::mutex> held(g_miss_lock);
        for (auto& entry : g_misses) sorted.push_back({entry.second.first, {entry.first, entry.second.second}});
    }
    std::sort(sorted.rbegin(), sorted.rend());
    uint64_t jitted = g_jit_instructions.load(), interpreted = g_interpreted.load();
    std::fprintf(stderr, "jit: %llu translated, %llu interpreted (%.1f%% translated)\n", (unsigned long long)jitted,
                 (unsigned long long)interpreted, 100.0 * jitted / (double)(jitted + interpreted + 1));
    uint64_t entries = g_entries.exchange(0);
    std::fprintf(stderr, "jit: %llu entries from the run loop, %.1f instructions each\n", (unsigned long long)entries,
                 jitted / (double)(entries + 1));
    for (size_t i = 0; i < sorted.size() && i < 30; ++i)
        std::fprintf(stderr, "jit:   %10llu  form %08x  e.g. %08x\n", (unsigned long long)sorted[i].first,
                     sorted[i].second.first, sorted[i].second.second);
}
}  // namespace

void guest_jit_print_stats() {
    if (QB_ENV("QB_JIT_STATS")) print_misses();
}

void guest_jit_note_interpreted(uint32_t insn) {
    static const bool on = [] {
        return QB_ENV("QB_JIT_STATS") != nullptr;
    }();
    if (!on) return;
    /* QB_JIT_STATS=N (N > 0): also a table every N seconds, cleared each
       time, so a steady state can be read without a clean exit. */
    static std::once_flag periodic;
    std::call_once(periodic, [] {
        int every = std::atoi(QB_ENV("QB_JIT_STATS"));
        if (every <= 0) return;
        std::thread([every] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(every));
                print_misses();
                print_exits();
                std::lock_guard<std::mutex> held(g_miss_lock);
                g_misses.clear();
                g_interpreted = 0;
                g_jit_instructions = 0;
            }
        }).detach();
    });
    g_interpreted.fetch_add(1, std::memory_order_relaxed);
    thread_local uint32_t batch = 0;
    if ((++batch & 15) != 0) return; /* sampled, to keep it cheap */
    std::lock_guard<std::mutex> held(g_miss_lock);
    auto& slot = g_misses[insn & 0xffe0fc00u]; /* opcode fields, registers dropped */
    slot.first += 16;
    slot.second = insn;
}
