#include "cpu.h"
#include "cpu_engine.h"
#include "kernel.h"

#include "log.h"
#include "memory.h"

#include <dynarmic/interface/A64/a64.h>
#include <dynarmic/interface/A64/config.h>
#include <cstdlib>
#include <dynarmic/interface/exclusive_monitor.h>

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <shared_mutex>

namespace axrb {
using namespace linux_abi;
using Dynarmic::HaltReason;

namespace {

constexpr HaltReason kHaltSvc = HaltReason::UserDefined1;
constexpr HaltReason kHaltKick = HaltReason::UserDefined2;
constexpr HaltReason kHaltException = HaltReason::UserDefined3;
constexpr HaltReason kHaltInvalidate = HaltReason::UserDefined4;

constexpr uint64_t kCntFrq = 19200000;  // Qualcomm-class counter frequency

thread_local GuestThread* t_current = nullptr;

// Host functions and their trampolines.
std::shared_mutex g_functions_lock;
std::vector<std::string> g_function_names;
std::vector<HostFunction> g_functions;
uint64_t g_trampolines = 0;
constexpr uint32_t kMaxHostFunctions = 1 << 16;
uint64_t g_return_trap = 0;

std::unordered_map<std::string, uint32_t> g_named;          // name -> function id
std::unordered_map<uint64_t, uint32_t> g_named_by_address;  // stub string address -> id

std::mutex g_cpus_lock;
std::set<GuestCpu*> g_cpus;

std::function<std::string(uint64_t)> g_describer;

uint64_t qpc_frequency() {
    static uint64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    return frequency;
}

std::mutex g_processor_lock;
std::vector<bool> g_processor_used;
constexpr size_t kProcessors = 512;

int allocate_processor() {
    std::lock_guard<std::mutex> held(g_processor_lock);
    if (g_processor_used.empty()) g_processor_used.resize(kProcessors);
    for (size_t i = 0; i < kProcessors; ++i)
        if (!g_processor_used[i]) {
            g_processor_used[i] = true;
            return static_cast<int>(i);
        }
    fatal("more than %zu guest threads", kProcessors);
}

void release_processor(int id) {
    std::lock_guard<std::mutex> held(g_processor_lock);
    if (id >= 0 && static_cast<size_t>(id) < g_processor_used.size()) g_processor_used[id] = false;
}

Dynarmic::ExclusiveMonitor& monitor() {
    static Dynarmic::ExclusiveMonitor instance(kProcessors);
    return instance;
}

}  // namespace

uint64_t cntfrq() { return kCntFrq; }
uint64_t cntpct() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t f = qpc_frequency();
    uint64_t ticks = static_cast<uint64_t>(now.QuadPart);
    return (ticks / f) * kCntFrq + (ticks % f) * kCntFrq / f;
}

GuestThread* current_thread() { return t_current; }
void set_current_thread(GuestThread* thread) { t_current = thread; }

// ---------------------------------------------------------------------------
// One thread's processor.

