#pragma once
// Opt-in hypervisor-emulated local APIC for the legacy emulator (AXRB_WHPX_HV_APIC=1).
//
// Emulator 36.5.11's WHPX code emulates the local APIC in QEMU: every guest
// APIC access (EOI, timer programming, IPIs) is a memory exit that QEMU
// decodes and emulates in userspace, and every idle HLT returns to QEMU. With
// the partition's LocalApicEmulationMode set, Hyper-V handles all of that
// without leaving the hypervisor, as upstream QEMU's kernel-irqchip mode does.
//
// QEMU keeps its own APIC model for what only it knows: device interrupts from
// its IOAPIC and MSI, and CPU startup. The hooks bridge the two:
//  * an interrupt QEMU injects through WHvRegisterPendingInterruption is
//    requested from the hypervisor APIC instead. An edge-triggered one (MSI)
//    is acknowledged in QEMU's APIC at once; a level-triggered one (IOAPIC
//    line) is requested level-triggered, so the guest's EOI returns as an
//    ApicEoi exit;
//  * that EOI, and guest writes to the APIC registers QEMU's model uses for
//    interrupt routing (SVR, LDR, DFR, LINT0/1) and INIT/SIPI, reach QEMU's
//    APIC as the MMIO writes QEMU would have seen, on the same vCPU thread;
//  * a secondary vCPU that QEMU starts leaves the hypervisor's
//    wait-for-SIPI state, since QEMU already applied the startup vector.
// These exits never reach QEMU, whose run loop does not know them.
//
// Linux verifies a calibrated APIC timer by switching it to periodic mode,
// and Hyper-V's timer then first delivers the periods elapsed since the
// calibration countdown began, so Linux rejects it and falls back to the PIT.
// A hypervisor that reports the timer frequency skips that check. The guest
// kernel has no Hyper-V support but does implement VMware's timing interface,
// so CPUID leaf 0x40000000 carries VMware's signature and the backdoor's
// GETHZ command answers with the host TSC rate (measured against the
// performance counter) and Hyper-V's fixed 200 MHz APIC timer rate. Every
// other backdoor command is left to QEMU.
#include <windows.h>
#include <WinHvPlatform.h>
#include <WinHvEmulation.h>
#include <MinHook.h>
#include "hv_apic_debug.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace axrb::hv_apic {

using SetPartitionProperty = decltype(&WHvSetPartitionProperty);
using RunProcessor = decltype(&WHvRunVirtualProcessor);
using RequestInterrupt = decltype(&WHvRequestInterrupt);
using GetRegisters = decltype(&WHvGetVirtualProcessorRegisters);
using SetRegisters = decltype(&WHvSetVirtualProcessorRegisters);
using CreateEmulator = decltype(&WHvEmulatorCreateEmulator);

inline bool enabled = false;
inline SetPartitionProperty originalSetProperty = nullptr;
inline CreateEmulator originalCreateEmulator = nullptr;
inline RunProcessor nextRun = nullptr;          // exit profiler or WHPX
inline SetRegisters rawSetRegisters = nullptr;  // WHPX, unhooked
inline GetRegisters getRegisters = nullptr;
inline RequestInterrupt requestInterrupt = nullptr;
inline WHV_EMULATOR_MEMORY_CALLBACK qemuMemory = nullptr;
inline std::atomic<bool> active{false};

constexpr int kCpus = 64;
inline std::atomic<bool> apicEnabled[kCpus]{}; // SVR software-enable bit, from write traps
inline std::atomic<uint32_t> spuriousVector[kCpus]{}; // SVR vector, from write traps
inline std::atomic<bool> startup[kCpus]{};     // a startup IPI was delivered through QEMU
inline std::atomic<bool> recheck[kCpus]{};     // QEMU's APIC may have another interrupt to inject
struct Counters { std::atomic<uint64_t> requested{0}, level{0}, direct{0}, spurious{0}, nmis{0}, eois{0}, writes{0}, initSipi{0}, halts{0}, failures{0}; };
inline Counters counters;

