#pragma once
// Native acceleration of hot ARM64 guest kernels.
//
// This code runs natively in the game process (it is part of the x86-64 Vulkan
// layer that Android loads into the game). The ARM64 translator already routes
// guest calls to host code for its proxy libraries; its exported
// MakeTrampolineCallable makes any guest address such an entry. For each
// guest function whose machine code matches a known kernel word for word,
// execution is redirected to an equivalent native implementation that reads
// the arguments from the guest registers and returns to the guest's link
// register. Nothing is written to the application's files or memory.
//
// Both the stock Google translator and Berberis-based builds export the same
// entry points. Anything unexpected (unknown translator, missing symbol, no FMA
// on the host, no matching kernel) leaves the game untouched.
// debug.axrb.guest_accel=0 disables the replacement.
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/system_properties.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include "unity_skinning.h"
#include "unity_skinning_signatures.h"

namespace axrb::accel {

struct TranslatorApi {
    using Trampoline = void (*)(const void*, void*);
    void (*makeTrampolineCallable)(uintptr_t, bool, Trampoline, const void*, const char*) = nullptr;
    void (*invalidateGuestRange)(uintptr_t, uintptr_t) = nullptr;
    size_t (*regOffset)(int) = nullptr;
    const char* name = nullptr;
};

inline TranslatorApi g_api;

inline size_t g_regOffset[31]{};

// Resolves a dynamic symbol from a loaded module's own tables. dlsym cannot be
// used: the translator lives in a linker namespace the layer cannot open.
inline const void* elf_symbol(const dl_phdr_info* info, const char* wanted) {
    const ElfW(Dyn)* dynamic = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dynamic = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dynamic) return nullptr;
    const ElfW(Sym)* symbols = nullptr;
    const char* strings = nullptr;
    const uint32_t* gnuHash = nullptr;
    const uint32_t* sysvHash = nullptr;
    // Android's linker relocates these d_ptr values to absolute addresses.
    auto address = [info](ElfW(Addr) value) {
        return value >= info->dlpi_addr ? value : value + info->dlpi_addr;
    };
    for (auto* d = dynamic; d->d_tag != DT_NULL; ++d) {
        if (d->d_tag == DT_SYMTAB) symbols = reinterpret_cast<const ElfW(Sym)*>(address(d->d_un.d_ptr));
        else if (d->d_tag == DT_STRTAB) strings = reinterpret_cast<const char*>(address(d->d_un.d_ptr));
        else if (d->d_tag == DT_GNU_HASH) gnuHash = reinterpret_cast<const uint32_t*>(address(d->d_un.d_ptr));
        else if (d->d_tag == DT_HASH) sysvHash = reinterpret_cast<const uint32_t*>(address(d->d_un.d_ptr));
    }
    if (!symbols || !strings) return nullptr;
    auto match = [&](uint32_t index) -> const void* {
        const ElfW(Sym)& s = symbols[index];
        if (s.st_shndx == SHN_UNDEF || !s.st_value || std::strcmp(strings + s.st_name, wanted)) return nullptr;
        return reinterpret_cast<const void*>(info->dlpi_addr + s.st_value);
    };
    if (gnuHash) {
        const uint32_t buckets = gnuHash[0], offset = gnuHash[1], bloomWords = gnuHash[2];
        const uint32_t* bucket = gnuHash + 4 + bloomWords * (sizeof(ElfW(Addr)) / 4);
        const uint32_t* chain = bucket + buckets;
        uint32_t h = 5381;
        for (const char* c = wanted; *c; ++c) h = h * 33 + static_cast<unsigned char>(*c);
        for (uint32_t i = bucket[h % buckets]; i >= offset; ++i) {
            const uint32_t entry = chain[i - offset];
            if ((entry | 1) == (h | 1))
                if (const void* found = match(i)) return found;
            if (entry & 1) break;
        }
        return nullptr;
    }
    if (sysvHash) {
        for (uint32_t i = 0; i < sysvHash[1]; ++i)
            if (const void* found = match(i)) return found;
    }
    return nullptr;
}