class GuestCpu final : public Dynarmic::A64::UserCallbacks {
public:
    explicit GuestCpu(GuestThread& thread) : thread_(thread) {
        Dynarmic::A64::UserConfig config;
        config.callbacks = this;
        config.processor_id = static_cast<size_t>(thread.processor_id);
        config.global_monitor = &monitor();
        config.fastmem_pointer = 0;  // identity mapping: host address == guest address
        config.fastmem_address_space_bits = 48;
        config.silently_mirror_fastmem = false;
        config.recompile_on_fastmem_failure = true;
        config.fastmem_exclusive_access = true;
        config.recompile_on_exclusive_fastmem_failure = true;
        config.tpidr_el0 = &thread.tpidr;
        config.tpidrro_el0 = &tpidrro_;
        config.cntfrq_el0 = static_cast<uint32_t>(kCntFrq);
        config.ctr_el0 = 0x8444c004;   // 64-byte lines, IDC/DIC clear
        config.dczid_el0 = 0x14;       // DZP: DC ZVA prohibited, so memset avoids it
        config.hook_data_cache_operations = true;
        // YIELD is a no-op, as on the processor itself. Engines put it in the
        // spin loops of their worker threads; as a callback that gave up the
        // time slice, every iteration was a Windows system call and a spin
        // meant for microseconds kept a core busy for far longer.
        // AXRB_DIRECT_HOOK_HINTS=1 restores the callbacks.
        static const bool hook_hints = [] {
            const char* v = std::getenv("AXRB_DIRECT_HOOK_HINTS");
            return v && v[0] == '1';
        }();
        config.hook_hint_instructions = hook_hints;
        config.enable_cycle_counting = false;
        config.wall_clock_cntpct = true;
        config.define_unpredictable_behaviour = true;
        config.code_cache_size = 256ull << 20;
        // Exclusive stores are a host compare-and-swap against the value the
        // paired exclusive load saw (as QEMU's user mode does): atomic, without
        // the global monitor's process-wide lock and its check of every one of
        // kProcessors slots on each store. Only an ABA change between the two
        // goes unnoticed. AXRB_DIRECT_STRICT_EXCLUSIVE=1 restores the monitor.
        static const bool strict_exclusive = [] {
            const char* v = std::getenv("AXRB_DIRECT_STRICT_EXCLUSIVE");
            return v && v[0] == '1';
        }();
        if (!strict_exclusive) {
            config.unsafe_optimizations = true;
            config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_IgnoreGlobalMonitor;
        }
        // AXRB_DIRECT_FAST_FP=1: host floating-point semantics for NaN payloads,
        // estimate instructions and standard-FPCR vector code.
        static const bool fast_fp = [] {
            const char* v = std::getenv("AXRB_DIRECT_FAST_FP");
            return v && v[0] == '1';
        }();
        if (fast_fp) {
            config.unsafe_optimizations = true;
            config.optimizations |= Dynarmic::OptimizationFlag::Unsafe_ReducedErrorFP |
                                    Dynarmic::OptimizationFlag::Unsafe_InaccurateNaN |
                                    Dynarmic::OptimizationFlag::Unsafe_IgnoreStandardFPCRValue;
        }
        jit_ = std::make_unique<CpuEngine>(config, thread);
        std::lock_guard<std::mutex> held(g_cpus_lock);
        g_cpus.insert(this);
    }
    ~GuestCpu() override {
        std::lock_guard<std::mutex> held(g_cpus_lock);
        g_cpus.erase(this);
    }

    CpuEngine& jit() { return *jit_; }

    // Why the last Run stopped, beyond the halt reason.
    uint32_t svc_imm = 0;
    struct PendingException {
        int signal = 0;
        int code = 0;
        uint64_t address = 0;
        uint64_t pc = 0;
    } exception;

    void queue_invalidation(uint64_t addr, uint64_t len) {
        std::lock_guard<std::mutex> held(invalidate_lock_);
        invalidations_.push_back({addr, len});
        jit_->HaltExecution(kHaltInvalidate);
    }
    void apply_invalidations() {
        std::vector<std::pair<uint64_t, uint64_t>> ranges;
        {
            std::lock_guard<std::mutex> held(invalidate_lock_);
            ranges.swap(invalidations_);
        }
        for (auto& [addr, len] : ranges) jit_->InvalidateCacheRange(addr, len);
    }

