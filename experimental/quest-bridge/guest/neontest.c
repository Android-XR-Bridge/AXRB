/* Instruction tests for the arm64 interpreter.

   Every case computes an answer two ways: once with the instruction under
   test, and once with a reference the compiler is not allowed to optimise
   into the same instruction. If the interpreter has a bug in one of them the
   two disagree, and a bug that produced the same wrong answer twice would
   have to be wrong in the same way in the scalar and the vector path.

   Where an instruction has no scalar counterpart, such as a lane permute, the
   inputs are lane numbers so the expected answer is written out directly.

   This runs under qb-test with no headset and no graphics. */

#include <arm_neon.h>
#include <stdint.h>

/* The one import: the runner prints these and counts the failures. */
extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK_U64(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

static void check_vec(const char* name, uint8x16_t got, uint64_t want_lo, uint64_t want_hi) {
    uint64x2_t as64 = vreinterpretq_u64_u8(got);
    qb_check(name, vgetq_lane_u64(as64, 0), vgetq_lane_u64(as64, 1), want_lo, want_hi);
}

static void check_f32x4(const char* name, float32x4_t got, const float* want) {
    uint64_t lo = 0, hi = 0;
    float32x4_t reference = vld1q_f32(want);
    uint64x2_t g = vreinterpretq_u64_f32(got);
    uint64x2_t r = vreinterpretq_u64_f32(reference);
    lo = vgetq_lane_u64(g, 0);
    hi = vgetq_lane_u64(g, 1);
    qb_check(name, lo, hi, vgetq_lane_u64(r, 0), vgetq_lane_u64(r, 1));
}

/* Kept away from the optimiser so these stay ordinary scalar code. */
__attribute__((noinline, optnone)) static float ref_add(float a, float b) { return a + b; }
__attribute__((noinline, optnone)) static float ref_sub(float a, float b) { return a - b; }
__attribute__((noinline, optnone)) static float ref_mul(float a, float b) { return a * b; }
__attribute__((noinline, optnone)) static float ref_div(float a, float b) { return a / b; }
__attribute__((noinline, optnone)) static double ref_dadd(double a, double b) { return a + b; }
__attribute__((noinline, optnone)) static double ref_dmul(double a, double b) { return a * b; }
__attribute__((noinline, optnone)) static int ref_to_int(float a) { return (int)a; }
__attribute__((noinline, optnone)) static float ref_from_int(int a) { return (float)a; }
__attribute__((noinline, optnone)) static unsigned ref_to_uint(float a) { return (unsigned)a; }
__attribute__((noinline, optnone)) static float ref_from_uint(unsigned a) { return (float)a; }
__attribute__((noinline, optnone)) static int ref_less(float a, float b) { return a < b ? 7 : 9; }
__attribute__((noinline, optnone)) static float ref_select(float a, float b) { return a > b ? a : b; }
__attribute__((noinline, optnone)) static float ref_neg(float a) { return -a; }
__attribute__((noinline, optnone)) static float ref_abs(float a) { return a < 0 ? -a : a; }
__attribute__((noinline, optnone)) static int32_t ref_imul(int32_t a, int32_t b) { return a * b; }
__attribute__((noinline, optnone)) static int32_t ref_iadd(int32_t a, int32_t b) { return a + b; }

/* Counting matches scalar-side keeps the comparison itself out of the test. */
__attribute__((noinline, optnone)) static int ref_same(const unsigned char* a, const unsigned char* b, int n) {
    int same = 0;
    for (int i = 0; i < n; ++i)
        if (a[i] == b[i]) ++same;
    return same;
}

static uint32_t bits_of(float value) {
    uint32_t bits;
    __builtin_memcpy(&bits, &value, 4);
    return bits;
}

static uint64_t bits_of_double(double value) {
    uint64_t bits;
    __builtin_memcpy(&bits, &value, 8);
    return bits;
}

/* Read through volatile so the compiler cannot fold the case away and leave
   the test proving nothing. The first version of this file did exactly that. */
static volatile float g_a = 3.5f;
static volatile float g_b = -1.25f;
static volatile double g_c = 12.75;
static volatile double g_d = 0.5;

static void test_scalar_float(void) {
    float a = g_a, b = g_b;
    CHECK_U64("fadd s", bits_of(a + b), bits_of(ref_add(a, b)));
    CHECK_U64("fsub s", bits_of(a - b), bits_of(ref_sub(a, b)));
    CHECK_U64("fmul s", bits_of(a * b), bits_of(ref_mul(a, b)));
    CHECK_U64("fdiv s", bits_of(a / b), bits_of(ref_div(a, b)));
    CHECK_U64("fneg s", bits_of(-a), bits_of(ref_neg(a)));
    CHECK_U64("fabs s", bits_of(__builtin_fabsf(b)), bits_of(ref_abs(b)));
    CHECK_U64("fsqrt s", bits_of(__builtin_sqrtf(a)), bits_of(1.8708287f));
    CHECK_U64("fmadd s", bits_of(a * b + 2.0f), bits_of(ref_add(ref_mul(a, b), 2.0f)));
    CHECK_U64("fmsub s", bits_of(2.0f - a * b), bits_of(ref_sub(2.0f, ref_mul(a, b))));

    double c = g_c, d = g_d;
    CHECK_U64("fadd d", bits_of_double(c + d), bits_of_double(ref_dadd(c, d)));
    CHECK_U64("fmul d", bits_of_double(c * d), bits_of_double(ref_dmul(c, d)));
    CHECK_U64("fcvt d to s", bits_of((float)c), bits_of(12.75f));
    CHECK_U64("fcvt s to d", bits_of_double((double)a), bits_of_double(3.5));
}

static void test_conversions(void) {
    float a = -7.9f;
    unsigned u = 4000000000u;
    CHECK_U64("fcvtzs", (uint64_t)(int64_t)ref_to_int(a), (uint64_t)(int64_t)(-7));
    CHECK_U64("scvtf", bits_of(ref_from_int(-7)), bits_of(-7.0f));
    CHECK_U64("ucvtf", bits_of(ref_from_uint(u)), bits_of(4000000000.0f));
    CHECK_U64("fcvtzu", ref_to_uint(123.9f), 123u);
    CHECK_U64("fcmp and branch", (uint64_t)ref_less(1.0f, 2.0f), 7u);
    CHECK_U64("fcmp the other way", (uint64_t)ref_less(2.0f, 1.0f), 9u);
    CHECK_U64("fcsel", bits_of(ref_select(2.5f, 1.5f)), bits_of(2.5f));
    /* fmov between a general register and a vector one */
    uint32_t moved = bits_of(a);
    float back;
    __builtin_memcpy(&back, &moved, 4);
    CHECK_U64("fmov both ways", bits_of(back), bits_of(a));
}

static void test_vector_float(void) {
    const float av[4] = {1.5f, -2.25f, 3.0f, 0.5f};
    const float bv[4] = {0.5f, 4.0f, -1.0f, 2.0f};
    float32x4_t x = vld1q_f32(av);
    float32x4_t y = vld1q_f32(bv);
    float want[4];

    for (int i = 0; i < 4; ++i) want[i] = ref_add(av[i], bv[i]);
    check_f32x4("fadd 4s", vaddq_f32(x, y), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_sub(av[i], bv[i]);
    check_f32x4("fsub 4s", vsubq_f32(x, y), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_mul(av[i], bv[i]);
    check_f32x4("fmul 4s", vmulq_f32(x, y), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_div(av[i], bv[i]);
    check_f32x4("fdiv 4s", vdivq_f32(x, y), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_neg(av[i]);
    check_f32x4("fneg 4s", vnegq_f32(x), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_abs(av[i]);
    check_f32x4("fabs 4s", vabsq_f32(x), want);
    for (int i = 0; i < 4; ++i) want[i] = ref_add(ref_mul(av[i], bv[i]), 10.0f);
    float32x4_t acc = vdupq_n_f32(10.0f);
    check_f32x4("fmla 4s", vmlaq_f32(acc, x, y), want);
}

static void test_vector_int(void) {
    const uint32_t av[4] = {1, 2, 3, 4};
    const uint32_t bv[4] = {10, 20, 30, 40};
    uint32x4_t x = vld1q_u32(av);
    uint32x4_t y = vld1q_u32(bv);

    check_vec("add 4s", vreinterpretq_u8_u32(vaddq_u32(x, y)),
              ((uint64_t)ref_iadd(2, 20) << 32) | (uint32_t)ref_iadd(1, 10),
              ((uint64_t)ref_iadd(4, 40) << 32) | (uint32_t)ref_iadd(3, 30));
    check_vec("sub 4s", vreinterpretq_u8_u32(vsubq_u32(y, x)), (18ull << 32) | 9, (36ull << 32) | 27);
    check_vec("mul 4s", vreinterpretq_u8_u32(vmulq_u32(x, y)),
              ((uint64_t)ref_imul(2, 20) << 32) | (uint32_t)ref_imul(1, 10),
              ((uint64_t)ref_imul(4, 40) << 32) | (uint32_t)ref_imul(3, 30));
    check_vec("and 16b", vandq_u8(vreinterpretq_u8_u32(vdupq_n_u32(0xf0f0f0f0)), vdupq_n_u8(0x3c)),
              0x3030303030303030ull, 0x3030303030303030ull);
    check_vec("orr 16b", vorrq_u8(vdupq_n_u8(0xf0), vdupq_n_u8(0x0c)), 0xfcfcfcfcfcfcfcfcull,
              0xfcfcfcfcfcfcfcfcull);
    check_vec("eor 16b", veorq_u8(vdupq_n_u8(0xff), vdupq_n_u8(0x0f)), 0xf0f0f0f0f0f0f0f0ull,
              0xf0f0f0f0f0f0f0f0ull);
    check_vec("bic 16b", vbicq_u8(vdupq_n_u8(0xff), vdupq_n_u8(0x0f)), 0xf0f0f0f0f0f0f0f0ull,
              0xf0f0f0f0f0f0f0f0ull);
    /* bsl picks bits from one or the other according to the mask */
    uint8x16_t mask = vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(0xff00ff00ff00ff00ull),
                                                        vcreate_u64(0xff00ff00ff00ff00ull)));
    check_vec("bsl 16b", vbslq_u8(mask, vdupq_n_u8(0xaa), vdupq_n_u8(0x55)), 0xaa55aa55aa55aa55ull,
              0xaa55aa55aa55aa55ull);
    check_vec("cmeq 4s", vreinterpretq_u8_u32(vceqq_u32(x, vdupq_n_u32(3))), 0, 0xffffffffull);
    check_vec("movi 16b", vdupq_n_u8(0x7e), 0x7e7e7e7e7e7e7e7eull, 0x7e7e7e7e7e7e7e7eull);

    /* The pieces a vectorised "are these arrays equal" loop is built from.
       A round trip through memory hid a fault in these once already. */
    uint8x16_t same = vceqq_u8(vdupq_n_u8(0x42), vdupq_n_u8(0x42));
    check_vec("cmeq 16b all equal", same, 0xffffffffffffffffull, 0xffffffffffffffffull);
    check_vec("cmeq 16b none equal", vceqq_u8(vdupq_n_u8(1), vdupq_n_u8(2)), 0, 0);
    check_vec("orn 16b", vornq_u8(vdupq_n_u8(0x0f), vdupq_n_u8(0xf0)), 0x0f0f0f0f0f0f0f0full,
              0x0f0f0f0f0f0f0f0full);
    check_vec("orn 16b with ones", vornq_u8(vdupq_n_u8(0), same), 0, 0);
    CHECK_U64("umaxv 16b of zero", vmaxvq_u8(vdupq_n_u8(0)), 0u);
    CHECK_U64("umaxv 16b", vmaxvq_u8(vsetq_lane_u8(0x9c, vdupq_n_u8(3), 11)), 0x9cu);
    CHECK_U64("uminv 16b", vminvq_u8(vsetq_lane_u8(2, vdupq_n_u8(0x40), 7)), 2u);
    CHECK_U64("addv 4s", vaddvq_u32(vdupq_n_u32(5)), 20u);
    CHECK_U64("fabd scalar", bits_of(vabds_f32(5.5f, 2.25f)), bits_of(3.25f));
    CHECK_U64("scvtf scalar", bits_of(vcvts_f32_s32(-7)), bits_of(-7.0f));
    check_vec("mvn 16b", vmvnq_u8(vdupq_n_u8(0x0f)), 0xf0f0f0f0f0f0f0f0ull, 0xf0f0f0f0f0f0f0f0ull);
    check_vec("mvn 16b of ones", vmvnq_u8(same), 0, 0);
    check_vec("mvn then orn", vornq_u8(vmvnq_u8(vceqq_u8(vdupq_n_u8(4), vdupq_n_u8(4))), same), 0, 0);
}

static void test_lanes(void) {
    /* Lane numbers as data, so the permute answers are plain to read. */
    const uint8_t bytes[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t other[16] = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    uint8x16_t a = vld1q_u8(bytes);
    uint8x16_t b = vld1q_u8(other);

    check_vec("uzp1 16b", vuzp1q_u8(a, b), 0x0e0c0a0806040200ull, 0x1e1c1a1816141210ull);
    check_vec("uzp2 16b", vuzp2q_u8(a, b), 0x0f0d0b0907050301ull, 0x1f1d1b1917151311ull);
    check_vec("zip1 16b", vzip1q_u8(a, b), 0x1303120211011000ull, 0x1707160615051404ull);
    check_vec("zip2 16b", vzip2q_u8(a, b), 0x1b0b1a0a19091808ull, 0x1f0f1e0e1d0d1c0cull);
    check_vec("trn1 16b", vtrn1q_u8(a, b), 0x1606140412021000ull, 0x1e0e1c0c1a0a1808ull);
    check_vec("trn2 16b", vtrn2q_u8(a, b), 0x1707150513031101ull, 0x1f0f1d0d1b0b1909ull);

    /* dup, ins and umov */
    check_vec("dup lane 4s", vreinterpretq_u8_u32(vdupq_laneq_u32(vreinterpretq_u32_u8(a), 2)),
              0x0b0a09080b0a0908ull, 0x0b0a09080b0a0908ull);
    uint32x4_t inserted = vsetq_lane_u32(0xdeadbeef, vreinterpretq_u32_u8(a), 1);
    check_vec("ins lane 4s", vreinterpretq_u8_u32(inserted), 0xdeadbeef03020100ull, 0x0f0e0d0c0b0a0908ull);
    CHECK_U64("umov lane 4s", vgetq_lane_u32(vreinterpretq_u32_u8(a), 3), 0x0f0e0d0cu);
    CHECK_U64("umov lane 16b", vgetq_lane_u8(a, 5), 5u);
}

static void test_memory(void) {
    /* ld1 and st1 move whole registers; ld2 and ld4 de-interleave as they go. */
    static uint8_t buffer[64];
    for (int i = 0; i < 64; ++i) buffer[i] = (uint8_t)i;

    uint8x16_t one = vld1q_u8(buffer);
    check_vec("ld1 16b", one, 0x0706050403020100ull, 0x0f0e0d0c0b0a0908ull);

    uint8x16x2_t two = vld2q_u8(buffer);
    check_vec("ld2 16b even", two.val[0], 0x0e0c0a0806040200ull, 0x1e1c1a1816141210ull);
    check_vec("ld2 16b odd", two.val[1], 0x0f0d0b0907050301ull, 0x1f1d1b1917151311ull);

    uint8x16x4_t four = vld4q_u8(buffer);
    check_vec("ld4 16b lane 0", four.val[0], 0x1c1814100c080400ull, 0x3c3834302c282420ull);
    check_vec("ld4 16b lane 3", four.val[3], 0x1f1b17130f0b0703ull, 0x3f3b37332f2b2723ull);

    static uint8_t out[64];
    for (int i = 0; i < 64; ++i) out[i] = 0;
    vst4q_u8(out, four);
    CHECK_U64("st4 puts back what ld4 took", (uint64_t)ref_same(out, buffer, 64), 64u);
    /* The same question asked the way the vectoriser asks it. This form once
       disagreed with the scalar count above, so it stays as its own case. */
    int all_same = 1;
    for (int i = 0; i < 64; ++i)
        if (out[i] != buffer[i]) all_same = 0;
    CHECK_U64("st4 round trip, vectorised check", (uint64_t)all_same, 1u);

    /* Scalar float through memory, which is ldr and str on an s register. */
    static float floats[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    CHECK_U64("ldr s", bits_of(floats[2]), bits_of(3.5f));
    floats[1] = ref_add(floats[0], floats[3]);
    CHECK_U64("str s", bits_of(floats[1]), bits_of(6.0f));

    static double doubles[2] = {0.25, 0.75};
    CHECK_U64("ldp and stp d", bits_of_double(doubles[0] + doubles[1]), bits_of_double(1.0));
}

/* Add and subtract with an extended register: the sxtw and uxtw forms. */
static volatile int64_t g_base = 1000;
static volatile int32_t g_neg = -24;
static volatile uint32_t g_big = 0xfffffff0u;

static void test_extended_register(void) {
    int64_t base = g_base;
    int32_t neg = g_neg;
    uint32_t big = g_big;
    CHECK_U64("add sxtw", (uint64_t)(base + (int64_t)neg), 976u);
    CHECK_U64("sub sxtw", (uint64_t)(base - (int64_t)neg), 1024u);
    CHECK_U64("add uxtw", (uint64_t)(base + (uint64_t)big), 1000ull + 0xfffffff0ull);
    CHECK_U64("add sxtw shifted", (uint64_t)(base + ((int64_t)neg << 3)), (uint64_t)(1000 - 192));
    CHECK_U64("compare sxtw", (uint64_t)(base > (int64_t)neg), 1u);

    const int32_t av[4] = {5, -3, 7, 0};
    const int32_t bv[4] = {2, -1, 7, -9};
    int32x4_t x = vld1q_s32(av), y = vld1q_s32(bv);
    check_vec("cmgt 4s", vreinterpretq_u8_u32(vcgtq_s32(x, y)), 0xffffffffull, 0xffffffff00000000ull);
    check_vec("cmge 4s", vreinterpretq_u8_u32(vcgeq_s32(x, y)), 0xffffffffull, 0xffffffffffffffffull);
    const uint8_t seq[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8x16_t lanes = vld1q_u8(seq);
    check_vec("rev64 4s", vreinterpretq_u8_u32(vrev64q_u32(vreinterpretq_u32_u8(lanes))), 0x0302010007060504ull,
              0x0b0a09080f0e0d0cull);
    check_vec("rev32 8h", vreinterpretq_u8_u16(vrev32q_u16(vreinterpretq_u16_u8(lanes))), 0x0504070601000302ull,
              0x0d0c0f0e09080b0aull);
    check_vec("rev16 16b", vrev16q_u8(lanes), 0x0607040502030001ull, 0x0e0f0c0d0a0b0809ull);
    check_vec("cnt 16b", vcntq_u8(vdupq_n_u8(0xb3)), 0x0505050505050505ull, 0x0505050505050505ull);
    check_vec("clz 4s", vreinterpretq_u8_u32(vclzq_u32(vdupq_n_u32(0x00f00000))), 0x0000000800000008ull,
              0x0000000800000008ull);
    check_vec("uaddlp 16b", vreinterpretq_u8_u16(vpaddlq_u8(lanes)), 0x000d000900050001ull, 0x001d001900150011ull);
    check_vec("xtn 4s", vreinterpretq_u8_u16(vcombine_u16(vmovn_u32(vdupq_n_u32(0x12345678)), vdup_n_u16(0))),
              0x5678567856785678ull, 0);
    CHECK_U64("uaddlv 16b", vaddlvq_u8(lanes), 120u);
    {
        /* Test-and-branch on every kind of bit, and compare-and-branch on the
           32-bit form, which must ignore the upper half. */
        static volatile uint64_t pattern = 0x8000010000000001ull;
        uint64_t p = pattern, taken = 0;
#define TB(bit, want)                                                                                          \
    taken = 0;                                                                                                 \
    __asm__ volatile("tbz %1, #" #bit ", 1f\n mov %0, #1\n1:" : "+r"(taken) : "r"(p));                          \
    CHECK_U64("tbz bit " #bit, taken, want);
        TB(0, 1) TB(1, 0) TB(40, 1) TB(41, 0) TB(63, 1) TB(62, 0)
        uint64_t hi_only = 0xffffffff00000000ull ^ (p & 0) ;
        static volatile uint64_t hi_src = 0xffffffff00000000ull;
        hi_only = hi_src;
        taken = 0;
        __asm__ volatile("cbz %w1, 1f\n mov %0, #1\n1:" : "+r"(taken) : "r"(hi_only));
        CHECK_U64("cbz w ignores upper half", taken, 0);
        taken = 0;
        __asm__ volatile("cbnz %1, 1f\n mov %0, #1\n1:" : "+r"(taken) : "r"(hi_only));
        CHECK_U64("cbnz x sees upper half", taken, 0);
        taken = 0;
        __asm__ volatile("tbnz %w1, #31, 1f\n mov %0, #1\n1:" : "+r"(taken) : "r"(hi_only));
        CHECK_U64("tbnz w bit 31", taken, 1);
    }
    {
        /* LSE atomics written out, since outlined helpers pick them only when
           HWCAP says so. */
        static volatile uint32_t word = 40;
        static volatile uint64_t wide = 7;
        uint32_t old32 = 2;
        __asm__ volatile(".arch_extension lse\n ldaddal %w0, %w0, [%1]" : "+r"(old32) : "r"(&word) : "memory");
        CHECK_U64("ldaddal old", old32, 40);
        CHECK_U64("ldaddal new", word, 42);
        uint64_t old64 = 0xf0;
        __asm__ volatile(".arch_extension lse\n ldsetal %0, %0, [%1]" : "+r"(old64) : "r"(&wide) : "memory");
        CHECK_U64("ldsetal old", old64, 7);
        CHECK_U64("ldsetal new", wide, 0xf7);
        uint64_t expected = 0xf7, desired = 99;
        __asm__ volatile(".arch_extension lse\n casal %0, %2, [%1]" : "+r"(expected) : "r"(&wide), "r"(desired) : "memory");
        CHECK_U64("casal old", expected, 0xf7);
        CHECK_U64("casal new", wide, 99);
        uint32_t swapped = 5;
        __asm__ volatile(".arch_extension lse\n swpal %w0, %w0, [%1]" : "+r"(swapped) : "r"(&word) : "memory");
        CHECK_U64("swpal old", swapped, 42);
        CHECK_U64("swpal new", word, 5);
    }
    {
        /* The integer three-same group, fed through volatile so nothing folds. */
        static volatile int32_t va_src[4] = {100, -100, 0x7ffffff0, -7};
        static volatile int32_t vb_src[4] = {3, -3, 0x100, 2};
        int32_t av[4], bv[4];
        for (int i = 0; i < 4; ++i) {
            av[i] = va_src[i];
            bv[i] = vb_src[i];
        }
        int32x4_t A = vld1q_s32(av), B = vld1q_s32(bv);
        check_vec("sshl 4s", vreinterpretq_u8_s32(vshlq_s32(A, B)), 0xfffffff300000320ull, 0xffffffe47ffffff0ull);
        check_vec("ushl 4s neg", vreinterpretq_u8_u32(vshlq_u32(vreinterpretq_u32_s32(A), vdupq_n_s32(-4))),
                  0x0ffffff900000006ull, 0x0fffffff07ffffffull);
        check_vec("srshl 4s", vreinterpretq_u8_s32(vrshlq_s32(A, vdupq_n_s32(-3))), 0xfffffff40000000dull,
                  0xffffffff0ffffffeull);
        check_vec("sqshl 4s", vreinterpretq_u8_s32(vqshlq_s32(A, vdupq_n_s32(4))), 0xfffff9c000000640ull,
                  0xffffff907fffffffull);
        check_vec("sqadd 4s", vreinterpretq_u8_s32(vqaddq_s32(A, B)), 0xffffff9900000067ull, 0xfffffffb7fffffffull);
        check_vec("uqsub 4s", vreinterpretq_u8_u32(vqsubq_u32(vreinterpretq_u32_s32(B), vreinterpretq_u32_s32(A))),
                  0x0000006100000000ull, 0x0000000000000000ull);
        check_vec("shadd 4s", vreinterpretq_u8_s32(vhaddq_s32(A, B)), 0xffffffcc00000033ull, 0xfffffffd40000078ull);
        check_vec("sabd 4s", vreinterpretq_u8_s32(vabdq_s32(A, B)), 0x0000006100000061ull, 0x000000097ffffef0ull);
        check_vec("mla 4s", vreinterpretq_u8_s32(vmlaq_s32(vdupq_n_s32(1), A, B)), 0x0000012d0000012dull,
                  0xfffffff3fffff001ull);
        check_vec("smaxp 4s", vreinterpretq_u8_s32(vpmaxq_s32(A, B)), 0x7ffffff000000064ull,
                  0x0000010000000003ull);
        check_vec("addp 4s", vreinterpretq_u8_s32(vpaddq_s32(A, B)), 0x7fffffe900000000ull, 0x0000010200000000ull);
        static volatile int16_t qh_src[8] = {16384, -32768, 1000, -1000, 0, 0, 0, 0};
        int16_t qh[8];
        for (int i = 0; i < 8; ++i) qh[i] = qh_src[i];
        int16x8_t Q = vld1q_s16(qh);
        check_vec("sqdmulh 8h", vreinterpretq_u8_s16(vqdmulhq_s16(Q, Q)), 0x001e001e7fff2000ull, 0);
    }
    {
        /* Float to integer in each rounding mode, and saturation. */
        static volatile float half_up = 2.5f, below = -2.5f, huge = 3.0e10f;
        float h = half_up, b = below, g = huge;
        CHECK_U64("fcvtns 2.5", (uint64_t)vcvtns_s32_f32(h), 2);
        CHECK_U64("fcvtas 2.5", (uint64_t)vcvtas_s32_f32(h), 3);
        CHECK_U64("fcvtps -2.5", (uint64_t)(uint32_t)vcvtps_s32_f32(b), 0xfffffffeu);
        CHECK_U64("fcvtms -2.5", (uint64_t)(uint32_t)vcvtms_s32_f32(b), 0xfffffffdu);
        CHECK_U64("fcvtpu 2.5", (uint64_t)vcvtps_u32_f32(h), 3);
        CHECK_U64("fcvtzs sat", (uint64_t)(uint32_t)vcvts_s32_f32(g), 0x7fffffffu);
        CHECK_U64("fcvtzu neg", (uint64_t)vcvts_u32_f32(b), 0);
        uint64x2_t pair = vcombine_u64(vcreate_u64(0x1111), vcreate_u64(0x2222));
        CHECK_U64("fmov x, v.d[1]", vgetq_lane_u64(pair, 1), 0x2222);
    }
    {
        /* Shifts by an immediate. */
        /* Loaded through volatile so the compiler cannot fold the answers. */
        static volatile int32_t source[4] = {-100, 100, 0x7fffffff, -8};
        int32_t sv[4];
        for (int i = 0; i < 4; ++i) sv[i] = source[i];
        int32x4_t sx = vld1q_s32(sv);
        check_vec("sshr 4s", vreinterpretq_u8_s32(vshrq_n_s32(sx, 2)), 0x00000019ffffffe7ull, 0xfffffffe1fffffffull);
        check_vec("ushr 4s", vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_s32(sx), 28)), 0x000000000000000full,
                  0x0000000f00000007ull);
        check_vec("srshr 4s", vreinterpretq_u8_s32(vrshrq_n_s32(sx, 3)), 0x0000000dfffffff4ull, 0xffffffff10000000ull);
        check_vec("ssra 4s", vreinterpretq_u8_s32(vsraq_n_s32(vdupq_n_s32(1000), sx, 1)), 0x0000041a000003b6ull,
                  0x000003e4400003e7ull);
        check_vec("shl 4s", vreinterpretq_u8_s32(vshlq_n_s32(sx, 4)), 0x00000640fffff9c0ull, 0xffffff80fffffff0ull);
        check_vec("sli 4s", vreinterpretq_u8_u32(vsliq_n_u32(vdupq_n_u32(0xffffffff), vdupq_n_u32(0x1), 8)),
                  0x000001ff000001ffull, 0x000001ff000001ffull);
        check_vec("sri 4s", vreinterpretq_u8_u32(vsriq_n_u32(vdupq_n_u32(0xffffffff), vdupq_n_u32(0x80000000), 8)),
                  0xff800000ff800000ull, 0xff800000ff800000ull);
        check_vec("sxtl 2d", vreinterpretq_u8_s64(vmovl_s32(vget_low_s32(sx))), 0xffffffffffffff9cull, 100);
        check_vec("uxtl2 8h", vreinterpretq_u8_u16(vmovl_high_u8(lanes)), 0x000b000a00090008ull,
                  0x000f000e000d000cull);
        check_vec("ushll 4s", vreinterpretq_u8_u32(vshll_n_u16(vget_low_u16(vreinterpretq_u16_u8(lanes)), 4)),
                  0x0000302000001000ull, 0x0000706000005040ull);
        check_vec("shrn 4h", vreinterpretq_u8_u16(vcombine_u16(vshrn_n_u32(vreinterpretq_u32_s32(sx), 16),
                                                               vdup_n_u16(0))),
                  0xffff7fff0000ffffull, 0);
        check_vec("sqshrn 4h", vreinterpretq_u8_s16(vcombine_s16(vqshrn_n_s32(sx, 4), vdup_n_s16(0))),
                  0xffff7fff0006fff9ull, 0);
        check_vec("sqshrun 4h", vreinterpretq_u8_u16(vcombine_u16(vqshrun_n_s32(sx, 4), vdup_n_u16(0))),
                  0x0000ffff00060000ull, 0);
        check_vec("uqshl 4s", vreinterpretq_u8_u32(vqshlq_n_u32(vreinterpretq_u32_s32(sx), 1)),
                  0x000000c8ffffffffull, 0xfffffffffffffffeull);
        check_vec("sqshl 4s", vreinterpretq_u8_s32(vqshlq_n_s32(sx, 1)), 0x000000c8ffffff38ull,
                  0xfffffff07fffffffull);
        check_vec("scvtf fixed", vreinterpretq_u8_f32(vcvtq_n_f32_s32(sx, 2)), 0x41c80000c1c80000ull,
                  0xc00000004e000000ull);
        check_vec("fcvtzs fixed", vreinterpretq_u8_s32(vcvtq_n_s32_f32(vdupq_n_f32(1.75f), 4)),
                  0x0000001c0000001cull, 0x0000001c0000001cull);
        CHECK_U64("ushr d", vshrd_n_u64(0x8000000000000000ull, 63), 1);
        CHECK_U64("sshr d", (uint64_t)vshrd_n_s64((int64_t)0x8000000000000000ull, 63), 0xffffffffffffffffull);
    }
    {
        const float fa[4] = {1.0f, -2.0f, 3.0f, 4.0f};
        const float fb[4] = {1.0f, -3.0f, 5.0f, -4.0f};
        float32x4_t p = vld1q_f32(fa), r = vld1q_f32(fb);
        check_vec("fcvtl 2d", vreinterpretq_u8_f64(vcvt_f64_f32(vget_low_f32(p))), 0x3ff0000000000000ull,
                  0xc000000000000000ull);
        {
            const double dd[2] = {1.5, -0.25};
            check_vec("fcvtn 2s", vreinterpretq_u8_f32(vcombine_f32(vcvt_f32_f64(vld1q_f64(dd)), vdup_n_f32(0))),
                      0xbe8000003fc00000ull, 0);
            const uint16_t hh[4] = {0x3c00, 0xc000, 0x3800, 0x7bff};
            float32x4_t widened = vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(hh)));
            check_vec("fcvtl 4s", vreinterpretq_u8_f32(widened), 0xc00000003f800000ull, 0x477fe0003f000000ull);
            check_vec("fcvtn 4h", vreinterpretq_u8_f16(vcombine_f16(vcvt_f16_f32(widened), vcvt_f16_f32(vdupq_n_f32(0)))),
                      0x7bff3800c0003c00ull, 0);
        }
        check_vec("fcmgt 4s", vreinterpretq_u8_u32(vcgtq_f32(p, r)), 0xffffffff00000000ull, 0xffffffff00000000ull);
        check_vec("fcmge 4s", vreinterpretq_u8_u32(vcgeq_f32(p, r)), 0xffffffffffffffffull, 0xffffffff00000000ull);
        check_vec("fcmeq 4s", vreinterpretq_u8_u32(vceqq_f32(p, r)), 0x00000000ffffffffull, 0);
        check_vec("facge 4s", vreinterpretq_u8_u32(vcageq_f32(p, r)), 0x00000000ffffffffull, 0xffffffff00000000ull);
        float want_max[4] = {1.0f, -2.0f, 5.0f, 4.0f};
        check_f32x4("fmax 4s", vmaxq_f32(p, r), want_max);
    }
    {
        static uint32_t cell[2] = {0, 0};
        uint32x4_t filled = vdupq_n_u32(0);
        filled = vsetq_lane_u32(0xabcd1234, filled, 2);
        vst1q_lane_u32(&cell[1], filled, 2);
        CHECK_U64("st1 one lane", cell[1], 0xabcd1234u);
        static const uint16_t source = 0x7777;
        check_vec("ld1r 8h", vreinterpretq_u8_u16(vld1q_dup_u16(&source)), 0x7777777777777777ull,
                  0x7777777777777777ull);
        uint32x4_t loaded = vld1q_lane_u32(&cell[1], vdupq_n_u32(5), 3);
        check_vec("ld1 one lane", vreinterpretq_u8_u32(loaded), 0x0000000500000005ull, 0xabcd123400000005ull);
    }
    CHECK_U64("csetm", (uint64_t)(g_base > 5 ? -1LL : 0LL), ~0ull);
    CHECK_U64("csneg", (uint64_t)(g_neg < 0 ? -(int64_t)g_neg : (int64_t)g_neg), 24u);
    CHECK_U64("csinv", (uint64_t)(g_base < 5 ? g_base : ~g_base), ~1000ull);
    CHECK_U64("msub", (uint64_t)(g_base - 3 * (int64_t)g_neg), 1072u);
    check_vec("smax 4s", vreinterpretq_u8_s32(vmaxq_s32(x, y)), ((uint64_t)(uint32_t)-1 << 32) | 5u,
              7u);
}

void qb_guest_main(void) {
    test_extended_register();
    test_scalar_float();
    test_conversions();
    test_vector_float();
    test_vector_int();
    test_lanes();
    test_memory();
}
