#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>

static _Atomic uint64_t counter;
static void* worker(void* unused) {
    (void)unused;
    for (int i = 0; i < 10000; ++i) atomic_fetch_add(&counter, 1);
    return NULL;
}
int main(void) {
    pthread_t threads[4];
    for (int i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, worker, NULL));
    for (int i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    assert(counter == 40000);
    uint64_t value = 10, old, increment = 7;
    __asm__ volatile("ldaddal %x[increment], %x[old], [%x[ptr]]"
        : [old] "=&r"(old) : [increment] "r"(increment), [ptr] "r"(&value) : "memory");
    assert(old == 10 && value == 17);
    uint64_t expected = 17, replacement = 42;
    __asm__ volatile("casal %x[expected], %x[replacement], [%x[ptr]]"
        : [expected] "+r"(expected) : [replacement] "r"(replacement), [ptr] "r"(&value) : "memory");
    assert(expected == 17 && value == 42);
    puts("TRANSLATOR_ATOMICS_SELFTEST_PASS");
    return 0;
}
