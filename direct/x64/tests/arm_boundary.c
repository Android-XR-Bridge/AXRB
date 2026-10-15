#include "../arm_bridge_abi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
static long call(long op,long a,long b,long version){return syscall(AXRB_ARM_SYSCALL,op,a,b,version);}
static long invocation;
static void* wrong_thread(void* unused) {
    (void)unused;struct AxrbArmRegisters registers={0};errno=0;
    long result=call(AXRB_ARM_STEP,invocation,(long)&registers,1);
    return (void*)(intptr_t)(result==-1 && errno==EBADF?0:1);
}
int main(void) {
    errno=0;if(call(AXRB_ARM_GATES,0,0,2)!=-1 || errno!=EINVAL)return 10;
    errno=0;if(call(AXRB_ARM_BEGIN,1,0,1)!=-1 || errno!=EFAULT)return 11;
    errno=0;if(call(AXRB_ARM_END,123456,0,1)!=-1 || errno!=EBADF)return 12;
    FILE* file=fopen("/data/local/tmp/arm-boundary/input.so","rb");if(!file)return 13;
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    unsigned char* data=malloc((size_t)size);if(!data)return 14;
    if(fread(data,1,(size_t)size,file)!=(size_t)size)return 15;fclose(file);
    errno=0;if(call(AXRB_ARM_LOAD,(long)data,20,1)!=-1 || errno!=ENOEXEC)return 16;
    data[18]=62;errno=0;
    if(call(AXRB_ARM_LOAD,(long)data,size,1)!=-1 || errno!=ENOEXEC)return 17;
    data[18]=183;
    // Corrupt the program-table range without making a host pointer dereference.
    uint64_t offset;memcpy(&offset,data+32,8);uint64_t bad=UINT64_MAX;memcpy(data+32,&bad,8);
    errno=0;if(call(AXRB_ARM_LOAD,(long)data,size,1)!=-1 || errno!=ENOEXEC)return 18;
    memcpy(data+32,&offset,8);
    long library=call(AXRB_ARM_LOAD,(long)data,size,1);free(data);if(library<=0)return 19;
    errno=0;if(call(AXRB_ARM_SYMBOL,library,(long)"missing",1)!=-1 || errno!=ENOENT)return 20;
    long pc=call(AXRB_ARM_SYMBOL,library,(long)"arm_round_trip",1);if(pc<=0)return 21;
    if(call(AXRB_ARM_IS_FUNCTION,pc,0,1)!=1 || call(AXRB_ARM_IS_FUNCTION,(long)main,0,1)!=0)return 22;
    invocation=call(AXRB_ARM_BEGIN,pc,0,1);if(invocation<=0)return 23;
    pthread_t worker;void* result=0;
    if(pthread_create(&worker,0,wrong_thread,0) || pthread_join(worker,&result) || result)return 24;
    if(call(AXRB_ARM_END,invocation,0,1))return 25;
    puts("NATIVE_X64_ARM_BOUNDARY_PASS malformed ELF, version, symbols, ownership");return 0;
}