    // --- Dynarmic callbacks ---------------------------------------------------
    std::optional<uint32_t> MemoryReadCode(Dynarmic::A64::VAddr vaddr) override {
        uint32_t word = 0;
        // File-backed pages become committed before their contents have been
        // copied. Synchronize with the loader instead of decoding transient zeros.
        if (!address_space().populate(vaddr, 4) || !guest_read(vaddr, &word, 4)) return std::nullopt;
        return word;
    }
    uint8_t MemoryRead8(Dynarmic::A64::VAddr vaddr) override { return read<uint8_t>(vaddr); }
    uint16_t MemoryRead16(Dynarmic::A64::VAddr vaddr) override { return read<uint16_t>(vaddr); }
    uint32_t MemoryRead32(Dynarmic::A64::VAddr vaddr) override { return read<uint32_t>(vaddr); }
    uint64_t MemoryRead64(Dynarmic::A64::VAddr vaddr) override { return read<uint64_t>(vaddr); }
    Dynarmic::A64::Vector MemoryRead128(Dynarmic::A64::VAddr vaddr) override {
        Dynarmic::A64::Vector v{};
        if (!guest_read(vaddr, v.data(), 16)) fault(vaddr, false);
        return v;
    }
    void MemoryWrite8(Dynarmic::A64::VAddr vaddr, uint8_t value) override { write(vaddr, value); }
    void MemoryWrite16(Dynarmic::A64::VAddr vaddr, uint16_t value) override { write(vaddr, value); }
    void MemoryWrite32(Dynarmic::A64::VAddr vaddr, uint32_t value) override { write(vaddr, value); }
    void MemoryWrite64(Dynarmic::A64::VAddr vaddr, uint64_t value) override { write(vaddr, value); }
    void MemoryWrite128(Dynarmic::A64::VAddr vaddr, Dynarmic::A64::Vector value) override {
        if (!guest_write(vaddr, value.data(), 16)) fault(vaddr, true);
    }
    bool MemoryWriteExclusive8(Dynarmic::A64::VAddr vaddr, uint8_t value, uint8_t expected) override {
        return cas(reinterpret_cast<volatile char*>(vaddr), static_cast<char>(value), static_cast<char>(expected));
    }
    bool MemoryWriteExclusive16(Dynarmic::A64::VAddr vaddr, uint16_t value, uint16_t expected) override {
        return cas(reinterpret_cast<volatile short*>(vaddr), static_cast<short>(value), static_cast<short>(expected));
    }
    bool MemoryWriteExclusive32(Dynarmic::A64::VAddr vaddr, uint32_t value, uint32_t expected) override {
        return cas(reinterpret_cast<volatile long*>(vaddr), static_cast<long>(value), static_cast<long>(expected));
    }
    bool MemoryWriteExclusive64(Dynarmic::A64::VAddr vaddr, uint64_t value, uint64_t expected) override {
        return cas(reinterpret_cast<volatile long long*>(vaddr), static_cast<long long>(value),
                   static_cast<long long>(expected));
    }
    bool MemoryWriteExclusive128(Dynarmic::A64::VAddr vaddr, Dynarmic::A64::Vector value,
                                 Dynarmic::A64::Vector expected) override {
        __try {
            long long compare[2] = {static_cast<long long>(expected[0]), static_cast<long long>(expected[1])};
            return _InterlockedCompareExchange128(reinterpret_cast<volatile long long*>(vaddr),
                                                  static_cast<long long>(value[1]), static_cast<long long>(value[0]),
                                                  compare) != 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fault(vaddr, true);
            return false;
        }
    }

    void InterpreterFallback(Dynarmic::A64::VAddr pc, size_t num_instructions) override {
        for (size_t i = 0; i < num_instructions; ++i) {
            uint32_t instruction = 0;
            if (!guest_read(pc, &instruction, 4)) {
                raise(SIGSEGV_, SEGV_MAPERR_, pc, pc);
                return;
            }
            if (!interpret_instruction(thread_, pc, instruction)) {
                raise(SIGILL_, ILL_ILLOPC_, pc, pc);
                return;
            }
            if (exception.signal) return;  // the interpreter faulted
            pc = jit_->GetPC();
        }
    }

    void CallSVC(uint32_t swi) override {
        svc_imm = swi;
        jit_->HaltExecution(kHaltSvc);
    }

    void ExceptionRaised(Dynarmic::A64::VAddr pc, Dynarmic::A64::Exception kind) override {
        using E = Dynarmic::A64::Exception;
        switch (kind) {
            case E::Yield:
            case E::WaitForEvent:
                SwitchToThread();
                return;
            case E::WaitForInterrupt:
                Sleep(0);
                return;
            case E::SendEvent:
            case E::SendEventLocal:
                return;
            case E::Breakpoint:
                raise(SIGTRAP_, TRAP_BRKPT_, pc, pc);
                return;
            case E::NoExecuteFault:
                raise(SIGSEGV_, SEGV_ACCERR_, pc, pc);
                return;
            default: {
                // Dynarmic knows the encoding but not the semantics: the
                // interpreter may still (system register reads, for example).
                uint32_t instruction = 0;
                if (guest_read(pc, &instruction, 4) && interpret_instruction(thread_, pc, instruction)) return;
                raise(SIGILL_, ILL_ILLOPC_, pc, pc);
                return;
            }
        }
    }

