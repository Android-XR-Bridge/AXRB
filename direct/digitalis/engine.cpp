#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <deque>
#include "digitalis.h"
#include "interpreter/arm64/interpreter.h"
#include "berberis/lite_translator/lite_translate_region.h"
#include "berberis/code_gen_lib/code_gen_lib.h"
#include "berberis/runtime_primitives/platform.h"
#ifdef AXRB_DIGITALIS_HEAVY
#include "berberis/heavy_optimizer/arm64/heavy_optimize_region.h"
#include "berberis/base/config_globals.h"
#endif

extern "C" void digitalis_enter(berberis::ThreadState*, const void*, void*);
extern "C" void digitalis_leave();
extern "C" void digitalis_dispatch();

namespace axrb::digitalis {
struct Block {
    void* code{};
    size_t bytes{};
    uint32_t countdown = UINT32_MAX;
    uint32_t syscall = UINT32_MAX;
};
// Two branch lookup tables, indexed by the same bits of the guest address.
// FinalEntry holds blocks that no longer count toward optimization: generated
// code's inline indirect-branch lookup reads only that table, four entries to
// a cache line. CacheEntry holds the blocks still counting.
struct FinalEntry { uint64_t pc{}; void* code{}; };
struct CacheEntry { uint64_t pc{}; void* code{}; uint32_t* countdown{}; uint64_t unused{}; };
struct DispatchContext {
    CacheEntry* entries;
    std::atomic<bool>* interrupted;
    Result* result;
    uint32_t remaining, used;
    uint32_t* loop_budget;
    uint32_t index_mask;  // the table index of a guest address, times four
    FinalEntry* finals;
};
static_assert(sizeof(CacheEntry) == 32 && sizeof(FinalEntry) == 16 && offsetof(DispatchContext, remaining) == 24);
static_assert(offsetof(DispatchContext, loop_budget) == 32);
static_assert(offsetof(DispatchContext, index_mask) == 40 && offsetof(DispatchContext, finals) == 48);
static_assert(offsetof(berberis::ThreadState, cpu.insn_addr) == 784);
static_assert(sizeof(std::atomic<bool>) == 1 && uint32_t(Stop::Yield) == 0);
// A per-thread branch lookup table. Its pages are zero until written, so
// a thread pays only for the part its code reaches, and clearing hands the
// pages back instead of writing megabytes of zeros.
template <typename Entry>
struct DispatchCache {
    Entry* entries{};
    size_t bytes{};
    void allocate(size_t count) {
        bytes = count * sizeof(Entry);
        entries = static_cast<Entry*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!entries) throw std::bad_alloc();
        entries[0].pc = 1;
    }
    void clear() {
        if (!entries) return;
        // Generated code holds the table's address: the same range, new zero pages.
        if (!VirtualFree(entries, bytes, MEM_DECOMMIT) || !VirtualAlloc(entries, bytes, MEM_COMMIT, PAGE_READWRITE))
            std::memset(entries, 0, bytes);
        // A zero entry must not match a branch to address zero.
        entries[0].pc = 1;
    }
    Entry* data() { return entries; }
    Entry& operator[](size_t index) { return entries[index]; }
    ~DispatchCache() { if (entries) VirtualFree(entries, 0, MEM_RELEASE); }
};
// One contiguous range for all of an engine's generated code, so regions
// reach each other and the dispatcher stub at its start with a rel32 jump.
// First-tier code grows up from the bottom and optimized code down from the
// top: the code a thread spends its time in stays together instead of being
// scattered among the regions that ran a few times.
struct CodeArena {
    static constexpr size_t kStub = 16, kCommit = size_t{1} << 20;
    uint8_t* base{};
    size_t size{}, low{}, high{}, low_committed{}, high_committed{};
    void reserve(size_t bytes) {
        size = bytes;
        // Within one 4 GiB block of the address space: branch predictors keep
        // only the low half of a target and take the rest from the branch's
        // own address, so a branch across such a boundary is never predicted.
        // Found by reserving a block more than needed and taking its aligned part.
        constexpr uint64_t kBlock = uint64_t{1} << 32;
        for (int attempt = 0; attempt != 16 && !base && size <= kBlock; ++attempt) {
            void* probe = VirtualAlloc(nullptr, size + kBlock, MEM_RESERVE, PAGE_NOACCESS);
            if (!probe) break;
            const uint64_t aligned = (reinterpret_cast<uint64_t>(probe) + kBlock - 1) & ~(kBlock - 1);
            VirtualFree(probe, 0, MEM_RELEASE);
            // Another thread may have taken the range in between.
            base = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(aligned), size, MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        }
        if (!base) base = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!base) throw std::bad_alloc();
        low_committed = 0;
        high_committed = size;
        start();
    }
    // Writable and executable, like Dynarmic's code cache: switching
    // protection per block cost two system calls on every compile.
    void commit(size_t from, size_t to) {
        if (!VirtualAlloc(base + from, to - from, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) throw std::bad_alloc();
    }
    void start() {
        low = kStub;
        high = size;
        // jmp [rip+0]; the dispatcher's address.
        const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
        const uint64_t target = reinterpret_cast<uint64_t>(&digitalis_dispatch);
        ensure_low(kStub);
        std::memcpy(base, jump, sizeof jump);
        std::memcpy(base + sizeof jump, &target, sizeof target);
    }
    void ensure_low(size_t end) {
        if (end <= low_committed) return;
        const size_t to = std::min(size, (end + kCommit - 1) & ~(kCommit - 1));
        commit(low_committed, to);
        low_committed = to;
    }
    uint8_t* allocate(size_t bytes, bool optimized) {
        if (bytes > high - low) return nullptr;
        if (!optimized) {
            ensure_low(low + bytes);
            low += bytes;
            return base + low - bytes;
        }
        high -= bytes;
        if (high < high_committed) {
            const size_t from = high & ~(kCommit - 1);
            commit(from, high_committed);
            high_committed = from;
        }
        return base + high;
    }
    void reset() {
        VirtualFree(base, size, MEM_DECOMMIT);
        low_committed = 0;
        high_committed = size;
        start();
    }
    ~CodeArena() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
};
// A direct jump to another region: where its rel32 field is in the code just
// generated, and the guest address it leads to.
struct JumpSite { uint32_t offset; uint64_t pc; };
struct Optimized {
    uint64_t pc{}, generation{};
    std::unique_ptr<berberis::MachineCode> code;  // null: nothing to install
    std::vector<JumpSite> sites;
};
// Every installed region, in installation order, for guest_pc_for_host_code.
// The owner appends; a profiler thread reads up to the published count.
struct RegionLog {
    struct Region { uint64_t host, pc; uint32_t bytes; bool optimized; };
    static constexpr size_t kChunk = 4096, kChunks = 8192;
    std::unique_ptr<std::atomic<Region*>[]> chunks{new std::atomic<Region*>[kChunks]{}};
    std::atomic<size_t> count{0};
    std::atomic<uint64_t> resets{0};
    // The reader's sorted copy.
    std::mutex index_lock;
    std::vector<Region> index;
    size_t indexed{};
    uint64_t index_resets{};
    void add(uint64_t host, uint32_t bytes, uint64_t pc, bool optimized) {
        const size_t n = count.load(std::memory_order_relaxed);
        if (n == kChunk * kChunks) return;
        Region* chunk = chunks[n / kChunk].load(std::memory_order_relaxed);
        if (!chunk) chunks[n / kChunk].store(chunk = new Region[kChunk], std::memory_order_release);
        chunk[n % kChunk] = {host, pc, bytes, optimized};
        count.store(n + 1, std::memory_order_release);
    }
    // Host addresses are reused after a cache reset.
    void reset() {
        count.store(0, std::memory_order_release);
        resets.fetch_add(1, std::memory_order_release);
    }
    ~RegionLog() { for (size_t i = 0; i < kChunks; ++i) delete[] chunks[i].load(); }
};
struct Engine {
    Callbacks callbacks;
    berberis::ThreadState state{};
    uint32_t loop_budget{};
    std::unordered_map<uint64_t, Block> blocks;
    std::unordered_map<uint64_t, std::vector<uint64_t>> page_blocks;
    DispatchCache<CacheEntry> dispatch_cache;
    DispatchCache<FinalEntry> final_cache;
    uint32_t cache_mask = 262143;
    bool chain = !GetEnvironmentVariableA("AXRB_DIGITALIS_NO_CHAIN", nullptr, 0);
    // Whether all of this engine's code clears the top byte of the addresses
    // it uses (see TaggedSites). The background optimizer reads it too.
    std::atomic<bool> mask_addresses{false};
    std::unordered_set<uint64_t> retranslated_sites;  // instructions this engine retranslated for a tagged pointer
    bool heavy = false, force_heavy = false;
    uint32_t heavy_min_instructions = 1;
    bool interpret_only = GetEnvironmentVariableA("AXRB_DIGITALIS_INTERPRET_ONLY", nullptr, 0) != 0;
    CodeArena arena;
    berberis::RecoveryMap recovery;
    std::atomic<bool> interrupted{false};
    Result result{Stop::Yield};
    uint64_t compiled{}, interpreted{};
    uint64_t heavy_attempts{}, heavy_compiled{};
    size_t cache_bytes{};
    bool profile = GetEnvironmentVariableA("AXRB_DIGITALIS_PROFILE", nullptr, 0) != 0;
    uint64_t jit_calls{}, clears{}, compile_ns{}, interpret_ns{}, run_ns{}, reports{};
    uint64_t runs{}, enters{}, svc_exits{};
    std::unordered_map<uint32_t, uint64_t> fallback_words;
    // Direct-branch chaining: a region jumps straight to its target's final
    // host code, or to the arena's dispatcher stub until the target is
    // translated (or while it still counts toward optimization). The owner
    // patches the rel32 fields listed here, as arena offsets by target; no
    // other thread runs or writes this engine's code.
    std::unordered_map<uint64_t, std::vector<uint32_t>> sites_by_pc;
    bool async_heavy = !GetEnvironmentVariableA("AXRB_DIGITALIS_SYNC_HEAVY", nullptr, 0);
    uint64_t generation{};  // invalidations and cache resets; stale optimizations are dropped
    std::mutex optimized_lock;
    std::vector<Optimized> optimized;  // finished in the background, installed by the owner
    std::atomic<bool> optimizer_done{false};
    std::chrono::steady_clock::time_point next_report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    RegionLog regions;
};
static_assert(offsetof(Engine, loop_budget) - offsetof(Engine, state) == sizeof(berberis::ThreadState));
using Clock = std::chrono::steady_clock;
uint64_t elapsed_ns(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
// A thread's translated code is discarded and translated again from nothing
// when it reaches this size, which the player sees as a stall. An engine's main
// thread reaches 150 MiB in a start scene, so machines with the memory for it
// get more room than Dynarmic's fixed 256 MiB: 1/32 of RAM, up to 1 GiB.
size_t cache_limit() {
    static const size_t limit = [] {
        MEMORYSTATUSEX status{sizeof status};
        const uint64_t ram = GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
        return size_t(std::clamp<uint64_t>(ram / 32, 256ull << 20, 1024ull << 20));
    }();
    return limit;
}
// ARM64 ignores the top byte of an address, and a guest may keep a tag there.
// The host does not, so translated code would have to clear the byte before
// every load and store: two shifts in the path of each address. Few
// instructions ever see a tagged pointer (AXRB reports no tagged-address ABI
// to bionic; its allocator still tags the address of its chunk headers), so
// code starts out without the shifts. A tagged pointer then faults as a
// non-canonical address, and the instruction it faulted in joins this
// process-wide set of those translated with the shifts. A guest that tags
// pointers all over gets the shifts everywhere: `everywhere`, which
// AXRB_DIGITALIS_MASK_ADDRESSES=1 starts with.
struct TaggedSites {
    static constexpr size_t kMost = 2048;
    std::shared_mutex lock;
    std::unordered_set<uint64_t> sites;
    std::atomic<size_t> count{0};
    std::atomic<bool> everywhere{GetEnvironmentVariableA("AXRB_DIGITALIS_MASK_ADDRESSES", nullptr, 0) != 0};
    bool contains(uint64_t pc) {
        if (everywhere.load(std::memory_order_relaxed)) return true;
        if (!count.load(std::memory_order_relaxed)) return false;
        std::shared_lock held(lock);
        return sites.contains(pc);
    }
    void add(uint64_t pc) {
        std::unique_lock held(lock);
        sites.insert(pc);
        count.store(sites.size(), std::memory_order_relaxed);
        if (sites.size() > kMost && !everywhere.exchange(true))
            std::fprintf(stderr, "Digitalis: tagged pointers at over %zu instructions, masking every address from now on\n", kMost);
    }
} tagged_sites;
thread_local Engine* active = nullptr;
thread_local bool optimizing = false;  // a background optimizer thread
thread_local std::vector<JumpSite> emitted_sites;  // by the region being generated
// The guest code a region is being generated from, for return_addresses().
struct GuestWindow { uint64_t begin{}, end{}; const uint8_t* bytes{}; bool lite{}; };
thread_local GuestWindow guest_window;
// Generated code is per engine, so its interrupt flag sits at a fixed offset
// from the ThreadState that RBP points to.
int32_t interrupted_offset() {
    return int32_t(offsetof(Engine, interrupted)) - int32_t(offsetof(Engine, state));
}
bool chainable(const Block& block) { return block.code && block.countdown == UINT32_MAX && block.syscall == UINT32_MAX; }
// Points the jump whose rel32 field is at this arena offset at code, or at the dispatcher stub.
void point(Engine* engine, uint32_t field, const void* code) {
    uint8_t* at = engine->arena.base + field;
    const int32_t relative = int32_t((code ? static_cast<const uint8_t*>(code) : engine->arena.base) - (at + 4));
    std::memcpy(at, &relative, sizeof relative);
}
void point_all(Engine* engine, uint64_t pc, const void* code) {
    auto found = engine->sites_by_pc.find(pc);
    if (found == engine->sites_by_pc.end()) return;
    for (const uint32_t field : found->second) point(engine, field, code);
}
// Called from generated code (Windows x64 ABI) with every guest register
// already stored to the ThreadState.
bool fast_syscall(berberis::ThreadState* state) {
    Engine* engine = active;
    return engine->callbacks.syscall(engine->callbacks.opaque, state->cpu.x);
}
void publish(Engine* engine, uint64_t pc, const Block& block) {
    point_all(engine, pc, chainable(block) ? block.code : nullptr);
}
bool fetch(Engine* engine, uint64_t address, void* out, size_t bytes) {
    const auto callback = engine->callbacks.fetch ? engine->callbacks.fetch : engine->callbacks.read;
    return callback(engine->callbacks.opaque, address, out, bytes);
}

LONG recover(EXCEPTION_POINTERS* exception) {
    if (!active || exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto address = exception->ExceptionRecord->ExceptionInformation[1];
    if (active->callbacks.fault && active->callbacks.fault(active->callbacks.opaque, address,
            exception->ExceptionRecord->ExceptionInformation[0] == 1)) return EXCEPTION_CONTINUE_EXECUTION;
    if (optimizing) return EXCEPTION_CONTINUE_SEARCH;  // the optimizer abandons the region
    auto found = active->recovery.find(exception->ContextRecord->Rip);
    if (found == active->recovery.end()) return EXCEPTION_CONTINUE_SEARCH;
    active->result = {Stop::MemoryFault, 0, address};
    exception->ContextRecord->Rip = found->second;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void interpret_one(Engine* engine) {
    berberis::Interpreter interpreter(&engine->state);
    berberis::SemanticsPlayer player(&interpreter);
    berberis::Decoder decoder(&player);
    auto length = decoder.Decode(berberis::ToHostAddr<const uint16_t>(engine->state.cpu.insn_addr));
    interpreter.FinalizeInsn(length);
}
int interpreter_fault(EXCEPTION_POINTERS* exception, Engine* engine) {
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    engine->result = {Stop::MemoryFault, 0, exception->ExceptionRecord->ExceptionInformation[1]};
    return EXCEPTION_EXECUTE_HANDLER;
}
void safe_interpret(Engine* engine) {
    __try { interpret_one(engine); }
    __except(interpreter_fault(GetExceptionInformation(), engine)) {}
}

void clear_cache(Engine* engine) {
    ++engine->clears;
    ++engine->generation;
    engine->arena.reset();
    engine->sites_by_pc.clear();
    engine->retranslated_sites.clear();
    engine->blocks.clear();
    engine->page_blocks.clear();
    engine->dispatch_cache.clear();
    engine->final_cache.clear();
    engine->recovery.clear();
    engine->cache_bytes = 0;
    engine->regions.reset();
}
Engine* create(Callbacks callbacks) {
    static std::once_flag installed;
    std::call_once(installed, [] { if (!AddVectoredExceptionHandler(1, recover)) throw std::runtime_error("Digitalis fault handler registration failed"); });
    auto engine = std::make_unique<Engine>();
    engine->callbacks = callbacks;
    char cache_bits[16]{};
    if (GetEnvironmentVariableA("AXRB_DIGITALIS_CACHE_BITS", cache_bits, sizeof cache_bits)) {
        const auto bits = std::clamp<uint32_t>(std::strtoul(cache_bits, nullptr, 10), 12, 20);
        engine->cache_mask = (1u << bits) - 1;
    }
    engine->dispatch_cache.allocate(size_t{engine->cache_mask} + 1);
    engine->final_cache.allocate(size_t{engine->cache_mask} + 1);
    // The cache is reset at cache_limit(); the margin holds what is installed before the next check.
    engine->arena.reserve(cache_limit() + (size_t{4} << 20));
#ifdef AXRB_DIGITALIS_HEAVY
    char mode[16]{};
    GetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY", mode, sizeof mode);
    engine->heavy = std::strcmp(mode, "0") != 0;
    engine->force_heavy = std::strcmp(mode, "force") == 0;
    char minimum[16]{};
    if (GetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY_MIN_INSNS", minimum, sizeof minimum))
        engine->heavy_min_instructions = std::clamp<uint32_t>(std::strtoul(minimum, nullptr, 10), 1, 200);
#endif
    return engine.release();
}
void invalidate_cache(Engine* engine, uint64_t address, uint64_t size) {
    if (!size) return;
    const uint64_t first = address & ~uint64_t{4095};
    const uint64_t last = size - 1 > UINT64_MAX - address ? UINT64_MAX : (address + size - 1) | uint64_t{4095};
    bool erased = false;
    // Compilation never crosses a guest page, so page-granular invalidation
    // covers blocks beginning before a modified instruction without flushing
    // unrelated game code. Retired code stays allocated until the next cache
    // reset; no executing thread shares this engine's code.
    auto erase_page = [&](auto page) {
        for (const auto pc : page->second) {
            engine->blocks.erase(pc);
            point_all(engine, pc, nullptr);
        }
        erased = true;
        return engine->page_blocks.erase(page);
    };
    const auto last_page = last & ~uint64_t{4095};
    const auto pages = ((last_page - first) >> 12) + 1;
    // Most invalidations are tiny. Index by guest page instead of scanning
    // hundreds of thousands of blocks on every mmap/protection change.
    if (pages <= engine->page_blocks.size()) {
        for (uint64_t page = first;; page += 4096) {
            auto found = engine->page_blocks.find(page);
            if (found != engine->page_blocks.end()) erase_page(found);
            if (page == last_page) break;
        }
    } else {
        for (auto it = engine->page_blocks.begin(); it != engine->page_blocks.end();) {
            if (it->first >= first && it->first <= last_page) it = erase_page(it);
            else ++it;
        }
    }
    if (erased) {
        ++engine->generation;
        engine->dispatch_cache.clear();
        engine->final_cache.clear();
        if (engine->blocks.empty()) clear_cache(engine);
    }
}
void start_masking_addresses(Engine* engine) {
    engine->mask_addresses.store(true, std::memory_order_relaxed);
    clear_cache(engine);  // optimizations in progress belong to the old generation and are dropped
}
void forget_optimizations(Engine* engine);
void destroy(Engine* engine) {
    if (!engine) return;
    forget_optimizations(engine);
    clear_cache(engine);
    delete engine;
}
void interrupt(Engine* engine) {
    engine->interrupted.store(true, std::memory_order_release);
    // Optimized loops count this down and exit at zero; a lost race with
    // their decrement only delays the exit to the regular budget.
    reinterpret_cast<volatile uint32_t&>(engine->loop_budget) = 1;
}
void clear_interrupt(Engine* engine) { engine->interrupted.store(false, std::memory_order_release); }
uint64_t translated_blocks(const Engine* engine) { return engine->compiled; }
uint64_t interpreted_instructions(const Engine* engine) { return engine->interpreted; }
uint64_t optimized_blocks(const Engine* engine) { return engine->heavy_compiled; }
uint64_t guest_pc_for_host_code(Engine* engine, const void* code, bool* optimized, uint64_t* region_start,
                                uint32_t* region_bytes) {
    auto& log = engine->regions;
    std::lock_guard held(log.index_lock);
    const uint64_t resets = log.resets.load(std::memory_order_acquire);
    if (resets != log.index_resets) {
        log.index.clear();
        log.indexed = 0;
        log.index_resets = resets;
    }
    const size_t count = log.count.load(std::memory_order_acquire);
    if (count > log.indexed) {
        for (size_t i = log.indexed; i < count; ++i)
            log.index.push_back(log.chunks[i / RegionLog::kChunk].load(std::memory_order_acquire)[i % RegionLog::kChunk]);
        log.indexed = count;
        std::sort(log.index.begin(), log.index.end(), [](const auto& a, const auto& b) { return a.host < b.host; });
    }
    const uint64_t ip = reinterpret_cast<uint64_t>(code);
    auto after = std::upper_bound(log.index.begin(), log.index.end(), ip, [](uint64_t value, const auto& region) { return value < region.host; });
    if (after == log.index.begin()) return ~uint64_t{0};
    const auto& region = *std::prev(after);
    if (ip - region.host >= region.bytes) return ~uint64_t{0};
    if (optimized) *optimized = region.optimized;
    if (region_start) *region_start = region.host;
    if (region_bytes) *region_bytes = region.bytes;
    return region.pc;
}

// Places generated code in the engine's arena and connects its direct jumps.
// Only the owning thread does; no other thread executes or modifies the code.
// No code in the result: the arena is full until the next cache reset.
Block install(Engine* engine, berberis::MachineCode& code, uint64_t pc, bool optimized, const std::vector<JumpSite>& sites) {
    const size_t bytes = (code.install_size() + 15) & ~size_t{15};
    uint8_t* memory = engine->arena.allocate(bytes, optimized);
    if (!memory) return {};
    code.InstallUnsafe(memory, &engine->recovery);
    for (const auto& site : sites) {
        if (site.offset < 1 || site.offset + 4 > code.install_size() || memory[site.offset - 1] != 0xe9)
            throw std::runtime_error("Digitalis lost track of a direct jump");
        const uint32_t field = uint32_t(memory + site.offset - engine->arena.base);
        engine->sites_by_pc[site.pc].push_back(field);
        auto target = engine->blocks.find(site.pc);
        if (target != engine->blocks.end() && chainable(target->second)) point(engine, field, target->second.code);
    }
    ++engine->compiled;
    engine->cache_bytes += bytes;
    engine->regions.add(reinterpret_cast<uint64_t>(memory), uint32_t(bytes), pc, optimized);
    // AXRB_DIGITALIS_DUMP=<directory>: every region's host code, for a disassembler
    // (llvm-objdump -D -b binary -m i386:x86-64), named <guest pc>-<tier>-<host address>.
    static const std::string dump = [] {
        char directory[MAX_PATH]{};
        GetEnvironmentVariableA("AXRB_DIGITALIS_DUMP", directory, sizeof directory);
        return std::string(directory);
    }();
    if (!dump.empty()) {
        char name[MAX_PATH + 64];
        std::snprintf(name, sizeof name, "%s\\%llx-%s-%llx.bin", dump.c_str(), static_cast<unsigned long long>(pc),
                      optimized ? "opt" : "lite", static_cast<unsigned long long>(reinterpret_cast<uint64_t>(memory)));
        if (FILE* file = std::fopen(name, "wb")) {
            std::fwrite(memory, 1, code.install_size(), file);
            std::fclose(file);
        }
    }
    return {memory, bytes};
}

Block compile(Engine* engine, uint64_t pc, bool optimize = false) {
    if (engine->interpret_only) return {};
    // Compiler arithmetic must neither inherit guest rounding nor contribute
    // exceptions to the guest's FPSR when returning to generated code.
    struct CompilerFpEnvironment {
        unsigned saved = _mm_getcsr();
        CompilerFpEnvironment() { _mm_setcsr(0x1f80); }
        ~CompilerFpEnvironment() { _mm_setcsr(saved); }
    } compiler_fp_environment;
    // Never decode beyond the readable guest page. Failed prefixes are retried
    // up to the unsupported instruction so the interpreter handles only that op.
    uint64_t end = (pc & ~uint64_t{4095}) + 4096;
    end = std::min(end, pc + (optimize || engine->force_heavy ? 800 : 256));
    uint8_t readable[800];
    const bool fetched = fetch(engine, pc, readable, end - pc);
    if (!fetched) end = pc + 4;
    struct Window {
        ~Window() { guest_window = {}; }
    } window;
    for (int attempt = 0; attempt != 2; ++attempt) {
        berberis::MachineCode code;
        berberis::MachineCode* selected_code = &code;
        berberis::LiteTranslateParams params;
        params.end_pc = end;
        params.allow_dispatch = engine->chain;
        bool ok = false;
        uint64_t next = pc;
#ifdef AXRB_DIGITALIS_HEAVY
        berberis::MachineCode heavy_code;
        emitted_sites.clear();
        guest_window = fetched ? GuestWindow{pc, end, readable, false} : GuestWindow{};
        if (optimize || engine->force_heavy) {
            ++engine->heavy_attempts;
            berberis::HeavyOptimizeParams heavy_params;
            heavy_params.end_pc = end;
            bool loop = false;
            auto [stop, success, instructions] = berberis::HeavyOptimizeRegion(pc, &heavy_code, heavy_params, &loop);
            // Even an unsupported final instruction leaves a valid translated
            // prefix. No progress falls through to the lite tier.
            ok = instructions > 0 && stop != pc;
            if (optimize && ((!success && !loop) || (success && instructions < engine->heavy_min_instructions)))
                ok = false;
            next = stop;
            if (ok) { selected_code = &heavy_code; ++engine->heavy_compiled; }
            else if (optimize) return {};
        }
#endif
        if (!ok) {
            emitted_sites.clear();
            guest_window.lite = true;
            std::tie(ok, next) = berberis::TryLiteTranslateRegion(pc, &code, params);
        }
        if (!ok) {
            if (next == pc) return {};
            end = next;
            continue;
        }
        Block block = install(engine, *selected_code, pc, selected_code != &code, emitted_sites);
        if (!block.code) return {};
        block.countdown = engine->heavy && !optimize && !engine->force_heavy ? 512u : UINT32_MAX;
        return block;
    }
    return {};
}

#ifdef AXRB_DIGITALIS_HEAVY
// Optimizes the hot region at pc on a background thread. False if the region
// does not qualify or became unreadable (a fault abandons it).
bool optimize_region(Engine* engine, uint64_t pc, berberis::MachineCode* code) {
    const uint64_t end = std::min((pc & ~uint64_t{4095}) + 4096, pc + 800);
    uint8_t readable[800];
    if (!fetch(engine, pc, readable, end - pc)) return false;
    berberis::HeavyOptimizeParams params;
    params.end_pc = end;
    bool loop = false;
    emitted_sites.clear();
    guest_window = {pc, end, readable, false};
    auto [stop, success, instructions] = berberis::HeavyOptimizeRegion(pc, code, params, &loop);
    guest_window = {};
    return instructions > 0 && stop != pc && (success || loop) &&
        !(success && instructions < engine->heavy_min_instructions);
}
struct OptimizeCall { Engine* engine; uint64_t pc; berberis::MachineCode* code; bool ok; };
void optimize_unguarded(OptimizeCall* call) {
    try { call->ok = optimize_region(call->engine, call->pc, call->code); } catch (...) { call->ok = false; }
}
bool optimize_guarded(OptimizeCall* call) {
    __try { optimize_unguarded(call); return call->ok; }
    __except(GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

// Hot regions are optimized by background threads, so the guest thread that
// made them hot keeps running their lite translation instead of stalling for
// the optimizer (about 0.1 ms per region; thousands while a game loads). The
// owner installs the code the next time it runs, unless an invalidation or a
// cache reset happened since the region was queued.
class Optimizer {
public:
    static Optimizer& instance() {
        static Optimizer optimizer;
        return optimizer;
    }
    bool post(Engine* engine, uint64_t pc) {
        std::call_once(started_, [this] {
            for (int i = 0; i < kWorkers; ++i) std::thread([this, i] { work(i); }).detach();
        });
        std::lock_guard held(lock_);
        if (jobs_.size() >= 4096) return false;
        jobs_.push_back({engine, pc, engine->generation});
        ready_.notify_one();
        return true;
    }
    // Before an engine goes away: drops its jobs and waits for one in progress.
    void forget(Engine* engine) {
        std::unique_lock held(lock_);
        std::erase_if(jobs_, [&](const Job& job) { return job.engine == engine; });
        idle_.wait(held, [&] { return std::find(std::begin(busy_), std::end(busy_), engine) == std::end(busy_); });
    }

private:
    static constexpr int kWorkers = 2;
    struct Job { Engine* engine; uint64_t pc, generation; };
    void work(int index) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        optimizing = true;
        for (;;) {
            Job job;
            {
                std::unique_lock held(lock_);
                ready_.wait(held, [&] { return !jobs_.empty(); });
                job = jobs_.front();
                jobs_.pop_front();
                busy_[index] = job.engine;
            }
            active = job.engine;
            auto code = std::make_unique<berberis::MachineCode>();
            OptimizeCall call{job.engine, job.pc, code.get(), false};
            const bool ok = optimize_guarded(&call);
            active = nullptr;
            {
                std::lock_guard held(job.engine->optimized_lock);
                if (ok) job.engine->optimized.push_back({job.pc, job.generation, std::move(code), std::move(emitted_sites)});
                emitted_sites.clear();
                job.engine->optimizer_done.store(true, std::memory_order_release);
            }
            std::lock_guard held(lock_);
            busy_[index] = nullptr;
            idle_.notify_all();
        }
    }
    std::once_flag started_;
    std::mutex lock_;
    std::condition_variable ready_, idle_;
    std::deque<Job> jobs_;
    Engine* busy_[kWorkers]{};
};

void forget_optimizations(Engine* engine) { Optimizer::instance().forget(engine); }

// Owner thread, before running translated code.
void install_optimized(Engine* engine) {
    if (!engine->optimizer_done.load(std::memory_order_acquire)) return;
    std::vector<Optimized> ready;
    {
        std::lock_guard held(engine->optimized_lock);
        ready.swap(engine->optimized);
        engine->optimizer_done.store(false, std::memory_order_relaxed);
    }
    for (auto& result : ready) {
        auto found = engine->blocks.find(result.pc);
        if (!result.code || result.generation != engine->generation || found == engine->blocks.end() ||
            !found->second.code) continue;
        const Block optimized = install(engine, *result.code, result.pc, true, result.sites);
        if (!optimized.code) continue;  // no room: the first-tier code stays
        found->second = optimized;
        ++engine->heavy_compiled;
        publish(engine, result.pc, found->second);
        const size_t index = (result.pc >> 2) & engine->cache_mask;
        if (engine->dispatch_cache[index].pc == result.pc) engine->dispatch_cache[index] = {};
        engine->final_cache[index] = {result.pc, optimized.code};
    }
}
#else
void forget_optimizations(Engine*) {}
#endif

Result run(Engine* engine, Registers* registers, uint32_t budget) {
    const auto run_started = engine->profile ? Clock::now() : Clock::time_point{};
    auto& cpu = engine->state.cpu;
    std::memcpy(cpu.x, registers->x, sizeof cpu.x);
    std::memcpy(cpu.v, registers->v, sizeof cpu.v);
    cpu.sp = registers->sp;
    cpu.insn_addr = registers->pc;
    cpu.cached_fpcr = registers->fpcr;
    cpu.emulated_fpsr = registers->fpsr;
    cpu.flags = ((registers->pstate >> 16) & 0xc000) | ((registers->pstate >> 21) & 0x100) | ((registers->pstate >> 28) & 1);
    engine->state.tls = registers->tls;
    engine->result = {Stop::Yield};
    engine->loop_budget = std::min(budget, 4096u);
    const unsigned saved_mxcsr = _mm_getcsr();
    constexpr unsigned rounding[] = {0, 2, 1, 3};
    unsigned mxcsr = 0x1f80 | (rounding[(registers->fpcr >> 22) & 3] << 13);
    if (registers->fpcr & (1u << 24)) mxcsr |= 0x8040;
    _mm_setcsr(mxcsr);
    struct Restore {
        Engine* previous; unsigned mxcsr;
        ~Restore() { active = previous; _mm_setcsr(mxcsr); }
    } restore{active, saved_mxcsr};
    active = engine;
    if (tagged_sites.everywhere.load(std::memory_order_relaxed) && !engine->mask_addresses.load(std::memory_order_relaxed))
        start_masking_addresses(engine);
#ifdef AXRB_DIGITALIS_HEAVY
    install_optimized(engine);
#endif
    // The host reports a non-canonical address as -1: a tagged pointer in an
    // instruction this engine translated without masking. The fault left the
    // guest at that instruction, which runs again once its page is translated
    // anew. A second fault there is the guest's own.
    auto tagged_pointer_fault = [&] {
        if (engine->result.reason != Stop::MemoryFault || engine->result.address != ~uint64_t{0} ||
            engine->mask_addresses.load(std::memory_order_relaxed) ||
            !engine->retranslated_sites.insert(cpu.insn_addr).second) return false;
        tagged_sites.add(cpu.insn_addr);
        if (tagged_sites.everywhere.load(std::memory_order_relaxed)) start_masking_addresses(engine);
        else invalidate_cache(engine, cpu.insn_addr, 4);
        engine->result = {Stop::Yield};
        return true;
    };
    auto execute = [&](uint64_t pc, Block& block, uint32_t remaining) {
        if (block.countdown != UINT32_MAX) {
            if (block.countdown) --block.countdown;
            if (!block.countdown) {
#ifdef AXRB_DIGITALIS_HEAVY
                if (engine->async_heavy) {
                    // Chained as it is until the optimized code arrives;
                    // with the queue full, try again later.
                    if (Optimizer::instance().post(engine, pc)) {
                        ++engine->heavy_attempts;
                        block.countdown = UINT32_MAX;
                        publish(engine, pc, block);
                    } else {
                        block.countdown = 4096;
                    }
                } else
#endif
                {
                    const auto start = engine->profile ? Clock::now() : Clock::time_point{};
                    auto optimized = compile(engine, pc, true);
                    if (optimized.code) block = optimized;
                    block.countdown = UINT32_MAX;
                    publish(engine, pc, block);
                    if (engine->profile) engine->compile_ns += elapsed_ns(start);
                }
            }
        }
        const size_t index = (pc >> 2) & engine->cache_mask;
        if (chainable(block)) engine->final_cache[index] = {pc, block.code};
        else engine->dispatch_cache[index] = {pc, block.code, &block.countdown};
        DispatchContext context{engine->dispatch_cache.data(), &engine->interrupted, &engine->result,
            engine->chain ? remaining : 1, 0, &engine->loop_budget, engine->cache_mask << 2, engine->final_cache.data()};
        digitalis_enter(&engine->state, block.code, &context);
        if (engine->profile) { engine->jit_calls += context.used; ++engine->enters; }
        return context.used;
    };
    for (uint32_t i = 0; i < budget && !engine->interrupted.exchange(false, std::memory_order_acq_rel); ++i) {
        const uint64_t pc = cpu.insn_addr;
        if (!engine->loop_budget) engine->loop_budget = 4096;
        // The arena is committed only as code is compiled.
        if (engine->cache_bytes >= cache_limit()) clear_cache(engine);
        auto found = engine->blocks.find(pc);
        if (found != engine->blocks.end() && found->second.syscall != UINT32_MAX) {
            cpu.insn_addr += 4;
            engine->result = {Stop::Svc, found->second.syscall};
            break;
        }
        if (found != engine->blocks.end() && found->second.code) {
            i += execute(pc, found->second, budget - i) - 1;
            if (engine->result.reason != Stop::Yield && !tagged_pointer_fault()) break;
            continue;
        }
        if (found != engine->blocks.end() && !engine->profile) {
            // A known untranslatable instruction (not an SVC, which returned
            // above): interpret it without fetching and decoding it again.
            ++engine->interpreted;
            safe_interpret(engine);
            if (engine->result.reason != Stop::Yield) break;
            continue;
        }
        uint32_t word{};
        if ((pc & 3) || !fetch(engine, pc, &word, sizeof word)) {
            engine->result = {Stop::MemoryFault, 0, pc}; break;
        }
        if ((word & 0xffe0001f) == 0xd4000001) {
            // SVC sites are code too. Re-fetching every invocation repeatedly
            // entered the Windows virtual-memory query and population locks.
            // They share native blocks' page index and invalidation lifetime.
            if (found == engine->blocks.end()) {
                engine->blocks.emplace(pc, Block{nullptr, 0, UINT32_MAX, (word >> 5) & 0xffff});
                engine->page_blocks[pc & ~uint64_t{4095}].push_back(pc);
            }
            cpu.insn_addr += 4;
            engine->result = {Stop::Svc, (word >> 5) & 0xffff}; break;
        }
        if (found == engine->blocks.end()) {
            const auto start = engine->profile ? Clock::now() : Clock::time_point{};
            found = engine->blocks.emplace(pc, compile(engine, pc)).first;
            engine->page_blocks[pc & ~uint64_t{4095}].push_back(pc);
            publish(engine, pc, found->second);
            if (engine->profile) engine->compile_ns += elapsed_ns(start);
        }
        if (found->second.code) {
            i += execute(pc, found->second, budget - i) - 1;
            if (tagged_pointer_fault()) continue;
        } else {
            ++engine->interpreted;
            const auto start = engine->profile ? Clock::now() : Clock::time_point{};
            safe_interpret(engine);
            if (engine->profile) { engine->interpret_ns += elapsed_ns(start); ++engine->fallback_words[word]; }
        }
        if (engine->result.reason != Stop::Yield) break;
    }
    std::memcpy(registers->x, cpu.x, sizeof cpu.x);
    std::memcpy(registers->v, cpu.v, sizeof cpu.v);
    registers->sp = cpu.sp;
    registers->pc = cpu.insn_addr;
    registers->tls = engine->state.tls;
    registers->pstate = (uint32_t(cpu.flags & 0xc000) << 16) | (uint32_t(cpu.flags & 0x100) << 21) | (uint32_t(cpu.flags & 1) << 28);
    registers->fpcr = cpu.cached_fpcr;
    // Preserve cumulative FP exceptions across syscall/yield boundaries before
    // restoring the Windows thread's MXCSR. Digitalis mirrors these lazily on MRS.
    const unsigned exceptions = _mm_getcsr() & 63;
    registers->fpsr = cpu.emulated_fpsr | (exceptions & 1) |
        ((exceptions & 2) << 6) | ((exceptions & 60) >> 1);
    if (engine->profile) {
        ++engine->runs;
        if (engine->result.reason == Stop::Svc) ++engine->svc_exits;
        engine->run_ns += elapsed_ns(run_started);
        if (Clock::now() >= engine->next_report) {
            std::fprintf(stderr, "Digitalis.Profile tid=%lu report=%llu jit=%llu interpreted=%llu compiled=%llu clears=%llu cache=%zu run_ms=%.1f compile_ms=%.1f interpret_ms=%.1f\n",
                GetCurrentThreadId(), ++engine->reports, engine->jit_calls, engine->interpreted, engine->compiled,
                engine->clears, engine->cache_bytes, engine->run_ns / 1e6, engine->compile_ns / 1e6, engine->interpret_ns / 1e6);
            std::fprintf(stderr, "Digitalis.Heavy tid=%lu attempts=%llu compiled=%llu runs=%llu enters=%llu svc=%llu\n",
                GetCurrentThreadId(), engine->heavy_attempts, engine->heavy_compiled, engine->runs, engine->enters, engine->svc_exits);
            std::vector<std::pair<uint64_t, uint32_t>> words;
            for (auto [word, count] : engine->fallback_words) words.push_back({count, word});
            std::sort(words.rbegin(), words.rend());
            for (size_t i = 0; i < std::min<size_t>(words.size(), 5); ++i)
                std::fprintf(stderr, "Digitalis.Fallback tid=%lu word=%08x count=%llu\n", GetCurrentThreadId(), words[i].second, words[i].first);
            engine->next_report = Clock::now() + std::chrono::seconds(5);
        }
    }
    return engine->result;
}
}

namespace berberis {
using namespace axrb::digitalis;
#ifdef AXRB_DIGITALIS_HEAVY
ConfigStr::ConfigStr(const char* env_name, const char*) : value_(std::getenv(env_name)) {}
bool IsConfigFlagSet(ConfigFlag) { return false; }
size_t GetThreadStateSimdRegOffset(int reg) { return offsetof(ThreadState, cpu.v) + size_t(reg) * 16; }
bool IsSimdOffset(size_t offset) {
    return offset >= offsetof(ThreadState, cpu.v) &&
        offset - offsetof(ThreadState, cpu.v) < sizeof(ThreadState::cpu.v);
}
#endif
GuestAddr GetGuestAddrRangeEnd() { return UINT64_MAX; }
bool MaskAddressTopByte(GuestAddr insn_addr) {
    return active->mask_addresses.load(std::memory_order_relaxed) || tagged_sites.contains(insn_addr);
}
extern "C" void berberis_entry_HandleLiteCounterThresholdReached() {}
void UndefinedInsn(GuestAddr) { active->result = {Stop::Undefined}; }
void BreakpointInsn(GuestAddr) { active->result = {Stop::Breakpoint}; }
void RunGuestSyscall(ThreadState*) { active->result = {Stop::Undefined}; }
void InvalidateGuestRange(GuestAddr start, GuestAddr end) {
    if (active->callbacks.invalidate) active->callbacks.invalidate(active->callbacks.opaque, start, end - start);
}
FaultyLoadResult FaultyLoad(const void* address, uint8_t bytes) {
    uint64_t value{};
    if (!active->callbacks.read(active->callbacks.opaque, reinterpret_cast<uint64_t>(address), &value, bytes)) {
        active->result = {Stop::MemoryFault, 0, reinterpret_cast<uint64_t>(address)};
        return {0, 1};
    }
    return {value, 0};
}
bool FaultyStore(void* address, uint8_t bytes, uint64_t value) {
    if (active->callbacks.write(active->callbacks.opaque, reinterpret_cast<uint64_t>(address), &value, bytes)) return false;
    active->result = {Stop::MemoryFault, 0, reinterpret_cast<uint64_t>(address)};
    return true;
}
void EmitExitGeneratedCode(x86_64::Assembler* as, x86_64::Assembler::Register target) {
    as->Movq({.base = as->rbp, .disp = int32_t(offsetof(ThreadState, cpu.insn_addr))}, target);
    as->Movq(as->rax, reinterpret_cast<uint64_t>(&digitalis_dispatch));
    as->Jmp(as->rax);
}
void EmitSyscall(x86_64::Assembler* as, GuestAddr pc) {
    uint32_t word{};
    if (!fetch(active, pc, &word, sizeof word))
        throw std::runtime_error("Digitalis syscall became unreadable during compilation");
    // A translated SVC already knows its immediate and return PC. Return it
    // directly instead of dispatching to the SVC address for a second lookup.
    if (((word >> 5) & 0xffff) == 0 && active->callbacks.syscall) {
        // Non-blocking syscalls complete in a direct call and continue at the
        // next instruction. The stack stays 16-byte aligned with 32 bytes of
        // shadow space, below the dispatch context at [rsp+8].
        auto* slow = as->MakeLabel();
        as->Movq(as->rcx, as->rbp);
        as->Movq(as->rax, reinterpret_cast<uint64_t>(&fast_syscall));
        as->Subq(as->rsp, int32_t{48});
        as->Call(as->rax);
        as->Addq(as->rsp, int32_t{48});
        as->Testb(as->rax, as->rax);
        as->Jcc(x86_64::Assembler::Condition::kZero, *slow);
        EmitDirectDispatch(as, pc + 4, true);
        as->Bind(slow);
    }
    as->Movq(as->rax, reinterpret_cast<uint64_t>(&active->result));
    as->Movl({.base = as->rax, .disp = int32_t(offsetof(Result, reason))}, uint32_t(Stop::Svc));
    as->Movl({.base = as->rax, .disp = int32_t(offsetof(Result, immediate))}, (word >> 5) & 0xffff);
    as->Movq(as->rax, pc + 4);
    EmitExitGeneratedCode(as, as->rax);
}
// A guest return is an indirect branch: a lookup in the table of final
// blocks, whose entry has usually left the cache by the time the callee
// returns. The call knows where the return will land, so it fetches that
// entry ahead. Which exit of a region is a call is read off the guest code:
// the returns of the calls that this exit can be, at most two. A wrong guess
// costs a prefetch.
int return_addresses(GuestAddr target, bool indirect, GuestAddr out[2]) {
    const GuestWindow& window = guest_window;
    int count = 0;
    for (uint64_t at = window.begin; at + 4 <= window.end && count != 2; at += 4) {
        uint32_t word;
        std::memcpy(&word, window.bytes + (at - window.begin), sizeof word);
        const bool call = indirect ? (word & 0xfffffc1f) == 0xd63f0000  // BLR
                                   : (word & 0xfc000000) == 0x94000000 && at + (int64_t(int32_t(word << 6)) >> 4) == target;  // BL
        if (call) out[count++] = at + 4;
        // An optimized region has several exits; a return or a jump through a register among them
        // would fetch for calls it is not.
        else if (indirect && !window.lite && (word & 0xff9ffc1f) == 0xd61f0000) return 0;
        // A first-tier region ends at its first branch that is not conditional.
        if (window.lite && ((word & 0x7c000000) == 0x14000000 || (word & 0xff9ffc1f) == 0xd61f0000)) break;
    }
    return count;
}
void emit_return_prefetch(x86_64::Assembler* as, GuestAddr target, bool indirect) {
    GuestAddr returns[2];
    const int count = return_addresses(target, indirect, returns);
    for (int i = 0; i != count; ++i) {
        as->Movq(as->rdx, reinterpret_cast<uint64_t>(&active->final_cache[(returns[i] >> 2) & active->cache_mask]));
        as->Emit8(0x0f);  // prefetcht0 [rdx]
        as->Emit8(0x18);
        as->Emit8(0x0a);
    }
}
void EmitDirectDispatch(x86_64::Assembler* as, GuestAddr pc, bool check_interrupt) {
    as->Movq(as->rax, pc);
    if (!active->chain) { EmitExitGeneratedCode(as, as->rax); return; }
    as->Movq({.base = as->rbp, .disp = int32_t(offsetof(ThreadState, cpu.insn_addr))}, as->rax);
    // Every cycle of chained regions contains a jump that checks for an
    // interrupt (upstream marks loop back edges), so halts stay prompt.
    if (check_interrupt) {
        as->Cmpb({.base = as->rbp, .disp = interrupted_offset()}, int8_t{0});
        as->Jcc32(x86_64::Assembler::Condition::kNotEqual, reinterpret_cast<uintptr_t>(active->arena.base));
    }
    emit_return_prefetch(as, pc, false);
    // To the dispatcher stub at the start of the arena; install() and
    // publish() point the jump at the target's code.
    as->Jmp32(reinterpret_cast<uintptr_t>(active->arena.base));
    emitted_sites.push_back({as->pc() - 4, pc});
}
void EmitIndirectDispatch(x86_64::Assembler* as, x86_64::Assembler::Register target) {
    if (!active->chain) { EmitExitGeneratedCode(as, target); return; }
    // Inline lookup in this engine's direct-mapped table of final blocks;
    // misses, blocks still counting toward optimization and interrupts take
    // the full dispatcher. RCX and RDX are free at region exits.
    if (target != as->rax) as->Movq(as->rax, target);
    as->Movq({.base = as->rbp, .disp = int32_t(offsetof(ThreadState, cpu.insn_addr))}, as->rax);
    as->Cmpb({.base = as->rbp, .disp = interrupted_offset()}, int8_t{0});
    as->Jcc32(x86_64::Assembler::Condition::kNotEqual, reinterpret_cast<uintptr_t>(active->arena.base));
    emit_return_prefetch(as, 0, true);
    // The index times sizeof(FinalEntry) is the address's index bits times four.
    as->Movl(as->rcx, as->rax);
    as->Andl(as->rcx, int32_t(active->cache_mask << 2));
    as->Movq(as->rdx, reinterpret_cast<uint64_t>(active->final_cache.data()));
    as->Cmpq(as->rax, {.base = as->rdx, .index = as->rcx, .scale = x86_64::Assembler::kTimesFour});
    as->Jcc32(x86_64::Assembler::Condition::kNotEqual, reinterpret_cast<uintptr_t>(active->arena.base));
    as->Jmpq({.base = as->rdx, .index = as->rcx, .scale = x86_64::Assembler::kTimesFour,
              .disp = int32_t(offsetof(FinalEntry, code))});
}
void EmitAllocStackFrame(x86_64::Assembler* as, uint32_t bytes) { as->Subq(as->rsp, int32_t(bytes)); }
void EmitFreeStackFrame(x86_64::Assembler* as, uint32_t bytes) { as->Addq(as->rsp, int32_t(bytes)); }
namespace host_platform {
const PlatformCapabilities kPlatformCapabilities = [] {
    PlatformCapabilities caps{};
    int leaf[4]{};
    __cpuid(leaf, 0);
    const unsigned max_basic = unsigned(leaf[0]);
    caps.kIsAuthenticAMD =
        (leaf[1] == 0x68747541 && leaf[3] == 0x69746e65 && leaf[2] == 0x444d4163) ||
        (leaf[1] == 0x6f677948 && leaf[3] == 0x6e65476e && leaf[2] == 0x656e6975);
    __cpuid(leaf, 1);
    unsigned family = (unsigned(leaf[0]) >> 8) & 15;
    if (family == 15) family += (unsigned(leaf[0]) >> 20) & 255;
    caps.kHasAES = (leaf[2] & (1 << 25)) != 0;
    caps.kHasCLMUL = (leaf[2] & 2) != 0;
    caps.kHasSSE3 = (leaf[2] & 1) != 0;
    caps.kHasSSSE3 = (leaf[2] & (1 << 9)) != 0;
    caps.kHasSSE4_1 = (leaf[2] & (1 << 19)) != 0;
    caps.kHasSSE4_2 = (leaf[2] & (1 << 20)) != 0;
    caps.kHasPOPCNT = (leaf[2] & (1 << 23)) != 0;
    // VEX instructions require both CPU support and Windows saving the AVX
    // register state. Upper YMM halves are volatile under the Windows ABI;
    // entry.asm preserves the nonvolatile lower XMM halves.
    const bool avx = (leaf[2] & (1 << 27)) && (leaf[2] & (1 << 28)) &&
        (GetEnabledXStateFeatures() & XSTATE_MASK_AVX) &&
        !GetEnvironmentVariableA("AXRB_DIGITALIS_DISABLE_AVX", nullptr, 0);
    caps.kHasAVX = avx;
    caps.kHasFMA = avx && (leaf[2] & (1 << 12));
    caps.kHasF16C = avx && (leaf[2] & (1 << 29));
    if (!GetEnvironmentVariableA("AXRB_DIGITALIS_DISABLE_EXTENDED_CPU", nullptr, 0)) {
        if (max_basic >= 7) {
            __cpuidex(leaf, 7, 0);
            caps.kHasAVX2 = avx && (leaf[1] & (1 << 5));
            caps.kHasBMI = (leaf[1] & (1 << 3)) != 0;
            caps.kHasBMI2 = (leaf[1] & (1 << 8)) != 0;
            // Match upstream's tuning: early Zen implements PDEP/PEXT slowly.
            caps.kHasPDEP = caps.kHasBMI2 && (!caps.kIsAuthenticAMD || family >= 0x19);
            caps.kHasSHA = (leaf[1] & (1 << 29)) != 0;
        }
        __cpuid(leaf, int(0x80000000u));
        if (unsigned(leaf[0]) >= 0x80000001u) {
            __cpuid(leaf, int(0x80000001u));
            caps.kHasLZCNT = (leaf[2] & (1 << 5)) != 0;
            caps.kHasSSE4a = (leaf[2] & (1 << 6)) != 0;
            caps.kHasFMA4 = avx && (leaf[2] & (1 << 16));
        }
    }
    return caps;
}();
}
}
