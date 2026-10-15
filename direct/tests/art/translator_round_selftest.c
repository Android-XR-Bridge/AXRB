// FRINT* (round to integral) in every rounding mode, scalar and vector, single
// and double, against integer arithmetic; FRINTX must also report inexact.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { NEAREST, PLUS, MINUS, ZERO, AWAY };

static double reference(double v, int mode, double limit) {
    if (v != v || fabs(v) >= limit) return v;
    double t = (double)(int64_t)v, frac = v - t, sign = v < 0 ? -1 : 1;
    switch (mode) {
        case NEAREST:
            if (fabs(frac) > 0.5 || (fabs(frac) == 0.5 && ((int64_t)t & 1))) t += sign;
            break;
        case PLUS: if (frac > 0) t += 1; break;
        case MINUS: if (frac < 0) t -= 1; break;
        case AWAY: if (fabs(frac) >= 0.5) t += sign; break;
    }
    return copysign(t, v);
}

static void set_fpcr(uint64_t v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
static uint64_t fpsr(void) { uint64_t v; __asm__ volatile("mrs %0, fpsr" : "=r"(v)); return v; }
static void clear_fpsr(void) { uint64_t zero = 0; __asm__ volatile("msr fpsr, %0" : : "r"(zero)); }

#define SCALAR(name, insn, type, reg) \
    static type __attribute__((noinline)) name(type v) { __asm__ volatile(insn " %" reg "0, %" reg "0" : "+w"(v)); return v; }
SCALAR(sx, "frintx", float, "s") SCALAR(si, "frinti", float, "s") SCALAR(sn, "frintn", float, "s")
SCALAR(sp, "frintp", float, "s") SCALAR(sm, "frintm", float, "s") SCALAR(sz, "frintz", float, "s") SCALAR(sa, "frinta", float, "s")
SCALAR(dx, "frintx", double, "d") SCALAR(di, "frinti", double, "d") SCALAR(dn, "frintn", double, "d")
SCALAR(dp, "frintp", double, "d") SCALAR(dm, "frintm", double, "d") SCALAR(dz, "frintz", double, "d") SCALAR(da, "frinta", double, "d")

#define VECTOR(name, insn, type, arrangement) \
    static void __attribute__((noinline)) name(type* v) { \
        __asm__ volatile("ldr q0, [%0]\n" insn " v0." arrangement ", v0." arrangement "\n str q0, [%0]" : : "r"(v) : "v0", "memory"); }
VECTOR(vsx, "frintx", float, "4s") VECTOR(vsi, "frinti", float, "4s") VECTOR(vsn, "frintn", float, "4s")
VECTOR(vsp, "frintp", float, "4s") VECTOR(vsm, "frintm", float, "4s") VECTOR(vsz, "frintz", float, "4s") VECTOR(vsa, "frinta", float, "4s")
VECTOR(vdx, "frintx", double, "2d") VECTOR(vdi, "frinti", double, "2d") VECTOR(vdn, "frintn", double, "2d")
VECTOR(vdp, "frintp", double, "2d") VECTOR(vdm, "frintm", double, "2d") VECTOR(vdz, "frintz", double, "2d") VECTOR(vda, "frinta", double, "2d")

static int failures;
static void check32(const char* what, int fpcr_mode, float in, float got, float want) {
    uint32_t a, b;
    memcpy(&a, &got, 4); memcpy(&b, &want, 4);
    if (a != b && !(got != got && want != want) && failures++ < 20)
        printf("FAIL %s mode=%d in=%a got=%a want=%a\n", what, fpcr_mode, in, got, want);
}
static void check64(const char* what, int fpcr_mode, double in, double got, double want) {
    uint64_t a, b;
    memcpy(&a, &got, 8); memcpy(&b, &want, 8);
    if (a != b && !(got != got && want != want) && failures++ < 20)
        printf("FAIL %s mode=%d in=%a got=%a want=%a\n", what, fpcr_mode, in, got, want);
}

int main(void) {
    static const double values[] = {0.0, -0.0, 0.25, -0.25, 0.5, -0.5, 0.75, -0.75, 1.0, -1.0, 1.5, -1.5, 2.5, -2.5,
        3.5, -3.5, 1e-30, -1e-30, 123456.5, -123456.5, 8388607.5, -8388607.5, 8388608.0, 16777216.0, 1e15, -1e15,
        4503599627370495.5, -4503599627370495.5, 1e30, -1e30, INFINITY, -INFINITY, NAN, 0.49999997, 0.9999999};
    // FPCR RMode (bits 23:22) for each rounding: RN, RP, RM, RZ.
    static const int fpcr_bits[] = {0, 1, 2, 3};
    static const int fpcr_rounding[] = {NEAREST, PLUS, MINUS, ZERO};
    for (int m = 0; m < 4; ++m) {
        set_fpcr((uint64_t)fpcr_bits[m] << 22);
        const int current = fpcr_rounding[m];
        for (size_t i = 0; i < sizeof values / sizeof *values; ++i) {
            const float f = (float)values[i];
            const double d = values[i];
            struct { const char* name; float (*s)(float); double (*dd)(double); void (*vs)(float*); void (*vd)(double*); int mode; } ops[] = {
                {"frintx", sx, dx, vsx, vdx, current}, {"frinti", si, di, vsi, vdi, current}, {"frintn", sn, dn, vsn, vdn, NEAREST},
                {"frintp", sp, dp, vsp, vdp, PLUS}, {"frintm", sm, dm, vsm, vdm, MINUS}, {"frintz", sz, dz, vsz, vdz, ZERO},
                {"frinta", sa, da, vsa, vda, AWAY}};
            for (size_t o = 0; o < sizeof ops / sizeof *ops; ++o) {
                const float want32 = (float)reference(f, ops[o].mode, 8388608.0);
                const double want64 = reference(d, ops[o].mode, 4503599627370496.0);
                check32(ops[o].name, m, f, ops[o].s(f), want32);
                check64(ops[o].name, m, d, ops[o].dd(d), want64);
                float v32[4] = {f, -f, f, 1.0f};
                ops[o].vs(v32);
                check32(ops[o].name, m, f, v32[0], want32);
                check32(ops[o].name, m, f, v32[2], want32);
                check32(ops[o].name, m, 1.0f, v32[3], 1.0f);
                double v64[2] = {d, 1.0};
                ops[o].vd(v64);
                check64(ops[o].name, m, d, v64[0], want64);
                check64(ops[o].name, m, 1.0, v64[1], 1.0);
            }
        }
    }
    set_fpcr(0);
    // Inexact (FPSR.IXC, bit 4): FRINTX reports it, and only for a changed value.
    clear_fpsr();
    volatile float exact = sx(2.0f);
    if (fpsr() & 16) { printf("FAIL frintx reported inexact for 2.0\n"); ++failures; }
    volatile float inexact = sx(2.25f);
    if (!(fpsr() & 16)) { printf("FAIL frintx did not report inexact for 2.25\n"); ++failures; }
    clear_fpsr();
    float lanes[4] = {1.0f, 2.0f, 3.25f, 4.0f};
    vsx(lanes);
    if (!(fpsr() & 16)) { printf("FAIL vector frintx did not report inexact\n"); ++failures; }
    (void)exact; (void)inexact;
    if (failures) { printf("TRANSLATOR_ROUND_SELFTEST_FAIL %d\n", failures); return 1; }
    puts("TRANSLATOR_ROUND_SELFTEST_PASS");
    return 0;
}
