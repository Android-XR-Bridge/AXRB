#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>

namespace axrb::host::detail {
// Refract's adaptive pickup budget: half a frame initially, back off after
// compositor misses and recover gradually after ten seconds without misses.
class AdaptivePickupBudget {
public:
    void observe(int64_t displayTime, int64_t period, uint64_t now) {
        if (period < 4'000'000 || period > 25'000'000) { lastDisplay_ = 0; return; }
        if (period != period_) {
            period_ = period; budget_ = period / 2; lastDisplay_ = 0;
            misses_.clear(); changed_ = now;
        }
        if (lastDisplay_ && displayTime > lastDisplay_) {
            const int64_t missed = (displayTime - lastDisplay_ + period / 2) / period - 1;
            for (int64_t i = 0; i < (std::min)(missed, int64_t{8}); ++i) misses_.push_back(now);
        }
        lastDisplay_ = displayTime;
        while (!misses_.empty() && now - misses_.front() > 10'000'000'000ULL) misses_.pop_front();
        const auto recent = std::count_if(misses_.begin(), misses_.end(), [now](auto t) { return now - t <= 2'000'000'000ULL; });
        if (recent >= 2 && now - changed_ >= 1'000'000'000ULL) {
            budget_ = (std::max)(period / 4, budget_ - 500'000); changed_ = now;
        } else if (misses_.empty() && now - changed_ >= 10'000'000'000ULL) {
            budget_ = (std::min)(period * 65 / 100, budget_ + 500'000); changed_ = now;
        }
    }
    uint32_t microseconds() const { return static_cast<uint32_t>((std::min)(int64_t{8000}, budget_ / 1000)); }
private:
    int64_t lastDisplay_ = 0, period_ = 0, budget_ = 0;
    uint64_t changed_ = 0;
    std::deque<uint64_t> misses_;
};
}
