// Runs every Unity 2021 skinning kernel on deterministic inputs and writes the
// output bytes. Built for arm64 it executes Unity's original machine code;
// built for x86_64 it executes AXRB's native replacement. Identical output
// files prove the replacement is bit-exact (including trailing store widths).
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include "../../runtime/vulkan/guest_accel/unity_skinning_signatures.h"
#if defined(__x86_64__)
#include "../../runtime/vulkan/guest_accel/unity_skinning.h"
#endif

namespace {
using namespace axrb::accel::unity2021;
struct Variant { const char* name; const uint32_t* code; size_t words; int bones; bool normal, tangent; };
#define AXRB_VARIANT(n, b, nor, tan) {#n, n, sizeof(n) / 4, b, nor, tan}
const Variant kVariants[] = {
    AXRB_VARIANT(kSkin1P, 1, false, false), AXRB_VARIANT(kSkin1PN, 1, true, false), AXRB_VARIANT(kSkin1PNT, 1, true, true),
    AXRB_VARIANT(kSkin2P, 2, false, false), AXRB_VARIANT(kSkin2PN, 2, true, false), AXRB_VARIANT(kSkin2PNT, 2, true, true),
    AXRB_VARIANT(kSkin4P, 4, false, false), AXRB_VARIANT(kSkin4PN, 4, true, false), AXRB_VARIANT(kSkin4PNT, 4, true, true),
};

uint64_t g_state = 0x9e3779b97f4a7c15ull;
uint32_t next() { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return uint32_t(g_state >> 16); }
float value() {
    switch (next() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: { float f; uint32_t bits = next() & 0x807fffffu; std::memcpy(&f, &bits, 4); return f; } // subnormal
    case 4: return float(int32_t(next())) * 1e-3f;
    default: return (float(next()) / 4294967296.0f - 0.5f) * 8.0f;
    }
}

#if defined(__x86_64__)
using namespace axrb::accel;
using Native = SkinResult (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Native native(int bones, bool normal, bool tangent) {
#define AXRB_PICK(b) \
    if (bones == b) return tangent ? skin_unity_2021<b, true, true> : normal ? skin_unity_2021<b, true, false> : skin_unity_2021<b, false, false>;
    AXRB_PICK(1) AXRB_PICK(2) AXRB_PICK(4)
#undef AXRB_PICK
    return nullptr;
}
#endif
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <output>\n", argv[0]); return 2; }
    FILE* out = std::fopen(argv[1], "wb");
    if (!out) return 3;
    constexpr size_t kBones = 64, kGuard = 64;
    uint64_t checks = 0;
    for (const Variant& v : kVariants) {
#if defined(__aarch64__)
        void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) return 4;
        std::memcpy(page, v.code, v.words * 4);
        __builtin___clear_cache(static_cast<char*>(page), static_cast<char*>(page) + v.words * 4);
        auto guest = reinterpret_cast<void (*)(const float*, const uint8_t*, const uint8_t*, const uint8_t*, uint8_t*)>(page);
#endif
        const size_t stride = 12 + (v.normal ? 12 : 0) + (v.tangent ? 16 : 0);
        const size_t influenceStride = v.bones == 1 ? 4 : v.bones * 8;
        for (int round = 0; round < 40; ++round) {
            const size_t count = 1 + (round < 20 ? round : next() % 300);
            std::vector<float> bones(kBones * 16);
            for (float& f : bones) f = value();
            // Guest code prefetches one influence record past the end.
            std::vector<uint8_t> influences((count + 2) * influenceStride + 64);
            for (size_t i = 0; i < count + 2; ++i) {
                uint8_t* record = influences.data() + i * influenceStride;
                if (v.bones == 1) { uint32_t index = next() % kBones; std::memcpy(record, &index, 4); continue; }
                for (int b = 0; b < v.bones; ++b) {
                    float w = value(); uint32_t index = next() % kBones;
                    std::memcpy(record + b * 4, &w, 4);
                    std::memcpy(record + v.bones * 4 + b * 4, &index, 4);
                }
            }
            std::vector<uint8_t> input(count * stride + 64);
            for (size_t i = 0; i < input.size() / 4; ++i) { float f = value(); std::memcpy(input.data() + i * 4, &f, 4); }
            std::vector<uint8_t> output(count * stride + 2 * kGuard, 0xcd);
            uint8_t* first = output.data() + kGuard;
#if defined(__aarch64__)
            guest(bones.data(), input.data(), input.data() + count * stride, influences.data(), first);
#else
            native(v.bones, v.normal, v.tangent)(reinterpret_cast<uintptr_t>(bones.data()), reinterpret_cast<uintptr_t>(input.data()),
                reinterpret_cast<uintptr_t>(input.data() + count * stride), reinterpret_cast<uintptr_t>(influences.data()),
                reinterpret_cast<uintptr_t>(first));
#endif
            std::fwrite(output.data(), 1, output.size(), out);
            ++checks;
        }
        std::printf("%s ok\n", v.name);
    }
    std::fclose(out);
    std::printf("cases=%" PRIu64 "\n", checks);
    return 0;
}