// Kick latency: from QEMU's WHvCancelRunVirtualProcessor to the run call
// returning, bucketed <10,<50,<200,<1000,<5000,>=5000 us.
using CancelRun = decltype(&WHvCancelRunVirtualProcessor);
inline CancelRun originalCancel = nullptr;
inline std::atomic<uint64_t> cancelAt[64]{};
inline std::atomic<uint64_t> kickBuckets[6]{};

constexpr uint64_t kApicTimerHz = 200000000; // Hyper-V's architectural APIC timer rate
inline uint64_t startTsc = 0, startQpc = 0;
inline thread_local uint64_t cpuidLeaf = ~0ull; // leaf QEMU is answering on this vCPU thread

inline uint64_t qpc_now();
inline std::atomic<bool> longMode[64]{}; // the kernel runs with its APIC enabled
inline std::atomic<bool> inRun[64]{};

// QEMU kicks a processor (cancels its run) to inject an interrupt from its
// own APIC. A processor halted inside the hypervisor does not return from a
// cancel until something wakes it, typically its next timer interrupt, which
// delayed device interrupts by milliseconds and stalled the ADB transport. A
// kick of a running processor therefore also requests Linux's reschedule IPI,
// which wakes it; the guest acknowledges it and finds nothing to do.
constexpr uint32_t kRescheduleVector = 0xfd;
inline std::atomic<uint64_t> wakes{0};
inline bool wakeOnKick = true; // AXRB_WHPX_HV_APIC_WAKE=0 turns it off (measurement)
inline HRESULT WINAPI cancel_run(WHV_PARTITION_HANDLE partition, UINT32 cpu, UINT32 flags) {
    if (cpu < 64 && hv_apic_debug::enabled) { uint64_t zero = 0; cancelAt[cpu].compare_exchange_strong(zero, qpc_now()); }
    const HRESULT result = originalCancel(partition, cpu, flags);
    if (wakeOnKick && cpu < 64 && inRun[cpu].load() && longMode[cpu].load(std::memory_order_relaxed)) {
        WHV_INTERRUPT_CONTROL wake{};
        wake.Type = WHvX64InterruptTypeFixed;
        wake.DestinationMode = WHvX64InterruptDestinationModePhysical;
        wake.TriggerMode = WHvX64InterruptTriggerModeEdge;
        wake.Destination = cpu;
        wake.Vector = kRescheduleVector;
        if (SUCCEEDED(requestInterrupt(partition, &wake, sizeof(wake)))) wakes.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}
inline uint64_t qpc_now() { LARGE_INTEGER v; QueryPerformanceCounter(&v); return static_cast<uint64_t>(v.QuadPart); }

inline uint64_t tsc_hz() {
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    const uint64_t ticks = qpc_now() - startQpc, cycles = __rdtsc() - startTsc;
    return static_cast<uint64_t>(static_cast<double>(cycles) * frequency.QuadPart / ticks + 0.5);
}

inline bool read_flag(const char* name) {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length && length < sizeof(value) && !std::strcmp(value, "1");
}

// x2APIC (AXRB_WHPX_HV_X2APIC=1, with the hypervisor APIC): APIC registers
// become MSRs, which the hypervisor handles without decoding an instruction,
// and an IPI is one register write instead of three. The guest is offered
// x2APIC through CPUID and VMware's "legacy x2APIC" vCPU flag, which Linux
// requires when there is no interrupt remapping.
inline bool x2apic = false;

inline bool in_x2apic_mode(WHV_PARTITION_HANDLE partition, UINT32 cpu) {
    WHV_REGISTER_NAME name = WHvX64RegisterApicBase;
    WHV_REGISTER_VALUE value{};
    return x2apic && SUCCEEDED(getRegisters(partition, cpu, &name, 1, &value)) && (value.Reg64 & 0x400);
}

// A write QEMU's APIC model would have received through its MMIO page.
inline uint32_t qemu_apic_read(uint32_t offset) {
    WHV_EMULATOR_MEMORY_ACCESS_INFO access{};
    access.GpaAddress = 0xFEE00000ull + offset;
    access.Direction = 0;
    access.AccessSize = 4;
    qemuMemory(nullptr, &access);
    uint32_t value;
    std::memcpy(&value, access.Data, sizeof(value));
    return value;
}

inline void qemu_apic_write(uint32_t offset, uint32_t value) {
    WHV_EMULATOR_MEMORY_ACCESS_INFO access{};
    access.GpaAddress = 0xFEE00000ull + offset;
    access.Direction = 1;
    access.AccessSize = 4;
    std::memcpy(access.Data, &value, sizeof(value));
    qemuMemory(nullptr, &access);
}

// QEMU sets its extended exits before WHvSetupPartition. The APIC trap exits
// need a partition that emulates the APIC, so the mode is set first; if
// either is refused, QEMU keeps its APIC and its original request.
inline HRESULT WINAPI set_partition_property(WHV_PARTITION_HANDLE partition, WHV_PARTITION_PROPERTY_CODE code,
                                             const VOID* buffer, UINT32 size) {
    if (code != WHvPartitionPropertyCodeExtendedVmExits || size < sizeof(UINT64) || active)
        return originalSetProperty(partition, code, buffer, size);
    WHV_X64_LOCAL_APIC_EMULATION_MODE mode = x2apic ? WHvX64LocalApicEmulationModeX2Apic : WHvX64LocalApicEmulationModeXApic;
    HRESULT result = originalSetProperty(partition, WHvPartitionPropertyCodeLocalApicEmulationMode, &mode, sizeof(mode));
    if (FAILED(result)) {
        std::fprintf(stderr, "AXRB hv-apic: hypervisor APIC unavailable (%08lx); QEMU keeps the APIC\n", result);
        return originalSetProperty(partition, code, buffer, size);
    }
    WHV_EXTENDED_VM_EXITS exits{};
    std::memcpy(&exits.AsUINT64, buffer, sizeof(UINT64));
    exits.X64ApicInitSipiExitTrap = 1;
    exits.X64ApicWriteSvrExitTrap = 1;
    exits.X64ApicWriteLdrExitTrap = 1;
    exits.X64ApicWriteDfrExitTrap = 1;
    exits.X64ApicWriteLint0ExitTrap = 1;
    exits.X64ApicWriteLint1ExitTrap = 1;
    result = originalSetProperty(partition, code, &exits, sizeof(exits));
    if (SUCCEEDED(result)) {
        active = true;
        std::fprintf(stderr, "AXRB hv-apic: local APIC emulated by the hypervisor\n");
        return result;
    }
    std::fprintf(stderr, "AXRB hv-apic: APIC exit traps refused (%08lx, exits %016llx); QEMU keeps the APIC\n",
                 result, static_cast<unsigned long long>(exits.AsUINT64));
    mode = WHvX64LocalApicEmulationModeNone;
    originalSetProperty(partition, WHvPartitionPropertyCodeLocalApicEmulationMode, &mode, sizeof(mode));
    return originalSetProperty(partition, code, buffer, size);
}

inline HRESULT WINAPI create_emulator(const WHV_EMULATOR_CALLBACKS* callbacks, WHV_EMULATOR_HANDLE* emulator) {
    if (callbacks && callbacks->WHvEmulatorMemoryCallback) qemuMemory = callbacks->WHvEmulatorMemoryCallback;
    return originalCreateEmulator(callbacks, emulator);
}

// A processor halted or waiting for SIPI inside the hypervisor ignores the
// state QEMU loads and interrupts QEMU injects directly; release it.
inline void release(WHV_PARTITION_HANDLE partition, UINT32 cpu) {
    WHV_REGISTER_NAME name = WHvRegisterInternalActivityState;
    WHV_REGISTER_VALUE value{};
    if (SUCCEEDED(getRegisters(partition, cpu, &name, 1, &value)) &&
        (value.InternalActivity.StartupSuspend || value.InternalActivity.HaltSuspend)) {
        value.InternalActivity.StartupSuspend = 0;
        value.InternalActivity.HaltSuspend = 0;
        rawSetRegisters(partition, cpu, &name, 1, &value);
    }
}

// Replaces an interrupt QEMU injects with a request to the hypervisor APIC.
// A software-disabled APIC (firmware, early boot) would drop it, and vectors
// below 16 cannot be requested: those keep QEMU's direct injection.
// Returns the number of registers left for WHPX.
inline UINT32 redirect_injection(WHV_PARTITION_HANDLE partition, UINT32 cpu, const WHV_REGISTER_NAME* names,
                                 const WHV_REGISTER_VALUE* values, UINT32 count,
                                 WHV_REGISTER_NAME* keptNames, WHV_REGISTER_VALUE* keptValues) {
    UINT32 kept = 0;
    for (UINT32 i = 0; i < count; ++i) {
        if (names[i] == WHvRegisterPendingInterruption && values[i].PendingInterruption.InterruptionPending &&
            (values[i].PendingInterruption.InterruptionType == WHvX64PendingInterrupt ||
             values[i].PendingInterruption.InterruptionType == WHvX64PendingNmi)) {
            const bool nmi = values[i].PendingInterruption.InterruptionType == WHvX64PendingNmi;
            // QEMU's APIC answers a request blocked by priority with the
            // spurious vector, which needs no EOI. Requested level-triggered it
            // would stay in service and block every interrupt; it is dropped.
            if (!nmi && cpu < kCpus && apicEnabled[cpu].load(std::memory_order_relaxed) &&
                values[i].PendingInterruption.InterruptionVector == spuriousVector[cpu].load(std::memory_order_relaxed)) {
                counters.spurious.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!nmi && (cpu >= kCpus || !apicEnabled[cpu].load(std::memory_order_relaxed) ||
                         values[i].PendingInterruption.InterruptionVector < 16)) {
                release(partition, cpu);
                counters.direct.fetch_add(1, std::memory_order_relaxed);
                keptNames[kept] = names[i];
                keptValues[kept++] = values[i];
                continue;
            }
            // An edge-triggered vector (MSI) needs nothing from QEMU after
            // delivery: it is acknowledged in QEMU's APIC right away, so that
            // APIC's in-service state never diverges from the hypervisor's,
            // and requested edge-triggered, which needs no EOI exit. QEMU may
            // have raised the next pending interrupt meanwhile; the next run
            // returns to QEMU at once so it can inject that one too. Only
            // level-triggered vectors (IOAPIC lines) wait for the guest's EOI.
            const uint32_t vector = values[i].PendingInterruption.InterruptionVector;
            const bool level = !nmi && ((qemu_apic_read(0x180 + (vector / 32) * 0x10) >> (vector % 32)) & 1);
            if (!nmi && !level) {
                qemu_apic_write(0xB0, 0);
                recheck[cpu].store(true, std::memory_order_relaxed);
            }
            if (level) counters.level.fetch_add(1, std::memory_order_relaxed);
            hv_apic_debug::progress.fetch_add(1, std::memory_order_relaxed);
            WHV_INTERRUPT_CONTROL control{};
            control.Type = nmi ? WHvX64InterruptTypeNmi : WHvX64InterruptTypeFixed;
            control.DestinationMode = WHvX64InterruptDestinationModePhysical;
            control.TriggerMode = level ? WHvX64InterruptTriggerModeLevel : WHvX64InterruptTriggerModeEdge;
            control.Destination = cpu;
            control.Vector = values[i].PendingInterruption.InterruptionVector;
            const HRESULT result = requestInterrupt(partition, &control, sizeof(control));
            if (FAILED(result) && counters.failures.fetch_add(1) < 8)
                std::fprintf(stderr, "AXRB hv-apic: request vector %u on vCPU %u failed: %08lx\n", control.Vector, cpu, result);
            (nmi ? counters.nmis : counters.requested).fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        keptNames[kept] = names[i];
        keptValues[kept++] = values[i];
    }
    return kept;
}

// QEMU's APIC model has no x2APIC mode, so a full register write carries an
// xAPIC base, which would take the processor out of x2APIC mode. Returns the
// values to write, with the current base kept.
inline const WHV_REGISTER_VALUE* keep_x2apic_base(WHV_PARTITION_HANDLE partition, UINT32 cpu, const WHV_REGISTER_NAME* names,
                                                  const WHV_REGISTER_VALUE* values, UINT32 count, WHV_REGISTER_VALUE* copy) {
    for (UINT32 i = 0; i < count; ++i) {
        if (names[i] != WHvX64RegisterApicBase) continue;
        if ((values[i].Reg64 & 0x400) || !in_x2apic_mode(partition, cpu)) return values;
        WHV_REGISTER_NAME name = WHvX64RegisterApicBase;
        WHV_REGISTER_VALUE current{};
        if (FAILED(getRegisters(partition, cpu, &name, 1, &current))) return values;
        std::memcpy(copy, values, count * sizeof(*values));
        copy[i] = current;
        return copy;
    }
    return values;
}

inline bool injects(const WHV_REGISTER_NAME* names, UINT32 count) {
    for (UINT32 i = 0; i < count; ++i)
        if (names[i] == WHvRegisterPendingInterruption) return true;
    return false;
}

inline void report() {
    static std::atomic<uint64_t> next{0};
    const uint64_t now = GetTickCount64();
    uint64_t due = next.load();
    if (now < due || !next.compare_exchange_strong(due, now + 10000)) return;
    std::fprintf(stderr, "AXRB hv-apic: requested=%llu level=%llu direct=%llu spurious=%llu wakes=%llu nmi=%llu eoi=%llu writes=%llu init-sipi=%llu halts=%llu failures=%llu\n",
        (unsigned long long)counters.requested.load(), (unsigned long long)counters.level.load(), (unsigned long long)counters.direct.load(), (unsigned long long)counters.spurious.load(), (unsigned long long)wakes.load(),
        (unsigned long long)counters.nmis.load(),
        (unsigned long long)counters.eois.load(), (unsigned long long)counters.writes.load(),
        (unsigned long long)counters.initSipi.load(), (unsigned long long)counters.halts.load(),
        (unsigned long long)counters.failures.load());
    std::fprintf(stderr, "AXRB hv-apic: kick latency <10:%llu <50:%llu <200:%llu <1000:%llu <5000:%llu >=5000:%llu\n",
        (unsigned long long)kickBuckets[0].load(), (unsigned long long)kickBuckets[1].load(), (unsigned long long)kickBuckets[2].load(),
        (unsigned long long)kickBuckets[3].load(), (unsigned long long)kickBuckets[4].load(), (unsigned long long)kickBuckets[5].load());
}

// QEMU answers CPUID in its own run loop and writes the result registers
// (RIP, RAX, RCX, RDX, RBX) from the same thread.
inline void patch_cpuid(const WHV_REGISTER_NAME* names, WHV_REGISTER_VALUE* values, UINT32 count) {
    const uint64_t leaf = cpuidLeaf;
    cpuidLeaf = ~0ull;
    if (count != 5 || names[1] != WHvX64RegisterRax || names[2] != WHvX64RegisterRcx ||
        names[3] != WHvX64RegisterRdx || names[4] != WHvX64RegisterRbx) return;
    if (leaf == 1 && x2apic) values[2].Reg64 |= 1ull << 21;
    if (leaf != 0x40000000) return;
    values[1].Reg64 = 0x40000001;     // below 0x40000010: the backdoor is the port
    values[4].Reg64 = 0x61774d56;     // "VMwa"
    values[2].Reg64 = 0x4d566572;     // "reVM"
    values[3].Reg64 = 0x65726177;     // "ware"
}

// VMware backdoor: IN from port 0x5658 with RAX = 'VMXh' and the command in
// CX: GETHZ (45) and, with x2APIC, GETVCPU_INFO (68).
inline bool answer_backdoor(WHV_PARTITION_HANDLE partition, UINT32 cpu, const WHV_RUN_VP_EXIT_CONTEXT* exit) {
    const auto& io = exit->IoPortAccess;
    if (io.PortNumber != 0x5658 || io.AccessInfo.IsWrite || io.AccessInfo.StringOp || io.AccessInfo.AccessSize != 4 ||
        static_cast<uint32_t>(io.Rax) != 0x564D5868) return false;
    // The exit context carries RCX only for string instructions.
    WHV_REGISTER_NAME rcxName = WHvX64RegisterRcx;
    WHV_REGISTER_VALUE rcx{};
    if (FAILED(getRegisters(partition, cpu, &rcxName, 1, &rcx))) return false;
    const uint32_t command = rcx.Reg64 & 0xffff;
    const WHV_REGISTER_NAME names[] = {WHvX64RegisterRip, WHvX64RegisterRax, WHvX64RegisterRbx, WHvX64RegisterRcx};
    WHV_REGISTER_VALUE values[4]{};
    values[0].Reg64 = exit->VpContext.Rip + exit->VpContext.InstructionLength;
    if (command == 68 && x2apic) {
        values[1].Reg64 = 1u << 3; // legacy x2APIC; bit 31 clear: the information is valid
        return SUCCEEDED(rawSetRegisters(partition, cpu, names, 2, values));
    }
    if (command != 45) return false;
    const uint64_t hz = tsc_hz();
    values[1].Reg64 = static_cast<uint32_t>(hz);
    values[2].Reg64 = static_cast<uint32_t>(hz >> 32);
    values[3].Reg64 = static_cast<uint32_t>(kApicTimerHz);
    if (FAILED(rawSetRegisters(partition, cpu, names, 4, values))) return false;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true))
        std::fprintf(stderr, "AXRB hv-apic: reported TSC %llu Hz, APIC timer %llu Hz\n",
                     static_cast<unsigned long long>(hz), static_cast<unsigned long long>(kApicTimerHz));
    return true;
}

