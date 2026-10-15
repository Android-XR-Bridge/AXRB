#pragma once
#include "kernel.h"
#include "../digitalis/digitalis.h"
#include <dynarmic/interface/A64/a64.h>
#include <dynarmic/interface/A64/config.h>

namespace axrb {
// Common execution interface; Android services and host bridges do not depend
// on the selected instruction translator.
class CpuEngine {
public:
    CpuEngine(const Dynarmic::A64::UserConfig& config, GuestThread& thread);
    ~CpuEngine();
    Dynarmic::HaltReason Run();
    void HaltExecution(Dynarmic::HaltReason reason);
    void InvalidateCacheRange(uint64_t address, size_t length);
    uint64_t GuestPCForHostCode(const void* code) const;
    uint64_t GuestPCForHostCodeResumed(const void* code, bool* optimized, uint64_t* start = nullptr, uint32_t* bytes = nullptr) const;
    uint64_t GetRegister(size_t i) const { return dynarmic_ ? dynarmic_->GetRegister(i) : registers_.x[i]; }
    void SetRegister(size_t i, uint64_t v) { if (dynarmic_) dynarmic_->SetRegister(i, v); else registers_.x[i] = v; }
    std::array<uint64_t, 31> GetRegisters() const;
    void SetRegisters(const std::array<uint64_t, 31>& values);
    Vec128 GetVector(size_t i) const;
    void SetVector(size_t i, Vec128 value);
    std::array<Vec128, 32> GetVectors() const;
    void SetVectors(const std::array<Vec128, 32>& values);
#define AXRB_CPU_FIELD(Name, Field, Type) \
    Type Get##Name() const { return dynarmic_ ? dynarmic_->Get##Name() : registers_.Field; } \
    void Set##Name(Type value) { if (dynarmic_) dynarmic_->Set##Name(value); else registers_.Field = value; }
    AXRB_CPU_FIELD(PC, pc, uint64_t)
    AXRB_CPU_FIELD(SP, sp, uint64_t)
    AXRB_CPU_FIELD(Pstate, pstate, uint32_t)
    AXRB_CPU_FIELD(Fpcr, fpcr, uint32_t)
    AXRB_CPU_FIELD(Fpsr, fpsr, uint32_t)
#undef AXRB_CPU_FIELD
private:
    GuestThread& thread_;
    Dynarmic::A64::UserCallbacks* callbacks_;
    std::unique_ptr<Dynarmic::A64::Jit> dynarmic_;
    digitalis::Engine* digitalis_{};
    digitalis::Registers registers_{};
    std::atomic<uint32_t> halt_{};
};
}
