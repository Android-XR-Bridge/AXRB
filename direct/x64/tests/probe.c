#include <stdint.h>
#include <stddef.h>

// Explicitly audited, no-red-zone fixture; not a claim that arbitrary ELF is safe.
__attribute__((used,section(".axrb.native")))
static const char contract[] = "AXRB_NATIVE_PROBE_V1_NO_RED_ZONE";
static const volatile unsigned char data_syscall[] = {0x0f, 0x05};
static uintptr_t tls[8];
extern int immediate_probe(void);
extern int red_zone_probe(void);
extern int gs_bridge_probe(uintptr_t*);
static inline long sc(long n,long a,long b,long c,long d,long e,long f) {
    register long r10 __asm__("r10")=d;
    register long r8 __asm__("r8")=e;
    register long r9 __asm__("r9")=f;
    __asm__ volatile("syscall" : "+a"(n) : "D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9) : "rcx","r11","memory");
    return n;
}
struct ts { long sec, ns; };
int probe_main(uintptr_t* initial) {
    if(initial[0]!=1 || !initial[1] || initial[2] || initial[3]) return 10;
    int page=0, entry=0;
    for(uintptr_t* a=initial+4; a[0]; a+=2) {
        if(a[0]==6 && a[1]==4096) page=1;
        if(a[0]==9 && a[1]) entry=1;
    }
    if(!page || !entry || data_syscall[0]!=0x0f || data_syscall[1]!=5) return 11;
    if(immediate_probe()!=0x050f) return 27;
    if(sc(1,1,1,16,0,0,0)!=-14) return 12; // EFAULT, not a host crash.
    if(sc(9999,0,0,0,0,0,0)!=-38) return 13;
    long p=sc(9,0,4096,3,0x22,-1,0);
    if(p<0) return 14;
    volatile uint64_t* words=(void*)p;
    uint64_t hash=7;
    for(unsigned i=0;i<1000000;i++) hash=(hash*6364136223846793005ULL+1)^i;
    words[0]=hash; words[1]=0x123456789abcdef0ULL;
    if(sc(10,p,4096,1,0,0,0) || words[0]!=hash || words[1]!=0x123456789abcdef0ULL) return 15;
    if(sc(10,p,4096,5,0,0,0)!=-13) return 16; // No uninstrumented executable mappings.
    if(sc(10,p,4096,3,0,0,0) || sc(28,p,4096,4,0,0,0) || words[0] || words[1]) return 30;
    if(sc(11,p,4096,0,0,0,0)) return 17;
    if(sc(1,1,p,16,0,0,0)!=-14) return 18;
    tls[0]=(uintptr_t)tls; tls[1]=0x12345678abcdef01ULL;
    if(sc(158,0x1002,(long)tls,0,0,0,0)) return 19; // ARCH_SET_FS.
    uintptr_t read_tls;
    __asm__ volatile("mov %%fs:8,%0" : "=r"(read_tls));
    if(read_tls!=tls[1]) return 20;
    uintptr_t base=0;
    if(sc(158,0x1003,(long)&base,0,0,0,0) || base!=(uintptr_t)tls) return 21;
    if(sc(158,0x1001,(long)tls,0,0,0,0)) return 22; // Virtual guest GS; Windows owns hardware GS.
    uintptr_t read_gs;
    __asm__ volatile("mov %%gs:8,%0" : "=r"(read_gs));
    if(read_gs!=tls[1]) return 28;
    if(gs_bridge_probe(tls)) return 29;
    struct ts before,after,delay={0,5000000};
    if(sc(228,1,(long)&before,0,0,0,0) || sc(35,(long)&delay,0,0,0,0,0) || sc(228,1,(long)&after,0,0,0,0)) return 23;
    long elapsed=(after.sec-before.sec)*1000000000L+after.ns-before.ns;
    if(elapsed<1000000 || elapsed>2000000000L) return 24;
    if(sc(39,0,0,0,0,0,0)<=0 || sc(186,0,0,0,0,0,0)<=0) return 25;
    if(red_zone_probe()) {
        static const char line[]="RED_ZONE_OBSERVED_PRESERVED\n";
        sc(1,1,(long)line,sizeof(line)-1,0,0,0);
    } else {
        static const char line[]="RED_ZONE_OBSERVED_CLOBBERED\n";
        sc(1,1,(long)line,sizeof(line)-1,0,0,0);
    }
    static const char message[]="NATIVE_X64_PROBE_PASS (ELF, SysV, syscalls, native FS TLS, memory, clock)\n";
    if(sc(1,1,(long)message,sizeof(message)-1,0,0,0)!=(long)sizeof(message)-1) return 26;
    return 0;
}
