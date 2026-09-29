#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <dynarmic/interface/A64/a64.h>
#include <dynarmic/interface/exclusive_monitor.h>

using U64 = std::uint64_t;
using Vec = Dynarmic::A64::Vector;
using Clock = std::chrono::steady_clock;
constexpr U64 Code = 0x1000, Data = 0x8000;
constexpr U64 Seed = 0x123456789abcdef0, Multiplier = 0x2545f4914f6cdd1d;

struct Environment final : Dynarmic::A64::UserCallbacks {
    std::array<std::uint8_t, 65536> memory{};
    std::array<void*, 16> pages{};
    Dynarmic::ExclusiveMonitor monitor{1};
    std::unique_ptr<Dynarmic::A64::Jit> cpu;
    U64 ticks = 0;
    bool completed = false;
    bool unsupported_instruction = false;
    std::string failure;
    std::size_t code_size = 0;

    void fail(const std::string& message) {
        if (failure.empty()) failure = message;
        cpu->HaltExecution();
    }
    bool valid(U64 address, std::size_t length) {
        if (length > memory.size() || address > memory.size() - length) {
            fail("out-of-range memory access");
            return false;
        }
        return true;
    }
    template<class T> T read(U64 address) {
        T value{};
        if (valid(address, sizeof(T))) std::memcpy(&value, memory.data() + address, sizeof(T));
        return value;
    }
    template<class T> void write(U64 address, T value) {
        if (valid(address, sizeof(T))) std::memcpy(memory.data() + address, &value, sizeof(T));
    }
    std::optional<std::uint32_t> MemoryReadCode(U64 address) override {
        if (address < Code || address >= Code + code_size || (address & 3)) return std::nullopt;
        return read<std::uint32_t>(address);
    }
    std::uint8_t MemoryRead8(U64 a) override { return read<std::uint8_t>(a); }
    std::uint16_t MemoryRead16(U64 a) override { return read<std::uint16_t>(a); }
    std::uint32_t MemoryRead32(U64 a) override { return read<std::uint32_t>(a); }
    U64 MemoryRead64(U64 a) override { return read<U64>(a); }
    Vec MemoryRead128(U64 a) override { return read<Vec>(a); }
    void MemoryWrite8(U64 a, std::uint8_t v) override { write(a, v); }
    void MemoryWrite16(U64 a, std::uint16_t v) override { write(a, v); }
    void MemoryWrite32(U64 a, std::uint32_t v) override { write(a, v); }
    void MemoryWrite64(U64 a, U64 v) override { write(a, v); }
    void MemoryWrite128(U64 a, Vec v) override { write(a, v); }
    // This harness runs one guest CPU. These callbacks are deliberately not
    // presented as a valid implementation for concurrent guest threads.
    template<class T> bool exclusive(U64 a, T value, T expected) {
        if (!valid(a, sizeof(T)) || read<T>(a) != expected) return false;
        write(a, value);
        return true;
    }
    bool MemoryWriteExclusive8(U64 a, std::uint8_t v, std::uint8_t e) override { return exclusive(a,v,e); }
    bool MemoryWriteExclusive16(U64 a, std::uint16_t v, std::uint16_t e) override { return exclusive(a,v,e); }
    bool MemoryWriteExclusive32(U64 a, std::uint32_t v, std::uint32_t e) override { return exclusive(a,v,e); }
    bool MemoryWriteExclusive64(U64 a, U64 v, U64 e) override { return exclusive(a,v,e); }
    bool MemoryWriteExclusive128(U64 a, Vec v, Vec e) override { return exclusive(a,v,e); }
    void InterpreterFallback(U64 pc, std::size_t) override {
        unsupported_instruction = true;
        fail("interpreter fallback at " + std::to_string(pc));
    }
    void CallSVC(std::uint32_t immediate) override {
        if (immediate != 0) { fail("unexpected SVC"); return; }
        completed = true;
        cpu->HaltExecution();
    }
    void ExceptionRaised(U64 pc, Dynarmic::A64::Exception e) override {
        unsupported_instruction = e == Dynarmic::A64::Exception::UnallocatedEncoding;
        fail("guest exception " + std::to_string(static_cast<int>(e)) + " at " + std::to_string(pc));
    }
    void AddTicks(U64 count) override { ticks -= std::min(ticks, count); }
    U64 GetTicksRemaining() override { return ticks; }
    U64 GetCNTPCT() override { return 0; }