    void DataCacheOperationRaised(Dynarmic::A64::DataCacheOperation op, Dynarmic::A64::VAddr value) override {
        if (op == Dynarmic::A64::DataCacheOperation::ZeroByVA) {
            static const uint8_t zeros[64] = {};
            if (!guest_write(untag(value) & ~63ull, zeros, 64)) fault(value, true);
        }
    }
    void InstructionCacheOperationRaised(Dynarmic::A64::InstructionCacheOperation op,
                                         Dynarmic::A64::VAddr value) override {
        if (op == Dynarmic::A64::InstructionCacheOperation::InvalidateByVAToPoU)
            invalidate_code(value & ~63ull, 64);
        else {
            invalidate_code(address_space().arena_base(), address_space().arena_end() - address_space().arena_base());
            if (address_space().overlaps_guest(0, 0x100000000ull)) invalidate_code(0, 0x100000000ull);
        }
    }

    void AddTicks(uint64_t) override {}
    uint64_t GetTicksRemaining() override { return ~0ull >> 2; }
    uint64_t GetCNTPCT() override { return cntpct(); }

    void raise(int signal, int code, uint64_t address, uint64_t pc) {
        exception.signal = signal;
        exception.code = code;
        exception.address = address;
        exception.pc = pc;
        jit_->HaltExecution(kHaltException);
    }

private:
    template <class T>
    T read(uint64_t vaddr) {
        T value{};
        if (!guest_read(vaddr, &value, sizeof(T))) fault(vaddr, false);
        return value;
    }
    template <class T>
    void write(uint64_t vaddr, T value) {
        if (!guest_write(vaddr, &value, sizeof(T))) fault(vaddr, true);
    }
    template <class T>
    bool cas(volatile T* p, T value, T expected) {
        __try {
            if constexpr (sizeof(T) == 1) return _InterlockedCompareExchange8(p, value, expected) == expected;
            else if constexpr (sizeof(T) == 2) return _InterlockedCompareExchange16(p, value, expected) == expected;
            else if constexpr (sizeof(T) == 4) return _InterlockedCompareExchange(p, value, expected) == expected;
            else return _InterlockedCompareExchange64(p, value, expected) == expected;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fault(reinterpret_cast<uint64_t>(p), true);
            return false;
        }
    }
    void fault(uint64_t address, bool write) {
        (void)write;
        if (exception.signal) return;
        // The precise instruction is not known here (precise aborts would cost
        // GetSetElimination); the block's pc is close enough for a report.
        raise(SIGSEGV_, address_space().check(address, 1, 0) ? SEGV_ACCERR_ : SEGV_MAPERR_, address, jit_->GetPC());
    }

    GuestThread& thread_;
    std::unique_ptr<CpuEngine> jit_;
    uint64_t tpidrro_ = 0;
    std::mutex invalidate_lock_;
    std::vector<std::pair<uint64_t, uint64_t>> invalidations_;
};

// ---------------------------------------------------------------------------

void GuestCpuDeleter::operator()(GuestCpu* cpu) const { delete cpu; }

void attach_cpu(GuestThread& thread) {
    thread.processor_id = allocate_processor();
    thread.cpu.reset(new GuestCpu(thread));
    t_current = &thread;
}

void detach_cpu(GuestThread& thread) {
    monitor().ClearProcessor(static_cast<size_t>(thread.processor_id));
    thread.cpu.reset();
    release_processor(thread.processor_id);
    if (t_current == &thread) t_current = nullptr;
}

void load_context(GuestThread& thread, const CpuContext& c) {
    auto& jit = thread.cpu->jit();
    std::array<uint64_t, 31> regs;
    std::copy(std::begin(c.x), std::end(c.x), regs.begin());
    jit.SetRegisters(regs);
    jit.SetSP(c.sp);
    jit.SetPC(c.pc);
    jit.SetPstate(c.pstate);
    jit.SetFpcr(c.fpcr);
    jit.SetFpsr(c.fpsr);
    std::array<Dynarmic::A64::Vector, 32> vectors;
    for (int i = 0; i < 32; ++i) vectors[i] = c.v[i];
    jit.SetVectors(vectors);
}

