// AXRB_DIRECT_PROFILE=<file>: a sampling profiler over every thread of the
// process. Each millisecond, threads that consumed CPU since the previous
// sample are suspended briefly and their instruction pointer attributed to a
// module and symbol (translated guest code shows as [jit]). A sample weighs
// the CPU time its thread used since that thread's previous sample, so the
// figures are milliseconds of CPU. A guest thread found blocked in a system
// call ran only briefly in between: its time goes to "[burst before <call>]".
// AXRB_DIRECT_PROFILE_JIT=1 (Digitalis) also writes <report>.jit, every sample
// in translated code with its region, and <report>.code, those regions' host
// code, for an instruction-level look with a disassembler.
// The report is
// written after AXRB_DIRECT_PROFILE_SECONDS (default 60) seconds of sampling,
// starting AXRB_DIRECT_PROFILE_START seconds (default 0) after launch. Host
// samples are also attributed to call stacks. AXRB_DIRECT_PROFILE_TIMELINE
// (thread name prefixes, comma separated) writes every sample of those threads
// in order, running or waiting, to <report>.timeline for finding stalls.

#include "profiler.h"

#include "cpu.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"

#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <psapi.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace axrb {
namespace {

struct ThreadState {
    HANDLE handle = nullptr;
    ULONG64 cycles = 0;
    std::string label;
};

// Thread cycle counts are processor timestamp cycles.
double cycles_per_microsecond() {
    LARGE_INTEGER frequency, a, b;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&a);
    const uint64_t first = __rdtsc();
    Sleep(50);
    QueryPerformanceCounter(&b);
    const uint64_t second = __rdtsc();
    return static_cast<double>(second - first) * frequency.QuadPart / (static_cast<double>(b.QuadPart - a.QuadPart) * 1e6);
}

std::string module_of(uint64_t address, uint64_t* base) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module))
        return {};
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring w = path;
    w = w.substr(w.find_last_of(L'\\') + 1);
    *base = reinterpret_cast<uint64_t>(module);
    return std::string(w.begin(), w.end());
}

std::string symbol_of(uint64_t address) {
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512]{};
    auto* info = reinterpret_cast<SYMBOL_INFO*>(buffer);
    info->SizeOfStruct = sizeof(SYMBOL_INFO);
    info->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, info)) return info->Name;
    return {};
}

std::string label_for(DWORD tid) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    for (const auto& [guest_tid, thread] : process().threads)
        if (thread->host_tid == tid) return thread->name + "#" + std::to_string(guest_tid);
    return "host#" + std::to_string(tid);
}

// The top of a suspended thread's stack. Unwinding runs on this copy after
// the thread resumes: the function table lookup may need a loader lock the
// suspended thread holds. Nothing here allocates, as the suspended thread may
// hold the heap lock too.
struct StackCopy {
    static constexpr uint64_t kBytes = 32 << 10;
    uint64_t at = 0;      // the thread's stack pointer
    uint64_t copied = 0;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(kBytes + 4096);  // zero slack past the copied top
};
bool copy_stack(const CONTEXT& context, StackCopy& copy) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(context.Rsp), &info, sizeof info) || info.State != MEM_COMMIT) return false;
    const uint64_t end = std::min<uint64_t>(reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize,
                                            context.Rsp + StackCopy::kBytes);
    copy.at = context.Rsp;
    copy.copied = end - context.Rsp;
    std::memcpy(copy.bytes.data(), reinterpret_cast<const void*>(context.Rsp), copy.copied);
    std::memset(copy.bytes.data() + copy.copied, 0, copy.bytes.size() - copy.copied);
    return true;
}

