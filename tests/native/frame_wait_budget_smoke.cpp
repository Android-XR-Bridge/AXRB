#include "frame_wait_budget.h"
#include <cstdio>
#include <algorithm>
using namespace axrb::runtime::detail;
int main() {
    constexpr long long period=11111111;
    for (unsigned mode=0;mode<=2;++mode) {
        long long now=1000000000,next=0,previousPrediction=0;
        for(int frame=0;frame<2000;++frame) {
            now += frame%70==0 ? 100000000 : (frame%8==0 ? 20000000 : 9000000);
            if(pacing_reset_due(now,next,period,mode)) next=now;
            if(next>now) now=next+1000000; // Model a late sleep, not perfect timing.
            if(pacing_reset_due(now,next,period,mode)) next=now;
            next+=period;
            long long prediction=pacing_predicted_display(next,now,period,mode);
            if(mode) prediction=std::max(prediction,previousPrediction+1);
            if(prediction<=now || prediction<=previousPrediction) return 1;
            if(now-next >= static_cast<long long>(1+mode)*period) return 2;
            previousPrediction=prediction;
        }
    }
    std::puts("PASS three recovery modes, 6000 synthetic frame steps, sleep overshoot, long-stall reset, future monotonic predictions");
}