// QEMU starts the processor from its own APIC; the hypervisor's copy must
// not keep it suspended.
inline void mark_startup(UINT32 self, uint64_t icr) {
    if (((icr >> 8) & 7) != 6) return; // not a startup IPI
    const uint32_t shorthand = (icr >> 18) & 3;
    const bool physical = !((icr >> 11) & 1);
    const uint32_t target = static_cast<uint32_t>(icr >> 56);
    for (UINT32 cpu = 0; cpu < kCpus; ++cpu) {
        const bool hit = shorthand == 2 || (shorthand == 3 && cpu != self) ||
            (shorthand == 1 && cpu == self) || (shorthand == 0 && (physical ? cpu == target : (target >> cpu) & 1));
        if (hit) startup[cpu] = true;
    }
}

// QEMU injects a device interrupt only while the exit shows the processor
// interruptible, and otherwise waits for an interrupt-window exit. The
// hypervisor APIC accepts a request at any time and delivers it when the
// guest allows, so once the guest enabled its APIC the exit QEMU sees always
// allows injection. QEMU reads RFLAGS back from the processor before it
// writes registers.
inline HRESULT to_qemu(UINT32 cpu, WHV_RUN_VP_EXIT_CONTEXT* exit, HRESULT result);
inline HRESULT to_qemu(WHV_PARTITION_HANDLE partition, UINT32 cpu, WHV_RUN_VP_EXIT_CONTEXT* exit, HRESULT result) {
    if (hv_apic_debug::enabled) {
        hv_apic_debug::partition = partition;
        if (static_cast<int>(cpu) + 1 > hv_apic_debug::cpus) hv_apic_debug::cpus = cpu + 1;
        if (cpu < 64 && hv_apic_debug::dump[cpu].exchange(false))
            hv_apic_debug::dump_state(partition, cpu, getRegisters, qemuMemory, exit);
    }
    return to_qemu(cpu, exit, result);
}
inline HRESULT to_qemu(UINT32 cpu, WHV_RUN_VP_EXIT_CONTEXT* exit, HRESULT result) {
    // Firmware injects real-mode PIC vectors directly and keeps its IF.
    if (cpu < kCpus) longMode[cpu].store(apicEnabled[cpu].load(std::memory_order_relaxed) &&
                                         exit->VpContext.ExecutionState.EferLma, std::memory_order_relaxed);
    if (cpu < kCpus && longMode[cpu].load(std::memory_order_relaxed)) {
        exit->VpContext.Rflags |= 0x200;
        exit->VpContext.ExecutionState.InterruptShadow = 0;
        exit->VpContext.ExecutionState.InterruptionPending = 0;
    }
    if (hv_apic_debug::enabled) report();
    return result;
}

