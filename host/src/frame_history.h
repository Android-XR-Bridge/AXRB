#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace axrb::host::detail {
// Caller synchronizes access. Keep at most two complete image+pose snapshots,
// without fabricating frames or allowing unbounded latency/storage. Retired
// leases are moved to the caller for destruction OUTSIDE its publication lock.
template<class Snapshot>
class FrameHistory {
public:
    void push(const Snapshot& frame, std::vector<Snapshot>& retired) {
        if ((hasSubmitted_ && frame.header.sequence <= submitted_) ||
            (size_ && frame.header.sequence <= frames_[size_-1].header.sequence)) return;
        if (size_ == frames_.size()) { pop(retired); ++overflowDrops; }
        frames_[size_++] = frame;
    }
    void submitted(uint64_t sequence, std::vector<Snapshot>& retired) {
        if (hasSubmitted_ && sequence < submitted_) return;
        hasSubmitted_ = true; submitted_ = sequence;
        while (size_ && frames_[0].header.sequence <= sequence) pop(retired);
    }
    void expire(std::chrono::steady_clock::time_point now, std::vector<Snapshot>& retired) {
        while (size_ && now - frames_[0].receivedAt > std::chrono::milliseconds(33)) {
            pop(retired); ++expiredDrops;
        }
    }
    void clear(std::vector<Snapshot>& retired) { while (size_) pop(retired); }
    const Snapshot* front() const { return size_ ? &frames_[0] : nullptr; }
    size_t size() const { return size_; }
    uint64_t overflowDrops = 0, expiredDrops = 0;
private:
    void pop(std::vector<Snapshot>& retired) {
        retired.push_back(std::move(frames_[0]));
        for (size_t i=1; i<size_; ++i) frames_[i-1]=std::move(frames_[i]);
        frames_[--size_] = {};
    }
    std::array<Snapshot,2> frames_{};
    size_t size_ = 0;
    uint64_t submitted_ = 0;
    bool hasSubmitted_ = false;
};
} // namespace axrb::host::detail
