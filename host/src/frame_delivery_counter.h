#pragma once
#include <cstdint>

namespace axrb::host::detail {
constexpr int64_t fresh_wait_budget_ns(uint32_t requestedUs, int64_t displayLeadNs,
                                     int64_t displayPeriodNs) {
    constexpr int64_t reserveNs = 2000000;
    constexpr int64_t periodReserveNs = 3000000;
    if (requestedUs > 8000 || displayLeadNs <= reserveNs || displayLeadNs > 100000000 ||
        displayPeriodNs < 4000000 || displayPeriodNs > 25000000) return 0;
    const int64_t requestedNs = static_cast<int64_t>(requestedUs) * 1000;
    const int64_t availableNs = displayLeadNs - reserveNs;
    const int64_t cadenceBudgetNs = displayPeriodNs - periodReserveNs;
    const int64_t deadlineBudgetNs = requestedNs < availableNs ? requestedNs : availableNs;
    return deadlineBudgetNs < cadenceBudgetNs ? deadlineBudgetNs : cadenceBudgetNs;
}
static_assert(fresh_wait_budget_ns(2000, 10000000, 11111111) == 2000000);
static_assert(fresh_wait_budget_ns(2000, 2500000, 11111111) == 500000);
static_assert(fresh_wait_budget_ns(2000, 2000000, 11111111) == 0);
static_assert(fresh_wait_budget_ns(0, 10000000, 11111111) == 0);
static_assert(fresh_wait_budget_ns(8001, 10000000, 11111111) == 0);
static_assert(fresh_wait_budget_ns(2000, -1, 11111111) == 0);
static_assert(fresh_wait_budget_ns(2000, 100000001, 11111111) == 0);
static_assert(fresh_wait_budget_ns(8000, 55000000, 11111111) == 8000000);
static_assert(fresh_wait_budget_ns(8000, 55000000, 8333333) == 5333333);
static_assert(fresh_wait_budget_ns(8000, 55000000, 0) == 0);
static_assert(fresh_wait_budget_ns(8000, 55000000, 25000001) == 0);
// Counts successful OpenXR submissions, not sequence gaps or compositor ticks.
// Regressed/cached images are conservatively non-fresh.
struct FrameDeliveryCounter {
    uint64_t total = 0, unique = 0, repeated = 0, regressed = 0;
    uint64_t lastSequence = 0;
    bool initialized = false;
    constexpr bool record(uint64_t sequence) {
        ++total;
        if (!initialized || sequence > lastSequence) {
            initialized = true;
            lastSequence = sequence;
            ++unique;
            return true;
        }
        ++repeated;
        if (sequence < lastSequence) ++regressed;
        return false;
    }
};
constexpr bool frame_delivery_counter_checks() {
    FrameDeliveryCounter c;
    if (!c.record(0) || c.record(0) || !c.record(10) || c.record(9)) return false;
    return c.total == 4 && c.unique == 2 && c.repeated == 2 && c.regressed == 1 && c.lastSequence == 10;
}
static_assert(frame_delivery_counter_checks(), "Unique submission counting must ignore repeats and sequence gaps");
}