inline TranslatorApi find_translator() {
    TranslatorApi api;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) {
        auto* api = static_cast<TranslatorApi*>(data);
        const char* name = info->dlpi_name ? info->dlpi_name : "";
        const char* base = std::strrchr(name, '/');
        base = base ? base + 1 : name;
        if (std::strcmp(base, "libndk_translation.so") && std::strcmp(base, "libberberis_arm64.so")) return 0;
        api->name = base;
        api->makeTrampolineCallable = reinterpret_cast<decltype(api->makeTrampolineCallable)>(const_cast<void*>(
            elf_symbol(info, "_ZN8berberis22MakeTrampolineCallableEmbPFvPKvPNS_11ThreadStateEES1_PKc")));
        api->invalidateGuestRange = reinterpret_cast<decltype(api->invalidateGuestRange)>(const_cast<void*>(
            elf_symbol(info, "_ZN8berberis20InvalidateGuestRangeEmm")));
        api->regOffset = reinterpret_cast<decltype(api->regOffset)>(const_cast<void*>(
            elf_symbol(info, "_ZN8berberis23GetThreadStateRegOffsetEi")));
        return 1;
    }, &api);
    return api;
}

inline uint64_t& guest_x(void* state, int reg) {
    return *reinterpret_cast<uint64_t*>(static_cast<char*>(state) + g_regOffset[reg]);
}

struct KernelStats {
    const char* name;
    std::atomic<uint64_t> calls{0}, vertices{0};
};

inline std::atomic<int64_t> g_nextReport{0};
inline KernelStats* g_statsBegin = nullptr;
inline KernelStats* g_statsEnd = nullptr;

inline void report_if_due() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t ns = int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
    int64_t due = g_nextReport.load(std::memory_order_relaxed);
    if (ns < due || !g_nextReport.compare_exchange_strong(due, ns + 10000000000ll)) return;
    char line[512];
    int used = std::snprintf(line, sizeof(line), "native kernels (cumulative):");
    for (auto* s = g_statsBegin; s != g_statsEnd && used < int(sizeof(line)); ++s) {
        const uint64_t calls = s->calls.load(std::memory_order_relaxed);
        if (calls) used += std::snprintf(line + used, sizeof(line) - used, " %s calls=%llu vertices=%llu", s->name,
            static_cast<unsigned long long>(calls), static_cast<unsigned long long>(s->vertices.load(std::memory_order_relaxed)));
    }
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Accel", "%s", line);
}

template <int Bones, bool Normal, bool Tangent>
struct UnitySkin {
    static inline KernelStats* stats = nullptr;
    static void trampoline(const void*, void* state) {
        const uint64_t input = guest_x(state, 1), end = guest_x(state, 2);
        const SkinResult r = skin_unity_2021<Bones, Normal, Tangent>(guest_x(state, 0), input, end,
                                                                       guest_x(state, 3), guest_x(state, 4));
        // Only caller-saved registers change; keep the pointers the guest leaves.
        guest_x(state, 1) = r.input;
        guest_x(state, 4) = r.output;
        constexpr uint64_t stride = 12 + (Normal ? 12 : 0) + (Tangent ? 16 : 0);
        if (const uint64_t n = stats->calls.fetch_add(1, std::memory_order_relaxed); (n & 1023) == 0) report_if_due();
        stats->vertices.fetch_add(end > input ? (end - input) / stride : 1, std::memory_order_relaxed);
    }
};

struct KernelSpec {
    const char* name;
    const uint32_t* words;
    size_t count;
    TranslatorApi::Trampoline trampoline;
    KernelStats** stats;
};

inline bool host_has_fma() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("fma") && __builtin_cpu_supports("sse4.1");
}

inline KernelStats g_stats[] = {{"skin1p"}, {"skin1pn"}, {"skin1pnt"}, {"skin2p"}, {"skin2pn"}, {"skin2pnt"},
                                {"skin4p"}, {"skin4pn"}, {"skin4pnt"}};

