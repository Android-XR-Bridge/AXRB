#include "runtime_internal.h"
#include "frame_wait_budget.h"

namespace axrb::runtime::detail {

namespace {
uint32_t pacing_catchup_periods() {
    static const uint32_t amount = []() -> uint32_t {
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX]{};
        __system_property_get("debug.axrb.pacing_catchup", value);
        if (value[0] >= '0' && value[0] <= '2' && value[1] == '\0')
            return static_cast<uint32_t>(value[0] - '0');
#endif
        return 0;
    }();
    return amount;
}
uint32_t pacing_spin_us() {
    static const uint32_t amount = []() -> uint32_t {
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX]{};
        __system_property_get("debug.axrb.pacing_spin_us", value);
        uint32_t parsed = 0;
        for (const char* p = value; *p; ++p) {
            if (*p < '0' || *p > '9') return 0;
            parsed = parsed * 10 + static_cast<uint32_t>(*p - '0');
            if (parsed > 2000) return 0;
        }
        return parsed;
#else
        return 0;
#endif
    }();
    return amount;
}

void pacing_spin_hint() {
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause");
#else
    std::this_thread::yield();
#endif
}

bool pacing_trace_enabled() {
    static const bool enabled = [] {
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX]{};
        __system_property_get("debug.axrb.pacing_trace", value);
        return std::strcmp(value, "1") == 0;
#else
        return false;
#endif
    }();
    return enabled;
}

// Diagnostic only: no pacing policy change. Stop after 24 five-second windows.
// Per-thread state avoids introducing cross-thread contention. "Between waits"
// includes application/runtime work, scheduling, and prior trace overhead; it
// must not be described as CPU time or added to overlapping thread timings.
struct PacingTrace {
    axrb::protocol::PerfStats enterIntervals{"pacing-enter-interval"};
    axrb::protocol::PerfStats outside{"pacing-between-waits"};
    axrb::protocol::PerfStats waitBody{"pacing-wait-body"};
    axrb::protocol::PerfStats arrivalLate{"pacing-entry-lateness"};
    axrb::protocol::PerfStats sleepRequested{"pacing-sleep-requested"};
    axrb::protocol::PerfStats sleepActual{"pacing-sleep-actual"};
    axrb::protocol::PerfStats sleepOvershoot{"pacing-sleep-overshoot"};
    XrTime previousEnter = 0, previousExit = 0, windowStart = 0;
    uint64_t calls = 0, sleeps = 0, resetsBefore = 0, resetsAfter = 0;
    unsigned reports = 0;
    bool active() const { return reports < 24; }
    void record(XrTime entered, XrTime exited, XrTime oldDeadline,
                XrTime requested, XrTime woke, XrTime period,
                bool resetBefore, bool resetAfter) {
        if (!windowStart) windowStart = entered;
        if (previousEnter) enterIntervals.record((entered - previousEnter) / 1000000.0);
        if (previousExit) outside.record((entered - previousExit) / 1000000.0);
        waitBody.record((exited - entered) / 1000000.0);
        if (oldDeadline) arrivalLate.record(std::max<XrTime>(0, entered - oldDeadline) / 1000000.0);
        if (requested > 0) {
            ++sleeps;
            sleepRequested.record(requested / 1000000.0);
            sleepActual.record((woke - entered) / 1000000.0);
            sleepOvershoot.record((woke - entered - requested) / 1000000.0);
        }
        ++calls;
        resetsBefore += resetBefore;
        resetsAfter += resetAfter;
        previousEnter = entered;
        previousExit = exited;
        if (exited - windowStart >= 5000000000LL) {
            ++reports;
#if defined(__ANDROID__)
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Pacing",
                "window=%u tid=%d calls=%llu sleeps=%llu reset_before=%llu reset_after=%llu period_ns=%lld seconds=%.6f spin_us=%u catchup_periods=%u",
                reports, gettid(), static_cast<unsigned long long>(calls),
                static_cast<unsigned long long>(sleeps), static_cast<unsigned long long>(resetsBefore),
                static_cast<unsigned long long>(resetsAfter), static_cast<long long>(period),
                (exited - windowStart) / 1000000000.0, pacing_spin_us(), pacing_catchup_periods());
#endif
            calls = sleeps = resetsBefore = resetsAfter = 0;
            windowStart = exited;
        }
    }
};
} // namespace

