#pragma once
#include <functional>
// Optional performance-overlay module; excluded from default host builds.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "performance_journal.h"

namespace axrb::host {
// All timestamps come from the HOST steady clock. Guest virtual time, predicted
// display times, sequence gaps, duplicated frames and compositor ticks are NOT FPS.
struct FreshFrameSummary {
    double fps = 0, averageMs = 0, worstMs = 0, low1Fps = 0, ageMs = -1;
    uint64_t total = 0;
    bool low1Ready = false;
    std::vector<double> graph;
};
class FreshFrameStream {
public:
    bool record(uint64_t sequence, int64_t ns) {
        if (started_ && (sequence <= sequence_ || ns < lastNs_)) return false;
        const double gap = started_ ? (ns - lastNs_) / 1e6 : 0;
        sequence_ = sequence; lastNs_ = ns; started_ = true; ++total_;
        events_.push_back({ns, gap});
        while (events_.size() > 4096 || (!events_.empty() && ns - events_.front().ns > 10000000000LL))
            events_.pop_front();
        return true;
    }
    FreshFrameSummary summarize(int64_t now, double budgetMs) const {
        FreshFrameSummary out; out.total = total_;
        if (!started_) return out;
        out.ageMs = (std::max)(0.0, (now - lastNs_) / 1e6);
        std::vector<double> gaps;
        for (const auto& e : events_) {
            // Exact event count in a one-second rolling window, including stalls.
            if (e.ns > now - 1000000000LL && e.ns <= now) out.fps += 1;
            if (e.ns > now - 5000000000LL && e.ns <= now && e.gapMs > 0) gaps.push_back(e.gapMs);
        }
        // Show an ongoing stall BEFORE another frame arrives. Never retain 90 FPS
        // indefinitely when the producer freezes. This is an unfinished interval.
        if (out.ageMs > (std::max)(budgetMs * 1.5, 20.0)) gaps.push_back(out.ageMs);
        if (!gaps.empty()) {
            out.graph.assign(gaps.begin() + (gaps.size() > 240 ? gaps.size() - 240 : 0), gaps.end());
            for (double gap : gaps) { out.averageMs += gap; out.worstMs = (std::max)(out.worstMs, gap); }
            out.averageMs /= gaps.size();
            out.low1Ready = gaps.size() >= 100;
            if (out.low1Ready) {
                std::sort(gaps.begin(), gaps.end(), std::greater<double>());
                const size_t count = (gaps.size() + 99) / 100;
                double slowest = 0;
                for (size_t i = 0; i < count; ++i) slowest += gaps[i];
                out.low1Fps = 1000.0 * count / slowest;
            }
        }
        return out;
    }
private:
    struct Event { int64_t ns; double gapMs; };
    std::deque<Event> events_;
    bool started_ = false;
    uint64_t sequence_ = 0, total_ = 0;
    int64_t lastNs_ = 0;
};
struct PerformanceSummary {
    FreshFrameSummary received, fresh, host, repeats;
    uint32_t width = 0, height = 0;
    double targetHz = 0;
    bool gpuShared = false;
};
class PerformanceTelemetry {
public:
    void received(uint64_t seq, int64_t ns, bool nonempty, bool gpuShared) {
        if (!nonempty) return;
        std::lock_guard lock(mutex_);
        const bool fresh=received_.record(seq, ns);
        if (fresh) gpuShared_ = gpuShared;
        performance_journal().record({'R',ns,seq,(fresh?1u:0u)|2u|(gpuShared?4u:0u),0,0,0});
    }
    void submitted(int64_t ns, bool successful, bool gameProjection, uint64_t seq,
                   uint32_t width, uint32_t height, int64_t displayPeriodNs) {
        if (!successful) {
            performance_journal().record({'S',ns,seq,gameProjection?2u:0u,width,height,displayPeriodNs});
            return;
        }
        std::lock_guard lock(mutex_);
        host_.record(++hostCount_, ns);
        if (displayPeriodNs > 0) targetHz_ = 1e9 / displayPeriodNs;
        bool fresh=false;
        if (gameProjection) {
            fresh=fresh_.record(seq, ns);
            if (fresh) { width_ = width; height_ = height; }
            else repeats_.record(++repeatCount_, ns);
        }
        performance_journal().record({'S',ns,seq,1u|(gameProjection?2u:0u)|(fresh?4u:0u),width,height,displayPeriodNs});
    }
    PerformanceSummary snapshot(int64_t now) const {
        FreshFrameStream received, fresh, host, repeats;
        PerformanceSummary out;
        { std::lock_guard lock(mutex_);
            received = received_; fresh = fresh_; host = host_; repeats = repeats_;
            out.width = width_; out.height = height_; out.targetHz = targetHz_; out.gpuShared = gpuShared_;
        }
        const double budget = out.targetHz > 0 ? 1000.0 / out.targetHz : 11.111;
        out.received = received.summarize(now, budget); out.fresh = fresh.summarize(now, budget);
        out.host = host.summarize(now, budget); out.repeats = repeats.summarize(now, budget);
        return out;
    }
private:
    mutable std::mutex mutex_;
    FreshFrameStream received_, fresh_, host_, repeats_;
    uint64_t hostCount_ = 0, repeatCount_ = 0;
    uint32_t width_ = 0, height_ = 0;
    double targetHz_ = 0;
    bool gpuShared_ = false;
};
} // namespace axrb::host