inline HRESULT WINAPI run_processor(WHV_PARTITION_HANDLE partition, UINT32 cpu, VOID* context, UINT32 size) {
    if (!active.load(std::memory_order_relaxed)) return nextRun(partition, cpu, context, size);
    if (cpu < kCpus && startup[cpu].exchange(false)) release(partition, cpu);
    auto* exit = static_cast<WHV_RUN_VP_EXIT_CONTEXT*>(context);
    if (cpu < kCpus && recheck[cpu].exchange(false, std::memory_order_relaxed)) {
        // Back to QEMU's injection step without entering the processor; the
        // context still describes the last exit.
        exit->ExitReason = WHvRunVpExitReasonX64InterruptWindow;
        return to_qemu(cpu, exit, S_OK);
    }
    for (;;) {
        if (cpu < 64) inRun[cpu].store(true);
        const HRESULT result = nextRun(partition, cpu, context, size);
        if (cpu < 64) inRun[cpu].store(false);
        if (FAILED(result)) return result;
        if (cpu < 64) {
            if (const uint64_t at = hv_apic_debug::enabled ? cancelAt[cpu].exchange(0) : 0) {
                static const uint64_t f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return static_cast<uint64_t>(v.QuadPart); }();
                const uint64_t us = (qpc_now() - at) * 1000000 / f;
                const int b = us < 10 ? 0 : us < 50 ? 1 : us < 200 ? 2 : us < 1000 ? 3 : us < 5000 ? 4 : 5;
                kickBuckets[b].fetch_add(1, std::memory_order_relaxed);
                static std::atomic<int> slowLogged{0};
                if (us >= 2000 && hv_apic_debug::enabled && slowLogged.fetch_add(1) < 40)
                    std::fprintf(stderr, "AXRB hv-apic debug: slow kick vCPU %u %llu us exit=%u rip=%llx if=%u\n", cpu,
                                 static_cast<unsigned long long>(us), static_cast<unsigned>(exit->ExitReason),
                                 static_cast<unsigned long long>(exit->VpContext.Rip),
                                 static_cast<unsigned>((exit->VpContext.Rflags >> 9) & 1));
            }
        }
        switch (exit->ExitReason) {
        // Each of these may leave QEMU's APIC with an interrupt to inject on
        // this processor (the next pending vector after an EOI, a level line
        // still asserted), which QEMU only does before running it again. The
        // exit reaches QEMU as an interrupt-window exit, which it handles by
        // doing exactly that.
        case WHvRunVpExitReasonX64ApicEoi:
            counters.eois.fetch_add(1, std::memory_order_relaxed);
            hv_apic_debug::progress.fetch_add(1, std::memory_order_relaxed);
            qemu_apic_write(0xB0, 0);
            exit->ExitReason = WHvRunVpExitReasonX64InterruptWindow;
            return to_qemu(partition, cpu, exit, result);
        case WHvRunVpExitReasonX64ApicWriteTrap:
            counters.writes.fetch_add(1, std::memory_order_relaxed);
            if (exit->ApicWrite.Type == WHvX64ApicWriteTypeSvr && cpu < kCpus) {
                apicEnabled[cpu] = (exit->ApicWrite.WriteValue & 0x100) != 0;
                spuriousVector[cpu] = static_cast<uint32_t>(exit->ApicWrite.WriteValue & 0xff);
                // An x2APIC's logical ID is fixed and never written. For up to
                // eight processors its low byte, the destination a logical
                // MSI or IOAPIC entry carries, equals flat mode's 1 << id,
                // which QEMU's xAPIC model matches.
                if (cpu < 8 && in_x2apic_mode(partition, cpu)) {
                    qemu_apic_write(0xE0, 0xffffffff);
                    qemu_apic_write(0xD0, (1u << cpu) << 24);
                }
            }
            qemu_apic_write(static_cast<uint32_t>(exit->ApicWrite.Type), static_cast<uint32_t>(exit->ApicWrite.WriteValue));
            exit->ExitReason = WHvRunVpExitReasonX64InterruptWindow;
            return to_qemu(partition, cpu, exit, result);
        case WHvRunVpExitReasonX64ApicInitSipiTrap: {
            counters.initSipi.fetch_add(1, std::memory_order_relaxed);
            uint64_t icr = exit->ApicInitSipi.ApicIcr;
            // An x2APIC ICR holds the destination in bits 32-63, xAPIC in 56-63.
            if (in_x2apic_mode(partition, cpu)) icr = (icr & 0xffffffffull) | ((icr >> 32) & 0xff) << 56;
            qemu_apic_write(0x310, static_cast<uint32_t>(icr >> 32));
            qemu_apic_write(0x300, static_cast<uint32_t>(icr));
            mark_startup(cpu, icr);
            exit->ExitReason = WHvRunVpExitReasonX64InterruptWindow;
            return to_qemu(partition, cpu, exit, result);
        }
        case WHvRunVpExitReasonX64IoPortAccess:
            if (answer_backdoor(partition, cpu, exit)) break;
            return to_qemu(partition, cpu, exit, result);
        case WHvRunVpExitReasonX64Cpuid:
            cpuidLeaf = exit->CpuidAccess.Rax;
            return to_qemu(partition, cpu, exit, result);
        case WHvRunVpExitReasonX64Halt:
            // The hypervisor APIC wakes the processor; QEMU would wait for an
            // interrupt of its own and never resume it.
            counters.halts.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            return to_qemu(partition, cpu, exit, result);
        }
    }
}

