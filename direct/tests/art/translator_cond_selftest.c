// Every condition code after SUBS, ADDS and ANDS, read by B.cond, CSET/CSEL and
// CCMP, against NZCV computed with plain arithmetic. Run repeatedly so every
// translation tier executes it; the checksum line must match across translators.
#include <stdint.h>
#include <stdio.h>

static const char* const names[14] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};

static int holds(int cond, int n, int z, int c, int v) {
    switch (cond) {
        case 0: return z; case 1: return !z; case 2: return c; case 3: return !c;
        case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
        case 8: return c & !z; case 9: return !c | z;
        case 10: return n == v; case 11: return n != v;
        case 12: return !z & (n == v); default: return z | (n != v);
    }
}

#define CONDITIONS(X) X(eq, 0) X(ne, 1) X(cs, 2) X(cc, 3) X(mi, 4) X(pl, 5) X(vs, 6) X(vc, 7) \
                      X(hi, 8) X(ls, 9) X(ge, 10) X(lt, 11) X(gt, 12) X(le, 13)

// op 0: SUBS, 1: ADDS, 2: ANDS. Returns bit 0: B.cond taken, bit 1: CSET,
// bit 2: CSEL chose the first, bit 3: CCMP ran its compare (then "eq" of 7==7).
#define DEFINE(cond, index) \
    static __attribute__((noinline)) unsigned run_##cond(int op, uint64_t a, uint64_t b) { \
        uint64_t taken = 0, set = 0, selected = 0, compared = 0, seven = 7, one = 1, zero = 0; \
        __asm__ volatile( \
            "cbz %w[op], 1f\n cmp %w[op], #1\n b.eq 2f\n" \
            "ands xzr, %[a], %[b]\n b 3f\n" \
            "1: subs xzr, %[a], %[b]\n b 3f\n" \
            "2: adds xzr, %[a], %[b]\n" \
            "3: cset %[set], " #cond "\n" \
            "csel %[selected], %[one], %[zero], " #cond "\n" \
            "mrs x9, nzcv\n" \
            "ccmp %[seven], #7, #0, " #cond "\n" \
            "cset %[compared], eq\n" \
            "msr nzcv, x9\n" \
            "b." #cond " 4f\n b 5f\n 4: mov %[taken], #1\n 5:\n" \
            : [taken] "+r"(taken), [set] "+r"(set), [selected] "+r"(selected), [compared] "+r"(compared) \
            : [op] "r"(op), [a] "r"(a), [b] "r"(b), [seven] "r"(seven), [one] "r"(one), [zero] "r"(zero) \
            : "x9", "cc"); \
        return (unsigned)(taken | set << 1 | selected << 2 | compared << 3); \
    }
CONDITIONS(DEFINE)
typedef unsigned (*Run)(int, uint64_t, uint64_t);
#define ENTRY(cond, index) run_##cond,
static const Run runs[14] = {CONDITIONS(ENTRY)};

int main(void) {
    static const uint64_t values[] = {0, 1, 2, 7, 0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
        0xffffffffffffffffull, 0xfffffffffffffffeull, 0x100000000ull, 0xffffffffull, 0x5555555555555555ull, 0xaaaaaaaaaaaaaaaaull};
    const int count = sizeof values / sizeof *values;
    int failures = 0;
    uint64_t checksum = 1469598103934665603ull;
    for (int round = 0; round < 40; ++round) {
        for (int i = 0; i < count; ++i) for (int j = 0; j < count; ++j) for (int op = 0; op < 3; ++op) {
            const uint64_t a = values[i], b = values[j];
            uint64_t r;
            int c, v;
            if (op == 0) { r = a - b; c = a >= b; v = (int)(((a ^ b) & (a ^ r)) >> 63); }
            else if (op == 1) { r = a + b; c = r < a; v = (int)((~(a ^ b) & (a ^ r)) >> 63); }
            else { r = a & b; c = 0; v = 0; }
            const int n = (int)(r >> 63), z = r == 0;
            for (int cond = 0; cond < 14; ++cond) {
                const unsigned got = runs[cond](op, a, b), want = holds(cond, n, z, c, v) ? 15u : 0u;
                checksum = (checksum ^ got) * 1099511628211ull;
                if (got != want && failures++ < 20)
                    printf("FAIL %s op=%d a=%llx b=%llx got=%x want=%x round=%d\n", names[cond], op,
                           (unsigned long long)a, (unsigned long long)b, got, want, round);
            }
        }
    }
    printf("checksum %016llx\n", (unsigned long long)checksum);
    if (failures) { printf("TRANSLATOR_COND_SELFTEST_FAIL %d\n", failures); return 1; }
    puts("TRANSLATOR_COND_SELFTEST_PASS");
    return 0;
}
