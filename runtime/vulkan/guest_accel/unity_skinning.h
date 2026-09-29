#pragma once
// Native x86-64 equivalents of Unity's ARM64 CPU skinning kernels.
//
// Unity skins vertices on the CPU with small NEON loops (one per bone count and
// vertex format). Under ARM64 translation these loops dominate the game's CPU
// time, so AXRB runs this host implementation in their place. It reproduces the
// guest code exactly, not approximately: the same multiply and fused
// multiply-add order per lane (FMLA is fused, like VFMADD), the same 16-byte
// stores that the next vertex overwrites, and the same loop exit when the input
// pointer passes the end. Results are therefore bit-identical to the guest.
//
// Arguments are the guest registers at entry:
//   x0 bone matrices (4x4 column-major floats, 64 bytes each)
//   x1 first input vertex, x2 input end
//   x3 bone influences: 1 bone = u32 index; 2 bones = 2 weights + 2 indices;
//      4 bones = 4 weights + 4 indices
//   x4 first output vertex
// Vertex layout (input and output): position xyz, then optionally normal xyz,
// then optionally tangent xyzw.
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace axrb::accel {

struct SkinResult {
    uintptr_t input;
    uintptr_t output;
};

#define AXRB_ACCEL_FMA __attribute__((target("sse4.1,fma")))

AXRB_ACCEL_FMA inline __m128 axrb_load4(uintptr_t address) {
    return _mm_loadu_ps(reinterpret_cast<const float*>(address));
}
AXRB_ACCEL_FMA inline float axrb_load1(uintptr_t address) {
    float value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
AXRB_ACCEL_FMA inline uint32_t axrb_load_u32(uintptr_t address) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
AXRB_ACCEL_FMA inline __m128 axrb_column(uintptr_t bones, uint32_t index, int j) {
    return axrb_load4(bones + uintptr_t(index) * 64 + uintptr_t(j) * 16);
}
AXRB_ACCEL_FMA inline void axrb_store4(uintptr_t address, __m128 value) {
    _mm_storeu_ps(reinterpret_cast<float*>(address), value);
}
AXRB_ACCEL_FMA inline void axrb_store3(uintptr_t address, __m128 value) {
    alignas(16) float lanes[4];
    _mm_store_ps(lanes, value);
    std::memcpy(reinterpret_cast<void*>(address), lanes, 12);
}

template <int Bones, bool Normal, bool Tangent>
AXRB_ACCEL_FMA SkinResult skin_unity_2021(uintptr_t bones, uintptr_t input, uintptr_t end,
                                          uintptr_t influences, uintptr_t output) {
    static_assert(Bones == 1 || Bones == 2 || Bones == 4);
    static_assert(Normal || !Tangent);
    constexpr uintptr_t inputStride = 12 + (Normal ? 12 : 0) + (Tangent ? 16 : 0);
    constexpr uintptr_t influenceStride = Bones == 1 ? 4 : Bones * 8;
    for (;;) {
        __m128 c0, c1, c2, c3;
        if constexpr (Bones == 1) {
            const uint32_t i0 = axrb_load_u32(influences);
            c0 = axrb_column(bones, i0, 0); c1 = axrb_column(bones, i0, 1); c2 = axrb_column(bones, i0, 2); c3 = axrb_column(bones, i0, 3);
        } else {
            const __m128 w0 = _mm_set1_ps(axrb_load1(influences));
            const __m128 w1 = _mm_set1_ps(axrb_load1(influences + 4));
            const uintptr_t indices = influences + Bones * 4;
            const uint32_t i0 = axrb_load_u32(indices), i1 = axrb_load_u32(indices + 4);
            c0 = _mm_mul_ps(axrb_column(bones, i0, 0), w0); c1 = _mm_mul_ps(axrb_column(bones, i0, 1), w0);
            c2 = _mm_mul_ps(axrb_column(bones, i0, 2), w0); c3 = _mm_mul_ps(axrb_column(bones, i0, 3), w0);
            c0 = _mm_fmadd_ps(axrb_column(bones, i1, 0), w1, c0); c1 = _mm_fmadd_ps(axrb_column(bones, i1, 1), w1, c1);
            c2 = _mm_fmadd_ps(axrb_column(bones, i1, 2), w1, c2); c3 = _mm_fmadd_ps(axrb_column(bones, i1, 3), w1, c3);
            if constexpr (Bones == 4) {
                const __m128 w2 = _mm_set1_ps(axrb_load1(influences + 8));
                const __m128 w3 = _mm_set1_ps(axrb_load1(influences + 12));
                const uint32_t i2 = axrb_load_u32(indices + 8), i3 = axrb_load_u32(indices + 12);
                c0 = _mm_fmadd_ps(axrb_column(bones, i2, 0), w2, c0); c1 = _mm_fmadd_ps(axrb_column(bones, i2, 1), w2, c1);
                c2 = _mm_fmadd_ps(axrb_column(bones, i2, 2), w2, c2); c3 = _mm_fmadd_ps(axrb_column(bones, i2, 3), w2, c3);
                c0 = _mm_fmadd_ps(axrb_column(bones, i3, 0), w3, c0); c1 = _mm_fmadd_ps(axrb_column(bones, i3, 1), w3, c1);
                c2 = _mm_fmadd_ps(axrb_column(bones, i3, 2), w3, c2); c3 = _mm_fmadd_ps(axrb_column(bones, i3, 3), w3, c3);
            }
        }
        influences += influenceStride;
        // Position: ((c0*x) + c1*y fused) + c2*z fused, then + c3.
        __m128 p = _mm_mul_ps(c0, _mm_set1_ps(axrb_load1(input)));
        p = _mm_fmadd_ps(c1, _mm_set1_ps(axrb_load1(input + 4)), p);
        p = _mm_fmadd_ps(c2, _mm_set1_ps(axrb_load1(input + 8)), p);
        p = _mm_add_ps(p, c3);
        __m128 n{}, t{};
        if constexpr (Normal) {
            n = _mm_mul_ps(c0, _mm_set1_ps(axrb_load1(input + 12)));
            n = _mm_fmadd_ps(c1, _mm_set1_ps(axrb_load1(input + 16)), n);
            n = _mm_fmadd_ps(c2, _mm_set1_ps(axrb_load1(input + 20)), n);
        }
        if constexpr (Tangent) {
            t = _mm_mul_ps(c0, _mm_set1_ps(axrb_load1(input + 24)));
            t = _mm_fmadd_ps(c1, _mm_set1_ps(axrb_load1(input + 28)), t);
            t = _mm_fmadd_ps(c2, _mm_set1_ps(axrb_load1(input + 32)), t);
            t = _mm_insert_ps(t, _mm_set_ss(axrb_load1(input + 36)), 0x30);
        }
        input += inputStride;
        const bool last = input == end;
        if (!last) {
            // Full 16-byte stores; the following vertex overwrites the spill.
            axrb_store4(output, p); output += 12;
            if constexpr (Normal) { axrb_store4(output, n); output += 12; }
            if constexpr (Tangent) { axrb_store4(output, t); output += 16; }
            if (input < end) continue;
        }
        // Final vertex: the guest narrows only the trailing 16-byte store.
        if constexpr (Tangent) {
            axrb_store4(output, p); output += 12;
            axrb_store4(output, n); output += 12;
            axrb_store4(output, t); output += 16;
        } else if constexpr (Normal) {
            axrb_store4(output, p); output += 12;
            axrb_store3(output, n); output += 12;
        } else {
            axrb_store3(output, p); output += 12;
        }
        return {input, output};
    }
}

#undef AXRB_ACCEL_FMA

} // namespace axrb::accel
