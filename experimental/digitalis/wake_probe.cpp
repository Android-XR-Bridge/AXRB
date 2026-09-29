#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <vector>

alignas(64) static unsigned turn=0;
static constexpr unsigned count=5000;
static void pin(unsigned cpu) {
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(cpu,&mask);
    if(sched_setaffinity(0,sizeof(mask),&mask)) {perror("affinity");std::exit(2);}
}
static void wait_turn(unsigned wanted) {
    for(;;) {
        auto value=__atomic_load_n(&turn,__ATOMIC_ACQUIRE);
        if(value==wanted)return;
        if(syscall(SYS_futex,&turn,FUTEX_WAIT_PRIVATE,value,nullptr,nullptr,0)<0 && errno!=EAGAIN && errno!=EINTR) {
            perror("futex wait");std::exit(3);
        }
    }
}
static void give(unsigned value) {
    __atomic_store_n(&turn,value,__ATOMIC_RELEASE);
    if(syscall(SYS_futex,&turn,FUTEX_WAKE_PRIVATE,1,nullptr,nullptr,0)<0){perror("futex wake");std::exit(4);}
}
static double now_us(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
static void* worker(void*) {
    pin(1);
    for(unsigned i=0;i<count+200;++i){wait_turn(1);give(0);}
    return nullptr;
}
int main(){
    alarm(30);pin(0);pthread_t thread;
    if(pthread_create(&thread,nullptr,worker,nullptr))return 5;
    std::vector<double> samples;samples.reserve(count);
    for(unsigned i=0;i<count+200;++i){
        double start=now_us();give(1);wait_turn(0);
        if(i>=200)samples.push_back(now_us()-start);
    }
    if(pthread_join(thread,nullptr))return 6;
    std::sort(samples.begin(),samples.end());
    double sum=0;for(double value:samples)sum+=value;
    std::printf("{\"roundtrips\":%u,\"mean_us\":%.3f,\"p50_us\":%.3f,\"p95_us\":%.3f,\"p99_us\":%.3f,\"max_us\":%.3f}\n",
        count,sum/count,samples[count/2],samples[count*95/100],samples[count*99/100],samples.back());
}
