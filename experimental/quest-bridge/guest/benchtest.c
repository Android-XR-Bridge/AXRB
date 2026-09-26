/* A speed benchmark for the interpreter and the JIT: ordinary integer code
   of the kind game logic compiles to (loops, loads and stores, compares,
   branches, calls), with a checksum so a wrong translation cannot pass for a
   fast one. qb_check reports the checksum against the value it must be. */

#include <stdint.h>

void qb_check(const char* label, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

static uint32_t table[4096];

__attribute__((noinline)) static uint32_t mix(uint32_t h, uint32_t v) {
    h ^= v;
    h *= 0x01000193u;
    return h ^ (h >> 13);
}

int qb_guest_main(void) {
    for (uint32_t i = 0; i < 4096; ++i) table[i] = i * 2654435761u;
    uint32_t h = 2166136261u;
    uint64_t sum = 0;
    for (int round = 0; round < 400; ++round) {
        for (uint32_t i = 0; i < 4096; ++i) {
            uint32_t v = table[(i * 7 + round) & 4095];
            if (v & 1) h = mix(h, v);
            else h += v >> 3;
            sum += (uint64_t)h * (i | 1);
            table[i] = v ^ h;
        }
    }
    qb_check("bench checksum", h, sum, 0xda862b9bu, 0x640c3dcc8f5db172ull);
    return 0;
}
