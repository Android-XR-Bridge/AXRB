// Top-byte-ignore: ARM64 loads and stores disregard bits 56-63 of the address.
// The same code runs first with plain pointers, often enough for every
// translation tier to have compiled it, then with tagged ones, on this thread
// and on one that was already running. Results must not depend on the tag.
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kWords = 64 };

static uint64_t* tag(uint64_t* p, uint64_t value) { return (uint64_t*)(((uintptr_t)p & 0x00ffffffffffffffull) | value << 56); }

// Every addressing form, with live registers across each access.
static __attribute__((noinline)) uint64_t work(uint64_t* p, uint64_t seed) {
    uint64_t sum = seed, a, b;
    uint64_t* q = p;
    for (int i = 0; i < kWords; ++i) p[i] = seed * (uint64_t)(i + 1) + (sum ^ (uint64_t)i);
    for (int i = 0; i < kWords; ++i) sum = sum * 31 + p[i];
    sum += ((uint8_t*)p)[3] + ((uint16_t*)p)[5] + (uint64_t)((int32_t*)p)[7] + (uint64_t)((int8_t*)p)[9];
    ((uint8_t*)p)[11] = (uint8_t)sum;
    ((uint16_t*)p)[13] = (uint16_t)sum;
    ((uint32_t*)p)[15] = (uint32_t)sum;
    __asm__ volatile(
        "ldr %[a], [%[q]], #8\n"            // post-index
        "ldr %[b], [%[q], #8]!\n"           // pre-index
        "add %[sum], %[sum], %[a]\n"
        "eor %[sum], %[sum], %[b]\n"
        "ldp %[a], %[b], [%[q], #16]\n"
        "add %[sum], %[sum], %[a], lsl #1\n"
        "sub %[sum], %[sum], %[b]\n"
        "stp %[sum], %[a], [%[q], #32]!\n"  // pre-index pair store
        "ldp %[a], %[b], [%[q]], #16\n"     // post-index pair load
        "add %[sum], %[sum], %[b]\n"
        "mov x9, #24\n"
        "ldr %[a], [%[q], x9]\n"            // register offset
        "eor %[sum], %[sum], %[a]\n"
        "str %[sum], [%[q], x9, lsl #0]\n"
        "ld1 {v16.16b}, [%[q]]\n"           // vector load and store
        "ldr q17, [%[q], #16]\n"
        "add v16.2d, v16.2d, v17.2d\n"
        "st1 {v16.16b}, [%[q]]\n"
        "str q16, [%[q], #48]\n"
        "ldr d18, [%[q], #8]\n"
        "fmov %[a], d18\n"
        "add %[sum], %[sum], %[a]\n"
        "1: ldxr %[a], [%[q]]\n"            // exclusive pair
        "add %[a], %[a], #3\n"
        "stxr w9, %[a], [%[q]]\n"
        "cbnz w9, 1b\n"
        "ldrsw %[b], [%[q], #4]\n"
        "add %[sum], %[sum], %[b]\n"
        : [sum] "+r"(sum), [q] "+r"(q), [a] "=&r"(a), [b] "=&r"(b)
        :
        : "x9", "v16", "v17", "v18", "memory");
    sum += (uint64_t)(q - p);
    sum += atomic_fetch_add((_Atomic uint64_t*)(p + 40), sum);
    for (int i = 0; i < kWords; ++i) sum = (sum << 7 | sum >> 57) ^ p[i];
    return sum;
}

static uint64_t run(uint64_t* p, int rounds) {
    uint64_t sum = 1469598103934665603ull;
    for (int i = 0; i < rounds; ++i) sum = work(p, sum + (uint64_t)i);
    return sum;
}

static atomic_int stage;
static uint64_t thread_plain, thread_tagged;

static void* thread_main(void* argument) {
    uint64_t* buffer = argument;
    thread_plain = run(buffer, 3000);
    atomic_store(&stage, 1);
    while (atomic_load(&stage) != 2) {}
    thread_tagged = run(tag(buffer, 0x5a), 3000);
    return NULL;
}

int main(void) {
    uint64_t* buffer = aligned_alloc(64, kWords * sizeof(uint64_t));
    uint64_t* other = aligned_alloc(64, kWords * sizeof(uint64_t));
    pthread_t thread;
    if (!buffer || !other || pthread_create(&thread, NULL, thread_main, other)) return 2;
    const uint64_t plain = run(buffer, 3000);
    while (atomic_load(&stage) != 1) {}
    const uint64_t tagged = run(tag(buffer, 0xb4), 3000);
    const uint64_t again = run(tag(buffer, 0xff), 3000);
    atomic_store(&stage, 2);
    pthread_join(thread, NULL);
    printf("checksum %016llx %016llx\n", (unsigned long long)plain, (unsigned long long)thread_plain);
    if (plain != tagged || plain != again || thread_plain != thread_tagged || plain != thread_plain) {
        printf("TRANSLATOR_TBI_SELFTEST_FAIL %016llx %016llx %016llx %016llx\n", (unsigned long long)tagged,
               (unsigned long long)again, (unsigned long long)thread_plain, (unsigned long long)thread_tagged);
        return 1;
    }
    puts("TRANSLATOR_TBI_SELFTEST_PASS");
    return 0;
}
