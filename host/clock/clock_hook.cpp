#include "register_filter.h"
#include "exit_stats.h"
#include "hv_apic.h"
#include <MinHook.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <atomic>
#include <array>

namespace {
using SetRegisters = decltype(&WHvSetVirtualProcessorRegisters);
SetRegisters original = nullptr;
using GetRegisters = decltype(&WHvGetVirtualProcessorRegisters);
GetRegisters getRegisters = nullptr;
std::array<std::atomic<bool>, 64> syncedSecondary{};
HRESULT WINAPI set_registers(WHV_PARTITION_HANDLE partition, UINT32 cpu,
    const WHV_REGISTER_NAME* names, UINT32 count, const WHV_REGISTER_VALUE* values) {
    WHV_REGISTER_NAME keptNames[8];
    WHV_REGISTER_VALUE keptValues[8];
    if (axrb::hv_apic::active.load(std::memory_order_relaxed) && count == 5 && axrb::hv_apic::cpuidLeaf != ~0ull) {
        std::memcpy(keptNames, names, 5 * sizeof(*names));
        std::memcpy(keptValues, values, 5 * sizeof(*values));
        axrb::hv_apic::patch_cpuid(keptNames, keptValues, 5);
        return original(partition, cpu, keptNames, 5, keptValues);
    }
    if (axrb::hv_apic::active.load(std::memory_order_relaxed) && count <= 8 && axrb::hv_apic::injects(names, count)) {
        const UINT32 kept = axrb::hv_apic::redirect_injection(partition, cpu, names, values, count, keptNames, keptValues);
        return kept ? original(partition, cpu, keptNames, kept, keptValues) : S_OK;
    }
    WHV_REGISTER_VALUE baseKept[70];
    if (axrb::hv_apic::x2apic && axrb::hv_apic::active.load(std::memory_order_relaxed) && count > 8 && count <= 70)
        values = axrb::hv_apic::keep_x2apic_base(partition, cpu, names, values, count, baseKept);
    WHV_REGISTER_NAME filteredNames[70];
    WHV_REGISTER_VALUE filteredValues[70];
    const UINT32 filtered = filter_clock_registers(names, values, count, filteredNames, filteredValues);
    if (filtered == count) return original(partition, cpu, names, count, values);
    HRESULT result = original(partition, cpu, filteredNames, filtered, filteredValues);
    if (FAILED(result) || !cpu || cpu >= syncedSecondary.size() ||
        syncedSecondary[cpu].load(std::memory_order_relaxed) || !getRegisters) return result;
    // This is the first full context write for a secondary vCPU. The legacy
    // emulator restores a stale TSC here, while WHPX gives each newly created
    // vCPU a different origin. Align it to vCPU 0 before Linux starts the CPU.
    WHV_REGISTER_NAME tsc = WHvX64RegisterTsc;
    WHV_REGISTER_VALUE primary{};
    HRESULT read = getRegisters(partition, 0, &tsc, 1, &primary);
    if (FAILED(read)) {
        std::fprintf(stderr, "AXRB clock: vCPU %u TSC sync read failed: %08lx\n", cpu, read);
        return result;
    }
    HRESULT write = original(partition, cpu, &tsc, 1, &primary);
    if (SUCCEEDED(write)) {
        syncedSecondary[cpu].store(true, std::memory_order_relaxed);
        std::fprintf(stderr, "AXRB clock: synchronized vCPU %u TSC to vCPU 0\n", cpu);
    } else {
        std::fprintf(stderr, "AXRB clock: vCPU %u TSC sync write failed: %08lx\n", cpu, write);
    }
    return result;
}
}

// Called by our launcher before QEMU's main thread runs, outside DllMain.
// Cold boot only: preserving the host counter is not snapshot time restoration.
extern "C" __declspec(dllexport) DWORD WINAPI AxrbInitializeClock(void*) {
    wchar_t path[32768];
    if (!GetModuleFileNameW(nullptr, path, 32768)) return 1;
    const wchar_t* name = std::wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"qemu-system-x86_64-headless.exe")) return 2;
    HMODULE module = LoadLibraryExW(L"WinHvPlatform.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return 3;
    void* target = reinterpret_cast<void*>(GetProcAddress(module, "WHvSetVirtualProcessorRegisters"));
    getRegisters = reinterpret_cast<GetRegisters>(GetProcAddress(module, "WHvGetVirtualProcessorRegisters"));
    if (!target || !getRegisters || MH_Initialize() != MH_OK) return 4;
    if (MH_CreateHook(target, reinterpret_cast<void*>(set_registers), reinterpret_cast<void**>(&original)) != MH_OK) return 5;
    if (!axrb::hv_apic::install(module)) return 8;
    const bool profiling = axrb::exit_stats::prepare(module) != nullptr;
    if (profiling || axrb::hv_apic::enabled) {
        // One hook on the run call: the hypervisor-APIC bridge outermost, so
        // the exit profile still sees every exit the hypervisor returns.
        void* run = reinterpret_cast<void*>(GetProcAddress(module, "WHvRunVirtualProcessor"));
        axrb::exit_stats::RunProcessor originalRun = nullptr;
        void* detour = axrb::hv_apic::enabled ? reinterpret_cast<void*>(axrb::hv_apic::run_processor)
                                              : reinterpret_cast<void*>(axrb::exit_stats::run_processor);
        if (!run || MH_CreateHook(run, detour, reinterpret_cast<void**>(&originalRun)) != MH_OK ||
            MH_EnableHook(run) != MH_OK) return 7;
        axrb::exit_stats::originalRun = originalRun;
        axrb::hv_apic::nextRun = profiling ? axrb::exit_stats::run_processor : originalRun;
        if (profiling) std::fprintf(stderr, "AXRB clock: WHPX exit profiling enabled\n");
    }
    if (MH_EnableHook(target) != MH_OK) return 6;
    axrb::hv_apic::rawSetRegisters = original;
    std::fprintf(stderr, "AXRB clock: synchronizing WHPX TSC in legacy 70-register updates; kernel stability checks remain enabled\n");
    return 0;
}
