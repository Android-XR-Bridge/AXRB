// Small compiler support routines needed by clang's Windows build of Digitalis.
// No compiler runtime DLL is required on the player's machine.
#include <cstdint>
#include <cstring>
#include <cstdlib>
extern "C" __uint128_t __udivti3(__uint128_t numerator, __uint128_t denominator) {
    if (!denominator) std::abort();
    __uint128_t quotient = 0, remainder = 0;
    for (int bit = 127; bit >= 0; --bit) {
        const bool carry = (remainder >> 127) != 0;
        remainder = (remainder << 1) | ((numerator >> bit) & 1);
        if (carry || remainder >= denominator) { remainder -= denominator; quotient |= __uint128_t{1} << bit; }
    }
    return quotient;
}
extern "C" float __extendhfsf2(_Float16 value) {
    uint16_t half; std::memcpy(&half, &value, sizeof half);
    uint32_t sign = uint32_t(half & 0x8000) << 16;
    uint32_t exponent = (half >> 10) & 31, mantissa = half & 1023, bits;
    if (!exponent) {
        if (!mantissa) bits = sign;
        else {
            int shift = 0;
            while (!(mantissa & 1024)) { mantissa <<= 1; ++shift; }
            bits = sign | (uint32_t(113 - shift) << 23) | ((mantissa & 1023) << 13);
        }
    } else if (exponent == 31) bits = sign | 0x7f800000 | (mantissa << 13);
    else bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float result; std::memcpy(&result, &bits, sizeof result); return result;
}
extern "C" _Float16 __truncsfhf2(float value) {
    uint32_t bits; std::memcpy(&bits, &value, sizeof bits);
    uint16_t result = (bits >> 16) & 0x8000;
    uint32_t exponent = (bits >> 23) & 255, mantissa = bits & 0x7fffff;
    if (exponent == 255) result |= 0x7c00 | (mantissa ? ((mantissa >> 13) | 0x200) : 0);
    else if (exponent >= 143) result |= 0x7c00;
    else if (exponent >= 102) {
        uint32_t significand = mantissa | 0x800000;
        unsigned shift = exponent < 113 ? 126 - exponent : 13;
        uint32_t rounded = significand >> shift;
        const uint32_t remainder = significand & ((1u << shift) - 1);
        const uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (rounded & 1))) ++rounded;
        if (exponent >= 113) rounded += (exponent - 113) << 10;
        result |= static_cast<uint16_t>(rounded);
    }
    _Float16 half; std::memcpy(&half, &result, sizeof half); return half;
}
