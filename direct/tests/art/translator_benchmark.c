// Deterministic translator throughput probe, not a game FPS measurement.
// Each line is one construct games lean on, in nanoseconds per operation.
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
#define NOINLINE __attribute__((noinline))
static volatile uint64_t sink;

static NOINLINE uint64_t leaf(uint64_t x) { __asm__ volatile(""); return x + 1; }
static NOINLINE uint64_t leaf_a(uint64_t x) { __asm__ volatile(""); return x + 3; }
static NOINLINE uint64_t leaf_b(uint64_t x) { __asm__ volatile(""); return x ^ 5; }
static NOINLINE uint64_t leaf_c(uint64_t x) { __asm__ volatile(""); return x * 7; }
static NOINLINE uint64_t leaf_d(uint64_t x) { __asm__ volatile(""); return x - 9; }
typedef uint64_t (*Fn)(uint64_t);
static Fn table[4] = {leaf_a, leaf_b, leaf_c, leaf_d};
// One function returning to many call sites, as accessors and allocators do.
static NOINLINE uint64_t sites(uint64_t x) {
    x = leaf(x) + 1; x = leaf(x) ^ 2; x = leaf(x) + 3; x = leaf(x) ^ 4;
    x = leaf(x) + 5; x = leaf(x) ^ 6; x = leaf(x) + 7; x = leaf(x) ^ 8;
    return x;
}
static NOINLINE uint64_t deep(uint64_t x, int depth) {
    __asm__ volatile("");
    return depth ? deep(x + 1, depth - 1) + 1 : x;
}
struct Node { struct Node* next; uint64_t value; uint64_t pad[2]; };
struct Vec4 { float v[4]; };
static NOINLINE void transform(const float* restrict m, const struct Vec4* restrict in, struct Vec4* restrict out, int n) {
    for (int i = 0; i < n; ++i)
        for (int r = 0; r < 4; ++r)
            out[i].v[r] = m[r * 4] * in[i].v[0] + m[r * 4 + 1] * in[i].v[1] + m[r * 4 + 2] * in[i].v[2] + m[r * 4 + 3] * in[i].v[3];
}
static void report(const char* name, double seconds, double operations) {
    printf("%-22s %8.2f ns\n", name, seconds * 1e9 / operations);
    fflush(stdout);
}

int main(void) {
    double start;
    uint64_t x = 1;
    const int n = 20000000;

    float sum = 1.0f, multiplier = 0.999999f, addend = 0.000123f;
    start = now();
    for (int i = 0; i < n; ++i)
        __asm__ volatile("fmadd %s0,%s0,%s1,%s2" : "+w"(sum) : "w"(multiplier), "w"(addend));
    report("fmadd", now() - start, n);

    uint64_t value = 123;
    start = now();
    for (int i = 0; i < n; ++i)
        __asm__ volatile("eor %0,%0,%0,lsl #13\n eor %0,%0,%0,lsr #7\n eor %0,%0,%0,lsl #17" : "+r"(value));
    report("integer xorshift", now() - start, n);

    start = now();
    for (int i = 0; i < n; ++i) x = leaf(x);
    report("call+return", now() - start, n);

    start = now();
    for (int i = 0; i < n / 8; ++i) x = sites(x);
    report("return, 8 sites", now() - start, n);

    start = now();
    for (int i = 0; i < n; ++i) x = table[i & 3](x);
    report("indirect call, 4", now() - start, n);

    start = now();
    for (int i = 0; i < n; ++i) x = table[(x >> 7) & 3](x + i);
    report("indirect call, random", now() - start, n);

    start = now();
    for (int i = 0; i < n / 16; ++i) x = deep(x, 15);
    report("recursion, depth 16", now() - start, n);

    static struct Node nodes[4096];
    for (int i = 0; i < 4096; ++i) { nodes[i].next = &nodes[(i * 1237 + 1) & 4095]; nodes[i].value = i; }
    struct Node* node = nodes;
    start = now();
    for (int i = 0; i < n; ++i) { node->value += x; node = node->next; }
    report("pointer chase+store", now() - start, n);

    static char a[4096], b[4096];
    start = now();
    for (int i = 0; i < n; ++i) { memcpy(a + (i & 63), b + (i & 31), 48); __asm__ volatile("" : : "r"(a)); }
    report("memcpy 48", now() - start, n);
    start = now();
    for (int i = 0; i < n / 20; ++i) { memcpy(a, b, 4096); __asm__ volatile("" : : "r"(a)); }
    report("memcpy 4096", now() - start, n / 20);
    start = now();
    for (int i = 0; i < n / 4; ++i) { memset(a, i, 256); __asm__ volatile("" : : "r"(a)); }
    report("memset 256", now() - start, n / 4);

    static struct Vec4 in[256], out[256];
    static float matrix[16];
    for (int i = 0; i < 16; ++i) matrix[i] = 0.01f * (i + 1);
    for (int i = 0; i < 256; ++i) for (int k = 0; k < 4; ++k) in[i].v[k] = i * 0.5f + k;
    start = now();
    for (int i = 0; i < n / 256; ++i) { transform(matrix, in, out, 256); in[i & 255].v[0] = out[0].v[1]; }
    report("mat4*vec4", now() - start, (n / 256) * 256.0);

    double d = 1.5;
    start = now();
    for (int i = 0; i < n; ++i) { d = d / 1.0000001 + 0.25; __asm__ volatile("fsqrt %d0,%d0" : "+w"(d)); }
    report("fdiv+fsqrt", now() - start, n);

    float f = 3.7f; int converted = 0;
    start = now();
    for (int i = 0; i < n; ++i) { converted += (int)(f + (float)i * 0.001f); __asm__ volatile("" : : "r"(converted)); }
    report("int<->float", now() - start, n);

    static _Atomic uint64_t counter;
    start = now();
    for (int i = 0; i < n; ++i) atomic_fetch_add(&counter, 1);
    report("atomic add", now() - start, n);
    uint64_t expected = 0;
    start = now();
    for (int i = 0; i < n; ++i) { expected = i; atomic_compare_exchange_strong(&counter, &expected, (uint64_t)i + 1); atomic_store(&counter, (uint64_t)i + 1); }
    report("compare+swap", now() - start, n);

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    start = now();
    for (int i = 0; i < n; ++i) { pthread_mutex_lock(&mutex); pthread_mutex_unlock(&mutex); }
    report("mutex lock+unlock", now() - start, n);

    start = now();
    for (int i = 0; i < n / 4; ++i) { void* p = malloc(64 + (i & 255)); *(volatile char*)p = 1; free(p); }
    report("malloc+free", now() - start, n / 4);

    struct timespec ts;
    start = now();
    for (int i = 0; i < n / 4; ++i) clock_gettime(CLOCK_MONOTONIC, &ts);
    report("clock_gettime", now() - start, n / 4);

    static __thread uint64_t local;
    start = now();
    for (int i = 0; i < n; ++i) { local += x; __asm__ volatile("" : : "r"(&local)); }
    report("thread-local add", now() - start, n);

    sink = x + value + converted + (uint64_t)sum + (uint64_t)d + local + node->value + counter;
    return 0;
}
