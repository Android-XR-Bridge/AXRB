#pragma once
#include <cstddef>
#include <cstdint>

namespace axrb::digitalis {
struct Registers {
    uint64_t x[31]{};
    uint64_t sp{}, pc{}, tls{};
    uint32_t pstate{}, fpcr{}, fpsr{};
    alignas(16) uint64_t v[32][2]{};
};
struct Callbacks {
    void* opaque{};
    bool (*read)(void*, uint64_t, void*, size_t){};
    bool (*write)(void*, uint64_t, const void*, size_t){};
    bool (*fault)(void*, uint64_t, bool){};
    void (*invalidate)(void*, uint64_t, uint64_t){};
    bool (*fetch)(void*, uint64_t, void*, size_t){};
    // Optional: completes a non-blocking Linux syscall (number in x[8]) from
    // translated code without leaving it; false selects the regular SVC exit.
    bool (*syscall)(void*, uint64_t* x){};
};
enum class Stop : uint32_t { Yield, Svc, MemoryFault, Undefined, Breakpoint };
struct Result { Stop reason; uint32_t immediate{}; uint64_t address{}; };
struct Engine;
Engine* create(Callbacks callbacks);
void destroy(Engine* engine);
Result run(Engine* engine, Registers* registers, uint32_t block_budget);
void clear_cache(Engine* engine);
void invalidate_cache(Engine* engine, uint64_t address, uint64_t size);
void interrupt(Engine* engine);
// Drops an interrupt requested by the owning thread itself (a handled halt).
void clear_interrupt(Engine* engine);
uint64_t translated_blocks(const Engine* engine);
uint64_t interpreted_instructions(const Engine* engine);
uint64_t optimized_blocks(const Engine* engine);
// For a profiler on another thread: the guest address of the translated
// region containing host code (~0 if none), and whether the optimizing tier
// made it. The owner may be running.
uint64_t guest_pc_for_host_code(Engine* engine, const void* code, bool* optimized = nullptr,
                                uint64_t* region_start = nullptr, uint32_t* region_bytes = nullptr);
}
