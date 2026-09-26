/* The differential fuzzer's harness. Built twice from the same source: as a
   native executable for the headset and as a guest library for the
   interpreter. Both print the register state after each generated test, and
   any line that differs is an instruction the interpreter gets wrong.

   Output goes straight to write(1), formatted here, so a bug in the C library
   under test cannot hide or cause a difference. */

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "fuzz_list.h"

struct State {
    uint64_t x[31];
    uint64_t nzcv;
    uint8_t v[32][16];
} __attribute__((aligned(16)));

static uint8_t scratch[0x14000] __attribute__((aligned(64)));

static uint64_t prng_state;
static uint64_t next_random(void) {
    /* splitmix64 */
    uint64_t z = (prng_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static char line[4096];
static int used;

static void put_text(const char* text) {
    while (*text) line[used++] = *text++;
}

static void put_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    line[used++] = ' ';
    for (int shift = 60; shift >= 0; shift -= 4) line[used++] = digits[(value >> shift) & 15];
}

static void put_decimal(unsigned value) {
    char buffer[16];
    int n = 0;
    do {
        buffer[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n) line[used++] = buffer[--n];
}

static void flush_line(void) {
    line[used++] = '\n';
    write(1, line, (size_t)used);
    used = 0;
}

/* Values that exercise the corners: zeros, signs, infinities, NaNs,
   denormals, and the integer extremes. */
static uint64_t special(void) {
    static const uint64_t table[] = {
        0,
        0x8000000000000000ull,
        0x7fffffffffffffffull,
        0xffffffffffffffffull,
        0x3f800000bf800000ull, /* 1.0f, -1.0f */
        0x7f8000007fc00000ull, /* +inf, qNaN (single) */
        0x7ff0000000000000ull, /* +inf (double) */
        0x7ff8000000000001ull, /* qNaN with payload */
        0x0000000100000001ull, /* denormal singles */
        0x0000000000000001ull, /* denormal double */
        0x4000000000000000ull, /* 2.0 */
        0xc01921fb54442d18ull, /* -2 pi */
        0x0000ffff0000ffffull,
        0x00000000ffffffffull,
        0x7f7fffff00800000ull, /* max float, min normal float */
    };
    return table[next_random() % (sizeof(table) / sizeof(table[0]))];
}

static uint64_t hash_scratch(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < sizeof(scratch); ++i) h = (h ^ scratch[i]) * 0x100000001b3ull;
    return h;
}

static void run_all(void) {
    static struct State state;
    for (int i = 0; i < FUZZ_COUNT; ++i) {
        prng_state = 0x5eed0000ull + (uint64_t)i;
        for (size_t b = 0; b < sizeof(scratch); b += 8) {
            uint64_t r = next_random();
            memcpy(scratch + b, &r, 8);
        }
        for (int r = 0; r < 31; ++r) {
            uint64_t kind = next_random() % 4;
            uint64_t value = kind == 0 ? special() : kind == 1 ? (next_random() & 0xff) : next_random();
            if (fuzz_memory[i]) value = next_random() & 63; /* small, so any offset stays in the buffer */
            state.x[r] = value;
        }
        if (fuzz_memory[i]) state.x[27] = (uint64_t)(uintptr_t)(scratch + 0x1000);
        state.nzcv = (next_random() & 0xf) << 28;
        for (int v = 0; v < 32; ++v) {
            uint64_t lo = next_random() % 3 == 0 ? special() : next_random();
            uint64_t hi = next_random() % 3 == 0 ? special() : next_random();
            memcpy(state.v[v], &lo, 8);
            memcpy(state.v[v] + 8, &hi, 8);
        }
        uint64_t base = state.x[27];
        fuzz_tests[i](&state);

        put_text("T");
        put_decimal((unsigned)i);
        for (int r = 0; r < 31; ++r) {
            uint64_t value = state.x[r];
            /* A written-back base is only comparable as an offset. */
            if (fuzz_memory[i] && value >= base - 0x1000 && value < base + 0x13000) value -= base;
            put_hex(value);
        }
        put_hex(state.nzcv);
        for (int v = 0; v < 32; ++v) {
            uint64_t lo, hi;
            memcpy(&lo, state.v[v], 8);
            memcpy(&hi, state.v[v] + 8, 8);
            put_hex(lo);
            put_hex(hi);
        }
        put_hex(fuzz_memory[i] ? hash_scratch() : 0);
        flush_line();
    }
    put_text("done");
    flush_line();
}

#ifdef FUZZ_NATIVE
int main(void) {
    run_all();
    return 0;
}
#else
int qb_guest_main(void) {
    run_all();
    return 0;
}
#endif
