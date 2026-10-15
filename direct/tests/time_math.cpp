#include "time_math.h"
#include <cstdio>
#include <limits>
#include <initializer_list>

int main() {
    using axrb::scale_time;
    using axrb::add_time;
    constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
    if (scale_time(946080000000000000LL, 10000000, 1000000000) != 9460800000000000LL) return 1;
    if (scale_time(9460800000000000LL, 10000000, 10000000) != 9460800000000000LL) return 2;
    if (scale_time(maximum, 1000000000, 1) != maximum) return 3;
    if (scale_time(maximum, maximum, maximum) != maximum) return 4;
    if (add_time(maximum - 10, 11) != maximum || add_time(20, 30) != 50) return 5;
    for (int64_t frequency : {10000000LL, 24000000LL, 3579545LL, 3000000000LL}) {
        for (int64_t ns = 1; ns < 100000; ns += 97)
            if (scale_time(ns, frequency, 1000000000) != ns * frequency / 1000000000) return 6;
    }
    std::puts("TIME_MATH_PASS");
}
