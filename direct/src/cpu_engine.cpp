#include "cpu_engine.h"
#include "cpu.h"
#include "memory.h"
#include "log.h"
#include "kernel.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <stdexcept>

namespace axrb {
CpuEngine::CpuEngine(const Dynarmic::A64::UserConfig& config, GuestThread& thread)
    : thread_(thread), callbacks_(config.callbacks) {
    const char* translator = std::getenv("AXRB_DIRECT_TRANSLATOR");
    if (!translator || !*translator || !std::strcmp(translator, "dynarmic")) {
        dynarmic_ = std::make_unique<Dynarmic::A64::Jit>(config);
    } else if (!std::strcmp(translator, "digitalis")) {
        digitalis::Callbacks callbacks;
        callbacks.read = [](void*, uint64_t address, void* out, size_t size) { return guest_read(address, out, size); };
        callbacks.write = [](void*, uint64_t address, const void* data, size_t size) { return guest_write(address, data, size); };
        callbacks.fault = [](void*, uint64_t address, bool write) { return address_space().handle_fault(address, write, false); };
        callbacks.invalidate = [](void*, uint64_t address, uint64_t size) { invalidate_code(address, size); };
        callbacks.syscall = [](void*, uint64_t* x) { return fast_syscall(x); };
        // Wait for lazy file-backed pages to finish populating before decoding.
        // A concurrent commit exposes zeroed memory until its content is copied.
        callbacks.fetch = [](void*, uint64_t address, void* out, size_t size) {
            g_syscall_counts[505].fetch_add(1, std::memory_order_relaxed);
            return address_space().read_complete(address, out, size);
        };
        digitalis_ = digitalis::create(callbacks);
    } else {
        throw std::runtime_error(std::string("Unknown Direct translator: ") + translator);
    }
    static std::once_flag logged;
    std::call_once(logged, [&] { LOGI("cpu", "ARM64 translator: %s", digitalis_ ? "Digitalis (optimizing JIT + lite JIT + interpreter)" : "Dynarmic"); });
}
CpuEngine::~CpuEngine() { digitalis::destroy(digitalis_); }
Dynarmic::HaltReason CpuEngine::Run() {
    if (dynarmic_) return dynarmic_->Run();
    auto pending = halt_.exchange(0, std::memory_order_acq_rel);
    if (pending) {
        digitalis::clear_interrupt(digitalis_);
        return static_cast<Dynarmic::HaltReason>(pending);
    }
    registers_.tls = thread_.tpidr;
    const auto result = digitalis::run(digitalis_, &registers_, 4096);
    thread_.tpidr = registers_.tls;
    using E = Dynarmic::A64::Exception;
    switch (result.reason) {
        case digitalis::Stop::Svc: callbacks_->CallSVC(result.immediate); break;
        case digitalis::Stop::Undefined: callbacks_->ExceptionRaised(registers_.pc, E::UnallocatedEncoding); break;
        case digitalis::Stop::Breakpoint: callbacks_->ExceptionRaised(registers_.pc, E::Breakpoint); break;
        case digitalis::Stop::MemoryFault:
            cpu_raise(thread_, linux_abi::SIGSEGV_, address_space().check(result.address, 1, 0) ?
                linux_abi::SEGV_ACCERR_ : linux_abi::SEGV_MAPERR_, result.address, registers_.pc);
            break;
        case digitalis::Stop::Yield: break;
    }
    const auto reason = halt_.exchange(0, std::memory_order_acq_rel);
    // Every pending halt is returned now; a halt requested after the exchange
    // is still in halt_ and returns at the top of the next Run.
    digitalis::clear_interrupt(digitalis_);
    return static_cast<Dynarmic::HaltReason>(reason);
}
void CpuEngine::HaltExecution(Dynarmic::HaltReason reason) {
    if (dynarmic_) dynarmic_->HaltExecution(reason);
    else { halt_.fetch_or(static_cast<uint32_t>(reason), std::memory_order_release); digitalis::interrupt(digitalis_); }
}
void CpuEngine::InvalidateCacheRange(uint64_t address, size_t length) {
    if (dynarmic_) dynarmic_->InvalidateCacheRange(address, length);
    else digitalis::invalidate_cache(digitalis_, address, length);
}
uint64_t CpuEngine::GuestPCForHostCode(const void* code) const {
    return dynarmic_ ? dynarmic_->GuestPCForHostCode(code) : ~uint64_t{0};
}
uint64_t CpuEngine::GuestPCForHostCodeResumed(const void* code, bool* optimized, uint64_t* start, uint32_t* bytes) const {
    return digitalis_ ? digitalis::guest_pc_for_host_code(digitalis_, code, optimized, start, bytes) : ~uint64_t{0};
}
std::array<uint64_t, 31> CpuEngine::GetRegisters() const {
    if (dynarmic_) return dynarmic_->GetRegisters();
    std::array<uint64_t, 31> result; std::copy_n(registers_.x, 31, result.begin()); return result;
}
void CpuEngine::SetRegisters(const std::array<uint64_t, 31>& values) {
    if (dynarmic_) dynarmic_->SetRegisters(values); else std::copy(values.begin(), values.end(), registers_.x);
}
Vec128 CpuEngine::GetVector(size_t i) const {
    return dynarmic_ ? dynarmic_->GetVector(i) : Vec128{registers_.v[i][0], registers_.v[i][1]};
}
void CpuEngine::SetVector(size_t i, Vec128 value) {
    if (dynarmic_) dynarmic_->SetVector(i, value);
    else { registers_.v[i][0] = value[0]; registers_.v[i][1] = value[1]; }
}
std::array<Vec128, 32> CpuEngine::GetVectors() const {
    if (dynarmic_) return dynarmic_->GetVectors();
    std::array<Vec128, 32> result;
    for (size_t i = 0; i < 32; ++i) result[i] = GetVector(i);
    return result;
}
void CpuEngine::SetVectors(const std::array<Vec128, 32>& values) {
    if (dynarmic_) dynarmic_->SetVectors(values);
    else for (size_t i = 0; i < 32; ++i) SetVector(i, values[i]);
}
}
