// Experimental, version-gated dispatch adapter. Android ABI/JNI/ELF handling
// remains in libndk_translation; Dynarmic executes normal ARM64 instructions.
// Unsupported instructions and atomics are counted and interpreted by its
// existing interpreter. This does not alter APKs or system library files.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <time.h>
#include <unistd.h>
#include <sys/mount.h>
#include <cerrno>
#include <android/log.h>
#include <dynarmic/interface/A64/a64.h>

extern "C" bool InitializeNativeBridge(void* env, const char*) {
    using Initialize=bool(*)(void*,const char*);
    auto original=reinterpret_cast<Initialize>(dlsym(RTLD_NEXT,"InitializeNativeBridge"));
    if (!original) {
        void* handle=dlopen("/apex/com.android.art/lib64/libnativebridge.so",RTLD_NOW|RTLD_NOLOAD);
        if(handle) original=reinterpret_cast<Initialize>(dlsym(handle,"InitializeNativeBridge"));
    }
    // WrapperInit registers framework JNI after Runtime::Start. Avoid touching
    // android.os.Build here before those methods have been registered.
    bool result=original && original(nullptr,"arm64");
    __android_log_print(ANDROID_LOG_INFO,"AXRB.Dynarmic","NativeBridge initialize arm64 result=%d original=%p",result,reinterpret_cast<void*>(original));
    return result;
}

// WrapperInit already inherits the zygote's mount namespace. Repeating this
// optional CPU-info bind mount after app sandboxing would trigger seccomp.
extern "C" int mount(const char* source, const char* target, const char* type,
                     unsigned long flags, const void* data) {
    if (target && !std::strcmp(target, "/proc/cpuinfo") && (flags & MS_BIND)) {
        errno=EPERM;
        return -1;
    }
    auto original=reinterpret_cast<decltype(&mount)>(dlsym(RTLD_NEXT,"mount"));
    if (!original) { errno=ENOSYS; return -1; }
    return original(source,target,type,flags,data);
}