void save_context(GuestThread& thread, CpuContext& c) {
    auto& jit = thread.cpu->jit();
    auto regs = jit.GetRegisters();
    std::copy(regs.begin(), regs.end(), std::begin(c.x));
    c.sp = jit.GetSP();
    c.pc = jit.GetPC();
    c.pstate = jit.GetPstate();
    c.fpcr = jit.GetFpcr();
    c.fpsr = jit.GetFpsr();
    auto vectors = jit.GetVectors();
    for (int i = 0; i < 32; ++i) c.v[i] = vectors[i];
}

uint64_t get_reg(GuestThread& t, int i) { return t.cpu->jit().GetRegister(static_cast<size_t>(i)); }
void set_reg(GuestThread& t, int i, uint64_t v) { t.cpu->jit().SetRegister(static_cast<size_t>(i), v); }
uint64_t get_pc(GuestThread& t) { return t.cpu->jit().GetPC(); }
uint64_t guest_pc_for_host_code(GuestThread& t, uint64_t host_ip) {
    return t.cpu ? t.cpu->jit().GuestPCForHostCode(reinterpret_cast<const void*>(host_ip)) : ~uint64_t{0};
}
uint64_t guest_pc_for_host_code_resumed(GuestThread& t, uint64_t host_ip, bool* optimized, uint64_t* region_start,
                                        uint32_t* region_bytes) {
    return t.cpu ? t.cpu->jit().GuestPCForHostCodeResumed(reinterpret_cast<const void*>(host_ip), optimized, region_start,
                                                           region_bytes) : ~uint64_t{0};
}
void set_pc(GuestThread& t, uint64_t pc) { t.cpu->jit().SetPC(pc); }
uint64_t get_sp(GuestThread& t) { return t.cpu->jit().GetSP(); }
void set_sp(GuestThread& t, uint64_t sp) { t.cpu->jit().SetSP(sp); }
Vec128 get_vec(GuestThread& t, int i) { return t.cpu->jit().GetVector(static_cast<size_t>(i)); }
void set_vec(GuestThread& t, int i, Vec128 v) { t.cpu->jit().SetVector(static_cast<size_t>(i), v); }

uint64_t HostCall::x(int i) const { return cpu_.jit().GetRegister(static_cast<size_t>(i)); }
float HostCall::s(int i) const {
    auto v = cpu_.jit().GetVector(static_cast<size_t>(i));
    float f;
    uint32_t bits = static_cast<uint32_t>(v[0]);
    std::memcpy(&f, &bits, 4);
    return f;
}
double HostCall::d(int i) const {
    auto v = cpu_.jit().GetVector(static_cast<size_t>(i));
    double f;
    std::memcpy(&f, &v[0], 8);
    return f;
}
Vec128 HostCall::v(int i) const { return cpu_.jit().GetVector(static_cast<size_t>(i)); }
uint64_t HostCall::sp() const { return cpu_.jit().GetSP(); }
uint64_t HostCall::lr() const { return cpu_.jit().GetRegister(30); }
uint64_t HostCall::stack(int slot) const {
    uint64_t value = 0;
    guest_read(sp() + 8ull * static_cast<uint64_t>(slot), &value, 8);
    return value;
}
void HostCall::ret(uint64_t value) { cpu_.jit().SetRegister(0, value); }
void HostCall::ret_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    cpu_.jit().SetVector(0, {bits, 0});
}
void HostCall::ret_double(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    cpu_.jit().SetVector(0, {bits, 0});
}
void HostCall::set_x(int i, uint64_t value) { cpu_.jit().SetRegister(static_cast<size_t>(i), value); }
void HostCall::set_v(int i, Vec128 value) { cpu_.jit().SetVector(static_cast<size_t>(i), value); }

// ---------------------------------------------------------------------------
// Trampolines: 16 bytes each in one read-only, executable guest page run.
//   movz x16, #lo ; movk x16, #hi, lsl #16 ; svc #1 ; ret

