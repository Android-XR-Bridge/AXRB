#include <arm_neon.h>
#include <cstdint>
#include <cstdio>
#include <time.h>
#ifdef AXRB_PROBE_APK
#include <android/log.h>
#define PROBE_PRINT(...) __android_log_print(ANDROID_LOG_INFO,"AXRB.CpuProbe",__VA_ARGS__)
#else
#define PROBE_PRINT(...) std::printf(__VA_ARGS__)
#endif

static double now() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

int cpu_probe() {
    // Report correctness separately so a failed instruction does not hide timings.
    alignas(16) uint64_t exclusive=21;
    uint64_t loaded; unsigned status;
    asm volatile("ldxr %0,[%2]\n clrex\n stxr %w1,%0,[%2]"
        : "=&r"(loaded),"=&r"(status) : "r"(&exclusive) : "memory");
    PROBE_PRINT("CHECK clrex %s status=%u",status ? "PASS" : "FAIL",status);
    alignas(16) uint64_t data[256]{};
    for(unsigned round=0;round<8;++round) {
        uint64_t sum=0;
        double start=now();
        for(unsigned i=0;i<1000000;++i) {
            auto* p=&data[i&255];
            uint64_t v;
            asm volatile("ldr %0,[%1]\n add %0,%0,#1\n str %0,[%1]"
                : "=&r"(v) : "r"(p) : "memory");
            sum+=v;
        }
        PROBE_PRINT("TIME integer %u %.6f checksum=%llu",round,now()-start,(unsigned long long)sum);
        uint64x2_t v=vdupq_n_u64(1);
        start=now();
        for(unsigned i=0;i<1000000;++i) {
            auto* p=&data[(i&127)*2];
            uint64x2_t value;
            asm volatile("ldr %q0,[%1]\n add %0.2d,%0.2d,%2.2d\n str %q0,[%1]"
                : "=&w"(value) : "r"(p),"w"(v) : "memory");
        }
        PROBE_PRINT("TIME simd %u %.6f checksum=%llu",round,now()-start,(unsigned long long)data[0]);
        uint64_t count=0;
        start=now();
        for(unsigned i=0;i<100000;++i) {
            uint64_t value; unsigned retry;
            asm volatile("1: ldaxr %0,[%2]\n add %0,%0,#1\n stlxr %w1,%0,[%2]\n cbnz %w1,1b"
                : "=&r"(value),"=&r"(retry) : "r"(&count) : "memory");
        }
        PROBE_PRINT("TIME atomic %u %.6f checksum=%llu",round,now()-start,(unsigned long long)count);
        if(count!=100000) return 2;
    }
    uint64_t total=0;
    for(auto v:data) total+=v;
    PROBE_PRINT("CHECK memory %s total=%llu",total==24000000 ? "PASS":"FAIL",(unsigned long long)total);
    return total==24000000 ? 0:3;
}
#ifndef AXRB_PROBE_APK
int main() { return cpu_probe(); }
#endif
