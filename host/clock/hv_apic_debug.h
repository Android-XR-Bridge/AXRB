#pragma once
// Stall diagnostics for hv_apic.h (development only, AXRB_WHPX_HV_APIC_DEBUG=1):
// when no device-interrupt EOI happens for two seconds, every vCPU is kicked
// and dumps the hypervisor APIC and QEMU APIC state from its own thread.
#include <windows.h>
#include <WinHvPlatform.h>
#include <WinHvEmulation.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace axrb::hv_apic_debug {

inline bool enabled = false;
inline std::atomic<uint64_t> progress{0};
inline std::atomic<bool> dump[64]{};
inline std::atomic<int> dumps{0};
inline WHV_PARTITION_HANDLE partition = nullptr;
inline int cpus = 0;

using GetRegisters = decltype(&WHvGetVirtualProcessorRegisters);
using CancelRun = decltype(&WHvCancelRunVirtualProcessor);

inline uint32_t qemu_read(WHV_EMULATOR_MEMORY_CALLBACK memory, uint32_t offset) {
    WHV_EMULATOR_MEMORY_ACCESS_INFO access{};
    access.GpaAddress = 0xFEE00000ull + offset;
    access.Direction = 0;
    access.AccessSize = 4;
    memory(nullptr, &access);
    uint32_t value;
    std::memcpy(&value, access.Data, sizeof(value));
    return value;
}

inline void dump_state(WHV_PARTITION_HANDLE handle, UINT32 cpu, GetRegisters get, WHV_EMULATOR_MEMORY_CALLBACK memory,
                       const WHV_RUN_VP_EXIT_CONTEXT* exit) {
    WHV_REGISTER_NAME names[27];
    WHV_REGISTER_VALUE values[27]{};
    for (int i = 0; i < 8; ++i) {
        names[i] = static_cast<WHV_REGISTER_NAME>(WHvX64RegisterApicIsr0 + i);
        names[8 + i] = static_cast<WHV_REGISTER_NAME>(WHvX64RegisterApicIrr0 + i);
        names[16 + i] = static_cast<WHV_REGISTER_NAME>(WHvX64RegisterApicTmr0 + i);
    }
    names[24] = WHvX64RegisterApicPpr;
    names[25] = WHvX64RegisterApicTpr;
    names[26] = WHvRegisterInternalActivityState;
    const HRESULT hr = get(handle, cpu, names, 27, values);
    std::fprintf(stderr, "AXRB hv-apic debug: vCPU %u exit=%u rip=%llx rflags=%llx hr=%08lx ppr=%llx tpr=%llx activity=%llx\n",
                 cpu, static_cast<unsigned>(exit->ExitReason), static_cast<unsigned long long>(exit->VpContext.Rip),
                 static_cast<unsigned long long>(exit->VpContext.Rflags), hr,
                 static_cast<unsigned long long>(values[24].Reg64), static_cast<unsigned long long>(values[25].Reg64),
                 static_cast<unsigned long long>(values[26].Reg64));
    // xAPIC state is only readable as the whole register page.
    using GetState = decltype(&WHvGetVirtualProcessorInterruptControllerState2);
    static const auto getState = reinterpret_cast<GetState>(
        GetProcAddress(GetModuleHandleW(L"WinHvPlatform.dll"), "WHvGetVirtualProcessorInterruptControllerState2"));
    alignas(16) static thread_local uint8_t page[4096];
    UINT32 written = 0;
    const HRESULT pageResult = getState ? getState(handle, cpu, page, sizeof(page), &written) : E_FAIL;
    std::fprintf(stderr, "AXRB hv-apic debug: vCPU %u hv page hr=%08lx bytes=%u tpr=%02x ppr=%02x svr=%03x timer-lvt=%08x init=%08x current=%08x\n",
                 cpu, pageResult, written, page[0x80], page[0xA0], *reinterpret_cast<uint32_t*>(page + 0xF0) & 0x1ff,
                 *reinterpret_cast<uint32_t*>(page + 0x320), *reinterpret_cast<uint32_t*>(page + 0x380),
                 *reinterpret_cast<uint32_t*>(page + 0x390));
    const uint32_t hvBases[] = {0x100, 0x200, 0x180};
    const char* labels[] = {"hv isr", "hv irr", "hv tmr"};
    for (int k = 0; k < 3; ++k) {
        std::fprintf(stderr, "AXRB hv-apic debug: vCPU %u %s", cpu, labels[k]);
        for (int i = 7; i >= 0; --i) std::fprintf(stderr, " %08x", *reinterpret_cast<uint32_t*>(page + hvBases[k] + i * 0x10));
        std::fprintf(stderr, "\n");
    }
    if (!memory) return;
    const uint32_t bases[] = {0x100, 0x200, 0x180};
    const char* qemuLabels[] = {"qemu isr", "qemu irr", "qemu tmr"};
    for (int k = 0; k < 3; ++k) {
        std::fprintf(stderr, "AXRB hv-apic debug: vCPU %u %s", cpu, qemuLabels[k]);
        for (int i = 7; i >= 0; --i) std::fprintf(stderr, " %08x", qemu_read(memory, bases[k] + i * 0x10));
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "AXRB hv-apic debug: vCPU %u qemu svr=%08x ldr=%08x dfr=%08x tpr=%08x\n", cpu,
                 qemu_read(memory, 0xF0), qemu_read(memory, 0xD0), qemu_read(memory, 0xE0), qemu_read(memory, 0x80));
}

inline void start(CancelRun cancel) {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("AXRB_WHPX_HV_APIC_DEBUG", value, sizeof(value));
    enabled = length && length < sizeof(value) && !std::strcmp(value, "1");
    if (!enabled) return;
    static CancelRun cancelRun = cancel;
    CreateThread(nullptr, 0, [](void*) -> DWORD {
        uint64_t last = 0, stalledSince = 0;
        char request[MAX_PATH]{};
        const DWORD length = GetTempPathA(MAX_PATH, request);
        if (length && length + 24 < MAX_PATH) std::strcat(request, "axrb-hv-apic-dump");
        for (;;) {
            Sleep(250);
            // On demand: create %TEMP%\axrb-hv-apic-dump.
            if (request[0] && partition && DeleteFileA(request)) {
                std::fprintf(stderr, "AXRB hv-apic debug: dump requested\n");
                for (int cpu = 0; cpu < cpus && cpu < 64; ++cpu) {
                    dump[cpu] = true;
                    cancelRun(partition, cpu, 0);
                }
                continue;
            }
            const uint64_t now = GetTickCount64(), value = progress.load();
            if (value != last || !partition) { last = value; stalledSince = now; continue; }
            if (now - stalledSince < 1000 || dumps.load() >= 6) continue;
            dumps.fetch_add(1);
            std::fprintf(stderr, "AXRB hv-apic debug: no device EOI for %llu ms\n",
                         static_cast<unsigned long long>(now - stalledSince));
            for (int cpu = 0; cpu < cpus && cpu < 64; ++cpu) {
                dump[cpu] = true;
                cancelRun(partition, cpu, 0);
            }
            stalledSince = now;
        }
    }, nullptr, 0, nullptr);
}

} // namespace axrb::hv_apic_debug