// Host call stack through modules with unwind data. Stops at generated code,
// which has none, and at the end of the copied stack.
using HostStack = std::array<uint64_t, 6>;
HostStack unwind_host(CONTEXT context, const StackCopy& copy, const std::vector<std::pair<uint64_t, uint64_t>>& modules) {
    auto in_module = [&](uint64_t ip) {
        for (auto [lo, hi] : modules) if (ip >= lo && ip < hi) return true;
        return false;
    };
    const uint64_t copied = copy.copied;
    const uint64_t base = reinterpret_cast<uint64_t>(copy.bytes.data());
    // Registers that point into the stack (frame pointers) point into the copy.
    auto rebase = [&] {
        for (DWORD64* r = &context.Rax; r <= &context.R15; ++r)
            if (*r >= copy.at && *r < copy.at + copied) *r = *r - copy.at + base;
    };
    HostStack stack{};
    for (size_t depth = 0; depth < stack.size() && in_module(context.Rip); ++depth) {
        stack[depth] = context.Rip;
        rebase();
        if (context.Rsp < base || context.Rsp + 512 > base + copied) break;
        DWORD64 image = 0;
        if (auto* entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr)) {
            void* handler = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler, &frame, nullptr);
        } else {
            // A leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
        }
    }
    return stack;
}

