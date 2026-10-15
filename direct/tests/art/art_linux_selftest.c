#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    pthread_attr_t attr;
    void* stack = 0;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) || pthread_attr_getstack(&attr, &stack, &size)) return 1;
    uintptr_t local = (uintptr_t)&attr;
    if (local < (uintptr_t)stack || local >= (uintptr_t)stack + size) return 2;
    pthread_attr_destroy(&attr);
    if (access("/system/build.prop", R_OK)) return 3;
    errno = 0;
    if (access("/system/build.prop", W_OK) == 0 || (errno != EROFS && errno != EACCES)) return 4;
    void* low = mmap((void*)0x02000000, 64 * 1024 * 1024, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (low == MAP_FAILED || low != (void*)0x02000000) return 5;
    if (mprotect(low, 4096, PROT_READ | PROT_WRITE)) return 6;
    *(volatile uint64_t*)low = 0x12345678;
    if (*(volatile uint64_t*)low != 0x12345678) return 7;
    int zero = open("/dev/zero", O_RDONLY);
    if (zero < 0) return 10;
    if (mprotect(low, 4096, PROT_READ)) return 11;
    errno = 0;
    if (read(zero, low, 8) != -1 || errno != EFAULT) return 12;
    if (mprotect(low, 4096, PROT_READ | PROT_WRITE)) return 13;
    void* tagged = (void*)((uintptr_t)low | UINT64_C(0xab00000000000000));
    if (read(zero, tagged, 8) != 8 || *(volatile uint64_t*)low != 0) return 14;
    close(zero);
    errno = 0;
    void* collision = mmap(low, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (collision != MAP_FAILED || errno != EEXIST) return 8;
    if (munmap(low, 64 * 1024 * 1024)) return 9;
    puts("ART_LINUX_SELFTEST_PASS");
    return 0;
}
