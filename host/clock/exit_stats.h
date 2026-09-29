#pragma once
// Opt-in WHPX exit profile for the launched emulator (AXRB_WHPX_EXIT_STATS=file).
// Wraps WHvRunVirtualProcessor and charges each exit's userspace handling time
// (thread cycles from the return of one run call to the start of the next) to
// its exit reason; memory exits are also grouped by guest-physical page. A
// report is appended every ten seconds. Measurement only: arguments and
// results pass through unchanged.
#include <windows.h>
#include <WinHvPlatform.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace axrb::exit_stats {

using RunProcessor = decltype(&WHvRunVirtualProcessor);
inline RunProcessor originalRun = nullptr;
inline FILE* output = nullptr;

constexpr int kReasons = 24;
constexpr int kPages = 64;
struct Totals {
    std::atomic<uint64_t> count[kReasons]{};
    std::atomic<uint64_t> handleCycles[kReasons]{};
    std::atomic<uint64_t> guestCycles{0}, runs{0};
    std::atomic<uint64_t> pageKey[kPages]{};
    std::atomic<uint64_t> pageCount[kPages]{};
    std::atomic<uint64_t> pageCycles[kPages]{};
    std::atomic<uint64_t> haltBuckets[9]{}; // wall time from HLT exit to the next run
};
constexpr uint64_t kHaltBucketUs[8] = {10, 25, 50, 100, 200, 500, 1000, 5000};
inline Totals totals;

inline int reason_slot(WHV_RUN_VP_EXIT_REASON reason) {
    switch (reason) {
    case WHvRunVpExitReasonNone: return 0;
    case WHvRunVpExitReasonMemoryAccess: return 1;
    case WHvRunVpExitReasonX64IoPortAccess: return 2;
    case WHvRunVpExitReasonUnrecoverableException: return 3;
    case WHvRunVpExitReasonInvalidVpRegisterValue: return 4;
    case WHvRunVpExitReasonUnsupportedFeature: return 5;
    case WHvRunVpExitReasonX64InterruptWindow: return 6;
    case WHvRunVpExitReasonX64Halt: return 7;
    case WHvRunVpExitReasonX64ApicEoi: return 8;
    case WHvRunVpExitReasonX64MsrAccess: return 9;
    case WHvRunVpExitReasonX64Cpuid: return 10;
    case WHvRunVpExitReasonException: return 11;
    case WHvRunVpExitReasonX64Rdtsc: return 12;
    case WHvRunVpExitReasonCanceled: return 13;
    default: return 14;
    }
}
inline const char* slot_name(int slot) {
    static const char* names[] = {"none", "mmio", "port-io", "unrecoverable", "bad-register", "unsupported",
        "interrupt-window", "halt", "apic-eoi", "msr", "cpuid", "exception", "rdtsc", "canceled", "other"};
    return slot < 15 ? names[slot] : "?";
}

struct PerThread {
    uint64_t exitedAt = 0;   // thread cycles when the last run call returned
    uint64_t exitedWall = 0; // QPC ticks when the last run call returned
    int lastSlot = -1;
    uint64_t lastPage = 0;
};
inline thread_local PerThread tls;

inline uint64_t cycles() {
    ULONG64 value = 0;
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
}

inline void record_page(uint64_t page, uint64_t handle) {
    for (int i = 0; i < kPages; ++i) {
        uint64_t key = totals.pageKey[i].load(std::memory_order_relaxed);
        if (key == 0 && !totals.pageKey[i].compare_exchange_strong(key, page + 1)) {
            // Another thread claimed the slot; key now holds its page.
        } else if (key == 0) {
            key = page + 1;
        }
        if (key == page + 1) {
            totals.pageCount[i].fetch_add(1, std::memory_order_relaxed);
            totals.pageCycles[i].fetch_add(handle, std::memory_order_relaxed);
            return;
        }
    }
}

