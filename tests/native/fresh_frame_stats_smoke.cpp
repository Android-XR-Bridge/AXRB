#include "fresh_frame_stats.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace axrb::host;
int main() {
    PerformanceTelemetry t;
    constexpr int64_t period = 11111111;
    // 45 real frames, 90 successful host submissions: duplicates MUST NOT be 90 FPS.
    for (int i = 0; i < 900; ++i) {
        const auto ns = i * period;
        if (!(i % 2)) t.received(i * 5, ns, true, true); // gaps are NOT extra frames
        t.submitted(ns, true, true, (i / 2) * 10, 2040, 2080, period);
    }
    auto s = t.snapshot(899 * period);
    assert(s.fresh.fps >= 44 && s.fresh.fps <= 46);
    assert(s.received.fps >= 44 && s.received.fps <= 46);
    assert(s.host.fps >= 90 && s.host.fps <= 91);
    assert(s.fresh.total == 450 && s.repeats.total == 450);
    assert(s.fresh.low1Ready && std::abs(s.fresh.low1Fps - 45) < .01);
    assert(s.width == 2040 && s.height == 2080 && s.gpuShared);
    const auto count = s.fresh.total;
    t.submitted(901 * period, false, true, 999999, 1, 1, period); // failed xrEndFrame
    t.submitted(902 * period, true, false, 999999, 1, 1, period); // splash / HUD only
    t.received(999999, 903 * period, false, false); // empty heartbeat
    t.submitted(904 * period, true, true, 0, 1, 1, period); // regressed cached frame
    s = t.snapshot(1001 * period);
    assert(s.fresh.total == count && s.fresh.fps == 0 && s.received.fps == 0);
    assert(s.fresh.ageMs > 1000 && s.fresh.worstMs > 1000);
    assert(s.fresh.low1Fps < 10); // ongoing stall included before next arrival
    FreshFrameStream gap;
    assert(gap.record(0, 0)); assert(!gap.record(0, 1));
    assert(gap.record(1000, 10000000)); assert(!gap.record(1, 20000000));
    assert(gap.summarize(20000000, 11.111).total == 2);
    assert(!gap.summarize(20000000, 11.111).low1Ready);
    assert(PerformanceTelemetry().snapshot(0).fresh.ageMs == -1);
    puts("PASS: 45/90 separation, sequence gaps, repeats, failed/empty submissions, stalls and 1% low");
}
