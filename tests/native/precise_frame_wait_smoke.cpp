#include "precise_frame_wait.h"
#include <thread>
#include <vector>
#include <algorithm>
#include <stdexcept>
using namespace std::chrono_literals;
using axrb::host::detail::PreciseFrameWait;
void check(bool passed, const char* message) { if (!passed) throw std::runtime_error(message); }
int main() try {
    PreciseFrameWait disabled(false);
    check(!disabled.enabled(), "disabled mode unexpectedly enabled");
    PreciseFrameWait waiter(true);
    check(waiter.enabled(), "high-resolution wait unavailable");
    std::atomic<unsigned> published{0};
    std::vector<double> timeoutMs;
    std::vector<double> notifiedMs;
    for (unsigned i=0; i<40; ++i) {
        waiter.notify(); // A stale event must not cause an early timeout.
        const auto start=std::chrono::steady_clock::now();
        waiter.wait_for(2ms, [] { return false; });
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        check(elapsed>=1.9, "stale event/old timer returned before deadline");
        timeoutMs.push_back(elapsed);
    }
    for (unsigned i=1; i<=100; ++i) {
        // Alternate publication before and during a wait. No required spin.
        if ((i%2)==0) { published.store(i); waiter.notify(); }
        std::thread publisher([&,i] {
            if ((i%2)!=0) {
                // Use the high-resolution timer for the producer too; ordinary
                // Windows sleep can round 100us to a whole system timer tick.
                HANDLE delay=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
                LARGE_INTEGER due{}; due.QuadPart=-1000;
                if(delay && SetWaitableTimerEx(delay,&due,0,nullptr,nullptr,nullptr,0)) WaitForSingleObject(delay,100);
                if(delay) CloseHandle(delay);
                published.store(i); waiter.notify();
            }
        });
        const auto start=std::chrono::steady_clock::now();
        waiter.wait_for(8ms, [&] { return published.load()>=i; });
        const bool observed=published.load()>=i;
        if ((i%2)!=0) notifiedMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        publisher.join();
        check(observed, "wait returned before publication or publication missed its budget");
        check(published.load()==i, "publication lost");
        waiter.wait_for(0ns, [] { return false; });
    }
    std::sort(timeoutMs.begin(),timeoutMs.end());
    std::sort(notifiedMs.begin(),notifiedMs.end());
    check(notifiedMs[25]<6.0, "most notifications woke only at timeout");
    std::printf("PASS disabled, stale notification, timer rearm, zero timeout, and 100 publication races; 2ms timeout p50=%.3fms p95=%.3fms max=%.3fms; notified median=%.3fms\n",timeoutMs[20],timeoutMs[38],timeoutMs.back(),notifiedMs[25]);
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s\n",error.what());
    return 1;
}