XrResult XRAPI_CALL xrWaitFrame_impl(
    XrSession session,
    const XrFrameWaitInfo* frameWaitInfo,
    XrFrameState* frameState)
{
    log_call("xrWaitFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (frameState == nullptr ||
        (frameWaitInfo != nullptr && frameWaitInfo->type != XR_TYPE_FRAME_WAIT_INFO) || frameState->type != XR_TYPE_FRAME_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    // Pace at the active host display period. Default resets after one late
    // period. Optional recovery tolerates at most two additional late periods,
    // still resetting after long stalls instead of accumulating unbounded debt.
    const XrTime period = axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    const bool tracePacing = pacing_trace_enabled();
    const uint32_t spinUs = pacing_spin_us();
    const uint32_t catchupPeriods = pacing_catchup_periods();
    XrTime now = monotonic_time_ns();
    const XrTime entered = now, oldDeadline = g_nextFrameStart;
    const bool shouldResetBefore = pacing_reset_due(now, g_nextFrameStart, period, catchupPeriods);
    const bool resetBefore = g_nextFrameStart != 0 && shouldResetBefore;
    if (shouldResetBefore) {
        g_nextFrameStart = now;
    }
    const XrTime requestedSleep = std::max<XrTime>(0, g_nextFrameStart - now);
    if (g_nextFrameStart > now) {
        const XrTime coarseNs = pacing_coarse_wait_ns(g_nextFrameStart - now, spinUs);
        if (coarseNs > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(coarseNs));
        now = monotonic_time_ns();
        if (spinUs && now < g_nextFrameStart) {
            // Bounded opt-in CPU tail. Never advance the absolute schedule or
            // return before its deadline. A late coarse wake skips spinning.
            const XrTime spinEnd = std::min(g_nextFrameStart, now + static_cast<XrTime>(spinUs) * 1000);
            for (uint32_t attempts = 0; now < spinEnd && attempts < 200000; ++attempts) {
                pacing_spin_hint();
                now = monotonic_time_ns();
            }
            if (now < g_nextFrameStart) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(g_nextFrameStart - now));
                now = monotonic_time_ns();
            }
        }
    }
    const bool resetAfter = pacing_reset_due(now, g_nextFrameStart, period, catchupPeriods);
    if (resetAfter) { g_nextFrameStart = now; }
    g_nextFrameStart += period;
    static XrTime previousPrediction = 0;
    if (oldDeadline == 0) previousPrediction = 0;
    XrTime prediction = pacing_predicted_display(g_nextFrameStart, now, period, catchupPeriods);
    if (catchupPeriods) prediction = std::max(prediction, previousPrediction + 1);
    previousPrediction = prediction;
    frameState->predictedDisplayTime = prediction;
    frameState->predictedDisplayPeriod = period;
    frameState->shouldRender = 1;
    if (tracePacing) {
        static thread_local PacingTrace trace;
        if (trace.active()) trace.record(entered, monotonic_time_ns(), oldDeadline,
            requestedSleep, now, period, resetBefore, resetAfter);
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrBeginFrame_impl(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
{
    log_call("xrBeginFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (frameBeginInfo != nullptr && frameBeginInfo->type != XR_TYPE_FRAME_BEGIN_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEndFrame_impl(XrSession session, const XrFrameEndInfo* frameEndInfo)
{
    static axrb::protocol::PerfStats stats("end-frame");
    axrb::protocol::PerfScope scope(stats);
    log_call("xrEndFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (frameEndInfo == nullptr || frameEndInfo->type != XR_TYPE_FRAME_END_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    return submit_projection_frame(*frameEndInfo);
}

XrResult XRAPI_CALL xrEnumerateDisplayRefreshRatesFB_impl(XrSession session, uint32_t capacity, uint32_t* count, float* rates) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!count || (capacity && !rates)) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity) rates[0] = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetDisplayRefreshRateFB_impl(XrSession session, float* rate) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!rate) return XR_ERROR_VALIDATION_FAILURE;
    *rate = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrRequestDisplayRefreshRateFB_impl(XrSession session, float rate) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    const float activeRate = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return rate == 0.0f || std::abs(rate - activeRate) < 0.01f ? XR_SUCCESS : static_cast<XrResult>(-1000101000);
}


} // namespace axrb::runtime::detail
