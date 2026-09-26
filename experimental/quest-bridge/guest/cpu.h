#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

/* One mapped span of guest address space: a loaded object, the stack, the
   heap. A linker maps several objects, so this is a list rather than a field
   per kind. */
/* Guest memory is identity mapped: a guest address is the host address of
   the same byte. Every guest region is reserved at its own address when it
   is made, so a pointer the guest builds can be handed to a host API as it
   is, which is what lets a Vulkan or OpenXR call carry its structures, their
   pNext chains and every pointer inside them straight to the host driver.

   Reserves (and commits, when asked) exactly [va, va + size). Stops the
   process if the range is taken, because nothing can work around that. */
uint8_t* guest_reserve(uint64_t va, uint64_t size, bool commit);
bool guest_commit(uint64_t va, uint64_t size);
/* For a region whose address is not written down anywhere else: tries the
   preferred address, then steps upward until a free span turns up. Returns
   the address it got. */
uint64_t guest_reserve_near(uint64_t preferred, uint64_t size, bool commit, uint64_t step);

/* Bytes that live at a fixed guest address. Set at, then assign. */
struct GuestBytes {
    uint64_t at = 0;
    uint8_t* ptr = nullptr;
    size_t n = 0;

    GuestBytes() = default;
    GuestBytes(const GuestBytes&) = delete;
    GuestBytes& operator=(const GuestBytes&) = delete;
    ~GuestBytes();
    void assign(size_t size, uint8_t fill);
    uint8_t* data() const { return ptr; }
    size_t size() const { return n; }
    bool empty() const { return n == 0; }
    uint8_t& operator[](size_t i) const { return ptr[i]; }
};

struct GuestRegion {
    uint8_t* data = nullptr;
    uint64_t va = 0;
    uint64_t size = 0;
};

/* Runs the arm64 guest. Host calls happen when the PC lands in the thunk page. */
struct GuestMem {
    std::vector<GuestRegion> regions;
    uint64_t thunk_va = 0;
    int thunk_stride = 16;
    int thunk_count = 0;

    void map(uint8_t* data, uint64_t va, uint64_t size) { regions.push_back({data, va, size}); }
};

/* One 128-bit vector register. Scalar floats live in the low half. */
struct GuestVec {
    uint64_t lo = 0;
    uint64_t hi = 0;
};

struct GuestCpu {
    uint64_t x[31]{};
    uint64_t sp = 0;
    uint64_t pc = 0;
    bool n = false, z = false, c = false, v = false;
    /* The thread pointer, which thread local storage is measured from. */
    uint64_t tpidr = 0;
    /* Floating point control and status. Kept so code that saves and
       restores them round-trips; rounding modes and flush-to-zero are not
       applied. */
    uint64_t fpcr = 0, fpsr = 0;
    GuestVec q[32]{};
};

/* Floating point and SIMD, which is everything arm64 code that was not built
   -mgeneral-regs-only does with a number. Returns false when the encoding is
   not one of the ones handled here. */
bool step_fp(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next);
bool step_crypto(GuestCpu& cpu, uint32_t insn, uint64_t next); /* crypto.cpp: AES, SHA1, SHA256 */

/* Atomic memory: the exclusive pair, the acquire and release forms, and the
   single instruction read modify write family that threaded code is built on. */
bool step_atomic(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next);
bool step_atomic_memory(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next);

using GuestThunk = void (*)(GuestCpu& cpu, GuestMem& mem, int index, void* user);

bool guest_run(GuestCpu& cpu, GuestMem& mem, GuestThunk thunk, void* user, int max_steps);
/* The block JIT (jit.cpp). guest_jit_step runs the translated block at
   cpu.pc and returns the instructions it ran, or 0 when the interpreter
   should step the instruction at cpu.pc. QB_JIT=0 turns it off. */
bool guest_jit_enabled();
int guest_jit_step(GuestCpu& cpu, GuestMem& mem);
unsigned long long guest_jit_instructions();
void guest_jit_note_interpreted(uint32_t insn);
void guest_jit_print_stats();
void guest_jit_invalidate();
int guest_step_once(GuestCpu& cpu, GuestMem& mem);
int guest_jit_host_call(GuestCpu& cpu, GuestMem& mem, int index);
const std::vector<std::pair<uint64_t, uint64_t>>& guest_early_returns();
const std::vector<uint64_t>& guest_break_pcs();
/* Calls visit for every processor currently running, for hang diagnosis. */
void guest_each_running(void (*visit)(const GuestCpu&));

/* Signals. A thread is named by its thread pointer, which is the one thing
   every copy of its registers agrees on. Raising marks the signal pending;
   the thread takes it at its next instruction boundary, or from inside a
   blocking call that polls for it. */
using GuestSignalHook = void (*)(GuestCpu& cpu, uint64_t pending);
/* Names a guest address as library+offset, for fault reports. */
using GuestDescribeHook = std::string (*)(uint64_t address);
void guest_set_describe_hook(GuestDescribeHook hook);
std::string guest_describe(uint64_t address);
/* The Android package being run: QB_PACKAGE, or Big Scary by default. */
const char* guest_package();

/* QB_WATCH=<hex address>: reports every guest store that touches it, with
   the thread and the instruction, for finding who corrupts a word. */
extern uint64_t g_guest_watch;
extern thread_local const GuestCpu* t_guest_cpu;
/* Instructions and host calls stepped so far, for measuring speed. */
extern std::atomic<uint64_t> g_guest_steps;
void guest_report_watch(uint64_t va, int bytes, uint64_t value);
void guest_set_signal_hook(GuestSignalHook hook);
/* Called when an instruction is undefined; true if it redirected the cpu
   (to the guest's SIGILL handler), false to stop as before. */
using GuestUndefinedHook = bool (*)(GuestCpu& cpu);
void guest_set_undefined_hook(GuestUndefinedHook hook);
bool guest_raise(uint64_t thread_pointer, int signal);
uint64_t guest_take_pending(uint64_t thread_pointer);
bool guest_thread_running(uint64_t thread_pointer);

/* How many guest instructions have been executed so far. */
unsigned long long guest_steps();
uint8_t* guest_ptr(GuestMem& mem, uint64_t va, uint64_t bytes);