namespace {
using U64=std::uint64_t;
using U32=std::uint32_t;
using Vec=Dynarmic::A64::Vector;
constexpr U64 Budget=8192;
std::atomic<U64> executed{}, fallbacks{}, entries{}, host_calls{}, generation{};
std::atomic<U64> code_reads{};
struct FallbackSample { U32 instruction{}; U64 count{}; };
std::mutex fallback_sample_mutex;
std::array<FallbackSample,128> fallback_samples{};
const bool profile_fallback=getenv("AXRB_DYNARMIC_PROFILE_FALLBACK")!=nullptr;
void sample_fallback(U32 instruction) {
    std::lock_guard lock(fallback_sample_mutex);
    for(auto& slot:fallback_samples) {
        if(!slot.count || slot.instruction==instruction) { slot.instruction=instruction; ++slot.count; return; }
    }
}
std::mutex contexts_mutex;
std::vector<Dynarmic::A64::Jit*> contexts;
std::atomic<U64> next_report{};
const bool legacy_fallback=getenv("AXRB_DYNARMIC_LEGACY_FALLBACK")!=nullptr;
const bool direct_acquire=getenv("AXRB_DYNARMIC_DISABLE_DIRECT_ACQUIRE")==nullptr;
std::atomic<U64> acquire_calls{};
std::atomic<U64> miss_runs{},miss_wall_ns{},miss_cpu_ns{},miss_max_ns{};
U64 timestamp(clockid_t clock) { timespec t{}; clock_gettime(clock,&t); return U64(t.tv_sec)*1000000000+t.tv_nsec; }
struct Counters {
    U64 instructions{},fallback{},dispatch{},host{},acquire{},calls{};
    void flush() {
        executed.fetch_add(instructions,std::memory_order_relaxed);
        fallbacks.fetch_add(fallback,std::memory_order_relaxed);
        entries.fetch_add(dispatch,std::memory_order_relaxed);
        host_calls.fetch_add(host,std::memory_order_relaxed);
        acquire_calls.fetch_add(acquire,std::memory_order_relaxed);
        instructions=fallback=dispatch=host=acquire=0;
    }
    bool tick() { if((++calls&4095)!=0) return false; flush(); return true; }
    ~Counters() { flush(); }
};
thread_local Counters counters;
template<class T> T& at(void* state, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<char*>(state)+offset); }
[[noreturn]] void fatal(const char* text) {
    __android_log_print(ANDROID_LOG_FATAL,"AXRB.Dynarmic","%s",text);
    std::fprintf(stderr,"AXRB.Dynarmic: %s\n",text); std::abort();
}
template<class T> T symbol(const char* name) {
    void* value=dlsym(RTLD_NEXT,name);
    if(!value) {
        static void* handle=dlopen("libndk_translation.so",RTLD_NOW|RTLD_NOLOAD);
        if(handle) value=dlsym(handle,name);
    }
    if(!value) fatal(name);
    return reinterpret_cast<T>(value);
}
bool verified_build=false;
int verify_module(dl_phdr_info* info, size_t, void*) {
    if(!std::strstr(info->dlpi_name,"libndk_translation.so")) return 0;
    constexpr unsigned char expected[]={0x28,0x10,0xe5,0xb4,0x48,0x95,0xc4,0xea,0x0b,0x1a,0xd6,0x88,0x2e,0xa7,0x3b,0x73};
    for(int i=0;i<info->dlpi_phnum;++i) {
        const auto& ph=info->dlpi_phdr[i]; if(ph.p_type!=PT_NOTE) continue;
        auto p=reinterpret_cast<const unsigned char*>(info->dlpi_addr+ph.p_vaddr);
        const auto end=p+ph.p_memsz;
        while(p+sizeof(Elf64_Nhdr)<=end) {
            auto n=reinterpret_cast<const Elf64_Nhdr*>(p); p+=sizeof(*n);
            auto name=p; p+=(n->n_namesz+3)&~3;
            auto desc=p; p+=(n->n_descsz+3)&~3;
            if(p>end) break;
            if(n->n_type==NT_GNU_BUILD_ID && n->n_namesz==4 && !std::memcmp(name,"GNU",4) &&
                n->n_descsz==sizeof(expected) && !std::memcmp(desc,expected,sizeof(expected))) verified_build=true;
        }
    }
    return 1;
}
struct Api {
    void (*run)(void*,const void*)=symbol<decltype(run)>("berberis_RunGeneratedCode");
    void (*interpret)(void*)=symbol<decltype(interpret)>("_ZN8berberis13InterpretInsnEPNS_11ThreadStateE");
    void* (*get_cache)()=symbol<decltype(get_cache)>("_ZN8berberis16TranslationCache11GetInstanceEv");
    bool (*wrapped)(const void*,U64)=symbol<decltype(wrapped)>("_ZNK8berberis16TranslationCache21IsHostFunctionWrappedEm");
    U64 (*read_fpcr)(U32)=symbol<decltype(read_fpcr)>("_ZN8berberis10intrinsics13Arm64ReadFpcrEj");
    U64 (*read_fpsr)(U32)=symbol<decltype(read_fpsr)>("_ZN8berberis10intrinsics13Arm64ReadFpsrEj");
    U64 (*write_fpcr)(U64)=symbol<decltype(write_fpcr)>("_ZN8berberis10intrinsics16Arm64WriteToFpcrEm");
    U64 (*write_fpsr)(U64)=symbol<decltype(write_fpsr)>("_ZN8berberis10intrinsics16Arm64WriteToFpsrEm");
    U32* stop=symbol<U32*>("_ZN8berberis10kEntryStopE");
    U32* noexec=symbol<U32*>("_ZN8berberis12kEntryNoExecE");
    U32* wrapping=symbol<U32*>("_ZN8berberis14kEntryWrappingE");
    U32* untranslated=symbol<U32*>("_ZN8berberis19kEntryNotTranslatedE");
    U32* interpreted=symbol<U32*>("_ZN8berberis15kEntryInterpretE");
    void* cache=nullptr;
    Api() {
        dl_iterate_phdr(verify_module,nullptr);
        if(!verified_build) fatal("Unsupported native-bridge build; refusing private ABI access");
        const char* mode=getenv("BERBERIS_MODE");
        if(!mode || std::strcmp(mode,"interpret-only")) fatal("BERBERIS_MODE=interpret-only is required to avoid stock JIT execution");
        auto reg=symbol<size_t(*)(int)>("_ZN8berberis23GetThreadStateRegOffsetEi");
        auto vec=symbol<size_t(*)(int)>("_ZN8berberis27GetThreadStateSimdRegOffsetEi");
        auto flags=symbol<size_t(*)()>("_ZN8berberis24GetThreadStateFlagOffsetEv");
        if(reg(30)!=240 || vec(0)!=0x110 || flags()!=0xf8) fatal("Guest state ABI mismatch");
        cache=get_cache();
        __android_log_print(ANDROID_LOG_INFO,"AXRB.Dynarmic","dispatch adapter active; build=2810e5b44895c4ea0b1ad6882ea73b73; stock instruction fallback enabled");
    }
    U32 code(U64 pc) {
        if(pc>>48) return *noexec;
        auto table=at<std::atomic<std::atomic<U32>*>*>(cache,0x80);
        return table[pc>>24].load(std::memory_order_acquire)[pc&0xffffff].load(std::memory_order_acquire);
    }
    bool boundary(U64 pc) { const auto c=code(pc); return c!=*untranslated && c!=*interpreted; }
};
Api& api() { static Api instance; return instance; }

