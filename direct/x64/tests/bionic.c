#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

int main(int argc,char** argv) {
    if(argc!=1 || !argv[0]) return 10;
    char* data=malloc(8192);
    if(!data) return 11;
    memset(data,0x57,8192);
    if(data[4095]!=0x57 || data[8191]!=0x57) return 12;
    free(data);
    struct timespec value;
    if(clock_gettime(CLOCK_MONOTONIC,&value) || getpid()<=0) return 13;
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&value) || value.tv_sec<0 || value.tv_nsec<0 || value.tv_nsec>=1000000000) return 16;
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value) || value.tv_sec<0 || value.tv_nsec<0 || value.tv_nsec>=1000000000) return 17;
    // ART's low-memory allocator needs an alternative low address when a hint
    // collides. Returning a high allocation caused millions of msync probes.
    void* reservation=mmap((void*)0x100000,512*1024*1024,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(reservation==MAP_FAILED || (uintptr_t)reservation>=0x80000000){fprintf(stderr,"low reservation=%p errno=%d\n",reservation,errno);return 18;}
    void* alternative=mmap((char*)reservation+4096,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(alternative==MAP_FAILED || (uintptr_t)alternative>=0x80000000 ||
       ((uintptr_t)alternative>=(uintptr_t)reservation && (uintptr_t)alternative<(uintptr_t)reservation+512*1024*1024))return 19;
    *(volatile int*)alternative=123;
    if(munmap(alternative,4096) || munmap(reservation,512*1024*1024))return 20;
    void* library=dlopen("libm.so",RTLD_NOW);
    if(!library) { fprintf(stderr,"dlopen: %s\n",dlerror());return 14; }
    double (*cosine)(double)=(double (*)(double))dlsym(library,"cos");
    if(!cosine || cosine(0.0)!=1.0) return 15;
    dlclose(library);
    puts("NATIVE_X64_BIONIC_PASS (dynamic linker, libc, malloc, TLS, clock, dlopen/libm)");
    return 0;
}
