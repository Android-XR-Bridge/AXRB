#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include "digitalis.h"
using namespace axrb::digitalis;
extern "C" float __extendhfsf2(_Float16);
extern "C" _Float16 __truncsfhf2(float);
extern "C" __uint128_t __udivti3(__uint128_t, __uint128_t);
static uint64_t reads = 0;
static bool read(void*, uint64_t address, void* out, size_t bytes) {
    ++reads;
    __try { std::memcpy(out, reinterpret_cast<void*>(address), bytes); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool write(void*, uint64_t address, const void* data, size_t bytes) {
    __try { std::memcpy(reinterpret_cast<void*>(address), data, bytes); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int main() try {
    for (unsigned bits = 0; bits < 65536; ++bits) {
        uint16_t original = static_cast<uint16_t>(bits), converted;
        _Float16 half; std::memcpy(&half, &original, 2);
        half = __truncsfhf2(__extendhfsf2(half));
        std::memcpy(&converted, &half, 2);
        const bool nan = (original & 0x7c00) == 0x7c00 && (original & 1023);
        assert(converted == (nan ? (original | 0x200) : original));
    }
    const __uint128_t high = __uint128_t{1} << 127;
    assert(__udivti3(high + 123, high) == 1);
    assert(__udivti3(~__uint128_t{0}, 3) == (~__uint128_t{0} / uint64_t{3}));
    assert(__udivti3(high, 2) == (high >> 1));
    auto* engine = create({nullptr, read, write});
    alignas(4096) std::array<uint32_t, 1024> code{};
    Registers registers;
    auto execute = [&](std::initializer_list<uint32_t> instructions) {
        if (GetEnvironmentVariableA("AXRB_DIGITALIS_DUMP_IR", nullptr, 0))
            std::fprintf(stderr, "SELFTEST first=%08x\n", *instructions.begin());
        clear_cache(engine);
        code.fill(0xd4000041); // svc #2
        std::copy(instructions.begin(), instructions.end(), code.begin());
        registers.pc = reinterpret_cast<uint64_t>(code.data());
        return run(engine, &registers, 100);
    };
    auto result = execute({0xd2800500, 0x91000800}); // mov x0,40; add x0,x0,2
    assert(result.reason == Stop::Svc && result.immediate == 2 && registers.x[0] == 42);
    assert(translated_blocks(engine));
    registers.x[0] = UINT64_MAX;
    registers.x[1] = 1;
    result = execute({0xab010002}); // adds x2,x0,x1
    assert(result.reason == Stop::Svc && registers.x[2] == 0 && registers.pstate == 0x60000000);
    registers.v[1][0] = 10; registers.v[1][1] = 20;
    registers.v[2][0] = 3; registers.v[2][1] = 7;
    result = execute({0x4ee28420}); // add v0.2d,v1.2d,v2.2d
    assert(result.reason == Stop::Svc && registers.v[0][0] == 13 && registers.v[0][1] == 27);
    registers.fpsr = 0;
    registers.v[1][0] = 0x3f800000; registers.v[2][0] = 0;
    result = execute({0x1e221820}); // fdiv s0,s1,s2 (1 / 0)
    assert(result.reason == Stop::Svc && (registers.fpsr & 2));
    result = execute({0xd53b4420}); // mrs x0,fpsr, in a separate execution batch
    assert(result.reason == Stop::Svc && (registers.x[0] & 2));
    // Fused arithmetic must retain single-rounding semantics and ARM signs.
    registers.v[1][0] = 0x3f800001; registers.v[2][0] = 0x3f7ffffe;
    registers.v[3][0] = 0xbf800000;
    result = execute({0x1f020c20}); // fmadd s0,s1,s2,s3
    assert(result.reason == Stop::Svc && registers.v[0][0] == 0xa8800000);
    registers.v[1][0] = 0x40000000; registers.v[2][0] = 0x40400000;
    registers.v[3][0] = 0x40800000;
    for (auto [instruction, expected] : std::array<std::pair<uint32_t, uint32_t>, 4>{{
        {0x1f020c20, 0x41200000}, {0x1f028c20, 0xc0000000},
        {0x1f220c20, 0xc1200000}, {0x1f228c20, 0x40000000}}}) {
        result = execute({instruction});
        assert(result.reason == Stop::Svc && registers.v[0][0] == expected);
        assert(registers.v[0][1] == 0);
    }
    registers.v[0][0] = registers.v[0][1] = 0x4008000000000000; // 3.0
    registers.v[1][0] = registers.v[1][1] = 0x3ff8000000000000; // 1.5
    registers.v[2][0] = registers.v[2][1] = 0x4000000000000000; // 2.0
    result = execute({0x4e62cc20}); // fmla v0.2d,v1.2d,v2.2d
    assert(result.reason == Stop::Svc && registers.v[0][0] == 0x4018000000000000);
    assert(registers.v[0][1] == 0x4018000000000000);
    registers.v[1][0] = 0x4000; registers.v[2][0] = 0x4200; registers.v[3][0] = 0x4400;
    result = execute({0x1fc20c20}); // fmadd h0,h1,h2,h3: 2*3+4 = 10
    assert(result.reason == Stop::Svc && registers.v[0][0] == 0x4900 && !registers.v[0][1]);
    registers.x[0] = 0x1234;
    registers.x[1] = 0x9988;
    result = execute({0xf9400001}); // ldr x1,[x0]
    assert(result.reason == Stop::MemoryFault && result.address == 0x1234);
    assert(registers.pc == reinterpret_cast<uint64_t>(code.data()));
    assert(registers.x[1] == 0x9988);
    result = execute({0xffffffff});
    assert(result.reason == Stop::Undefined);
    result = execute({0xd4200000});
    assert(result.reason == Stop::Breakpoint);
    {
        // b . : chained code runs until interrupted (unchained code also
        // yields at the execution budget).
        std::thread stopper([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); interrupt(engine); });
        result = execute({0x14000000});
        stopper.join();
        assert(result.reason == Stop::Yield && registers.pc == reinterpret_cast<uint64_t>(code.data()));
    }
    interrupt(engine);
    assert(run(engine, &registers, 100).reason == Stop::Yield);
    const auto started = std::chrono::steady_clock::now();
    std::thread kicker([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); interrupt(engine); });
    result = run(engine, &registers, UINT32_MAX);
    kicker.join();
    assert(result.reason == Stop::Yield);
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    // Drop any interrupt still pending before starting the next program.
    clear_interrupt(engine);
    // Reusing the same guest address after invalidation must execute fresh code.
    result = execute({0xd28000e0}); // mov x0,7
    assert(result.reason == Stop::Svc && registers.x[0] == 7);
    alignas(4096) std::array<uint32_t, 1024> other{};
    other[0] = 0xd28000e0; other[1] = 0xd4000041;
    registers.pc = reinterpret_cast<uint64_t>(other.data());
    assert(run(engine, &registers, 100).reason == Stop::Svc);
    const auto compiled = translated_blocks(engine);
    code[0] = 0xd2800120; // mov x0,9
    invalidate_cache(engine, reinterpret_cast<uint64_t>(code.data()) + 2, 1);
    registers.pc = reinterpret_cast<uint64_t>(code.data());
    assert(run(engine, &registers, 100).reason == Stop::Svc && registers.x[0] == 9);
    registers.pc = reinterpret_cast<uint64_t>(other.data());
    assert(run(engine, &registers, 100).reason == Stop::Svc && registers.x[0] == 7);
    assert(translated_blocks(engine) == compiled + 1); // unrelated cached block survives
    // A linked caller survives invalidation of its target on another page.
    // Both cold dispatch and a previously cached target must refresh.
    const auto target = reinterpret_cast<uint64_t>(other.data());
    const auto caller = reinterpret_cast<uint64_t>(code.data());
    code[0] = 0x14000000 | (uint32_t((int64_t(target) - int64_t(caller)) / 4) & 0x03ffffff);
    invalidate_cache(engine, caller, 4);
    for (uint32_t value : {11u, 13u}) {
        other[0] = 0xd2800000 | (value << 5); // mov x0,value
        invalidate_cache(engine, target, 4);
        for (unsigned repeat = 0; repeat != 3; ++repeat) {
            registers.pc = caller;
            result = run(engine, &registers, 100);
            assert(result.reason == Stop::Svc && registers.x[0] == value);
        }
    }
    result = execute({0xd4000041}); // svc #2
    assert(result.reason == Stop::Svc && result.immediate == 2);
    const auto fetched = reads;
    registers.pc = reinterpret_cast<uint64_t>(code.data());
    result = run(engine, &registers, 100);
    assert(result.reason == Stop::Svc && result.immediate == 2 && reads == fetched);
    code[0] = 0xd40000e1; // svc #7 after explicit instruction-cache invalidation
    invalidate_cache(engine, reinterpret_cast<uint64_t>(code.data()), 4);
    registers.pc = reinterpret_cast<uint64_t>(code.data());
    result = run(engine, &registers, 100);
    assert(result.reason == Stop::Svc && result.immediate == 7 && reads > fetched);
    result = execute({0xd503201f, 0xd4000041}); // nop; svc #2 translated together
    assert(result.reason == Stop::Svc && result.immediate == 2);
    code[1] = 0xd40000e1;
    invalidate_cache(engine, reinterpret_cast<uint64_t>(code.data()) + 4, 4);
    registers.pc = reinterpret_cast<uint64_t>(code.data());
    result = run(engine, &registers, 100);
    assert(result.reason == Stop::Svc && result.immediate == 7);
    {
        // Fast syscalls complete inside translated code; declined ones exit
        // with the SVC as before. Indirect branches reuse cached targets.
        Callbacks callbacks{nullptr, read, write};
        callbacks.syscall = [](void*, uint64_t* x) {
            if (x[8] != 999) return false;
            x[0] = 123;
            return true;
        };
        auto* fast = create(callbacks);
        alignas(4096) std::array<uint32_t, 1024> program{};
        program.fill(0xd4000041);                  // svc #2
        program[0] = 0xd2807ce8;                   // mov x8,#999
        program[1] = 0xd4000001;                   // svc #0
        program[2] = 0xd4000041;                   // svc #2
        Registers state{};
        for (int repeat = 0; repeat != 3; ++repeat) {
            state = {};
            state.pc = reinterpret_cast<uint64_t>(program.data());
            result = run(fast, &state, 100);
            assert(result.reason == Stop::Svc && result.immediate == 2 && state.x[0] == 123);
        }
        program[0] = 0xd2807d08;                   // mov x8,#1000: declined
        invalidate_cache(fast, reinterpret_cast<uint64_t>(program.data()), 4);
        state = {};
        state.pc = reinterpret_cast<uint64_t>(program.data());
        result = run(fast, &state, 100);
        assert(result.reason == Stop::Svc && result.immediate == 0 && state.x[0] == 0);
        assert(state.pc == reinterpret_cast<uint64_t>(program.data() + 2));
        program[0] = 0xd61f0020;                   // br x1
        program[64] = 0xd28000a0;                  // mov x0,#5
        program[65] = 0xd4000041;                  // svc #2
        invalidate_cache(fast, reinterpret_cast<uint64_t>(program.data()), 4096);
        for (int repeat = 0; repeat != 3; ++repeat) {
            state = {};
            state.pc = reinterpret_cast<uint64_t>(program.data());
            state.x[1] = reinterpret_cast<uint64_t>(program.data() + 64);
            result = run(fast, &state, 100);
            assert(result.reason == Stop::Svc && result.immediate == 2 && state.x[0] == 5);
        }
        destroy(fast);
    }
    // Exercise many independent call targets, then invalidate one code page
    // while the other targets remain cached.
    clear_cache(engine);
    auto* targets = static_cast<uint32_t*>(VirtualAlloc(nullptr, 65536,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(targets);
    for (uint32_t index = 0; index < 1200; ++index) {
        targets[index * 4] = 0xd2800000 | (index << 5);
        targets[index * 4 + 1] = 0xd4000041;
    }
    for (unsigned pass = 0; pass != 2; ++pass) {
        for (uint32_t index = 0; index < 1200; ++index) {
            registers.pc = reinterpret_cast<uint64_t>(targets + index * 4);
            result = run(engine, &registers, 100);
            assert(result.reason == Stop::Svc && registers.x[0] == index);
        }
        targets[0] = 0xd2800000;
        invalidate_cache(engine, reinterpret_cast<uint64_t>(targets), 4);
    }
    clear_cache(engine);
    VirtualFree(targets, 0, MEM_RELEASE);
    std::printf("DIGITALIS_WINDOWS_SELFTEST_PASS jit=%llu interpreted=%llu\n",
        (unsigned long long)translated_blocks(engine), (unsigned long long)interpreted_instructions(engine));
    destroy(engine);
} catch (const std::exception& error) {
    std::fprintf(stderr, "DIGITALIS_WINDOWS_SELFTEST_ERROR: %s\n", error.what());
    return 1;
}