static void ensure_trampolines() {
    if (g_trampolines) return;
    uint64_t bytes = 16ull * kMaxHostFunctions + kPageSize;
    int64_t at = address_space().mmap(0, bytes, PROT_READ_ | PROT_WRITE_, MAP_PRIVATE_ | MAP_ANONYMOUS_, nullptr, 0,
                                      "[axrb-host-calls]");
    if (at < 0) fatal("cannot map host trampolines");
    address_space().populate(static_cast<uint64_t>(at), bytes);
    uint32_t* words = reinterpret_cast<uint32_t*>(at);
    for (uint32_t id = 0; id < kMaxHostFunctions; ++id) {
        words[id * 4 + 0] = 0xd2800010u | ((id & 0xffff) << 5);          // movz x16, #lo
        words[id * 4 + 1] = 0xf2a00010u | (((id >> 16) & 0xffff) << 5);  // movk x16, #hi, lsl #16
        words[id * 4 + 2] = 0xd4000001u | (kSvcHostCall << 5);           // svc #1
        words[id * 4 + 3] = 0xd65f03c0u;                                 // ret
    }
    uint64_t trap = static_cast<uint64_t>(at) + 16ull * kMaxHostFunctions;
    reinterpret_cast<uint32_t*>(trap)[0] = 0xd4000001u | (kSvcReturnTrap << 5);  // svc #2
    reinterpret_cast<uint32_t*>(trap)[1] = 0xd4200000u;                          // brk #0 (never reached)
    address_space().mprotect(static_cast<uint64_t>(at), bytes, PROT_READ_ | PROT_EXEC_);
    g_return_trap = trap;
    g_trampolines = static_cast<uint64_t>(at);
}

uint32_t register_host_function(const std::string& name, HostFunction fn) {
    std::unique_lock<std::shared_mutex> held(g_functions_lock);
    ensure_trampolines();
    if (g_function_names.size() >= kMaxHostFunctions) fatal("host function table full");
    g_function_names.push_back(name);
    g_functions.push_back(std::move(fn));
    return static_cast<uint32_t>(g_functions.size() - 1);
}

void register_named_function(const std::string& name, HostFunction fn) {
    std::unique_lock<std::shared_mutex> held(g_functions_lock);
    auto it = g_named.find(name);
    if (it != g_named.end()) {
        g_functions[it->second] = std::move(fn);
        return;
    }
    ensure_trampolines();
    g_function_names.push_back(name);
    g_functions.push_back(std::move(fn));
    g_named[name] = static_cast<uint32_t>(g_functions.size() - 1);
}

uint64_t host_trampoline(uint32_t id) { return g_trampolines + 16ull * id; }
uint64_t host_trampoline(const std::string& name, HostFunction fn) {
    return host_trampoline(register_host_function(name, std::move(fn)));
}
const std::string& host_function_name(uint32_t id) {
    std::shared_lock<std::shared_mutex> held(g_functions_lock);
    static const std::string unknown = "?";
    return id < g_function_names.size() ? g_function_names[id] : unknown;
}
uint64_t return_trap_address() {
    std::unique_lock<std::shared_mutex> held(g_functions_lock);
    ensure_trampolines();
    return g_return_trap;
}

void set_address_describer(std::function<std::string(uint64_t)> describer) { g_describer = std::move(describer); }

std::string describe_address(uint64_t address) {
    if (g_describer) return g_describer(address);
    std::string result;
    char text[64];
    std::snprintf(text, sizeof text, "%llx", (unsigned long long)address);
    result = text;
    std::string name;
    uint64_t base = 0;
    address_space().for_each([&](const Vma& vma) {
        if (address >= vma.start && address < vma.end) name = vma.name;
    });
    if (!name.empty()) {
        address_space().for_each([&](const Vma& vma) {
            if (vma.name == name && (!base || vma.start < base)) base = vma.start;
        });
        size_t slash = name.rfind('/');
        std::snprintf(text, sizeof text, "+%llx", (unsigned long long)(address - base));
        result = name.substr(slash == std::string::npos ? 0 : slash + 1) + text;
    }
    return result;
}

