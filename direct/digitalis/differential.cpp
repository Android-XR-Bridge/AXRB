#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include "digitalis.h"
using namespace axrb::digitalis;
static bool read(void*, uint64_t address, void* out, size_t size) {
    std::memcpy(out, reinterpret_cast<void*>(address), size); return true;
}
static bool write(void*, uint64_t address, const void* value, size_t size) {
    std::memcpy(reinterpret_cast<void*>(address), value, size); return true;
}
int main() try {
    SetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY", "force");
    auto* optimized = create({nullptr, read, write});
    SetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY", nullptr);
    SetEnvironmentVariableA("AXRB_DIGITALIS_INTERPRET_ONLY", "1");
    auto* reference = create({nullptr, read, write});
    SetEnvironmentVariableA("AXRB_DIGITALIS_INTERPRET_ONLY", nullptr);
    constexpr uint32_t instructions[] = {
        0x8b010000, 0xcb020021, 0x9b017c02, 0xca010000, 0xaa020023,
        0xab010003, 0xeb020024, 0x9a030005, 0x8a020026,
        0xdac01003, 0xdac00003, 0xd3475004, 0x9ac12404, 0x9ac12c04,
        0x1e222820, 0x4e22cc20, 0x1f010803,
    };
    alignas(4096) std::array<uint32_t, 1024> code{};
    uint64_t seed = 0x825416351921a975;
    auto random = [&] { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
    for (unsigned trial = 0; trial != 1000; ++trial) {
        clear_cache(optimized); clear_cache(reference);
        code.fill(0xd4000041);
        for (unsigned i = 0; i != 24; ++i) code[i] = instructions[random() % std::size(instructions)];
        Registers expected{};
        for (auto& x : expected.x) x = random();
        for (auto& v : expected.v) {
            for (auto& word : v) {
                const uint32_t lo = 0x3e800000 | (random() & 0x7fffff);
                const uint32_t hi = 0x3e800000 | (random() & 0x7fffff);
                word = (uint64_t(hi) << 32) | lo;
            }
        }
        expected.pc = reinterpret_cast<uint64_t>(code.data());
        expected.pstate = uint32_t(random()) & 0xf0000000;
        Registers actual = expected;
        const auto a = run(reference, &expected, 1000);
        const auto b = run(optimized, &actual, 1000);
        if (a.reason != Stop::Svc || b.reason != a.reason || std::memcmp(&expected, &actual, sizeof actual)) {
            std::fprintf(stderr, "DIGITALIS_DIFFERENTIAL_FAIL trial=%u stop=%u/%u flags=%08x/%08x fpsr=%08x/%08x\n",
                trial, unsigned(a.reason), unsigned(b.reason), expected.pstate, actual.pstate, expected.fpsr, actual.fpsr);
            for (auto word : code) { if (word == 0xd4000041) break; std::fprintf(stderr, "%08x ", word); }
            std::fputc('\n', stderr);
            return 1;
        }
    }
    for (unsigned trial = 0; trial != 100; ++trial) {
        clear_cache(optimized); clear_cache(reference);
        alignas(16) std::array<uint64_t, 32> memory{}, initial{};
        for (auto& value : initial) value = random();
        const unsigned iterations = 1 + random() % memory.size();
        code.fill(0xd4000041);
        const uint32_t loop[] = {
            0xd2800000 | (iterations << 5), // mov x0,iterations
            0xf9400122, // ldr x2,[x9]
            0x8b010042, // add x2,x2,x1
            0xf9000122, // str x2,[x9]
            0x91002129, // add x9,x9,8
            0xf1000400, // subs x0,x0,1
            0x54ffff61, // b.ne to ldr (backedge)
        };
        std::copy(std::begin(loop), std::end(loop), code.begin());
        Registers expected{};
        expected.pc = reinterpret_cast<uint64_t>(code.data());
        expected.x[1] = random();
        expected.x[9] = reinterpret_cast<uint64_t>(memory.data());
        Registers actual = expected;
        memory = initial;
        const auto a = run(reference, &expected, 1000);
        const auto expected_memory = memory;
        memory = initial;
        const auto b = run(optimized, &actual, 1000);
        if (a.reason != Stop::Svc || b.reason != a.reason ||
            std::memcmp(&expected, &actual, sizeof actual) || memory != expected_memory) {
            std::fprintf(stderr, "DIGITALIS_LOOP_DIFFERENTIAL_FAIL trial=%u iterations=%u\n", trial, iterations);
            return 1;
        }
    }
    // A loop made hot normally: optimized in the background and installed by
    // a later run, then compared with the interpreter.
    unsigned long long promoted_count = 0;
    {
        auto* promoted = create({nullptr, read, write});
        alignas(16) std::array<uint64_t, 32> memory{}, initial{};
        code.fill(0xd4000041);
        const uint32_t loop[] = {
            0xf9400122, // ldr x2,[x9]
            0x8b010042, // add x2,x2,x1
            0xf9000122, // str x2,[x9]
            0x91002129, // add x9,x9,8
            0xf1000400, // subs x0,x0,1
            0x54ffff61, // b.ne to ldr (backedge)
        };
        std::copy(std::begin(loop), std::end(loop), code.begin());
        clear_cache(reference);
        auto start = [&](unsigned iterations) {
            Registers registers{};
            registers.pc = reinterpret_cast<uint64_t>(code.data());
            registers.x[0] = iterations;
            registers.x[1] = random();
            registers.x[9] = reinterpret_cast<uint64_t>(memory.data());
            return registers;
        };
        const auto deadline = GetTickCount64() + 10000;
        while (!optimized_blocks(promoted) && GetTickCount64() < deadline) {
            Registers registers = start(32);
            run(promoted, &registers, 1000);
            Sleep(0);
        }
        if (!optimized_blocks(promoted)) {
            std::fprintf(stderr, "DIGITALIS_PROMOTION_FAIL: no optimized code was installed\n");
            return 1;
        }
        for (unsigned trial = 0; trial != 200; ++trial) {
            for (auto& value : initial) value = random();
            Registers expected = start(1 + random() % memory.size());
            Registers actual = expected;
            memory = initial;
            const auto a = run(reference, &expected, 1000);
            const auto expected_memory = memory;
            memory = initial;
            const auto b = run(promoted, &actual, 1000);
            if (a.reason != Stop::Svc || b.reason != a.reason ||
                std::memcmp(&expected, &actual, sizeof actual) || memory != expected_memory) {
                std::fprintf(stderr, "DIGITALIS_PROMOTION_DIFFERENTIAL_FAIL trial=%u stop=%u/%u pc=%llx/%llx x0=%llx/%llx "
                    "x2=%llx/%llx x9=%llx/%llx nzcv=%08x/%08x memory=%d\n", trial, unsigned(a.reason), unsigned(b.reason),
                    (unsigned long long)expected.pc, (unsigned long long)actual.pc,
                    (unsigned long long)expected.x[0], (unsigned long long)actual.x[0],
                    (unsigned long long)expected.x[2], (unsigned long long)actual.x[2],
                    (unsigned long long)expected.x[9], (unsigned long long)actual.x[9], expected.pstate, actual.pstate,
                    memory == expected_memory);
                return 1;
            }
        }
        promoted_count = optimized_blocks(promoted);
        destroy(promoted);
    }
    // Pairwise FP max/min (FMAXP/FMINP/FMAXNMP/FMINNMP .2S/.4S/.2D) in the
    // lite tier, including NaN, signed-zero and infinite lanes.
    SetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY", "0");
    auto* lite = create({nullptr, read, write});
    SetEnvironmentVariableA("AXRB_DIGITALIS_HEAVY", nullptr);
    constexpr uint32_t pairwise[] = {
        0x2e22f420, 0x6e22f420, 0x6e62f420, 0x2ea2f420, 0x6ea2f420, 0x6ee2f420,
        0x2e22c420, 0x6e22c420, 0x6e62c420, 0x2ea2c420, 0x6ea2c420, 0x6ee2c420,
        // The element-wise forms share the lowering.
        0x0e22f420, 0x4e22f420, 0x4e62f420, 0x0ea2f420, 0x4ea2f420, 0x4ee2f420,
        0x0e22c420, 0x4e22c420, 0x4e62c420, 0x0ea2c420, 0x4ea2c420, 0x4ee2c420,
    };
    constexpr uint32_t singles[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                                    0x7fc00000, 0x7fa00001, 0xffc00123, 0x40490fdb};
    constexpr uint64_t doubles[] = {0, 0x8000000000000000, 0x3ff0000000000000, 0xbff0000000000000,
                                    0x7ff0000000000000, 0xfff0000000000000, 0x7ff8000000000000,
                                    0x7ff4000000000001, 0x400921fb54442d18};
    for (unsigned trial = 0; trial != 2000; ++trial) {
        clear_cache(lite); clear_cache(reference);
        code.fill(0xd4000041);
        code[0] = pairwise[trial % std::size(pairwise)];
        Registers expected{};
        const bool is_double = (code[0] >> 22) & 1;
        for (int reg : {1, 2}) {
            for (auto& word : expected.v[reg]) {
                if (is_double) {
                    word = random() % 4 ? doubles[random() % std::size(doubles)] : random();
                } else {
                    const uint32_t lo = random() % 4 ? singles[random() % std::size(singles)] : uint32_t(random());
                    const uint32_t hi = random() % 4 ? singles[random() % std::size(singles)] : uint32_t(random());
                    word = (uint64_t(hi) << 32) | lo;
                }
            }
        }
        expected.v[0][0] = random(); expected.v[0][1] = random();
        expected.pc = reinterpret_cast<uint64_t>(code.data());
        Registers actual = expected;
        const auto a = run(reference, &expected, 1000);
        const auto b = run(lite, &actual, 1000);
        // Like the element-wise FMAX/FMIN lowering, a NaN result is a valid
        // NaN whose payload may differ from the interpreter's default NaN.
        bool lanes_match = true, nan_result = false;
        for (int half = 0; half != 2; ++half) {
            const uint64_t want = expected.v[0][half], got = actual.v[0][half];
            if (is_double) {
                const auto nan = [](uint64_t x) { return (x & 0x7ff0000000000000) == 0x7ff0000000000000 && (x << 12); };
                lanes_match = lanes_match && (want == got || (nan(want) && nan(got)));
                nan_result = nan_result || nan(want);
            } else {
                for (int shift : {0, 32}) {
                    const uint32_t w = uint32_t(want >> shift), g = uint32_t(got >> shift);
                    const auto nan = [](uint32_t x) { return (x & 0x7f800000) == 0x7f800000 && (x & 0x7fffff); };
                    lanes_match = lanes_match && (w == g || (nan(w) && nan(g)));
                    nan_result = nan_result || nan(w);
                }
            }
        }
        Registers compared = actual;
        compared.v[0][0] = expected.v[0][0]; compared.v[0][1] = expected.v[0][1];
        // x86 MINPS/MAXPS also signal invalid for quiet NaNs; ARM only for
        // signaling NaNs. Only IOC may differ, and only for NaN results.
        if (nan_result) compared.fpsr = (compared.fpsr & ~1u) | (expected.fpsr & 1u);
        if (a.reason != Stop::Svc || b.reason != a.reason || !lanes_match ||
            std::memcmp(&expected, &compared, sizeof compared)) {
            std::fprintf(stderr, "DIGITALIS_PAIRWISE_FAIL trial=%u insn=%08x v1=%016llx:%016llx v2=%016llx:%016llx "
                "want=%016llx:%016llx got=%016llx:%016llx fpsr=%08x/%08x\n", trial, code[0],
                (unsigned long long)actual.v[1][1], (unsigned long long)actual.v[1][0],
                (unsigned long long)actual.v[2][1], (unsigned long long)actual.v[2][0],
                (unsigned long long)expected.v[0][1], (unsigned long long)expected.v[0][0],
                (unsigned long long)actual.v[0][1], (unsigned long long)actual.v[0][0], expected.fpsr, actual.fpsr);
            return 1;
        }
    }
    if (interpreted_instructions(lite)) {
        std::fprintf(stderr, "DIGITALIS_PAIRWISE_FAIL: lite tier interpreted %llu instructions\n",
            static_cast<unsigned long long>(interpreted_instructions(lite)));
        return 1;
    }
    destroy(lite);
    if (!optimized_blocks(optimized)) return 2;
    std::printf("DIGITALIS_DIFFERENTIAL_PASS arithmetic=1000 memory_loops=100 pairwise=2000 promoted=200 "
        "optimized=%llu/%llu\n", static_cast<unsigned long long>(optimized_blocks(optimized)), promoted_count);
    destroy(reference); destroy(optimized);
} catch (const std::exception& error) {
    std::fprintf(stderr, "DIGITALIS_DIFFERENTIAL_ERROR: %s\n", error.what()); return 1;
}