// Calls found(spec, pc) for every place the spec's code occurs in a readable
// file mapping whose path ends with suffix. Guest libraries are mapped from
// their files, so this finds their code without touching them.
template <typename Found>
inline void scan_guest_code(const char* suffix, const KernelSpec* specs, size_t count, Found found) {
    FILE* maps = std::fopen("/proc/self/maps", "re");
    if (!maps) return;
    const size_t suffixLength = std::strlen(suffix);
    char line[1024];
    while (std::fgets(line, sizeof(line), maps)) {
        unsigned long long begin = 0, end = 0;
        char perms[8]{};
        int path = 0;
        if (std::sscanf(line, "%llx-%llx %7s %*s %*s %*s %n", &begin, &end, perms, &path) < 3 || perms[0] != 'r') continue;
        const char* file = line + path;
        const size_t length = std::strcspn(file, "\n");
        if (length < suffixLength || std::strncmp(file + length - suffixLength, suffix, suffixLength)) continue;
        const auto* words = reinterpret_cast<const uint32_t*>(begin);
        const size_t total = (end - begin) / 4;
        for (size_t w = 0; w < total; ++w)
            for (size_t k = 0; k < count; ++k)
                if (words[w] == specs[k].words[0] && w + specs[k].count <= total &&
                    !std::memcmp(words + w, specs[k].words, specs[k].count * 4))
                    found(specs[k], reinterpret_cast<uintptr_t>(words + w));
    }
    std::fclose(maps);
}

inline void wrap_guest_code(const KernelSpec& spec, uintptr_t pc) {
    // Drop any translation made before this point, then wrap.
    g_api.invalidateGuestRange(pc, pc + spec.count * 4);
    g_api.makeTrampolineCallable(pc, false, spec.trampoline, nullptr, spec.name);
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Accel", "%s: %s at %p -> native", g_api.name, spec.name,
                        reinterpret_cast<void*>(pc));
}

inline bool prepare_guest_accel() {
    char value[PROP_VALUE_MAX]{};
    __system_property_get("debug.axrb.guest_accel", value);
    if (!std::strcmp(value, "0")) {
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Accel", "disabled by debug.axrb.guest_accel=0");
        return false;
    }
    if (!host_has_fma()) {
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Accel", "host CPU lacks FMA; guest kernels unchanged");
        return false;
    }
    g_api = find_translator();
    if (!g_api.name) return false; // Not a translated process: nothing to accelerate.
    if (!g_api.makeTrampolineCallable || !g_api.invalidateGuestRange || !g_api.regOffset) {
        __android_log_print(ANDROID_LOG_WARN, "AXRB.Accel", "%s lacks the wrapping entry points; unchanged", g_api.name);
        return false;
    }
    for (int i = 0; i < 31; ++i) g_regOffset[i] = g_api.regOffset(i);
    g_statsBegin = g_stats;
    g_statsEnd = g_stats + sizeof(g_stats) / sizeof(g_stats[0]);
    return true;
}

// Unity's CPU skinning kernels in the engine library (libunity.so).
inline int install_skinning() {
    using namespace unity2021;
    KernelStats* stats = g_stats;
#define AXRB_KERNEL(i, arr, b, n, t) \
    {stats[i].name, arr, sizeof(arr) / 4, &UnitySkin<b, n, t>::trampoline, &UnitySkin<b, n, t>::stats}
    const KernelSpec specs[] = {
        AXRB_KERNEL(0, kSkin1P, 1, false, false), AXRB_KERNEL(1, kSkin1PN, 1, true, false), AXRB_KERNEL(2, kSkin1PNT, 1, true, true),
        AXRB_KERNEL(3, kSkin2P, 2, false, false), AXRB_KERNEL(4, kSkin2PN, 2, true, false), AXRB_KERNEL(5, kSkin2PNT, 2, true, true),
        AXRB_KERNEL(6, kSkin4P, 4, false, false), AXRB_KERNEL(7, kSkin4PN, 4, true, false), AXRB_KERNEL(8, kSkin4PNT, 4, true, true),
    };
#undef AXRB_KERNEL
    for (int i = 0; i < 9; ++i) *specs[i].stats = &stats[i];
    int installed = 0;
    scan_guest_code("/libunity.so", specs, 9, [&](const KernelSpec& spec, uintptr_t pc) {
        wrap_guest_code(spec, pc);
        ++installed;
    });
    return installed;
}

inline void install_guest_accel_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (prepare_guest_accel() && !install_skinning())
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Accel", "%s: no Unity skinning kernels found", g_api.name);
    });
}

} // namespace axrb::accel