void dump_thread_state(GuestThread& thread, const char* why) {
    CpuContext c;
    save_context(thread, c);
    LOGE("cpu", "thread %d (%s): %s", thread.tid, thread.name.c_str(), why);
    LOGE("cpu", "  pc %s  lr %s  sp %llx", describe_address(c.pc).c_str(), describe_address(c.x[30]).c_str(),
         (unsigned long long)c.sp);
    for (int r = 0; r < 31; r += 4) {
        char line[256];
        int n = std::snprintf(line, sizeof line, "  x%-2d", r);
        for (int k = r; k < r + 4 && k < 31; ++k) n += std::snprintf(line + n, sizeof line - n, " %016llx", (unsigned long long)c.x[k]);
        LOGE("cpu", "%s", line);
    }
    // Frame records: [fp] = caller fp, [fp+8] = return address.
    uint64_t fp = c.x[29];
    for (int depth = 0; depth < 32 && fp; ++depth) {
        uint64_t record[2];
        if (!guest_read(fp, record, 16) || (fp & 7)) break;
        if (!record[1]) break;
        LOGE("cpu", "  #%02d %s", depth, describe_address(record[1]).c_str());
        if (record[0] <= fp) break;
        fp = record[0];
    }
}

void cpu_raise(GuestThread& thread, int signal, int code, uint64_t address, uint64_t pc) {
    thread.cpu->raise(signal, code, address, pc);
}

void kick_thread(GuestThread& thread) {
    if (thread.cpu) thread.cpu->jit().HaltExecution(kHaltKick);
    if (thread.event) SetEvent(thread.event);
}

void invalidate_code(uint64_t addr, uint64_t len) {
    g_syscall_counts[503].fetch_add(1, std::memory_order_relaxed);
    g_syscall_counts[504].fetch_add((len + 4095) >> 12, std::memory_order_relaxed);
    std::lock_guard<std::mutex> held(g_cpus_lock);
    for (GuestCpu* cpu : g_cpus) cpu->queue_invalidation(addr, len);
}

// ---------------------------------------------------------------------------
// The run loop.

static uint32_t named_function_id(GuestThread& thread) {
    uint64_t address = untag(get_reg(thread, 17));
    {
        std::shared_lock<std::shared_mutex> held(g_functions_lock);
        auto it = g_named_by_address.find(address);
        if (it != g_named_by_address.end()) return it->second;
    }
    std::string name;
    if (!guest_string(address, name, 256)) return ~0u;
    std::unique_lock<std::shared_mutex> held(g_functions_lock);
    auto it = g_named.find(name);
    if (it == g_named.end()) {
        LOGE("cpu", "guest called unknown host function '%s' from %s", name.c_str(),
             describe_address(get_reg(thread, 30)).c_str());
        return ~0u;
    }
    g_named_by_address[address] = it->second;
    return it->second;
}

static void handle_host_call(GuestThread& thread, uint32_t id) {
    HostFunction* fn = nullptr;
    {
        std::shared_lock<std::shared_mutex> held(g_functions_lock);
        if (id < g_functions.size()) fn = &g_functions[id];
    }
    if (!fn) {
        LOGE("cpu", "call to unregistered host function %u from %s", id, describe_address(get_reg(thread, 30)).c_str());
        set_reg(thread, 0, 0);
        return;
    }
    HostCall call(thread, *thread.cpu, id);
    (*fn)(call);
}