// Installs the partition hooks when requested; returns false on a hook
// failure. The caller hooks WHvRunVirtualProcessor with run_processor.
inline bool install(HMODULE platform) {
    enabled = read_flag("AXRB_WHPX_HV_APIC");
    if (!enabled) return true;
    x2apic = read_flag("AXRB_WHPX_HV_X2APIC");
    {
        char value[8]{};
        const DWORD length = GetEnvironmentVariableA("AXRB_WHPX_HV_APIC_WAKE", value, sizeof(value));
        wakeOnKick = !(length && length < sizeof(value) && !std::strcmp(value, "0"));
    }
    HMODULE emulation = LoadLibraryExW(L"WinHvEmulation.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    void* setProperty = reinterpret_cast<void*>(GetProcAddress(platform, "WHvSetPartitionProperty"));
    void* create = emulation ? reinterpret_cast<void*>(GetProcAddress(emulation, "WHvEmulatorCreateEmulator")) : nullptr;
    requestInterrupt = reinterpret_cast<RequestInterrupt>(GetProcAddress(platform, "WHvRequestInterrupt"));
    void* cancel = reinterpret_cast<void*>(GetProcAddress(platform, "WHvCancelRunVirtualProcessor"));
    if (!cancel || MH_CreateHook(cancel, reinterpret_cast<void*>(cancel_run), reinterpret_cast<void**>(&originalCancel)) != MH_OK ||
        MH_EnableHook(cancel) != MH_OK) return false;
    getRegisters = reinterpret_cast<GetRegisters>(GetProcAddress(platform, "WHvGetVirtualProcessorRegisters"));
    if (!setProperty || !create || !requestInterrupt || !getRegisters) return false;
    if (MH_CreateHook(setProperty, reinterpret_cast<void*>(set_partition_property), reinterpret_cast<void**>(&originalSetProperty)) != MH_OK ||
        MH_CreateHook(create, reinterpret_cast<void*>(create_emulator), reinterpret_cast<void**>(&originalCreateEmulator)) != MH_OK ||
        MH_EnableHook(setProperty) != MH_OK || MH_EnableHook(create) != MH_OK) return false;
    hv_apic_debug::start(originalCancel);
    startTsc = __rdtsc();
    startQpc = qpc_now();
    std::fprintf(stderr, "AXRB hv-apic: hooks installed\n");
    return true;
}

} // namespace axrb::hv_apic