void run(std::string path, double start, double seconds) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(start * 1000)));
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    const double per_microsecond = cycles_per_microsecond();
    timeBeginPeriod(1);
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    std::unordered_map<DWORD, ThreadState> threads;
    std::unordered_map<uint64_t, uint64_t> addresses;               // ip -> samples
    std::map<std::string, std::unordered_map<uint64_t, uint64_t>> thread_addresses;  // label -> ip -> samples
    std::map<std::string, std::map<std::string, uint64_t>> by_thread;  // label -> bucket -> samples
    std::map<std::string, uint64_t> thread_totals;
    uint64_t total = 0;
    std::map<std::string, uint64_t> bursts;
    uint64_t syscalls_before[512];
    for (int i = 0; i < 512; ++i) syscalls_before[i] = g_syscall_counts[i].load();
    g_syscall_stacks_enabled = true;
    const auto begin = std::chrono::steady_clock::now();
    // AXRB_DIRECT_PROFILE_TIMELINE=RenderThread,GameThread: every sample of
    // threads with these name prefixes, in order, for finding stalls.
    std::vector<std::string> timeline_names;
    if (const char* names = std::getenv("AXRB_DIRECT_PROFILE_TIMELINE")) {
        for (std::string rest = names; !rest.empty();) {
            const size_t comma = rest.find(',');
            if (comma) timeline_names.push_back(rest.substr(0, comma));
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
        }
    }
    struct Moment {
        uint32_t ms;
        std::string label;
        HostStack stack;
        uint64_t guest_pc;
        const char* why;
    };
    std::vector<Moment> timeline;
    StackCopy copy;
    auto on_timeline = [&](const std::string& label) {
        for (const auto& name : timeline_names) if (label.rfind(name, 0) == 0) return true;
        return false;
    };
    auto now_ms = [&] {
        return static_cast<uint32_t>(start * 1000 +
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    };
    auto refreshed = begin - std::chrono::seconds(10);
    std::unordered_map<uint64_t, std::pair<std::string, uint64_t>> module_cache;  // page -> (module, base)
    struct Sample {
        std::string label;
        uint64_t ip, weight;  // microseconds of CPU
        const char* burst;    // the system call the thread was found blocked in
        int tier;             // translated code: 1 first tier, 2 optimized
    };
    std::vector<Sample> pending;
    std::vector<std::pair<uint64_t, uint64_t>> modules;     // host images, for unwinding
    std::map<HostStack, uint64_t> host_stacks;
    std::unordered_map<GuestThread*, std::unordered_map<uint64_t, uint64_t>> jit_cache;  // host ip -> guest block
    std::unordered_map<uint64_t, uint64_t> guest_pcs;                                    // guest block -> samples
    std::unordered_map<GuestThread*, std::unordered_map<uint64_t, int>> tier_cache;      // host ip -> tier
    std::map<std::string, std::unordered_map<uint64_t, uint64_t>> thread_guest_pcs;      // label -> guest block -> samples
    const bool record_jit = std::getenv("AXRB_DIRECT_PROFILE_JIT") != nullptr;
    struct JitSample { std::string label; uint64_t ip, region, guest_pc, weight; int tier; };
    std::vector<JitSample> jit_samples;
    std::map<uint64_t, uint32_t> jit_regions;  // host start -> bytes
    // Wall time of the main guest thread: what it waits for when it does not run.
    DWORD main_tid = 0;
    uint64_t main_running = 0;
    std::map<std::pair<std::string, std::vector<uint64_t>>, uint64_t> main_blocked;  // (why, guest stack)
    std::unordered_map<uint64_t, uint64_t> main_host_waits;                          // host ip -> samples
    while (std::chrono::steady_clock::now() - begin < std::chrono::duration<double>(seconds)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - refreshed > std::chrono::seconds(1)) {
            refreshed = now;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 entry{sizeof entry};
            std::unordered_map<DWORD, bool> alive;
            for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
                if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
                alive[entry.th32ThreadID] = true;
                auto& state = threads[entry.th32ThreadID];
                if (!state.handle) {
                    state.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                              entry.th32ThreadID);
                    if (state.handle) QueryThreadCycleTime(state.handle, &state.cycles);
                }
                state.label = label_for(entry.th32ThreadID);
            }
            CloseHandle(snap);
            HMODULE loaded[1024];
            DWORD needed = 0;
            if (EnumProcessModules(GetCurrentProcess(), loaded, sizeof loaded, &needed)) {
                modules.clear();
                for (DWORD i = 0; i < std::min<DWORD>(needed / sizeof(HMODULE), 1024); ++i) {
                    MODULEINFO info{};
                    if (GetModuleInformation(GetCurrentProcess(), loaded[i], &info, sizeof info))
                        modules.emplace_back(reinterpret_cast<uint64_t>(info.lpBaseOfDll),
                                             reinterpret_cast<uint64_t>(info.lpBaseOfDll) + info.SizeOfImage);
                }
            }
            {
                std::lock_guard<std::mutex> held(process().threads_lock);
                // The busiest guest thread so far (the engine's main loop).
                uint64_t most = 0;
                for (const auto& [guest_tid, thread] : process().threads) {
                    auto n = thread_totals.find(thread->name + "#" + std::to_string(guest_tid));
                    if (n != thread_totals.end() && n->second > most) {
                        most = n->second;
                        main_tid = thread->host_tid;
                    }
                }
            }
            for (auto it = threads.begin(); it != threads.end();) {
                if (!alive.count(it->first)) {
                    if (it->second.handle) CloseHandle(it->second.handle);
                    it = threads.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& [tid, state] : threads) {
            if (!state.handle) continue;
            ULONG64 cycles = 0;
            QueryThreadCycleTime(state.handle, &cycles);
            const bool ran = cycles - state.cycles > 200000;  // ~0.05 ms of CPU
            const uint64_t weight = static_cast<uint64_t>((cycles - state.cycles) / per_microsecond);
            if (ran) state.cycles = cycles;  // shorter runs count toward the next sample
            if (!ran && on_timeline(state.label)) {
                const char* why = nullptr;
                uint64_t lr = 0;
                {
                    std::lock_guard<std::mutex> held(process().threads_lock);
                    for (const auto& [guest_tid, thread] : process().threads)
                        if (thread->host_tid == tid && (why = thread->blocked_in.load())) lr = get_reg(*thread, 30);
                }
                HostStack stack{};
                if (!why && SuspendThread(state.handle) != static_cast<DWORD>(-1)) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    const bool copied = GetThreadContext(state.handle, &context) && copy_stack(context, copy);
                    ResumeThread(state.handle);
                    if (copied) stack = unwind_host(context, copy, modules);
                }
                timeline.push_back({now_ms(), state.label, stack, lr, why ? why : "host wait"});
            }
            if (!ran) {
                if (tid != main_tid) continue;
                std::lock_guard<std::mutex> held(process().threads_lock);
                GuestThread* main = nullptr;
                for (const auto& [guest_tid, thread] : process().threads)
                    if (thread->host_tid == tid) main = thread;
                if (!main) continue;
                if (const char* why = main->blocked_in.load()) {
                    std::vector<uint64_t> frames{get_reg(*main, 30)};
                    uint64_t fp = get_reg(*main, 29);
                    for (int depth = 0; depth < 6 && fp && (fp & 7) == 0; ++depth) {
                        uint64_t record[2];
                        if (!guest_read(fp, record, sizeof record)) break;
                        frames.push_back(record[1]);
                        if (record[0] <= fp) break;
                        fp = record[0];
                    }
                    ++main_blocked[{why, frames}];
                } else if (SuspendThread(state.handle) != static_cast<DWORD>(-1)) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL;
                    const bool got = GetThreadContext(state.handle, &context);
                    ResumeThread(state.handle);
                    if (got) ++main_host_waits[context.Rip];
                }
                continue;
            }
            if (tid == main_tid) ++main_running;
            // The registry lock is held across the suspension so the guest
            // thread (and its JIT) cannot go away; a thread waiting for the
            // lock is not running and holds nothing the lookup needs.
            std::lock_guard<std::mutex> held(process().threads_lock);
            GuestThread* guest = nullptr;
            for (const auto& [guest_tid, thread] : process().threads)
                if (thread->host_tid == tid) guest = thread;
            const char* burst = guest ? guest->blocked_in.load() : nullptr;
            if (SuspendThread(state.handle) == static_cast<DWORD>(-1)) continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            const bool got = GetThreadContext(state.handle, &context);
            const bool copied = got && copy_stack(context, copy);
            uint64_t guest_pc = ~uint64_t{0};
            bool looked_up = false;
            if (got && guest) {
                bool cached = false;
                if (auto cache = jit_cache.find(guest); cache != jit_cache.end()) {
                    if (auto hit = cache->second.find(context.Rip); hit != cache->second.end()) {
                        guest_pc = hit->second;
                        cached = true;
                    }
                }
                if (!cached) {
                    guest_pc = guest_pc_for_host_code(*guest, context.Rip);
                    looked_up = true;
                }
            }
            ResumeThread(state.handle);
            if (!got) continue;
            int tier = 0;
            if (looked_up && guest_pc == ~uint64_t{0}) {
                bool optimized = false;
                guest_pc = guest_pc_for_host_code_resumed(*guest, context.Rip, &optimized);
                if (guest_pc != ~uint64_t{0}) tier = optimized ? 2 : 1;
                if (tier && tier_cache[guest].size() < 200000) tier_cache[guest].emplace(context.Rip, tier);
            } else if (guest) {
                if (auto cache = tier_cache.find(guest); cache != tier_cache.end())
                    if (auto hit = cache->second.find(context.Rip); hit != cache->second.end()) tier = hit->second;
            }
            // Cached only now: inserting allocates, and the thread may have held the heap lock.
            if (looked_up && jit_cache[guest].size() < 200000) jit_cache[guest].emplace(context.Rip, guest_pc);
            HostStack stack{};
            if (copied) stack = unwind_host(context, copy, modules);
            pending.push_back({state.label, context.Rip, weight, burst, tier});
            if (stack[0] && !burst) host_stacks[stack] += weight;
            if (on_timeline(state.label)) timeline.push_back({now_ms(), state.label, stack, guest_pc, nullptr});
            if (record_jit && guest && guest_pc != ~uint64_t{0} && !burst) {
                bool optimized = false;
                uint64_t region = 0;
                uint32_t bytes = 0;
                if (guest_pc_for_host_code_resumed(*guest, context.Rip, &optimized, &region, &bytes) != ~uint64_t{0}) {
                    jit_samples.push_back({state.label, context.Rip, region, guest_pc, weight, optimized ? 2 : 1});
                    jit_regions[region] = bytes;
                }
            }
            if (guest_pc != ~uint64_t{0} && !burst) {
                guest_pcs[guest_pc] += weight;
                thread_guest_pcs[state.label][guest_pc] += weight;
            }
        }
        // Attribution happens outside the suspension window.
        for (const auto& [label, ip, weight, burst, tier] : pending) {
            const uint64_t page = ip >> 12;
            auto cached = module_cache.find(page);
            if (cached == module_cache.end()) {
                uint64_t base = 0;
                std::string module = module_of(ip, &base);
                cached = module_cache.emplace(page, std::make_pair(module, base)).first;
            }
            std::string bucket = !cached->second.first.empty() ? cached->second.first :
                tier == 2 ? "[jit optimized]" : tier == 1 ? "[jit first tier]" : "[jit]";
            if (burst) {
                bucket = std::string("[burst before ") + burst + "]";
                bursts[bucket] += weight;
            } else if (!cached->second.first.empty()) {
                addresses[ip] += weight;
                thread_addresses[label][ip] += weight;
            }
            // Symbols are resolved once per address at report time; buckets here are modules.
            by_thread[label][bucket] += weight;
            thread_totals[label] += weight;
            total += weight;
        }
        pending.clear();
        Sleep(1);
    }
    timeEndPeriod(1);
    g_syscall_stacks_enabled = false;
    std::vector<std::pair<uint64_t, int>> syscalls;
    for (int i = 0; i < 512; ++i) {
        const uint64_t n = g_syscall_counts[i].load() - syscalls_before[i];
        if (n) syscalls.emplace_back(n, i);
    }
    std::sort(syscalls.rbegin(), syscalls.rend());
    // Per-address symbols, folded into module!function.
    std::map<std::string, uint64_t> functions;
    for (const auto& [ip, n] : addresses) {
        uint64_t base = 0;
        std::string module = module_of(ip, &base);
        std::string symbol = symbol_of(ip);
        functions[module + "!" + (symbol.empty() ? "?" : symbol)] += n;
    }
    // Guest blocks folded into library+offset (the library's file offset, as
    // llvm-symbolizer takes it for a .so mapped from offset 0).
    for (auto& [pc, n] : guest_pcs) n /= 1000;
    std::vector<Vma> code;
    address_space().for_each([&](const Vma& vma) {
        if ((vma.prot & 4) && !vma.name.empty()) code.push_back(vma);  // PROT_EXEC
    });
    std::map<std::string, uint64_t> guest_libraries;
    std::map<std::string, uint64_t> guest_blocks;
    uint64_t guest_total = 0;
    for (const auto& [pc, n] : guest_pcs) {
        std::string where = "?";
        for (const auto& vma : code) {
            if (pc >= vma.start && pc < vma.end) {
                char text[64];
                std::snprintf(text, sizeof text, "+0x%llx", static_cast<unsigned long long>(pc - vma.start + vma.offset));
                std::string name = vma.name.substr(vma.name.find_last_of('/') + 1);
                guest_libraries[name] += n;
                where = name + text;
                break;
            }
        }
        if (where == "?") guest_libraries["?"] += n;
        guest_blocks[where] += n;
        guest_total += n;
    }
    if (record_jit) {
        // Regions stay in place until their thread's cache is reset; a failed read skips one.
        std::map<uint64_t, uint64_t> offsets;
        if (FILE* code_file = std::fopen((path + ".code").c_str(), "wb")) {
            std::vector<uint8_t> bytes;
            uint64_t offset = 0;
            for (const auto& [start, size] : jit_regions) {
                bytes.resize(size);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(start), bytes.data(), size, &got) || got != size) continue;
                std::fwrite(bytes.data(), 1, size, code_file);
                offsets[start] = offset;
                offset += size;
            }
            std::fclose(code_file);
        }
        if (FILE* jit = std::fopen((path + ".jit").c_str(), "w")) {
            for (const auto& [start, offset] : offsets)
                std::fprintf(jit, "R %llx %u %llu\n", static_cast<unsigned long long>(start), jit_regions[start],
                             static_cast<unsigned long long>(offset));
            for (const auto& vma : code)
                std::fprintf(jit, "M %llx %llx %llx %s\n", static_cast<unsigned long long>(vma.start),
                             static_cast<unsigned long long>(vma.end), static_cast<unsigned long long>(vma.offset),
                             vma.name.substr(vma.name.find_last_of('/') + 1).c_str());
            // The last field is the guest instruction before the region: a call there makes it a return's landing.
            for (const auto& s : jit_samples) {
                uint32_t before = 0;
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(s.guest_pc - 4), &before, sizeof before, &got);
                std::fprintf(jit, "S %s|%llx|%llx|%llx|%d|%llu|%x\n", s.label.c_str(), static_cast<unsigned long long>(s.ip),
                             static_cast<unsigned long long>(s.region), static_cast<unsigned long long>(s.guest_pc), s.tier,
                             static_cast<unsigned long long>(s.weight), before);
            }
            std::fclose(jit);
        }
    }
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%.0f ms of CPU over %.0f s: %.2f cores (the figures below are ms of CPU)\n\n",
                 total / 1e3, seconds, total / 1e6 / seconds);
    // Weights were collected in microseconds.
    for (auto& [bucket, n] : bursts)
        std::fprintf(f, "%9.0f %5.1f%%  %s\n", n / 1e3, 100.0 * n / std::max<uint64_t>(total, 1), bucket.c_str());
    total /= 1000;
    for (auto& [ip, n] : addresses) n /= 1000;
    for (auto& [label, ips] : thread_addresses) for (auto& [ip, n] : ips) n /= 1000;
    for (auto& [label, buckets] : by_thread) for (auto& [bucket, n] : buckets) n /= 1000;
    for (auto& [label, n] : thread_totals) n /= 1000;
    for (auto& [stack, n] : host_stacks) n /= 1000;
    for (auto& [label, pcs] : thread_guest_pcs) for (auto& [pc, n] : pcs) n /= 1000;
    std::fprintf(f, "\n");
    std::vector<std::pair<std::string, uint64_t>> ordered(thread_totals.begin(), thread_totals.end());
    std::sort(ordered.begin(), ordered.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::fprintf(f, "== syscalls (per second)\n");
    for (size_t i = 0; i < syscalls.size() && i < 25; ++i)
        std::fprintf(f, "%10.0f  %s (%d)\n", syscalls[i].first / seconds, syscall_name(syscalls[i].second), syscalls[i].second);
    auto where_is = [&](uint64_t pc) {
        for (const auto& vma : code) {
            if (pc >= vma.start && pc < vma.end) {
                char text[64];
                std::snprintf(text, sizeof text, "+0x%llx", static_cast<unsigned long long>(pc - vma.start + vma.offset));
                return vma.name.substr(vma.name.find_last_of('/') + 1) + text;
            }
        }
        char text[32];
        std::snprintf(text, sizeof text, "0x%llx", static_cast<unsigned long long>(pc));
        return std::string(text);
    };
    {
        std::vector<std::pair<uint64_t, std::pair<uint64_t, std::vector<uint64_t>>>> stacks;
        {
            std::lock_guard<std::mutex> held(g_syscall_stacks_lock);
            for (const auto& [key, n] : g_syscall_stacks) stacks.emplace_back(n, key);
        }
        std::sort(stacks.begin(), stacks.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::fprintf(f, "\n== syscall stacks (1 sample per 4096 calls)\n");
        for (size_t i = 0; i < stacks.size() && i < 15; ++i) {
            std::fprintf(f, "%7llu  %s:", static_cast<unsigned long long>(stacks[i].first), syscall_name(stacks[i].second.first));
            for (uint64_t pc : stacks[i].second.second) std::fprintf(f, " %s", where_is(pc).c_str());
            std::fprintf(f, "\n");
        }
    }
    std::fprintf(f, "\n== threads\n");
    for (const auto& [label, n] : ordered) {
        std::fprintf(f, "%7llu %5.1f%%  %s:", static_cast<unsigned long long>(n), 100.0 * n / std::max<uint64_t>(total, 1),
                     label.c_str());
        std::vector<std::pair<std::string, uint64_t>> buckets(by_thread[label].begin(), by_thread[label].end());
        std::sort(buckets.begin(), buckets.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < buckets.size() && i < 5; ++i)
            std::fprintf(f, "  %s %.0f%%", buckets[i].first.c_str(), 100.0 * buckets[i].second / n);
        std::fprintf(f, "\n");
    }
    std::fprintf(f, "\n== host functions of the busiest threads\n");
    for (size_t t = 0; t < ordered.size() && t < 4; ++t) {
        std::map<std::string, uint64_t> mine;
        for (const auto& [ip, n] : thread_addresses[ordered[t].first]) {
            uint64_t base = 0;
            const std::string symbol = symbol_of(ip);
            mine[module_of(ip, &base) + "!" + (symbol.empty() ? "?" : symbol)] += n;
        }
        std::vector<std::pair<std::string, uint64_t>> v(mine.begin(), mine.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::fprintf(f, "  %s\n", ordered[t].first.c_str());
        for (size_t i = 0; i < v.size() && i < 15; ++i)
            std::fprintf(f, "%9llu %5.1f%%  %s\n", static_cast<unsigned long long>(v[i].second),
                         100.0 * v[i].second / std::max<uint64_t>(ordered[t].second, 1), v[i].first.c_str());
    }
    std::fprintf(f, "\n== guest blocks of the busiest threads\n");
    for (size_t t = 0; t < ordered.size() && t < 8; ++t) {
        std::vector<std::pair<uint64_t, uint64_t>> v;
        for (const auto& [pc, n] : thread_guest_pcs[ordered[t].first]) v.emplace_back(n, pc);
        std::sort(v.rbegin(), v.rend());
        std::fprintf(f, "  %s\n", ordered[t].first.c_str());
        for (size_t i = 0; i < v.size() && i < 12; ++i)
            std::fprintf(f, "%9llu %5.1f%%  %s\n", static_cast<unsigned long long>(v[i].first),
                         100.0 * v[i].first / std::max<uint64_t>(ordered[t].second, 1), where_is(v[i].second).c_str());
    }
    {
        uint64_t blocked = 0, waiting = 0;
        for (const auto& [key, n] : main_blocked) blocked += n;
        for (const auto& [ip, n] : main_host_waits) waiting += n;
        std::fprintf(f, "\n== busiest thread wall time: running %llu, blocked in syscalls %llu, waiting in host code %llu\n",
                     static_cast<unsigned long long>(main_running), static_cast<unsigned long long>(blocked),
                     static_cast<unsigned long long>(waiting));
        std::vector<std::pair<uint64_t, std::pair<std::string, std::vector<uint64_t>>>> v;
        for (const auto& [key, n] : main_blocked) v.emplace_back(n, key);
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < v.size() && i < 25; ++i) {
            std::fprintf(f, "%7llu  %s:", static_cast<unsigned long long>(v[i].first), v[i].second.first.c_str());
            for (uint64_t pc : v[i].second.second) std::fprintf(f, " %s", where_is(pc).c_str());
            std::fprintf(f, "\n");
        }
        std::map<std::string, uint64_t> waits;
        for (const auto& [ip, n] : main_host_waits) {
            uint64_t base = 0;
            const std::string symbol = symbol_of(ip);
            waits[module_of(ip, &base) + "!" + (symbol.empty() ? "?" : symbol)] += n;
        }
        std::vector<std::pair<std::string, uint64_t>> w(waits.begin(), waits.end());
        std::sort(w.begin(), w.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < w.size() && i < 15; ++i)
            std::fprintf(f, "%7llu  host wait %s\n", static_cast<unsigned long long>(w[i].second), w[i].first.c_str());
    }
    {
        // Folded by symbol, so different offsets within a function count together.
        std::map<std::string, uint64_t> folded;
        std::unordered_map<uint64_t, std::string> names;
        for (const auto& [stack, n] : host_stacks) {
            std::string text;
            for (uint64_t ip : stack) {
                if (!ip) break;
                auto it = names.find(ip);
                if (it == names.end()) {
                    uint64_t base = 0;
                    const std::string symbol = symbol_of(ip);
                    it = names.emplace(ip, module_of(ip, &base) + "!" + (symbol.empty() ? "?" : symbol)).first;
                }
                text += (text.empty() ? "" : " < ") + it->second;
            }
            folded[text] += n;
        }
        std::vector<std::pair<std::string, uint64_t>> v(folded.begin(), folded.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::fprintf(f, "\n== host stacks (non-JIT samples, callers to the right)\n");
        for (size_t i = 0; i < v.size() && i < 60; ++i)
            std::fprintf(f, "%7llu  %s\n", static_cast<unsigned long long>(v[i].second), v[i].first.c_str());
    }
    std::vector<std::pair<std::string, uint64_t>> top(functions.begin(), functions.end());
    std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::fprintf(f, "\n== host functions (non-JIT samples)\n");
    for (size_t i = 0; i < top.size() && i < 80; ++i)
        std::fprintf(f, "%7llu %5.1f%%  %s\n", static_cast<unsigned long long>(top[i].second),
                     100.0 * top[i].second / std::max<uint64_t>(total, 1), top[i].first.c_str());
    auto sorted = [](const std::map<std::string, uint64_t>& m) {
        std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        return v;
    };
    std::fprintf(f, "\n== guest libraries (JIT samples: %llu)\n", static_cast<unsigned long long>(guest_total));
    for (const auto& [name, n] : sorted(guest_libraries))
        std::fprintf(f, "%7llu %5.1f%%  %s\n", static_cast<unsigned long long>(n), 100.0 * n / std::max<uint64_t>(guest_total, 1),
                     name.c_str());
    std::fprintf(f, "\n== guest blocks\n");
    auto blocks = sorted(guest_blocks);
    for (size_t i = 0; i < blocks.size() && i < 400; ++i)
        std::fprintf(f, "%7llu %5.1f%%  %s\n", static_cast<unsigned long long>(blocks[i].second),
                     100.0 * blocks[i].second / std::max<uint64_t>(guest_total, 1), blocks[i].first.c_str());
    std::fclose(f);
    if (!timeline.empty()) {
        std::unordered_map<uint64_t, std::string> names;
        auto host_name = [&](uint64_t ip) -> const std::string& {
            auto it = names.find(ip);
            if (it == names.end()) {
                uint64_t base = 0;
                const std::string symbol = symbol_of(ip);
                it = names.emplace(ip, module_of(ip, &base) + "!" + (symbol.empty() ? "?" : symbol)).first;
            }
            return it->second;
        };
        if (FILE* t = std::fopen((path + ".timeline").c_str(), "w")) {
            for (const auto& m : timeline) {
                std::fprintf(t, "%7u %-22s %s", m.ms, m.label.c_str(), m.why ? m.why : "run");
                if (m.guest_pc != ~uint64_t{0} && m.guest_pc) std::fprintf(t, " guest=%s", where_is(m.guest_pc).c_str());
                for (uint64_t ip : m.stack) {
                    if (!ip) break;
                    std::fprintf(t, " < %s", host_name(ip).c_str());
                }
                std::fprintf(t, "\n");
            }
            std::fclose(t);
        }
    }
    LOGI("profile", "report written to %s", path.c_str());
}

}  // namespace

void profiler_start() {
    const char* path = std::getenv("AXRB_DIRECT_PROFILE");
    if (!path || !*path) return;
    const char* start = std::getenv("AXRB_DIRECT_PROFILE_START");
    const char* seconds = std::getenv("AXRB_DIRECT_PROFILE_SECONDS");
    std::thread(run, std::string(path), start ? std::atof(start) : 0.0, seconds ? std::atof(seconds) : 60.0).detach();
}

}  // namespace axrb
