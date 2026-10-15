#pragma once

#include <cstdint>
#include <limits>
#include <intrin.h>

namespace axrb {
// Timeouts can be decades in the future (including effectively infinite
// library waits). A multiply before a divide must not wrap into a past date.
inline int64_t scale_time(int64_t value, int64_t numerator, int64_t denominator) {
    if (value <= 0) return 0;
    constexpr uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    uint64_t high, remainder;
    const uint64_t low = _umul128(static_cast<uint64_t>(value), static_cast<uint64_t>(numerator), &high);
    if (high >= static_cast<uint64_t>(denominator)) return static_cast<int64_t>(limit);
    const uint64_t result = _udiv128(high, low, static_cast<uint64_t>(denominator), &remainder);
    return static_cast<int64_t>(result > limit ? limit : result);
}

inline int64_t add_time(int64_t a, int64_t b) {
    constexpr int64_t limit = std::numeric_limits<int64_t>::max();
    return b > limit - a ? limit : a + b;
}
}  // namespace axrb