inline void report() {
    static std::mutex mutex;
    static uint64_t previousCount[kReasons]{}, previousHandle[kReasons]{}, previousGuest = 0;
    std::lock_guard lock(mutex);
    const uint64_t guest = totals.guestCycles.load();
    std::fprintf(output, "AXRB exits: guest_cycles=%llu", static_cast<unsigned long long>(guest - previousGuest));
    previousGuest = guest;
    for (int i = 0; i < 15; ++i) {
        const uint64_t c = totals.count[i].load(), h = totals.handleCycles[i].load();
        if (c != previousCount[i])
            std::fprintf(output, " %s=%llu/%llu", slot_name(i), static_cast<unsigned long long>(c - previousCount[i]),
                         static_cast<unsigned long long>(h - previousHandle[i]));
        previousCount[i] = c; previousHandle[i] = h;
    }
    static uint64_t previousHalt[9]{};
    std::fprintf(output, "\nAXRB halt wall-time buckets (<10,<25,<50,<100,<200,<500,<1000,<5000,>=5000 us):");
    for (int i = 0; i < 9; ++i) {
        const uint64_t v = totals.haltBuckets[i].load();
        std::fprintf(output, " %llu", static_cast<unsigned long long>(v - previousHalt[i]));
        previousHalt[i] = v;
    }
    std::fprintf(output, "\nAXRB exit pages (cumulative):");
    for (int i = 0; i < kPages; ++i) {
        const uint64_t key = totals.pageKey[i].load();
        if (key) std::fprintf(output, " %llx=%llu/%llu", static_cast<unsigned long long>((key - 1) << 12),
            static_cast<unsigned long long>(totals.pageCount[i].load()), static_cast<unsigned long long>(totals.pageCycles[i].load()));
    }
    std::fprintf(output, "\n");
    std::fflush(output);
}

inline uint64_t qpc() { LARGE_INTEGER v; QueryPerformanceCounter(&v); return static_cast<uint64_t>(v.QuadPart); }
inline uint64_t qpc_frequency() { static const uint64_t f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return static_cast<uint64_t>(v.QuadPart); }(); return f; }

inline HRESULT WINAPI run_processor(WHV_PARTITION_HANDLE partition, UINT32 cpu, VOID* context, UINT32 size) {
    const uint64_t entered = cycles();
    if (tls.lastSlot == 7) {
        const uint64_t us = (qpc() - tls.exitedWall) * 1000000 / qpc_frequency();
        int bucket = 0;
        while (bucket < 8 && us >= kHaltBucketUs[bucket]) ++bucket;
        totals.haltBuckets[bucket].fetch_add(1, std::memory_order_relaxed);
    }
    if (tls.lastSlot >= 0) {
        const uint64_t handle = entered - tls.exitedAt;
        totals.handleCycles[tls.lastSlot].fetch_add(handle, std::memory_order_relaxed);
        if (tls.lastSlot == 1) record_page(tls.lastPage, handle);
    }
    const HRESULT result = originalRun(partition, cpu, context, size);
    const uint64_t exited = cycles();
    totals.guestCycles.fetch_add(exited - entered, std::memory_order_relaxed);
    tls.exitedAt = exited;
    tls.exitedWall = qpc();
    if (SUCCEEDED(result) && size >= sizeof(WHV_RUN_VP_EXIT_CONTEXT)) {
        const auto* exit = static_cast<const WHV_RUN_VP_EXIT_CONTEXT*>(context);
        tls.lastSlot = reason_slot(exit->ExitReason);
        totals.count[tls.lastSlot].fetch_add(1, std::memory_order_relaxed);
        if (tls.lastSlot == 1) tls.lastPage = exit->MemoryAccess.Gpa >> 12;
    } else {
        tls.lastSlot = -1;
    }
    static std::atomic<uint64_t> nextReport{0};
    if ((totals.runs.fetch_add(1, std::memory_order_relaxed) & 4095) == 0) {
        const uint64_t now = GetTickCount64();
        uint64_t due = nextReport.load();
        if (now >= due && nextReport.compare_exchange_strong(due, now + 10000)) report();
    }
    return result;
}

// Returns the address to hook, or nullptr when profiling is not requested.
inline void* prepare(HMODULE platform) {
    char path[1024];
    const DWORD length = GetEnvironmentVariableA("AXRB_WHPX_EXIT_STATS", path, sizeof(path));
    if (!length || length >= sizeof(path)) return nullptr;
    output = std::fopen(path, "a");
    if (!output) return nullptr;
    std::fprintf(output, "AXRB exits: profiling started; values are count/userspace-cycles per 10 s\n");
    return reinterpret_cast<void*>(GetProcAddress(platform, "WHvRunVirtualProcessor"));
}

} // namespace axrb::exit_stats