    Environment(const std::filesystem::path& binary, bool page_table) {
        std::ifstream file(binary, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("cannot open " + binary.string());
        const auto length = file.tellg();
        if (length <= 0 || length > static_cast<std::streamoff>(Data - Code))
            throw std::runtime_error("invalid guest binary size");
        code_size = static_cast<std::size_t>(length);
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(memory.data() + Code), length))
            throw std::runtime_error("cannot read guest binary");
        Dynarmic::A64::UserConfig config{};
        config.callbacks = this;
        config.global_monitor = &monitor;
        config.code_cache_size = 16 * 1024 * 1024;
        if (page_table) {
            // Only data pages get direct access. Everything else uses checked callbacks.
            for (std::size_t i = Data / 4096; i < pages.size(); ++i) pages[i] = memory.data() + i * 4096;
            config.page_table = pages.data();
            config.page_table_address_space_bits = 16;
            config.silently_mirror_page_table = false;
            config.detect_misaligned_access_via_page_table = 8 | 16 | 32 | 64 | 128;
            config.only_detect_misalignment_via_page_table_on_page_boundary = true;
        }
        cpu = std::make_unique<Dynarmic::A64::Jit>(config);
    }
    void prepare(U64 offset, U64 iterations) {
        // Reset() also clears pending cache-invalidation flags in this revision.
        // Reset architectural state explicitly so queued invalidations survive.
        cpu->SetRegisters({});
        cpu->SetVectors({});
        cpu->SetSP(0);
        cpu->SetPstate(0);
        cpu->SetFpcr(0);
        cpu->SetFpsr(0);
        cpu->ClearHalt();
        cpu->ClearExclusiveState();
        monitor.Clear();
        failure.clear();
        completed = false;
        unsupported_instruction = false;
        ticks = iterations * 64 + 1024;
        cpu->SetPC(Code + offset);
        cpu->SetRegister(1, iterations);
    }
    double run() {
        const auto start = Clock::now();
        cpu->Run();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (!completed && failure.empty()) failure = "instruction budget exhausted";
        return ms;
    }
    void require_success() const {
        if (!failure.empty()) throw std::runtime_error(failure);
        if (!completed) throw std::runtime_error("guest did not finish");
    }
};

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main(int argc, char** argv) try {
    U64 iterations = 1000000;
    bool page_table = false, require_lse = false;
    auto binary = std::filesystem::absolute(argv[0]).parent_path() / "guest.bin";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--page-table") page_table = true;
        else if (arg == "--require-lse") require_lse = true;
        else if (arg == "--iterations" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::size_t used = 0;
            iterations = std::stoull(value, &used);
            check(used == value.size(), "invalid iteration count");
        } else throw std::runtime_error("usage: axrb-dynarmic-probe [--iterations 1..100000000] [--page-table] [--require-lse]");
    }
    check(iterations >= 1 && iterations <= 100000000, "iteration count out of range");
    Environment env(binary, page_table);
    std::cout << "Dynarmic ARM64 -> x64; safe optimizations; memory="
              << (page_table ? "page-table" : "callbacks") << "; iterations=" << iterations << '\n';
    std::cout << "Microbenchmark only; no Android framework or game FPS comparison.\n";
    U64 expected_integer = Seed;
    for (U64 i = 0; i < iterations; ++i) {
        expected_integer ^= expected_integer >> 12;
        expected_integer ^= expected_integer << 25;
        expected_integer ^= expected_integer >> 27;
        expected_integer *= Multiplier;
    }
    std::array<U64, 1024> expected_memory{};
    U64 expected_checksum = 0;
    for (U64 i = iterations; i; --i) {
        expected_memory[i & 1023] += i;
        expected_checksum ^= expected_memory[i & 1023];
    }
    for (int workload = 0; workload < 3; ++workload) {
        env.cpu->ClearCache();
        std::vector<double> warm;
        double cold = 0;
        for (int repetition = 0; repetition < 6; ++repetition) {
            env.prepare(workload * 0x100, iterations);
            std::fill(env.memory.begin() + Data, env.memory.end(), 0);
            env.cpu->SetRegister(0, workload == 0 ? Seed : Data);
            env.cpu->SetRegister(2, workload == 0 ? Multiplier : 0);
            env.cpu->SetVector(0, {0, 0});
            env.cpu->SetVector(1, {0x0000000200000001ULL, 0x0000000400000003ULL});
            const double ms = env.run();
            env.require_success();
            if (workload == 0) check(env.cpu->GetRegister(0) == expected_integer, "integer checksum mismatch");
            if (workload == 1) {
                check(env.cpu->GetRegister(2) == expected_checksum, "memory checksum mismatch");
                check(std::memcmp(env.memory.data() + Data, expected_memory.data(), sizeof(expected_memory)) == 0, "memory contents mismatch");
            }
            if (workload == 2) {
                auto lanes = std::bit_cast<std::array<std::uint32_t, 4>>(env.cpu->GetVector(0));
                for (unsigned lane = 0; lane < 4; ++lane)
                    check(lanes[lane] == static_cast<std::uint32_t>(iterations * (lane + 1)), "SIMD mismatch");
            }
            if (!repetition) cold = ms; else warm.push_back(ms);
        }
        std::sort(warm.begin(), warm.end());
        std::cout << "PASS " << std::array{"integer", "memory", "SIMD"}[workload]
                  << " cold_ms=" << cold << " warm_median_ms=" << warm[2]
                  << " warm_min_ms=" << warm.front() << " warm_max_ms=" << warm.back() << '\n';
    }
    env.prepare(0x300, 10000);
    env.write<U64>(Data, 0);
    env.cpu->SetRegister(0, Data);
    env.run(); env.require_success();
    check(env.read<U64>(Data) == 10000, "exclusive-access mismatch");
    std::cout << "PASS LDAXR/STLXR (single guest CPU)\n";
    env.prepare(0x700, 1);
    env.cpu->SetVector(0, {std::bit_cast<U64>(1.5), 0});
    env.cpu->SetVector(1, {std::bit_cast<U64>(2.0), 0});
    env.run(); env.require_success();
    check(std::bit_cast<double>(env.cpu->GetVector(0)[0]) == 7.0, "floating-point mismatch");
    std::cout << "PASS floating-point\n";
    env.prepare(0x800, 1); env.run(); env.require_success();
    check(env.cpu->GetRegister(0) == 1, "initial code mismatch");
    env.write<std::uint32_t>(Code + 0x800, 0xd2800040); // mov x0, #2
    env.cpu->InvalidateCacheRange(Code + 0x800, 4);
    env.prepare(0x800, 1); env.run(); env.require_success();
    check(env.cpu->GetRegister(0) == 2, "code invalidation mismatch");
    std::cout << "PASS code-cache invalidation\n";
    int unsupported = 0;
    for (int op = 0; op < 3; ++op) {
        env.prepare(0x400 + op * 0x100, 1);
        env.write<U64>(Data, 7);
        env.cpu->SetRegister(0, Data);
        env.cpu->SetRegister(1, 7);
        env.cpu->SetRegister(2, 11);
        env.run();
        const auto name = std::array{"CASAL", "LDADDAL", "SWPAL"}[op];
        if (!env.failure.empty()) {
            if (!env.unsupported_instruction) throw std::runtime_error(env.failure);
            ++unsupported;
            std::cout << "UNSUPPORTED " << name << ": " << env.failure << '\n';
        } else {
            check(env.read<U64>(Data) == (op == 0 ? 11 : op == 1 ? 14 : 7), "LSE memory mismatch");
            check(env.cpu->GetRegister(op == 0 ? 1 : 2) == 7, "LSE result mismatch");
            std::cout << "PASS " << name << '\n';
        }
    }
    std::cout << "LSE_supported=" << 3 - unsupported << "/3; interpreter fallback is never emulated.\n";
    return require_lse && unsupported ? 2 : 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
