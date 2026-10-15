#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static pthread_barrier_t barrier;
static unsigned char* code;
enum { pages = 2048, threads = 4 };
static void* execute_pages(void* opaque) {
    uintptr_t worker = (uintptr_t)opaque;
    pthread_barrier_wait(&barrier);
    for (unsigned i = 0; i < pages / threads; ++i) {
        unsigned page = i * threads + worker;
        int (*function)(void) = (int (*)(void))(code + page * 4096);
        assert(function() == 42);
    }
    return NULL;
}
int main(void) {
    char path[] = "/data/local/tmp/translator-pages-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    unlink(path);
    uint32_t page[1024] = {0xd2800540, 0xd65f03c0}; // mov x0,42; ret
    for (unsigned i = 0; i < pages; ++i) assert(write(fd, page, sizeof page) == sizeof page);
    pthread_barrier_init(&barrier, NULL, threads);
    for (int round = 0; round < 4; ++round) {
        // Shared mappings use the lazy path even for this small fixture.
        code = mmap(NULL, pages * 4096, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
        assert(code != MAP_FAILED);
        pthread_t workers[threads];
        for (uintptr_t i = 0; i < threads; ++i) assert(!pthread_create(&workers[i], NULL, execute_pages, (void*)i));
        for (int i = 0; i < threads; ++i) assert(!pthread_join(workers[i], NULL));
        assert(!munmap(code, pages * 4096));
    }
    pthread_barrier_destroy(&barrier);
    close(fd);
    puts("TRANSLATOR_MMAP_SELFTEST_PASS");
    return 0;
}