struct Context final : Dynarmic::A64::UserCallbacks {
    std::unique_ptr<Dynarmic::A64::Jit> jit;
    U64 ticks{},tls{};
    U64 miss_start{},miss_cpu_start{};
    bool fallback=false,boundary_exit=false;
    U64 fallback_pc{}, fallback_sample_counter{};
    Context() {
        Dynarmic::A64::UserConfig config{};
        config.callbacks=this; config.tpidr_el0=&tls; config.tpidrro_el0=&tls;
        config.code_cache_size=64*1024*1024;
        config.fastmem_pointer=0; config.fastmem_address_space_bits=48;
        config.silently_mirror_fastmem=false;
        config.cntfrq_el0=1000000000;
        jit=std::make_unique<Dynarmic::A64::Jit>(config);
        std::lock_guard lock(contexts_mutex);
        contexts.push_back(jit.get());
    }
    ~Context() {
        std::lock_guard lock(contexts_mutex);
        std::erase(contexts,jit.get());
    }
    template<class T> T read(U64 address) { T result; std::memcpy(&result,reinterpret_cast<void*>(address),sizeof(T)); return result; }
    template<class T> void write(U64 address,T value) { std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T)); }
    std::optional<U32> MemoryReadCode(U64 pc) override {
        if(!miss_start) { miss_start=timestamp(CLOCK_MONOTONIC); miss_cpu_start=timestamp(CLOCK_THREAD_CPUTIME_ID); }
        code_reads.fetch_add(1,std::memory_order_relaxed);
        if(pc<4096) {
            __android_log_print(ANDROID_LOG_FATAL,"AXRB.Dynarmic","null code pc=%llx lr=%llx x16=%llx x17=%llx",(unsigned long long)pc,(unsigned long long)jit->GetRegister(30),(unsigned long long)jit->GetRegister(16),(unsigned long long)jit->GetRegister(17));
            fatal("invalid guest PC");
        }
        if(api().boundary(pc)) return 0xd41fffe1; // synthetic SVC #65535
        const auto insn=read<U32>(pc);
        // These operations do not touch the interpreter's exclusive monitor.
        // Let Dynarmic retain them in the compiled block instead of exporting
        // and importing the complete CPU state around a single instruction.
        if(!legacy_fallback) {
            // This version-gated Berberis build treats these PAC hints as NOPs.
            // Do not extend this to arbitrary pointer-authentication operations.
            if(insn==0xd503233f || insn==0xd503237f || insn==0xd50323bf || insn==0xd50323ff ||
               (insn&0xffffffe0)==0xd53bd040 || (insn&0xffffffe0)==0xd51bd040)
                return insn;
            // BTI landing-pad hints are also NOPs in this exact stock bridge
            // (checked by bridge_probe). Keep them in cached compiled blocks.
            if(insn==0xd503241f || insn==0xd503245f || insn==0xd503249f || insn==0xd50324df) return insn;
            const auto ordered=insn&0x3ffffc00;
            if(direct_acquire && ordered==0x08dffc00) return 0xd41fffa1; // SVC #65533
            // Dynarmic's ordered fastmem loads use locked XADD: they require
            // write permission even for LDAR. The callback above uses a real
            // acquire read; when disabled, LDAR falls back to the interpreter.
            if(insn==0xd503201f || ordered==0x089ffc00 ||
               (insn&0xfffff0ff)==0xd50330bf || (insn&0xfffff0ff)==0xd503309f)
                return insn;
        }
        // Keep exclusive reservations, system-register semantics, and host
        // syscall dispatch in the same existing per-thread interpreter state.
        if(((insn>>24)&0x3f)==8 || (insn&0xff000000)==0xd5000000) return 0xd41fffc1; // SVC #65534
        return insn;
    }
    std::uint8_t MemoryRead8(U64 a) override { return read<std::uint8_t>(a); }
    std::uint16_t MemoryRead16(U64 a) override { return read<std::uint16_t>(a); }
    U32 MemoryRead32(U64 a) override { return read<U32>(a); }
    U64 MemoryRead64(U64 a) override { return read<U64>(a); }
    Vec MemoryRead128(U64 a) override { return read<Vec>(a); }
    void MemoryWrite8(U64 a,std::uint8_t v) override { write(a,v); }
    void MemoryWrite16(U64 a,std::uint16_t v) override { write(a,v); }
    void MemoryWrite32(U64 a,U32 v) override { write(a,v); }
    void MemoryWrite64(U64 a,U64 v) override { write(a,v); }
    void MemoryWrite128(U64 a,Vec v) override { write(a,v); }
    void InterpreterFallback(U64 pc,size_t) override { fallback=true; fallback_pc=pc; jit->HaltExecution(); }
    void CallSVC(U32 number) override {
        if(number==65533) {
            const auto pc=jit->GetPC()-4;
            const U32 insn=read<U32>(pc);
            const unsigned base=(insn>>5)&31, dest=insn&31, size=insn>>30;
            const U64 address=base==31?jit->GetSP():jit->GetRegister(base);
            // A true acquire read: unlike Dynarmic's locked XADD fast path it
            // never writes the page. Keep unaligned accesses on the interpreter
            // so its architectural fault handling is preserved.
            if((address&((U64{1}<<size)-1))==0) {
                U64 value=0;
                switch(size) {
                case 0: value=__atomic_load_n(reinterpret_cast<const std::uint8_t*>(address),__ATOMIC_ACQUIRE); break;
                case 1: value=__atomic_load_n(reinterpret_cast<const std::uint16_t*>(address),__ATOMIC_ACQUIRE); break;
                case 2: value=__atomic_load_n(reinterpret_cast<const U32*>(address),__ATOMIC_ACQUIRE); break;
                case 3: value=__atomic_load_n(reinterpret_cast<const U64*>(address),__ATOMIC_ACQUIRE); break;
                }
                if(dest!=31) jit->SetRegister(dest,value);
                ++counters.acquire;
                return;
            }
        }
        if(number==65535) boundary_exit=true; else fallback=true;
        fallback_pc=jit->GetPC()-4; jit->HaltExecution();
    }
    void ExceptionRaised(U64 pc,Dynarmic::A64::Exception) override { InterpreterFallback(pc,1); }
    void AddTicks(U64 n) override { ticks-=std::min(ticks,n); }
    U64 GetTicksRemaining() override { return ticks; }
    U64 GetCNTPCT() override { timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t); return U64(t.tv_sec)*1000000000+t.tv_nsec; }
    void run(void* state) {
        auto& a=api();
        jit->ClearHalt();
        std::array<U64,31> regs{}; std::memcpy(regs.data(),state,sizeof(regs)); jit->SetRegisters(regs);
        std::array<Vec,32> vectors{}; std::memcpy(vectors.data(),static_cast<char*>(state)+0x110,sizeof(vectors)); jit->SetVectors(vectors);
        jit->SetSP(at<U64>(state,0x108)); jit->SetPC(at<U64>(state,0x310));
        const U32 f=at<U32>(state,0xf8);
        jit->SetPstate(((f&0x8000)<<16)|((f&0x4000)<<16)|((~f&0x100)<<21)|((f&1)<<28));
        jit->SetFpcr(a.read_fpcr(at<U32>(state,0xfc)));
        jit->SetFpsr(a.read_fpsr(at<U32>(state,0x104)|(at<U32>(state,0x100)?0x08000000:0)));
        tls=at<U64>(state,0x338); ticks=Budget; fallback=boundary_exit=false; miss_start=0;
        jit->Run();
        if(miss_start) {
            const U64 wall=timestamp(CLOCK_MONOTONIC)-miss_start;
            miss_runs.fetch_add(1,std::memory_order_relaxed);
            miss_wall_ns.fetch_add(wall,std::memory_order_relaxed);
            miss_cpu_ns.fetch_add(timestamp(CLOCK_THREAD_CPUTIME_ID)-miss_cpu_start,std::memory_order_relaxed);
            auto maximum=miss_max_ns.load(std::memory_order_relaxed);
            while(maximum<wall && !miss_max_ns.compare_exchange_weak(maximum,wall,std::memory_order_relaxed)) {}
        }
        regs=jit->GetRegisters(); std::memcpy(state,regs.data(),sizeof(regs));
        vectors=jit->GetVectors(); std::memcpy(static_cast<char*>(state)+0x110,vectors.data(),sizeof(vectors));
        at<U64>(state,0x108)=jit->GetSP(); at<U64>(state,0x310)=fallback||boundary_exit?fallback_pc:jit->GetPC();
        const U32 p=jit->GetPstate(); at<U32>(state,0xf8)=((p>>16)&0xc000)|((~p>>21)&0x100)|((p>>28)&1);
        at<U32>(state,0xfc)=a.write_fpcr(jit->GetFpcr());
        const U32 fpsr=a.write_fpsr(jit->GetFpsr()); at<U32>(state,0x104)=fpsr&~0x08000000; at<U32>(state,0x100)=(fpsr>>27)&1;
        at<U64>(state,0x338)=tls;
        counters.instructions+=Budget-ticks;
        if(fallback) {
            ++counters.fallback;
            if(profile_fallback && (++fallback_sample_counter % 1021)==0) sample_fallback(read<U32>(fallback_pc));
            a.interpret(state);
        }
    }
};
}
extern "C" void axrb_dynarmic_print_stats() {
    counters.flush();
    if(profile_fallback) {
        std::array<FallbackSample,128> samples;
        { std::lock_guard lock(fallback_sample_mutex); samples=fallback_samples; }
        std::sort(samples.begin(),samples.end(),[](auto a,auto b){return a.count>b.count;});
        for(size_t i=0;i<8 && samples[i].count;++i)
            __android_log_print(ANDROID_LOG_INFO,"AXRB.Dynarmic","fallback_sample opcode=%08x samples=%llu",samples[i].instruction,(unsigned long long)samples[i].count);
    }
    // Includes compilation plus the remainder of that JIT quantum, not pure
    // compiler time. CPU vs wall time helps distinguish work from descheduling.
    __android_log_print(ANDROID_LOG_INFO,"AXRB.Dynarmic","jit_miss_runs=%llu miss_wall_ms=%.3f miss_cpu_ms=%.3f miss_max_ms=%.3f",
        (unsigned long long)miss_runs.load(),miss_wall_ns.load()/1e6,miss_cpu_ns.load()/1e6,miss_max_ns.load()/1e6);
    __android_log_print(ANDROID_LOG_INFO,"AXRB.Dynarmic","instructions=%llu fallback=%llu dispatch=%llu host_calls=%llu code_reads=%llu invalidations=%llu direct_acquire=%llu",
        (unsigned long long)executed.load(),(unsigned long long)fallbacks.load(),(unsigned long long)entries.load(),(unsigned long long)host_calls.load(),(unsigned long long)code_reads.load(),(unsigned long long)generation.load(),(unsigned long long)acquire_calls.load());
    std::fprintf(stderr,"AXRB.Dynarmic instructions=%llu fallback=%llu dispatch=%llu host_calls=%llu\n",
        (unsigned long long)executed.load(),(unsigned long long)fallbacks.load(),(unsigned long long)entries.load(),(unsigned long long)host_calls.load());
}
extern "C" void berberis_RunGeneratedCode(void* state,const void* code) {
    auto& a=api();
    if(a.boundary(at<U64>(state,0x310))) { ++counters.host; a.run(state,code); counters.tick(); return; }
    thread_local Context context;
    context.run(state);
    ++counters.dispatch;
    if(counters.tick()) {
        timespec now{}; clock_gettime(CLOCK_MONOTONIC,&now);
        U64 previous=next_report.load(std::memory_order_relaxed);
        if(U64(now.tv_sec)>=previous && next_report.compare_exchange_strong(previous,U64(now.tv_sec)+5))
            axrb_dynarmic_print_stats();
    }
}
namespace berberis {
class TranslationCache { public: void InvalidateGuestRange(U64 start,U64 end); };
void TranslationCache::InvalidateGuestRange(U64 start,U64 end) {
    static auto original=symbol<void(*)(void*,U64,U64)>("_ZN8berberis16TranslationCache20InvalidateGuestRangeEmm");
    original(this,start,end);
    if(end<=start) return;
    ++generation;
    // Dynarmic's range invalidation is thread-safe and halts running JITs.
    // Keep unrelated compiled code when Android unmaps or changes a data range.
    std::lock_guard lock(contexts_mutex);
    for(auto* jit:contexts) jit->InvalidateCacheRange(start,end-start);
}
}
