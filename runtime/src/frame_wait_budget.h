#pragma once
#include <cstdint>

namespace axrb::runtime::detail {
constexpr bool pacing_reset_due(int64_t now, int64_t deadline, int64_t period,
                                uint32_t catchupPeriods) {
    if (period <= 0 || catchupPeriods > 2) return true;
    return deadline == 0 || (now >= deadline && now - deadline >= period * (1 + catchupPeriods));
}
constexpr int64_t pacing_predicted_display(int64_t nextDeadline, int64_t now,
                                          int64_t period, uint32_t catchupPeriods) {
    // Recovery pacing can carry a bounded scheduling debt, never a past display
    // prediction. Actual monotonic time is not scaled or substituted.
    return catchupPeriods && now + period > nextDeadline ? now + period : nextDeadline;
}
static_assert(pacing_reset_due(120, 100, 10, 0));
static_assert(!pacing_reset_due(120, 100, 10, 2));
static_assert(pacing_reset_due(130, 100, 10, 2));
static_assert(pacing_reset_due(100, 0, 10, 2));
static_assert(!pacing_reset_due(99, 100, 10, 2));
static_assert(pacing_predicted_display(110, 121, 10, 0) == 110);
static_assert(pacing_predicted_display(110, 121, 10, 2) == 131);
static_assert(pacing_predicted_display(133, 121, 10, 2) == 133);
constexpr int64_t pacing_coarse_wait_ns(int64_t remainingNs, uint32_t spinUs) {
    if (remainingNs <= 0) return 0;
    if (spinUs == 0 || spinUs > 2000) return remainingNs;
    const int64_t tailNs = static_cast<int64_t>(spinUs) * 1000;
    return remainingNs > tailNs ? remainingNs - tailNs : 0;
}
static_assert(pacing_coarse_wait_ns(3000000, 0) == 3000000);
static_assert(pacing_coarse_wait_ns(3000000, 1000) == 2000000);
static_assert(pacing_coarse_wait_ns(500000, 1000) == 0);
static_assert(pacing_coarse_wait_ns(0, 1000) == 0);
static_assert(pacing_coarse_wait_ns(-1, 1000) == 0);
static_assert(pacing_coarse_wait_ns(3000000, 2001) == 3000000);
} // namespace axrb::runtime::detail