#ifdef AXRB_ARM_SERVICE_EMBEDDED
void arm_service_trap(GuestThread&,uint32_t,uint64_t);
bool arm_service_named(GuestThread& thread,const std::string& name) {
    uint32_t id;
    {std::shared_lock<std::shared_mutex> held(g_functions_lock);auto it=g_named.find(name);if(it==g_named.end())return false;id=it->second;}
    handle_host_call(thread,id);return true;
}
#endif
void run_guest(GuestThread& thread, bool nested) {
    GuestCpu& cpu = *thread.cpu;
    auto& jit = cpu.jit();
    for (;;) {
        if (thread.pending.load(std::memory_order_relaxed)) deliver_signals(thread);
        HaltReason reason = jit.Run();
        if (Dynarmic::Has(reason, kHaltInvalidate)) cpu.apply_invalidations();
        if (Dynarmic::Has(reason, kHaltException)) {
            auto ex = cpu.exception;
            cpu.exception = {};
            jit.SetPC(ex.pc);
            raise_fault(thread, ex.signal, ex.code, ex.address);
            continue;
        }
        if (Dynarmic::Has(reason, kHaltSvc)) {
            uint32_t imm = cpu.svc_imm;
#ifdef AXRB_ARM_SERVICE_EMBEDDED
            arm_service_trap(thread,imm,jit.GetPC()-4);
#endif
            if (imm == kSvcSyscall) {
                uint64_t nr = jit.GetRegister(8);
                if (nr == 113 || nr == 169 || nr == 98) {
                    // The calls engines make thousands of times a second
                    // (clocks, futex wakes) skip the general dispatch.
                    uint64_t x[9];
                    for (size_t i = 0; i < 6; ++i) x[i] = jit.GetRegister(i);
                    x[8] = nr;
                    if (fast_syscall(x)) {
                        jit.SetRegister(0, x[0]);
                        continue;
                    }
                }
                int64_t result = do_syscall(thread, nr, jit.GetRegister(0), jit.GetRegister(1), jit.GetRegister(2),
                                            jit.GetRegister(3), jit.GetRegister(4), jit.GetRegister(5));
                // rt_sigreturn replaced the whole state; everything else returns in x0.
                if (nr != 139) jit.SetRegister(0, static_cast<uint64_t>(result));
            } else if (imm == kSvcHostCall) {
                handle_host_call(thread, static_cast<uint32_t>(jit.GetRegister(16)));
            } else if (imm == kSvcNamedCall) {
                uint32_t id = named_function_id(thread);
                if (id == ~0u) jit.SetRegister(0, 0);
                else handle_host_call(thread, id);
            } else if (imm == kSvcReturnTrap) {
                if (nested) return;
                LOGE("cpu", "thread %d returned to the host-call trap with no call in progress", thread.tid);
                throw ThreadExit{0};
            } else {
                LOGW("cpu", "svc #%u at %s", imm, describe_address(jit.GetPC() - 4).c_str());
                jit.SetRegister(0, static_cast<uint64_t>(-ENOSYS_));
            }
        }
    }
}

uint64_t call_guest_args(GuestThread& thread, uint64_t function, const GuestArgs& args, Vec128* fp_result) {
    if (!thread.cpu) fatal("call_guest on a thread without a cpu");
    CpuContext saved;
    save_context(thread, saved);
    auto& jit = thread.cpu->jit();
    for (size_t i = 0; i < args.ints.size() && i < 8; ++i) jit.SetRegister(i, args.ints[i]);
    for (size_t i = 0; i < args.fps.size() && i < 8; ++i) jit.SetVector(i, args.fps[i]);
    // Below the caller's frame, with room for stack arguments, 16-aligned.
    uint64_t sp = (saved.sp - 512 - 8 * args.stack.size()) & ~15ull;
    if (!args.stack.empty()) guest_write(sp, args.stack.data(), args.stack.size() * 8);
    jit.SetSP(sp);
    jit.SetRegister(30, return_trap_address());
    jit.SetPC(function);
    ++thread.call_depth;
    struct Restore {
        GuestThread& thread;
        CpuContext& saved;
        bool active = true;
        ~Restore() {
            --thread.call_depth;
            if (active && thread.cpu) load_context(thread, saved);
        }
    } restore{thread, saved};
    run_guest(thread, true);
    uint64_t result = jit.GetRegister(0);
    if (fp_result) *fp_result = jit.GetVector(0);
    return result;
}

uint64_t call_guest(GuestThread& thread, uint64_t function, std::initializer_list<uint64_t> args) {
    GuestArgs a;
    a.ints.assign(args.begin(), args.end());
    if (a.ints.size() > 8) {
        a.stack.assign(a.ints.begin() + 8, a.ints.end());
        a.ints.resize(8);
    }
    return call_guest_args(thread, function, a, nullptr);
}

}  // namespace axrb
