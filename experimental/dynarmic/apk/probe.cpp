#include <jni.h>
#include <pthread.h>
#include <cstdint>
#include <cstdio>
#include <time.h>
#include <sys/mman.h>
#include <sched.h>
#define AXRB_PROBE_APK
#include "../../digitalis/cpu_probe.cpp"
extern "C" JNIEXPORT jstring JNICALL Java_com_axrb_dynarmicprobe_MainActivity_benchmark(JNIEnv* env,jclass) {
    return env->NewStringUTF(cpu_probe()==0 ? "BENCHMARK_COMPLETE" : "BENCHMARK_FAILED");
}
struct Message { unsigned ready=0; std::uint64_t payload=0; };
static void* publish(void* opaque) {
    auto& message=*static_cast<Message*>(opaque);
    for(unsigned i=1;i<=1000;++i) {
        while(__atomic_load_n(&message.ready,__ATOMIC_ACQUIRE)!=0) sched_yield();
        message.payload=std::uint64_t(i)*0x123456789;
        __atomic_store_n(&message.ready,1u,__ATOMIC_RELEASE);
    }
    return nullptr;
}
static void* worker(void* value) {
    auto result=static_cast<std::uint64_t*>(value);
    std::uint64_t sum=1;
    for(int i=0;i<1000000;++i) { asm volatile("" : "+r"(sum)); sum=sum*3+1; }
    *result=sum;
    return nullptr;
}
static void* exclusive_worker(void* value) {
    auto address=static_cast<std::uint64_t*>(value);
    for(unsigned i=0;i<25000;++i) {
        std::uint64_t data; unsigned status;
        asm volatile("1: ldaxr %0,[%2]\n add %0,%0,#1\n stlxr %w1,%0,[%2]\n cbnz %w1,1b"
            : "=&r"(data), "=&r"(status) : "r"(address) : "memory");
    }
    return nullptr;
}
static char exclusive_detail[160]="exclusive widths";
static bool exclusive_widths() {
    alignas(8) std::uint64_t data=0; unsigned value,status;
    asm volatile("ldaxrb %w0,[%2]\n add %w0,%w0,#7\n stlxrb %w1,%w0,[%2]"
        : "=&r"(value),"=&r"(status) : "r"(&data) : "memory");
    if(status || data!=7) { std::snprintf(exclusive_detail,sizeof(exclusive_detail),"byte status=%u data=%llu",status,(unsigned long long)data); return false; }
    asm volatile("ldxrh %w0,[%2]\n add %w0,%w0,#9\n stxrh %w1,%w0,[%2]"
        : "=&r"(value),"=&r"(status) : "r"(&data) : "memory");
    if(status || data!=16) { std::snprintf(exclusive_detail,sizeof(exclusive_detail),"half status=%u data=%llu",status,(unsigned long long)data); return false; }
    asm volatile("ldaxr %w0,[%2]\n add %w0,%w0,#5\n stlxr %w1,%w0,[%2]"
        : "=&r"(value),"=&r"(status) : "r"(&data) : "memory");
    if(status || data!=21) { std::snprintf(exclusive_detail,sizeof(exclusive_detail),"word status=%u data=%llu",status,(unsigned long long)data); return false; }
    asm volatile("ldxr %w0,[%2]\n clrex\n stxr %w1,%w0,[%2]"
        : "=&r"(value),"=&r"(status) : "r"(&data) : "memory");
    if(status==0 || data!=21) { std::snprintf(exclusive_detail,sizeof(exclusive_detail),"CLREX status=%u data=%llu",status,(unsigned long long)data); return false; }
    std::uint64_t wide,fp; unsigned equal;
    asm volatile("fmov d31,%5\n cmp %5,%5\n ldaxr %0,[%4]\n stlxr %w1,%0,[%4]\n cset %w2,eq\n fmov %3,d31"
        : "=&r"(wide),"=&r"(status),"=&r"(equal),"=&r"(fp) : "r"(&data),"r"(std::uint64_t(0x123456789abcdef0)) : "v31","cc","memory");
    std::snprintf(exclusive_detail,sizeof(exclusive_detail),"registers status=%u equal=%u fp=%llx data=%llu",status,equal,(unsigned long long)fp,(unsigned long long)wide);
    return !status && equal==1 && fp==0x123456789abcdef0 && wide==21;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_axrb_dynarmicprobe_MainActivity_run(JNIEnv* env,jclass) {
    if(!exclusive_widths()) return env->NewStringUTF(exclusive_detail);
    std::uint64_t contended=0;
    pthread_t contenders[4]{};
    for(auto& t:contenders) if(pthread_create(&t,nullptr,exclusive_worker,&contended)!=0) return env->NewStringUTF("FAIL exclusive thread create");
    for(auto& t:contenders) if(pthread_join(t,nullptr)!=0) return env->NewStringUTF("FAIL exclusive thread join");
    if(contended!=100000) return env->NewStringUTF("FAIL contended exclusive count");
    timespec start{},end{};
    std::uint64_t hints=0;
    clock_gettime(CLOCK_MONOTONIC,&start);
    for(unsigned i=0;i<100000;++i) {
        asm volatile(".inst 0xd503245f\n add %0,%0,#1" : "+r"(hints));
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    const double hints_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
    if(hints!=100000) return env->NewStringUTF("FAIL compiled BTI hint loop");
    clock_gettime(CLOCK_MONOTONIC,&start);
    std::uint64_t exclusive=0;
    exclusive_worker(&exclusive);
    clock_gettime(CLOCK_MONOTONIC,&end);
    const double exclusive_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
    if(exclusive!=25000) return env->NewStringUTF("FAIL exclusive benchmark");
    clock_gettime(CLOCK_MONOTONIC,&start);
    std::uint64_t ordered=0;
    for(int i=0;i<100000;++i) {
        std::uint64_t value;
        asm volatile("nop\n ldar %0,[%1]\n add %0,%0,#1\n stlr %0,[%1]\n dmb ish"
            : "=&r"(value) : "r"(&ordered) : "memory");
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    const double ordered_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
    auto readonly=static_cast<std::uint64_t*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(readonly==MAP_FAILED) return env->NewStringUTF("FAIL mmap");
    *readonly=0x12345678;
    if(mprotect(readonly,4096,PROT_READ)!=0) return env->NewStringUTF("FAIL mprotect");
    std::uint64_t acquired=0;
    asm volatile("ldar %0,[%1]" : "=r"(acquired) : "r"(readonly) : "memory");
    unsigned byte=0,half=0,word=0;
    asm volatile("ldarb %w0,[%1]" : "=r"(byte) : "r"(readonly) : "memory");
    asm volatile("ldarh %w0,[%1]" : "=r"(half) : "r"(readonly) : "memory");
    asm volatile("ldar %w0,[%1]" : "=r"(word) : "r"(readonly) : "memory");
    asm volatile("ldar xzr,[%0]" : : "r"(readonly) : "memory");
    munmap(readonly,4096);
    if(acquired!=0x12345678 || byte!=0x78 || half!=0x5678 || word!=0x12345678)
        return env->NewStringUTF("FAIL read-only acquire");
    Message message;
    pthread_t publisher{};
    if(pthread_create(&publisher,nullptr,publish,&message)!=0) return env->NewStringUTF("FAIL publisher");
    bool synchronized=true;
    for(unsigned i=1;i<=1000;++i) {
        while(__atomic_load_n(&message.ready,__ATOMIC_ACQUIRE)!=1) sched_yield();
        synchronized &= message.payload==std::uint64_t(i)*0x123456789;
        __atomic_store_n(&message.ready,0u,__ATOMIC_RELEASE);
    }
    if(pthread_join(publisher,nullptr)!=0 || !synchronized) return env->NewStringUTF("FAIL acquire/release publication");
    std::uint64_t result=0;
    pthread_t thread{};
    if(pthread_create(&thread,nullptr,worker,&result)!=0 || pthread_join(thread,nullptr)!=0)
        return env->NewStringUTF("FAIL pthread");
    std::uint64_t reference=1;
    for(int i=0;i<1000000;++i) reference=reference*3+1;
    std::uint64_t atomic=7, expected=7, desired=11, previous=0, amount=3;
    asm volatile(".arch armv8.1-a\n casal %0,%2,[%1]" : "+r"(expected) : "r"(&atomic),"r"(desired) : "memory");
    asm volatile(".arch armv8.1-a\n ldaddal %2,%0,[%1]" : "=r"(previous) : "r"(&atomic),"r"(amount) : "memory");
    char text[240];
    std::snprintf(text,sizeof(text),"%s JNI+pthreads+ARM64+LSE result=%llx expected=%llx atomic=%llu old=%llu ordered=%llu ordered_ms=%.3f exclusive_ms=%.3f hints_ms=%.3f",
        result==reference && atomic==14 && expected==7 && previous==11 && ordered==100000 ? "PASS" : "FAIL",
        (unsigned long long)result,(unsigned long long)reference,(unsigned long long)atomic,(unsigned long long)previous,
        (unsigned long long)ordered,ordered_ms,exclusive_ms,hints_ms);
    return env->NewStringUTF(text);
}
