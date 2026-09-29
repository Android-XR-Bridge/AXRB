#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace axrb::host::detail {
// One consumer, any number of publishers. Handles never change while in use.
// An auto-reset notification plus a predicate prevents lost wakeups; a stale
// notification is harmless. No publication mutex is held during OS waits.
class PreciseFrameWait {
public:
    static bool environment_requested() {
        const char* value = std::getenv("AXRB_PRECISE_IMAGE_WAIT");
        return value && std::strcmp(value, "1") == 0;
    }
    explicit PreciseFrameWait(bool requested = environment_requested()) {
        if (!requested) return;
        changed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        timer_ = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!changed_ || !timer_) {
            const DWORD error = GetLastError();
            if (changed_) CloseHandle(changed_);
            if (timer_) CloseHandle(timer_);
            changed_ = timer_ = nullptr;
            std::fprintf(stderr, "AXRB timing experiment: precise image wait unavailable error=%lu; using condition variable\n", error);
        } else {
            std::fprintf(stderr, "AXRB timing experiment: precise image wait enabled (event + high-resolution timer)\n");
        }
    }
    ~PreciseFrameWait() {
        if (timer_) { CancelWaitableTimer(timer_); CloseHandle(timer_); }
        if (changed_) CloseHandle(changed_);
    }
    PreciseFrameWait(const PreciseFrameWait&) = delete;
    PreciseFrameWait& operator=(const PreciseFrameWait&) = delete;
    bool enabled() const { return timer_ && changed_; }
    void notify() const {
        if (changed_ && !SetEvent(changed_)) report_error("SetEvent");
    }
    template<class Ready>
    void wait_for(std::chrono::nanoseconds timeout, Ready ready) {
        // Caller keeps the normal CV path if construction was unsupported.
        // A runtime API failure ends this optional wait, never extends it.
        if (!enabled() || timeout.count() <= 0 || ready()) return;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        struct Cancel { HANDLE timer; ~Cancel() { CancelWaitableTimer(timer); } } cancel{timer_};
        const HANDLE handles[] = {changed_, timer_};
        while (!ready()) {
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return;
            LARGE_INTEGER due{};
            due.QuadPart = -((remaining + 99) / 100); // Relative 100ns, round up.
            if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
                report_error("SetWaitableTimerEx");
                return;
            }
            const DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0 + 1) return;
            if (result != WAIT_OBJECT_0) { report_error("WaitForMultipleObjects"); return; }
            // Recheck predicate and the original deadline after any signal.
        }
    }
private:
    static void report_error(const char* operation) {
        const DWORD error = GetLastError();
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed))
            std::fprintf(stderr, "AXRB timing experiment: precise wait %s failed error=%lu; skipping optional wait\n", operation, error);
    }
    HANDLE changed_ = nullptr, timer_ = nullptr;
};
} // namespace axrb::host::detail
#endif
