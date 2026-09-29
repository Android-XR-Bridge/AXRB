#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <dlfcn.h>
#include <sys/mman.h>

using U64 = std::uint64_t;
void* library;
template<class T> T sym(const char* name) {
    void* p = dlsym(library, name);
    if (!p) { std::fprintf(stderr,"missing %s: %s\n",name,dlerror()); std::exit(1); }
    return reinterpret_cast<T>(p);
}
template<class T> T& field(void* state, size_t offset) { return *reinterpret_cast<T*>(static_cast<char*>(state)+offset); }
int main() {
    setenv("BERBERIS_MODE", "interpret-only", 1);
    library = dlopen("/system/lib64/libndk_translation.so", RTLD_NOW | RTLD_GLOBAL);
    if (!library) { std::fprintf(stderr,"dlopen: %s\n", dlerror()); return 1; }
    sym<void(*)()>("_ZN8berberis12InitBerberisEv")();
    auto state = sym<void*(*)()>("_ZN8berberis17CreateThreadStateEv")();
    auto thread = sym<void*(*)(void*)>("_ZN8berberis11GuestThread13CreateForTestEPNS_11ThreadStateE")(state);
    sym<void(*)(void*,void*)>("_ZN8berberis14SetGuestThreadERNS_11ThreadStateEPNS_11GuestThreadE")(state,thread);
    auto code = static_cast<std::uint32_t*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    auto map=sym<void*(*)()>("_ZN8berberis14GuestMapShadow11GetInstanceEv")();
    sym<void(*)(void*,U64,U64)>("_ZN8berberis14GuestMapShadow13SetExecutableEmm")(map,reinterpret_cast<U64>(code),4096);
    code[0]=0xb1000400; // adds x0,x0,#1
    field<U64>(state,0)=~U64{};
    field<U64>(state,0x310)=reinterpret_cast<U64>(code);
    sym<void(*)(void*)>("_ZN8berberis13InterpretInsnEPNS_11ThreadStateE")(state);
    std::printf("state flags=%08x fpcr=%08x fpsr_qc=%08x emulated_fpsr=%08x x0=%llx\n",
        field<unsigned>(state,0xf8),field<unsigned>(state,0xfc),field<unsigned>(state,0x100),field<unsigned>(state,0x104),
        static_cast<unsigned long long>(field<U64>(state,0)));
    for(U64 input : {U64(0), U64(0x7fffffffffffffff), U64(0x8000000000000000), ~U64{}}) {
        code[0]=0xb1000400; code[1]=0xd53b4202;
        field<U64>(state,0)=input; field<U64>(state,0x310)=reinterpret_cast<U64>(code);
        auto step=sym<void(*)(void*)>("_ZN8berberis13InterpretInsnEPNS_11ThreadStateE");
        step(state); step(state);
        std::printf("flags input=%llx stored=%08x nzcv=%llx\n",(unsigned long long)input,field<unsigned>(state,0xf8),(unsigned long long)field<U64>(state,16));
    }
    auto cache=sym<void*(*)()>("_ZN8berberis16TranslationCache11GetInstanceEv")();
    auto step=sym<void(*)(void*)>("_ZN8berberis13InterpretInsnEPNS_11ThreadStateE");
    for(unsigned hint : {0xd503233fu,0xd503237fu,0xd50323bfu,0xd50323ffu,0xd503241fu,0xd503245fu,0xd503249fu,0xd50324dfu}) {
        code[0]=hint; field<U64>(state,240)=0x12345678;
        field<U64>(state,0x108)=0x100000;
        field<U64>(state,0x310)=reinterpret_cast<U64>(code);
        step(state);
        std::printf("stock hint=%08x lr=%llx\n",hint,(unsigned long long)field<U64>(state,240));
        if(field<U64>(state,240)!=0x12345678) return 2;
    }
    code[0]=0xd53bd041; // mrs x1,tpidr_el0
    field<U64>(state,0x338)=0x123456789abc;
    field<U64>(state,0x310)=reinterpret_cast<U64>(code); step(state);
    std::printf("stock TLS=%llx\n",(unsigned long long)field<U64>(state,8));
    if(field<U64>(state,8)!=0x123456789abc) return 3;
    // Register a stop entry after a short arithmetic/branch loop.
    const std::uint32_t program[]={0x91000400,0xf1000421,0x54ffffc1}; // add x0; subs x1; b.ne -8
    std::memcpy(code,program,sizeof(program));
    const U64 stop=reinterpret_cast<U64>(code+3);
    auto entry=sym<void*(*)(void*,U64,unsigned)>("_ZN8berberis16TranslationCache24AddAndLockForTranslationEmj")(cache,stop,0);
    struct Piece {unsigned address,size;};
    const unsigned stop_code=*sym<unsigned*>("_ZN8berberis10kEntryStopE");
    sym<void(*)(void*,U64,void*,unsigned,int,Piece)>("_ZN8berberis16TranslationCache22SetTranslatedAndUnlockEmPNS_14GuestCodeEntryEjNS1_4KindENS_13HostCodePieceE")(
        cache,stop,entry,4,6,{stop_code,0});
    field<U64>(state,0)=0; field<U64>(state,8)=10000; field<U64>(state,0x310)=reinterpret_cast<U64>(code);
    sym<void(*)(void*)>("_ZN8berberis12ExecuteGuestEPNS_11ThreadStateE")(state);
    std::printf("bridge loop result=%llu expected=10000\n",static_cast<unsigned long long>(field<U64>(state,0)));
    if(field<U64>(state,0)!=10000) return 1;
    code[0]=0x91000800; // add x0,x0,#2; invalidate only the changed instruction.
    using Invalidate=void(*)(void*,U64,U64);
    auto invalidate=reinterpret_cast<Invalidate>(dlsym(RTLD_DEFAULT,"_ZN8berberis16TranslationCache20InvalidateGuestRangeEmm"));
    invalidate(cache,reinterpret_cast<U64>(code),reinterpret_cast<U64>(code)+4);
    field<U64>(state,0)=0; field<U64>(state,8)=10000; field<U64>(state,0x310)=reinterpret_cast<U64>(code);
    sym<void(*)(void*)>("_ZN8berberis12ExecuteGuestEPNS_11ThreadStateE")(state);
    std::printf("bridge invalidation result=%llu expected=20000\n",static_cast<unsigned long long>(field<U64>(state,0)));
    if(field<U64>(state,0)!=20000) return 1;
    auto calls=code+64;
    const unsigned call_loop[]={0xd503233f,0xd53bd042,0x91000400,0xd50323bf,0xf1000421,0x54ffff61};
    std::memcpy(calls,call_loop,sizeof(call_loop));
    const U64 call_stop=reinterpret_cast<U64>(calls+6);
    entry=sym<void*(*)(void*,U64,unsigned)>("_ZN8berberis16TranslationCache24AddAndLockForTranslationEmj")(cache,call_stop,0);
    sym<void(*)(void*,U64,void*,unsigned,int,Piece)>("_ZN8berberis16TranslationCache22SetTranslatedAndUnlockEmPNS_14GuestCodeEntryEjNS1_4KindENS_13HostCodePieceE")(
        cache,call_stop,entry,4,6,{stop_code,0});
    field<U64>(state,0)=0; field<U64>(state,8)=10000; field<U64>(state,240)=0x12345678;
    field<U64>(state,0x310)=reinterpret_cast<U64>(calls);
    sym<void(*)(void*)>("_ZN8berberis12ExecuteGuestEPNS_11ThreadStateE")(state);
    std::printf("bridge hints/TLS result=%llu tls=%llx lr=%llx\n",(unsigned long long)field<U64>(state,0),(unsigned long long)field<U64>(state,16),(unsigned long long)field<U64>(state,240));
    auto stats=reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT,"axrb_dynarmic_print_stats"));
    if(stats) stats();
    return field<U64>(state,0)==10000 && field<U64>(state,16)==0x123456789abc && field<U64>(state,240)==0x12345678 ? 0:1;
}
