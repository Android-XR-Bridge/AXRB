/* Floating point and SIMD for the arm64 guest.

   The test guest was built -mgeneral-regs-only, which keeps every number in a
   general register. Real Android code does not do that: it passes floats in
   v0..v7 and computes with the FP and NEON instructions decoded here. */

#include "cpu.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace {

float bits_to_float(uint32_t bits) {
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t float_to_bits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

double bits_to_double(uint64_t bits) {
    double value = 0;
    std::memcpy(&value, &bits, 8);
    return value;
}

uint64_t double_to_bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, 8);
    return bits;
}

float read_s(const GuestCpu& cpu, int n) { return bits_to_float((uint32_t)cpu.q[n].lo); }
double read_d(const GuestCpu& cpu, int n) { return bits_to_double(cpu.q[n].lo); }

/* Writing a scalar clears the rest of the register, as the architecture says. */
void write_s(GuestCpu& cpu, int n, float value) {
    cpu.q[n].lo = float_to_bits(value);
    cpu.q[n].hi = 0;
}

void write_d(GuestCpu& cpu, int n, double value) {
    cpu.q[n].lo = double_to_bits(value);
    cpu.q[n].hi = 0;
}

double read_f(const GuestCpu& cpu, int n, int ftype) { return ftype ? read_d(cpu, n) : (double)read_s(cpu, n); }

void write_f(GuestCpu& cpu, int n, int ftype, double value) {
    if (ftype) write_d(cpu, n, value);
    else write_s(cpu, n, (float)value);
}

bool vec_load(GuestMem& mem, uint64_t va, GuestVec* out, int bytes) {
    uint8_t* p = guest_ptr(mem, va, bytes);
    if (!p) return false;
    out->lo = 0;
    out->hi = 0;
    std::memcpy(&out->lo, p, bytes > 8 ? 8 : bytes);
    if (bytes > 8) std::memcpy(&out->hi, p + 8, bytes - 8);
    return true;
}

bool vec_store(GuestMem& mem, uint64_t va, const GuestVec& in, int bytes) {
    if (g_guest_watch && g_guest_watch >= va && g_guest_watch < va + (uint64_t)bytes) guest_report_watch(va, bytes, in.lo);
    uint8_t* p = guest_ptr(mem, va, bytes);
    if (!p) return false;
    std::memcpy(p, &in.lo, bytes > 8 ? 8 : bytes);
    if (bytes > 8) std::memcpy(p + 8, &in.hi, bytes - 8);
    return true;
}

uint64_t reg(const GuestCpu& cpu, int r) { return r == 31 ? 0 : cpu.x[r]; }
uint64_t reg_or_sp(const GuestCpu& cpu, int r) { return r == 31 ? cpu.sp : cpu.x[r]; }

void write_reg(GuestCpu& cpu, int r, uint64_t v) {
    if (r != 31) cpu.x[r] = v;
}

void write_reg_or_sp(GuestCpu& cpu, int r, uint64_t v) {
    if (r == 31) cpu.sp = v;
    else cpu.x[r] = v;
}

bool condition(const GuestCpu& cpu, int cc) {
    if (cc >= 14) return true;
    bool r = false;
    switch (cc & 0xe) {
        case 0: r = cpu.z; break;
        case 2: r = cpu.c; break;
        case 4: r = cpu.n; break;
        case 6: r = cpu.v; break;
        case 8: r = cpu.c && !cpu.z; break;
        case 10: r = cpu.n == cpu.v; break;
        case 12: r = !cpu.z && cpu.n == cpu.v; break;
        default: r = true; break;
    }
    if (cc & 1) r = !r;
    return r;
}

/* The eight-bit form used by FMOV immediate and by MOVI. */
double expand_fp_immediate(uint32_t imm8) {
    uint64_t sign = (imm8 >> 7) & 1;
    uint64_t exp_high = (imm8 >> 6) & 1;
    uint64_t exp_low = (imm8 >> 4) & 3;
    uint64_t fraction = imm8 & 0xf;
    /* Bit 6 clear means the exponent starts 1 followed by zeros; bit 6 set
       means 0 followed by ones. Getting these the wrong way round turns
       #3.5 into #0.21875, which is exactly as wrong as it sounds. */
    uint64_t exponent = (exp_high ? 0x3fc : 0x400) | exp_low;
    uint64_t bits = (sign << 63) | (exponent << 52) | (fraction << 48);
    return bits_to_double(bits);
}

/* MOVI and friends build a 64-bit pattern from cmode and an eight-bit value.
   The shape of the pattern is what cmode says; op only chooses MOVI or MVNI,
   except at cmode 1110 and 1111 where it picks a different form entirely. */
uint64_t expand_simd_immediate(uint32_t imm8, int cmode, int op, bool* invert) {
    *invert = false;
    int top = (cmode >> 1) & 7;
    uint64_t lane32 = 0;
    switch (top) {
        case 0: lane32 = imm8; break;
        case 1: lane32 = (uint64_t)imm8 << 8; break;
        case 2: lane32 = (uint64_t)imm8 << 16; break;
        case 3: lane32 = (uint64_t)imm8 << 24; break;
        case 4: {
            uint64_t lane16 = imm8;
            *invert = op != 0;
            return lane16 | (lane16 << 16) | (lane16 << 32) | (lane16 << 48);
        }
        case 5: {
            uint64_t lane16 = (uint64_t)imm8 << 8;
            *invert = op != 0;
            return lane16 | (lane16 << 16) | (lane16 << 32) | (lane16 << 48);
        }
        case 6: lane32 = (cmode & 1) ? (((uint64_t)imm8 << 16) | 0xffff) : (((uint64_t)imm8 << 8) | 0xff); break;
        default:
            if (cmode == 0xe && op == 0) return (uint64_t)imm8 * 0x0101010101010101ull;
            if (cmode == 0xe && op == 1) {
                /* One bit of the immediate per byte of the result. */
                uint64_t result = 0;
                for (int i = 0; i < 8; ++i)
                    if (imm8 & (1u << i)) result |= 0xffull << (i * 8);
                return result;
            }
            if (cmode == 0xf && op == 0) {
                uint64_t single = float_to_bits((float)expand_fp_immediate(imm8));
                return single | (single << 32);
            }
            if (cmode == 0xf && op == 1) return double_to_bits(expand_fp_immediate(imm8));
            return 0;
    }
    *invert = op != 0;
    return lane32 | (lane32 << 32);
}


/* Shift by an immediate, vector and scalar: 0 Q U 011110 immh immb opcode 1
   for vectors, 01 U 111110 ... for the one-lane scalar forms. immh picks the
   element size by its highest set bit; the shift is measured from it. */
bool step_shift_immediate(GuestCpu& cpu, uint32_t insn, uint64_t next) {
    bool vector = (insn & 0x9f800400) == 0x0f000400;
    bool scalar = (insn & 0xdf800400) == 0x5f000400;
    if (!vector && !scalar) return false;
    int immh = (insn >> 19) & 0xf;
    if (immh == 0) return false; /* the modified immediate space: MOVI and friends */
    int immb = (insn >> 16) & 7;
    int opcode = (insn >> 11) & 0x1f;
    int u = (insn >> 29) & 1;
    int q = (insn >> 30) & 1;
    int rn = (insn >> 5) & 31;
    int rd = insn & 31;
    int esize = immh >= 8 ? 64 : immh >= 4 ? 32 : immh >= 2 ? 16 : 8;
    int bytes = esize / 8;
    int imm = (immh << 3) | immb;
    int right = 2 * esize - imm;
    int left = imm - esize;

    uint8_t a[16], d[16], out[16] = {};
    std::memcpy(a, &cpu.q[rn].lo, 8);
    std::memcpy(a + 8, &cpu.q[rn].hi, 8);
    std::memcpy(d, &cpu.q[rd].lo, 8);
    std::memcpy(d + 8, &cpu.q[rd].hi, 8);

    auto get = [](const uint8_t* from, int at, int size, bool is_signed) -> int64_t {
        uint64_t raw = 0;
        std::memcpy(&raw, from + at * size, size);
        if (is_signed && size < 8) {
            int shift = 64 - size * 8;
            return (int64_t)(raw << shift) >> shift;
        }
        return (int64_t)raw;
    };
    auto put = [](uint8_t* to, int at, int size, uint64_t value) { std::memcpy(to + at * size, &value, size); };
    /* Clamp a signed 64-bit value into a signed or unsigned lane. */
    auto clamp = [](int64_t value, int size, bool to_signed) -> uint64_t {
        int bits = size * 8;
        if (to_signed) {
            int64_t hi = bits == 64 ? INT64_MAX : ((int64_t)1 << (bits - 1)) - 1;
            int64_t lo = bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
            return (uint64_t)(value > hi ? hi : value < lo ? lo : value);
        }
        if (value < 0) return 0;
        uint64_t hi = bits == 64 ? UINT64_MAX : (((uint64_t)1 << bits) - 1);
        return (uint64_t)value > hi ? hi : (uint64_t)value;
    };
    /* Unsigned clamp for a value known to be non-negative. */
    auto clamp_unsigned = [](uint64_t value, int size) -> uint64_t {
        uint64_t hi = size == 8 ? UINT64_MAX : (((uint64_t)1 << (size * 8)) - 1);
        return value > hi ? hi : value;
    };
    /* A right shift with optional rounding. The rounding bit is added after
       the shift, which cannot overflow the way adding it first could. */
    auto shift_right = [](uint64_t raw, bool is_signed, int amount, bool round) -> uint64_t {
        if (is_signed) {
            int64_t value = (int64_t)raw;
            /* A rounded shift by the full 64 bits is always zero. */
            if (round && amount >= 64) return 0;
            int64_t r = amount >= 64 ? (value < 0 ? -1 : 0) : value >> amount;
            if (round && amount > 0) r += (int64_t)((value >> (amount - 1)) & 1);
            return (uint64_t)r;
        }
        uint64_t r = amount >= 64 ? 0 : raw >> amount;
        if (round && amount > 0 && amount <= 64) r += (raw >> (amount - 1)) & 1;
        return r;
    };

    int lanes = scalar ? 1 : (q ? 16 : 8) / bytes;
    bool handled = true;
    bool is_signed = !u;
    int result_bytes = scalar ? bytes : (q ? 16 : 8);
    uint64_t lane_mask = esize == 64 ? ~0ull : ((1ull << esize) - 1);

    switch (opcode) {
    case 0x00: case 0x02: case 0x04: case 0x06: { /* [SU]SHR, [SU]SRA, [SU]RSHR, [SU]RSRA */
        bool round = opcode == 0x04 || opcode == 0x06;
        bool accumulate = opcode == 0x02 || opcode == 0x06;
        for (int i = 0; i < lanes; ++i) {
            uint64_t r = shift_right((uint64_t)get(a, i, bytes, is_signed), is_signed, right, round);
            if (accumulate) r += (uint64_t)get(d, i, bytes, false);
            put(out, i, bytes, r);
        }
        break;
    }
    case 0x08: /* SRI */
        if (!u) { handled = false; break; }
        for (int i = 0; i < lanes; ++i) {
            uint64_t mask = right >= 64 ? 0 : lane_mask >> right;
            uint64_t shifted = right >= 64 ? 0 : (uint64_t)get(a, i, bytes, false) >> right;
            put(out, i, bytes, ((uint64_t)get(d, i, bytes, false) & ~mask) | (shifted & mask));
        }
        break;
    case 0x0a: /* SHL, SLI */
        for (int i = 0; i < lanes; ++i) {
            uint64_t shifted = (uint64_t)get(a, i, bytes, false) << left;
            if (u) {
                uint64_t mask = lane_mask & ~((1ull << left) - 1);
                shifted = ((uint64_t)get(d, i, bytes, false) & ~mask) | (shifted & mask);
            }
            put(out, i, bytes, shifted);
        }
        break;
    case 0x0c: case 0x0e: { /* SQSHLU; SQSHL and UQSHL by immediate */
        if (opcode == 0x0c && !u) { handled = false; break; }
        bool from_signed = opcode == 0x0c || !u;
        bool to_signed = opcode == 0x0e && !u;
        for (int i = 0; i < lanes; ++i) {
            int64_t v = get(a, i, bytes, from_signed);
            if (opcode == 0x0c) { /* SQSHLU */
                if (v < 0) {
                    put(out, i, bytes, 0);
                    continue;
                }
                uint64_t uv = (uint64_t)v;
                bool over = left > 0 && (uv >> (esize - left)) != 0;
                put(out, i, bytes, over ? lane_mask : ((uv << left) & lane_mask));
                continue;
            }
            if (!from_signed) {
                uint64_t uv = (uint64_t)v;
                bool over = left > 0 && (uv >> (esize - left)) != 0;
                put(out, i, bytes, over ? lane_mask : ((uv << left) & lane_mask));
                continue;
            }
            /* The exact result fits in 128 bits; it saturates if shifting
               back does not give the original. */
            int64_t shifted = (int64_t)((uint64_t)v << left);
            bool over = left > 0 && (left >= 64 || (shifted >> left) != v);
            if (!over && esize < 64) {
                int64_t top = shifted >> (esize - 1);
                over = to_signed ? (top != 0 && top != -1) : false;
            }
            int64_t r = over ? (v < 0 ? INT64_MIN : INT64_MAX) : shifted;
            put(out, i, bytes, clamp(r, bytes, to_signed));
        }
        break;
    }
    case 0x10: case 0x11: case 0x12: case 0x13: { /* narrowing right shifts */
        int wide = bytes * 2;
        if (wide > 8) { handled = false; break; }
        bool round = opcode == 0x11 || opcode == 0x13;
        /* 10000/10001: SHRN, RSHRN when U is clear, SQSHRUN, SQRSHRUN when set.
           10010/10011: SQSHRN, SQRSHRN, or UQSHRN, UQRSHRN with U. */
        bool saturating = opcode >= 0x12 || u;
        bool from_signed = opcode >= 0x12 ? !u : true;
        bool to_signed = opcode >= 0x12 ? !u : false;
        int count = scalar ? 1 : 8 / bytes;
        uint8_t narrowed[8] = {};
        for (int i = 0; i < count; ++i) {
            uint64_t v = (uint64_t)get(a, i, wide, from_signed);
            uint64_t r = shift_right(v, from_signed, right, round);
            uint64_t lane;
            if (!saturating) lane = r;
            else if (from_signed) lane = clamp((int64_t)r, bytes, to_signed);
            else lane = clamp_unsigned(r, bytes);
            put(narrowed, i, bytes, lane);
        }
        if (scalar) {
            std::memcpy(out, narrowed, bytes);
            result_bytes = bytes;
        } else if (q) {
            std::memcpy(out, d, 8);
            std::memcpy(out + 8, narrowed, 8);
            result_bytes = 16;
        } else {
            std::memcpy(out, narrowed, 8);
            result_bytes = 8;
        }
        break;
    }
    case 0x14: { /* SSHLL, USHLL, and so SXTL and UXTL */
        if (scalar || bytes == 8) { handled = false; break; }
        const uint8_t* from = a + (q ? 8 : 0);
        int count = 8 / bytes;
        for (int i = 0; i < count; ++i) put(out, i, bytes * 2, (uint64_t)get(from, i, bytes, is_signed) << left);
        result_bytes = 16;
        break;
    }
    case 0x1c: case 0x1f: { /* SCVTF/UCVTF and FCVTZS/FCVTZU with fraction bits */
        if (bytes < 4) { handled = false; break; }
        double scale = std::ldexp(1.0, right);
        for (int i = 0; i < lanes; ++i) {
            if (opcode == 0x1c) {
                double value = u ? (double)(uint64_t)get(a, i, bytes, false) : (double)get(a, i, bytes, true);
                value /= scale;
                if (bytes == 8) put(out, i, 8, double_to_bits(value));
                else put(out, i, 4, float_to_bits((float)value));
                continue;
            }
            uint64_t raw = (uint64_t)get(a, i, bytes, false);
            double value = std::trunc((bytes == 8 ? bits_to_double(raw) : (double)bits_to_float((uint32_t)raw)) * scale);
            uint64_t r;
            if (std::isnan(value)) {
                r = 0;
            } else if (u) {
                double top = bytes == 8 ? 18446744073709551615.0 : 4294967295.0;
                r = value <= 0 ? 0 : value >= top ? lane_mask : (uint64_t)value;
            } else {
                double top = bytes == 8 ? 9223372036854775807.0 : 2147483647.0;
                double bottom = bytes == 8 ? -9223372036854775808.0 : -2147483648.0;
                int64_t v = value >= top ? (bytes == 8 ? INT64_MAX : INT32_MAX)
                            : value <= bottom ? (bytes == 8 ? INT64_MIN : INT32_MIN) : (int64_t)value;
                r = (uint64_t)v;
            }
            put(out, i, bytes, r);
        }
        break;
    }
    default:
        handled = false;
    }
    if (!handled) return false;
    if (result_bytes < 16) std::memset(out + result_bytes, 0, 16 - result_bytes);
    std::memcpy(&cpu.q[rd].lo, out, 8);
    std::memcpy(&cpu.q[rd].hi, out + 8, 8);
    cpu.pc = next;
    return true;
}


/* Lanes of a 128-bit register, read and written by index and size. */
struct Lanes {
    uint8_t b[16];
    explicit Lanes(const GuestVec& v) {
        std::memcpy(b, &v.lo, 8);
        std::memcpy(b + 8, &v.hi, 8);
    }
    Lanes() { std::memset(b, 0, 16); }
    int64_t get(int index, int bytes, bool is_signed) const {
        uint64_t raw = 0;
        std::memcpy(&raw, b + index * bytes, bytes);
        if (is_signed && bytes < 8) {
            int shift = 64 - bytes * 8;
            return (int64_t)(raw << shift) >> shift;
        }
        return (int64_t)raw;
    }
    void set(int index, int bytes, uint64_t value) { std::memcpy(b + index * bytes, &value, bytes); }
    void store(GuestVec& v) const {
        std::memcpy(&v.lo, b, 8);
        std::memcpy(&v.hi, b + 8, 8);
    }
};

int64_t saturate_signed(int64_t value, int bytes, bool* saturated = nullptr) {
    if (bytes >= 8) return value;
    int64_t hi = ((int64_t)1 << (bytes * 8 - 1)) - 1, lo = -hi - 1;
    if (value > hi || value < lo) {
        if (saturated) *saturated = true;
        return value > hi ? hi : lo;
    }
    return value;
}

/* Signed saturating add of two values that may already be at the 64-bit
   extremes, for the 64-bit accumulations of the doubling multiplies. */
int64_t add_saturating64(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) return a < 0 ? INT64_MIN : INT64_MAX;
    return r;
}

/* SQRDMLAH and SQRDMLSH, exactly: (acc << w) +- 2xy + (1 << (w-1)), then
   the high half, saturated. For 32-bit lanes the sum needs 128 bits. */
int64_t rounding_doubling_accumulate(int64_t acc, int64_t x, int64_t y, int bits, bool subtract) {
    int64_t high;
    uint64_t low = (uint64_t)_mul128(x, subtract ? -2 * y : 2 * y, &high);
    auto add = [&](int64_t add_high, uint64_t add_low) {
        uint64_t before = low;
        low += add_low;
        high += add_high + (low < before ? 1 : 0);
    };
    add(acc >> (64 - bits), (uint64_t)acc << bits);
    add(0, 1ull << (bits - 1));
    int64_t shifted = (int64_t)((low >> bits) | ((uint64_t)high << (64 - bits)));
    return saturate_signed(shifted, bits / 8);
}

/* Carryless multiply of two 8-bit values, for PMUL and PMULL. */
uint64_t poly_multiply(uint64_t a, uint64_t b, int bits) {
    uint64_t r = 0;
    for (int i = 0; i < bits; ++i)
        if ((b >> i) & 1) r ^= a << i;
    return r;
}

/* The SIMD groups of integer arithmetic that change element size: "three
   different" (long, wide and narrowing forms, vector and scalar), the
   integer multiplies by element, and the three-same extension (rounding
   doubling multiply-accumulate, dot products). */
bool step_neon_widening(GuestCpu& cpu, uint32_t insn, uint64_t next) {
    int q = (insn >> 30) & 1;
    int u = (insn >> 29) & 1;
    int size = (insn >> 22) & 3;
    int rm = (insn >> 16) & 31;
    int rn = (insn >> 5) & 31;
    int rd = insn & 31;
    int bytes = 1 << size;        /* the narrow element */
    int wide = bytes * 2;         /* the element twice as big */
    int wide_bits = wide * 8;

    /* ---- Three different: 0 Q U 01110 size 1 Rm opcode 00 Rn Rd, and the
       scalar doubling forms 01 U 11110 size 1 Rm opcode 00 Rn Rd. */
    bool vector3 = (insn & 0x9f200c00u) == 0x0e200000u;
    bool scalar3 = (insn & 0xdf200c00u) == 0x5e200000u;
    if (vector3 || scalar3) {
        int opcode = (insn >> 12) & 15;
        if (size == 3 && !(opcode == 14 && !u)) return false;
        if (scalar3 && !(opcode == 9 || opcode == 11 || opcode == 13) ) return false;
        if (scalar3 && (size == 0 || size == 3)) return false;
        Lanes a(cpu.q[rn]), b(cpu.q[rm]), d(cpu.q[rd]), out;
        int count = scalar3 ? 1 : 8 / bytes;  /* narrow elements taken, or wide ones made */
        int part = scalar3 ? 0 : q * count;   /* the "2" forms take the upper half */
        bool is_signed = !u;
        switch (opcode) {
        case 0: case 1: case 2: case 3: { /* [SU]ADDL, [SU]ADDW, [SU]SUBL, [SU]SUBW */
            bool wide_first = opcode == 1 || opcode == 3;
            bool subtract = opcode >= 2;
            for (int i = 0; i < count; ++i) {
                int64_t x = wide_first ? a.get(i, wide, is_signed) : a.get(part + i, bytes, is_signed);
                int64_t y = b.get(part + i, bytes, is_signed);
                out.set(i, wide, (uint64_t)(subtract ? x - y : x + y));
            }
            out.store(cpu.q[rd]);
            break;
        }
        case 4: case 6: { /* ADDHN, RADDHN, SUBHN, RSUBHN: the high half of a wide sum */
            Lanes narrowed;
            for (int i = 0; i < count; ++i) {
                uint64_t x = (uint64_t)a.get(i, wide, false), y = (uint64_t)b.get(i, wide, false);
                uint64_t r = opcode == 4 ? x + y : x - y;
                if (u) r += 1ull << (bytes * 8 - 1);
                narrowed.set(i, bytes, r >> (bytes * 8));
            }
            if (q) {
                std::memcpy(out.b, d.b, 8);
                std::memcpy(out.b + 8, narrowed.b, 8);
            } else {
                std::memcpy(out.b, narrowed.b, 8);
            }
            out.store(cpu.q[rd]);
            break;
        }
        case 5: case 7: { /* [SU]ABAL, [SU]ABDL */
            for (int i = 0; i < count; ++i) {
                int64_t x = a.get(part + i, bytes, is_signed), y = b.get(part + i, bytes, is_signed);
                uint64_t diff = is_signed ? (uint64_t)(x > y ? x - y : y - x)
                                          : ((uint64_t)x > (uint64_t)y ? (uint64_t)x - (uint64_t)y
                                                                       : (uint64_t)y - (uint64_t)x);
                uint64_t acc = opcode == 5 ? (uint64_t)d.get(i, wide, false) : 0;
                out.set(i, wide, acc + diff);
            }
            out.store(cpu.q[rd]);
            break;
        }
        case 8: case 10: case 12: { /* [SU]MLAL, [SU]MLSL, [SU]MULL */
            for (int i = 0; i < count; ++i) {
                int64_t x = a.get(part + i, bytes, is_signed), y = b.get(part + i, bytes, is_signed);
                uint64_t product = is_signed ? (uint64_t)(x * y) : (uint64_t)x * (uint64_t)y;
                uint64_t acc = opcode == 12 ? 0 : (uint64_t)d.get(i, wide, false);
                out.set(i, wide, opcode == 10 ? acc - product : acc + product);
            }
            out.store(cpu.q[rd]);
            break;
        }
        case 9: case 11: case 13: { /* SQDMLAL, SQDMLSL, SQDMULL */
            if (u || (size != 1 && size != 2)) return false;
            for (int i = 0; i < count; ++i) {
                int64_t x = a.get(part + i, bytes, true), y = b.get(part + i, bytes, true);
                int64_t product = x * y;
                /* Doubling can only overflow for the most negative squared. */
                int64_t doubled = product == ((int64_t)1 << (bytes * 16 - 2)) && x == y && x < 0
                                      ? saturate_signed(INT64_MAX, wide)
                                      : saturate_signed(product * 2, wide);
                if (bytes == 4) doubled = (x == INT32_MIN && y == INT32_MIN) ? INT64_MAX : product * 2;
                int64_t r = doubled;
                if (opcode != 13) {
                    int64_t acc = d.get(i, wide, true);
                    if (opcode == 11) doubled = doubled == INT64_MIN ? INT64_MAX : -doubled;
                    r = wide == 8 ? add_saturating64(acc, doubled) : saturate_signed(acc + doubled, wide);
                }
                out.set(i, wide, (uint64_t)r);
            }
            out.store(cpu.q[rd]);
            break;
        }
        case 14: { /* PMULL: carryless, 8 to 16 bits, or 64 to 128 */
            if (u) return false;
            if (size == 0) {
                for (int i = 0; i < count; ++i)
                    out.set(i, 2, poly_multiply((uint64_t)a.get(part + i, 1, false),
                                                (uint64_t)b.get(part + i, 1, false), 8));
            } else if (size == 3) {
                uint64_t x = (uint64_t)a.get(q, 8, false), y = (uint64_t)b.get(q, 8, false);
                uint64_t lo = 0, hi = 0;
                for (int i = 0; i < 64; ++i)
                    if ((y >> i) & 1) {
                        lo ^= x << i;
                        if (i) hi ^= x >> (64 - i);
                    }
                out.set(0, 8, lo);
                out.set(1, 8, hi);
            } else {
                return false;
            }
            out.store(cpu.q[rd]);
            break;
        }
        default:
            return false;
        }
        cpu.pc = next;
        return true;
    }

    /* ---- Integer multiplies by element: 0 Q U 01111 size L M Rm opcode H 0,
       and the scalar forms 01 U 11111 size L M Rm opcode H 0. */
    bool vector_elem = (insn & 0x9f000400u) == 0x0f000000u;
    bool scalar_elem = (insn & 0xdf000400u) == 0x5f000000u;
    if (vector_elem || scalar_elem) {
        int opcode = (insn >> 12) & 15;
        /* The floating point forms (FMLA, FMLS, FMUL, FMULX) live elsewhere. */
        if (opcode == 1 || opcode == 5 || opcode == 9) return false;
        if (size != 1 && size != 2) return false;
        int l = (insn >> 21) & 1, m = (insn >> 20) & 1, h = (insn >> 11) & 1;
        int index = size == 1 ? (h << 2) | (l << 1) | m : (h << 1) | l;
        int element_reg = size == 1 ? (rm & 15) : rm;
        Lanes a(cpu.q[rn]), e(cpu.q[element_reg]), d(cpu.q[rd]), out;
        bool is_signed = !u;
        int lanes = scalar_elem ? 1 : (q ? 16 : 8) / bytes;
        int count = scalar_elem ? 1 : 8 / bytes;
        int part = scalar_elem ? 0 : q * count;
        int64_t es = e.get(index, bytes, true);
        uint64_t eu = (uint64_t)e.get(index, bytes, false);
        auto doubling_high = [&](int64_t x, int64_t y, bool round) -> int64_t {
            int64_t min = -((int64_t)1 << (bytes * 8 - 1));
            if (x == min && y == min) return -min - 1;
            int64_t product = x * y * 2;
            if (round) product += (int64_t)1 << (bytes * 8 - 1);
            return product >> (bytes * 8);
        };
        auto result_width = [&](int written) {
            if (!q || scalar_elem) {
                if (scalar_elem) std::memset(out.b + written, 0, 16 - written);
                else std::memset(out.b + 8, 0, 8);
            }
        };
        switch ((u << 4) | opcode) {
        case 0x10: case 0x14: case 0x08: { /* MLA, MLS, MUL */
            if (scalar_elem) return false;
            for (int i = 0; i < lanes; ++i) {
                uint64_t product = (uint64_t)a.get(i, bytes, false) * eu;
                uint64_t acc = (uint64_t)d.get(i, bytes, false);
                uint64_t r = opcode == 8 ? product : opcode == 0 ? acc + product : acc - product;
                out.set(i, bytes, r);
            }
            result_width(lanes * bytes);
            break;
        }
        case 0x02: case 0x12: case 0x06: case 0x16: case 0x0a: case 0x1a: { /* [SU]MLAL, [SU]MLSL, [SU]MULL */
            if (scalar_elem) return false;
            for (int i = 0; i < count; ++i) {
                int64_t x = a.get(part + i, bytes, is_signed);
                uint64_t product = is_signed ? (uint64_t)(x * es) : (uint64_t)x * eu;
                uint64_t acc = opcode == 10 ? 0 : (uint64_t)d.get(i, wide, false);
                out.set(i, wide, opcode == 6 ? acc - product : acc + product);
            }
            break;
        }
        case 0x03: case 0x07: case 0x0b: { /* SQDMLAL, SQDMLSL, SQDMULL */
            for (int i = 0; i < count; ++i) {
                int64_t x = a.get(part + i, bytes, true);
                int64_t min = -((int64_t)1 << (bytes * 8 - 1));
                int64_t doubled = (x == min && es == min) ? saturate_signed(INT64_MAX, wide) : x * es * 2;
                int64_t r = doubled;
                if (opcode != 11) {
                    int64_t acc = d.get(i, wide, true);
                    if (opcode == 7) doubled = doubled == INT64_MIN ? INT64_MAX : -doubled;
                    r = wide == 8 ? add_saturating64(acc, doubled) : saturate_signed(acc + doubled, wide);
                }
                out.set(i, wide, (uint64_t)r);
            }
            if (scalar_elem) std::memset(out.b + wide, 0, 16 - wide);
            break;
        }
        case 0x0c: case 0x0d: { /* SQDMULH, SQRDMULH */
            for (int i = 0; i < lanes; ++i)
                out.set(i, bytes, (uint64_t)doubling_high(a.get(i, bytes, true), es, opcode == 13));
            result_width(lanes * bytes);
            break;
        }
        case 0x1d: case 0x1f: { /* SQRDMLAH, SQRDMLSH */
            for (int i = 0; i < lanes; ++i) {
                int64_t x = a.get(i, bytes, true);
                int64_t acc = d.get(i, bytes, true);
                out.set(i, bytes, (uint64_t)rounding_doubling_accumulate(acc, x, es, bytes * 8, opcode == 0xf));
            }
            result_width(lanes * bytes);
            break;
        }
        case 0x0e: case 0x1e: { /* SDOT, UDOT by element: four bytes into each word */
            if (size != 2 || scalar_elem) return false;
            int group = index; /* which word of the element register */
            for (int i = 0; i < lanes; ++i) {
                uint64_t acc = (uint64_t)d.get(i, 4, false);
                for (int k = 0; k < 4; ++k) {
                    int64_t x = a.get(i * 4 + k, 1, is_signed);
                    int64_t y = e.get(group * 4 + k, 1, is_signed);
                    acc += (uint64_t)(x * y);
                }
                out.set(i, 4, acc);
            }
            result_width(lanes * 4);
            break;
        }
        default:
            return false;
        }
        out.store(cpu.q[rd]);
        cpu.pc = next;
        return true;
    }

    /* ---- Three same (extension): 0 Q U 01110 size 0 Rm 1 opcode 1 Rn Rd, and
       the scalar SQRDMLAH and SQRDMLSH, 01 1 11110 size 0 Rm 1 opcode 1. */
    bool scalar_extra = (insn & 0xdf208400u) == 0x5e008400u;
    if ((insn & 0x9f208400u) == 0x0e008400u || scalar_extra) {
        int opcode = (insn >> 11) & 15;
        Lanes a(cpu.q[rn]), b(cpu.q[rm]), d(cpu.q[rd]), out;
        int lanes = scalar_extra ? 1 : (q ? 16 : 8) / bytes;
        if (scalar_extra && !(u && (opcode == 0 || opcode == 1))) return false;
        if (u && (opcode == 0 || opcode == 1) && (size == 1 || size == 2)) { /* SQRDMLAH, SQRDMLSH */
            for (int i = 0; i < lanes; ++i) {
                int64_t x = a.get(i, bytes, true), y = b.get(i, bytes, true), acc = d.get(i, bytes, true);
                out.set(i, bytes, (uint64_t)rounding_doubling_accumulate(acc, x, y, bytes * 8, opcode == 1));
            }
        } else if (opcode == 2 && size == 2) { /* SDOT, UDOT */
            for (int i = 0; i < lanes; ++i) {
                uint64_t acc = (uint64_t)d.get(i, 4, false);
                for (int k = 0; k < 4; ++k)
                    acc += (uint64_t)(a.get(i * 4 + k, 1, !u) * b.get(i * 4 + k, 1, !u));
                out.set(i, 4, acc);
            }
        } else {
            return false;
        }
        std::memset(out.b + lanes * bytes, 0, 16 - lanes * bytes);
        out.store(cpu.q[rd]);
        cpu.pc = next;
        return true;
    }
    return false;
}


/* ---- Floating point with the architecture's own rules. ----

   Host floating point is IEEE 754 like the guest's, so ordinary results agree
   bit for bit; singles are computed as floats, not widened to doubles and
   rounded twice. What differs is everything around NaNs and a few defined
   special cases, which ARM specifies exactly and x86 does its own way:
     - which NaN operand becomes the result (signalling first, then quiet, in
       operand order; the addend first for fused multiply-add);
     - the default NaN, positive on ARM, negative on x86;
     - max and min of zeros of either sign;
     - FMULX, FRECPS and FRSQRTS at infinity times zero. */

bool fp_nan(uint64_t b, bool d) {
    return d ? (b & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (b & 0x000fffffffffffffull)
             : (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu);
}
bool fp_snan(uint64_t b, bool d) { return fp_nan(b, d) && !(b & (d ? (1ull << 51) : (1ull << 22))); }
uint64_t fp_quiet(uint64_t b, bool d) { return b | (d ? (1ull << 51) : (1ull << 22)); }
uint64_t fp_default_nan(bool d) { return d ? 0x7ff8000000000000ull : 0x7fc00000ull; }
uint64_t fp_negate(uint64_t b, bool d) { return b ^ (d ? (1ull << 63) : (1ull << 31)); }
bool fp_sign(uint64_t b, bool d) { return (b >> (d ? 63 : 31)) & 1; }
bool fp_zero(uint64_t b, bool d) { return (b & (d ? 0x7fffffffffffffffull : 0x7fffffffull)) == 0; }
bool fp_inf(uint64_t b, bool d) {
    return d ? (b & 0x7fffffffffffffffull) == 0x7ff0000000000000ull : (b & 0x7fffffffu) == 0x7f800000u;
}

bool fp_process_nans(const uint64_t* ops, int count, bool d, uint64_t* out) {
    for (int i = 0; i < count; ++i)
        if (fp_snan(ops[i], d)) {
            *out = fp_quiet(ops[i], d);
            return true;
        }
    for (int i = 0; i < count; ++i)
        if (fp_nan(ops[i], d)) {
            *out = ops[i];
            return true;
        }
    return false;
}

/* A computed result that is NaN with no NaN going in was an invalid
   operation, and ARM's answer to that is the positive default NaN. */
uint64_t fp_from_double(double r, bool d) {
    if (std::isnan(r)) return fp_default_nan(d);
    return d ? double_to_bits(r) : (uint64_t)float_to_bits((float)r);
}

enum FpOp { kAdd, kSub, kMul, kDiv, kNmul, kAbd, kMax, kMin, kMaxnm, kMinnm, kMulx, kRecps, kRsqrts };

uint64_t fp_binary(FpOp op, uint64_t a, uint64_t b, bool d) {
    if (op == kRecps || op == kRsqrts) a = fp_negate(a, d);
    bool anan = fp_nan(a, d), bnan = fp_nan(b, d);
    if ((op == kMaxnm || op == kMinnm) && anan != bnan) {
        /* One quiet NaN against a number: the number. */
        uint64_t nan = anan ? a : b;
        if (!fp_snan(nan, d)) return anan ? b : a;
    }
    if (anan || bnan) {
        uint64_t ops[2] = {a, b}, out = 0;
        fp_process_nans(ops, 2, d, &out);
        /* FABD and FNMUL apply their absolute value and negation after the
           NaN is chosen, so they reach the NaN's sign too. */
        if (op == kAbd) return out & (d ? 0x7fffffffffffffffull : 0x7fffffffull);
        if (op == kNmul) return fp_negate(out, d);
        return out;
    }
    if ((op == kMax || op == kMin || op == kMaxnm || op == kMinnm) && fp_zero(a, d) && fp_zero(b, d)) {
        bool negative = (op == kMax || op == kMaxnm) ? (fp_sign(a, d) && fp_sign(b, d))
                                                      : (fp_sign(a, d) || fp_sign(b, d));
        return negative ? (d ? 1ull << 63 : 1ull << 31) : 0;
    }
    bool inf_times_zero = (fp_inf(a, d) && fp_zero(b, d)) || (fp_zero(a, d) && fp_inf(b, d));
    if (op == kMulx && inf_times_zero) {
        uint64_t two = d ? 0x4000000000000000ull : 0x40000000ull;
        return (fp_sign(a, d) != fp_sign(b, d)) ? fp_negate(two, d) : two;
    }
    if (op == kRecps && inf_times_zero) return d ? 0x4000000000000000ull : 0x40000000ull; /* 2.0 */
    if (op == kRsqrts && inf_times_zero) return d ? 0x3ff8000000000000ull : 0x3fc00000ull; /* 1.5 */
    if (d) {
        double x = bits_to_double(a), y = bits_to_double(b), r = 0;
        switch (op) {
        case kAdd: r = x + y; break;
        case kSub: r = x - y; break;
        case kMul: case kMulx: r = x * y; break;
        case kDiv: r = x / y; break;
        case kNmul: r = -(x * y); break;
        case kAbd: r = std::fabs(x - y); break;
        case kMax: case kMaxnm: r = x > y ? x : y; break;
        case kMin: case kMinnm: r = x < y ? x : y; break;
        case kRecps: r = std::fma(x, y, 2.0); break;
        case kRsqrts: r = std::fma(x, y, 3.0) / 2.0; break;
        }
        return fp_from_double(r, true);
    }
    float x = bits_to_float((uint32_t)a), y = bits_to_float((uint32_t)b), r = 0;
    switch (op) {
    case kAdd: r = x + y; break;
    case kSub: r = x - y; break;
    case kMul: case kMulx: r = x * y; break;
    case kDiv: r = x / y; break;
    case kNmul: r = -(x * y); break;
    case kAbd: r = std::fabs(x - y); break;
    case kMax: case kMaxnm: r = x > y ? x : y; break;
    case kMin: case kMinnm: r = x < y ? x : y; break;
    case kRecps: r = std::fmaf(x, y, 2.0f); break;
    case kRsqrts: r = std::fmaf(x, y, 3.0f) / 2.0f; break;
    }
    if (std::isnan(r)) return fp_default_nan(false);
    return float_to_bits(r);
}

/* addend + op1 * op2, rounded once, with the architecture's NaN order. The
   negations of FMSUB, FNMADD, FNMSUB and FMLS are applied to the operands by
   the caller first, as the architecture does. */
uint64_t fp_fused(uint64_t addend, uint64_t op1, uint64_t op2, bool d) {
    uint64_t ops[3] = {addend, op1, op2}, out = 0;
    bool inf_times_zero = (fp_inf(op1, d) && fp_zero(op2, d)) || (fp_zero(op1, d) && fp_inf(op2, d));
    if (fp_nan(addend, d) && !fp_snan(addend, d) && inf_times_zero && !fp_snan(op1, d) && !fp_snan(op2, d))
        return fp_default_nan(d);
    if (fp_process_nans(ops, 3, d, &out)) return out;
    if (d) return fp_from_double(std::fma(bits_to_double(op1), bits_to_double(op2), bits_to_double(addend)), true);
    float r = std::fmaf(bits_to_float((uint32_t)op1), bits_to_float((uint32_t)op2), bits_to_float((uint32_t)addend));
    return std::isnan(r) ? fp_default_nan(false) : (uint64_t)float_to_bits(r);
}

/* 0: not ordered (a NaN), otherwise the NZCV an FCMP would give. */
int fp_compare_flags(uint64_t a, uint64_t b, bool d) {
    if (fp_nan(a, d) || fp_nan(b, d)) return 0x3;
    double x = d ? bits_to_double(a) : bits_to_float((uint32_t)a);
    double y = d ? bits_to_double(b) : bits_to_float((uint32_t)b);
    if (x == y) return 0x6;
    return x < y ? 0x8 : 0x2;
}

uint64_t fp_lane(const GuestVec& v, int index, bool d) {
    uint8_t bytes[16];
    std::memcpy(bytes, &v.lo, 8);
    std::memcpy(bytes + 8, &v.hi, 8);
    uint64_t out = 0;
    std::memcpy(&out, bytes + index * (d ? 8 : 4), d ? 8 : 4);
    return out;
}

void fp_set_lane(uint8_t* bytes, int index, bool d, uint64_t value) {
    std::memcpy(bytes + index * (d ? 8 : 4), &value, d ? 8 : 4);
}

bool step_fp_arith(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next) {
    int q = (insn >> 30) & 1;
    int u = (insn >> 29) & 1;
    int rm = (insn >> 16) & 31;
    int rn = (insn >> 5) & 31;
    int rd = insn & 31;
    auto finish_vector = [&](const uint8_t* out, int written) {
        uint8_t full[16] = {};
        std::memcpy(full, out, written);
        std::memcpy(&cpu.q[rd].lo, full, 8);
        std::memcpy(&cpu.q[rd].hi, full + 8, 8);
        cpu.pc = next;
        return true;
    };

    /* ---- Three same, floating point: vector 0 Q U 01110 a sz 1 Rm opcode 1,
       scalar 01 U 11110 a sz 1 Rm opcode 1, opcodes 11000 to 11111. */
    bool vector3 = (insn & 0x9f200400u) == 0x0e200400u;
    bool scalar3 = (insn & 0xdf200400u) == 0x5e200400u;
    int opcode5 = (insn >> 11) & 0x1f;
    if ((vector3 || scalar3) && opcode5 >= 0x18) {
        bool a_bit = (insn >> 23) & 1;
        bool d = (insn >> 22) & 1;
        if (d && !q && vector3) return false; /* no 64-bit vector of doubles */
        int lane_bytes = d ? 8 : 4;
        int lanes = scalar3 ? 1 : (q ? 16 : 8) / lane_bytes;
        int code = (u << 6) | (a_bit << 5) | opcode5;
        uint8_t out[16] = {};
        auto lane_a = [&](int i) { return fp_lane(cpu.q[rn], i, d); };
        auto lane_b = [&](int i) { return fp_lane(cpu.q[rm], i, d); };
        /* Pairwise: neighbours of a, then of b. */
        auto pair = [&](int i, uint64_t* x, uint64_t* y) {
            int half = lanes / 2;
            const GuestVec& from = i < half ? cpu.q[rn] : cpu.q[rm];
            int base = (i < half ? i : i - half) * 2;
            *x = fp_lane(from, base, d);
            *y = fp_lane(from, base + 1, d);
        };
        bool pairwise = u && !scalar3 && (opcode5 == 0x18 || opcode5 == 0x1e || (opcode5 == 0x1a && !a_bit));
        for (int i = 0; i < lanes; ++i) {
            uint64_t x, y;
            if (pairwise) pair(i, &x, &y);
            else {
                x = lane_a(i);
                y = lane_b(i);
            }
            uint64_t r = 0;
            switch (code) {
            case 0x18: r = fp_binary(kMaxnm, x, y, d); break;                       /* FMAXNM */
            case 0x38: r = fp_binary(kMinnm, x, y, d); break;                       /* FMINNM */
            case 0x19: r = fp_fused(fp_lane(cpu.q[rd], i, d), x, y, d); break;      /* FMLA */
            case 0x39: r = fp_fused(fp_lane(cpu.q[rd], i, d), fp_negate(x, d), y, d); break; /* FMLS */
            case 0x1a: r = fp_binary(kAdd, x, y, d); break;                          /* FADD */
            case 0x3a: r = fp_binary(kSub, x, y, d); break;                          /* FSUB */
            case 0x1b: r = fp_binary(kMulx, x, y, d); break;                         /* FMULX */
            case 0x1c: r = fp_compare_flags(x, y, d) == 0x6 ? ~0ull : 0; break;      /* FCMEQ */
            case 0x1e: r = fp_binary(kMax, x, y, d); break;                          /* FMAX */
            case 0x3e: r = fp_binary(kMin, x, y, d); break;                          /* FMIN */
            case 0x1f: r = fp_binary(kRecps, x, y, d); break;                        /* FRECPS */
            case 0x3f: r = fp_binary(kRsqrts, x, y, d); break;                       /* FRSQRTS */
            case 0x58: r = fp_binary(kMaxnm, x, y, d); break;                        /* FMAXNMP */
            case 0x78: r = fp_binary(kMinnm, x, y, d); break;                        /* FMINNMP */
            case 0x5a: r = fp_binary(kAdd, x, y, d); break;                          /* FADDP */
            case 0x7a: r = fp_binary(kAbd, x, y, d); break;                          /* FABD */
            case 0x5b: r = fp_binary(kMul, x, y, d); break;                          /* FMUL */
            case 0x5c: { int f = fp_compare_flags(x, y, d); r = (f == 0x6 || f == 0x2) ? ~0ull : 0; break; } /* FCMGE */
            case 0x7c: r = fp_compare_flags(x, y, d) == 0x2 ? ~0ull : 0; break;      /* FCMGT */
            case 0x5d: case 0x7d: {                                                  /* FACGE, FACGT */
                uint64_t mask = d ? 0x7fffffffffffffffull : 0x7fffffffull;
                int f = fp_compare_flags(x & mask, y & mask, d);
                r = (f == 0x2 || (code == 0x5d && f == 0x6)) ? ~0ull : 0;
                break;
            }
            case 0x5e: r = fp_binary(kMax, x, y, d); break;                          /* FMAXP */
            case 0x7e: r = fp_binary(kMin, x, y, d); break;                          /* FMINP */
            case 0x5f: r = fp_binary(kDiv, x, y, d); break;                          /* FDIV */
            default: return false;
            }
            if (scalar3 && pairwise) return false;
            fp_set_lane(out, i, d, r);
        }
        return finish_vector(out, lanes * lane_bytes);
    }

    /* ---- By element, floating point: vector 0 Q U 01111 1 sz L M Rm opcode
       H 0, scalar 01 U 11111 1 sz L M Rm opcode H 0; FMLA, FMLS, FMUL, FMULX. */
    bool vector_elem = (insn & 0x9f800400u) == 0x0f800000u;
    bool scalar_elem = (insn & 0xdf800400u) == 0x5f800000u;
    int opcode4 = (insn >> 12) & 15;
    if ((vector_elem || scalar_elem) && (opcode4 == 1 || opcode4 == 5 || opcode4 == 9) && !(u && opcode4 != 9)) {
        bool d = (insn >> 22) & 1;
        int l = (insn >> 21) & 1, h = (insn >> 11) & 1, m = (insn >> 20) & 1;
        if (d && l) return false;
        int index = d ? h : (h << 1) | l;
        int element_reg = (m << 4) | (rm & 15);
        if (d && !q && vector_elem) return false;
        int lane_bytes = d ? 8 : 4;
        int lanes = scalar_elem ? 1 : (q ? 16 : 8) / lane_bytes;
        uint64_t e = fp_lane(cpu.q[element_reg], index, d);
        uint8_t out[16] = {};
        for (int i = 0; i < lanes; ++i) {
            uint64_t x = fp_lane(cpu.q[rn], i, d), r;
            if (opcode4 == 1) r = fp_fused(fp_lane(cpu.q[rd], i, d), x, e, d);
            else if (opcode4 == 5) r = fp_fused(fp_lane(cpu.q[rd], i, d), fp_negate(x, d), e, d);
            else r = fp_binary(u ? kMulx : kMul, x, e, d);
            fp_set_lane(out, i, d, r);
        }
        return finish_vector(out, lanes * lane_bytes);
    }

    /* ---- Scalar data processing: 000 11110 type 1 ... */
    if ((insn & 0xff200000u) == 0x1e200000u || (insn & 0xff200000u) == 0x1e600000u) {
        bool d = (insn >> 22) & 1;
        /* Two source: Rm opcode 10 Rn Rd. */
        if (((insn >> 10) & 3) == 2) {
            static const FpOp kOps[] = {kMul, kDiv, kAdd, kSub, kMax, kMin, kMaxnm, kMinnm, kNmul};
            int opcode = (insn >> 12) & 15;
            if (opcode > 8) return false;
            uint64_t r = fp_binary(kOps[opcode], fp_lane(cpu.q[rn], 0, d), fp_lane(cpu.q[rm], 0, d), d);
            uint8_t out[16] = {};
            fp_set_lane(out, 0, d, r);
            return finish_vector(out, d ? 8 : 4);
        }
        /* Conditional compare: Rm cond 01 Rn op nzcv. */
        if (((insn >> 10) & 3) == 1) {
            int cc = (insn >> 12) & 15;
            int flags = condition(cpu, cc) ? fp_compare_flags(fp_lane(cpu.q[rn], 0, d), fp_lane(cpu.q[rm], 0, d), d)
                                           : (int)(insn & 15);
            cpu.n = (flags >> 3) & 1;
            cpu.z = (flags >> 2) & 1;
            cpu.c = (flags >> 1) & 1;
            cpu.v = flags & 1;
            cpu.pc = next;
            return true;
        }
    }

    /* ---- Fused multiply-add: 000 11111 type o1 Rm o0 Ra Rn Rd. */
    if ((insn & 0xff000000u) == 0x1f000000u && ((insn >> 23) & 1) == 0) {
        bool d = (insn >> 22) & 1;
        bool o1 = (insn >> 21) & 1, o0 = (insn >> 15) & 1;
        int ra = (insn >> 10) & 31;
        uint64_t n = fp_lane(cpu.q[rn], 0, d), m = fp_lane(cpu.q[rm], 0, d), a = fp_lane(cpu.q[ra], 0, d);
        if (o0 != o1) n = fp_negate(n, d);          /* FMSUB and FNMADD negate the product */
        if (o1) a = fp_negate(a, d);                /* FNMADD and FNMSUB negate the addend */
        uint8_t out[16] = {};
        fp_set_lane(out, 0, d, fp_fused(a, n, m, d));
        return finish_vector(out, d ? 8 : 4);
    }

    /* ---- Fixed point conversions, scalar: sf 0 0 11110 type 0 rmode opcode
       scale Rn Rd. */
    if ((insn & 0x7f200000u) == 0x1e000000u && ((insn >> 23) & 1) == 0) {
        bool d = (insn >> 22) & 1;
        bool wide = (insn >> 31) & 1;
        int rmode = (insn >> 19) & 3, opcode = (insn >> 16) & 7;
        int fbits = 64 - ((insn >> 10) & 63);
        if (!wide && fbits > 32) return false;
        double scale = std::ldexp(1.0, fbits);
        if (rmode == 0 && (opcode == 2 || opcode == 3)) { /* SCVTF, UCVTF */
            uint64_t raw = rn == 31 ? 0 : cpu.x[rn];
            double value;
            if (opcode == 2) value = wide ? (double)(int64_t)raw : (double)(int32_t)(uint32_t)raw;
            else value = wide ? (double)raw : (double)(uint32_t)raw;
            value /= scale;
            uint8_t out[16] = {};
            fp_set_lane(out, 0, d, d ? double_to_bits(value) : (uint64_t)float_to_bits((float)value));
            return finish_vector(out, d ? 8 : 4);
        }
        if (rmode == 3 && (opcode == 0 || opcode == 1)) { /* FCVTZS, FCVTZU */
            uint64_t bits = fp_lane(cpu.q[rn], 0, d);
            double value = std::trunc((d ? bits_to_double(bits) : (double)bits_to_float((uint32_t)bits)) * scale);
            uint64_t r;
            if (std::isnan(value)) r = 0;
            else if (opcode == 1) {
                double top = wide ? 18446744073709551615.0 : 4294967295.0;
                r = value <= 0 ? 0 : value >= top ? (wide ? ~0ull : 0xffffffffull) : (uint64_t)value;
            } else {
                double top = wide ? 9223372036854775807.0 : 2147483647.0, bottom = wide ? -9223372036854775808.0 : -2147483648.0;
                int64_t v = value >= top ? (wide ? INT64_MAX : INT32_MAX)
                            : value <= bottom ? (wide ? INT64_MIN : INT32_MIN) : (int64_t)value;
                r = (uint64_t)v;
            }
            if (!wide) r &= 0xffffffffull;
            if (rd != 31) cpu.x[rd] = r;
            cpu.pc = next;
            return true;
        }
        return false;
    }

    /* ---- TBL and TBX: 0 Q 001110 000 Rm 0 len op 00 Rn Rd. */
    if ((insn & 0xbfe08c00u) == 0x0e000000u) {
        int tables = ((insn >> 13) & 3) + 1;
        bool extend = (insn >> 12) & 1;
        uint8_t table[64];
        for (int t = 0; t < tables; ++t) {
            const GuestVec& v = cpu.q[(rn + t) & 31];
            std::memcpy(table + t * 16, &v.lo, 8);
            std::memcpy(table + t * 16 + 8, &v.hi, 8);
        }
        uint8_t index[16], out[16];
        std::memcpy(index, &cpu.q[rm].lo, 8);
        std::memcpy(index + 8, &cpu.q[rm].hi, 8);
        std::memcpy(out, &cpu.q[rd].lo, 8);
        std::memcpy(out + 8, &cpu.q[rd].hi, 8);
        int count = q ? 16 : 8;
        for (int i = 0; i < count; ++i) {
            if (index[i] < tables * 16) out[i] = table[index[i]];
            else if (!extend) out[i] = 0;
        }
        return finish_vector(out, count);
    }

    /* ---- SMOV: 0 Q 001110000 imm5 0 0101 1 Rn Rd. */
    if ((insn & 0xbfe0fc00u) == 0x0e002c00u) {
        int imm5 = (insn >> 16) & 31;
        int size = 0;
        while (size < 3 && !((imm5 >> size) & 1)) ++size;
        if (size >= 3 || (size == 2 && !q)) return false;
        int index = imm5 >> (size + 1);
        uint8_t bytes[16];
        std::memcpy(bytes, &cpu.q[rn].lo, 8);
        std::memcpy(bytes + 8, &cpu.q[rn].hi, 8);
        uint64_t raw = 0;
        std::memcpy(&raw, bytes + index * (1 << size), 1 << size);
        int shift = 64 - (8 << size);
        int64_t value = (int64_t)(raw << shift) >> shift;
        if (rd != 31) cpu.x[rd] = q ? (uint64_t)value : (uint64_t)(uint32_t)value;
        cpu.pc = next;
        return true;
    }

    /* ---- Single structures, every count: LD1-LD4 and ST1-ST4 of one lane,
       and LD1R-LD4R: 0 Q 0011010 L R 00000 opcode S size Rn Rt, or with
       0011011 and Rm for post-index. */
    if ((insn & 0xbf800000u) == 0x0d000000u || (insn & 0xbf800000u) == 0x0d800000u) {
        bool post = (insn >> 23) & 1;
        bool load = (insn >> 22) & 1;
        int r_bit = (insn >> 21) & 1;
        int opcode = (insn >> 13) & 7;
        int s_bit = (insn >> 12) & 1;
        int size = (insn >> 10) & 3;
        int selem = (((opcode & 1) << 1) | r_bit) + 1;
        int scale = opcode >> 1;
        int index = 0;
        bool replicate = false;
        if (!post && rm != 0) return false;
        switch (scale) {
        case 3:
            if (!load || s_bit) return false;
            replicate = true;
            scale = size;
            break;
        case 0: index = (q << 3) | (s_bit << 2) | size; break;
        case 1:
            if (size & 1) return false;
            index = (q << 2) | (s_bit << 1) | (size >> 1);
            break;
        case 2:
            if (size & 2) return false;
            if (!(size & 1)) index = (q << 1) | s_bit;
            else {
                if (s_bit) return false;
                index = q;
                scale = 3;
            }
            break;
        }
        int esize = 1 << scale;
        uint64_t base = rn == 31 ? cpu.sp : cpu.x[rn];
        for (int s = 0; s < selem; ++s) {
            int reg_index = (int)((insn & 31) + s) & 31;
            uint8_t* p = guest_ptr(mem, base + (uint64_t)s * esize, esize);
            if (!p) return false;
            uint8_t bytes[16];
            std::memcpy(bytes, &cpu.q[reg_index].lo, 8);
            std::memcpy(bytes + 8, &cpu.q[reg_index].hi, 8);
            if (replicate) {
                for (int at = 0; at < 16; at += esize) std::memcpy(bytes + at, p, esize);
                if (!q) std::memset(bytes + 8, 0, 8);
            } else if (load) {
                std::memcpy(bytes + index * esize, p, esize);
            } else {
                std::memcpy(p, bytes + index * esize, esize);
            }
            if (load) {
                std::memcpy(&cpu.q[reg_index].lo, bytes, 8);
                std::memcpy(&cpu.q[reg_index].hi, bytes + 8, 8);
            }
        }
        if (post) {
            uint64_t step = rm == 31 ? (uint64_t)selem * esize : cpu.x[rm];
            if (rn == 31) cpu.sp = base + step;
            else cpu.x[rn] = base + step;
        }
        cpu.pc = next;
        return true;
    }
    return false;
}


/* Two register misc, the saturating and rounding members not handled with
   the rest: SUQADD, USQADD, SQABS, SQNEG (vector and scalar) and the vector
   FRINT family. 0 Q U 01110 size 10000 opcode 10 Rn Rd, or 01 U 11110 ...
   for the scalar forms. */
bool step_misc_extra(GuestCpu& cpu, uint32_t insn, uint64_t next) {
    bool misc_vector = (insn & 0x9f3e0c00u) == 0x0e200800u;
    bool misc_scalar = (insn & 0xdf3e0c00u) == 0x5e200800u;
    if (!misc_vector && !misc_scalar) return false;
    int q = (insn >> 30) & 1;
    int u = (insn >> 29) & 1;
    int size = (insn >> 22) & 3;
    int opcode = (insn >> 12) & 0x1f;
    int rn = (insn >> 5) & 31;
    int rd = insn & 31;
    int bytes = 1 << size;

    if (opcode == 0x06 && misc_vector && size < 3) {
        /* SADALP, UADALP: add neighbouring pairs, widened, into the lanes of
           the destination. */
        Lanes a(cpu.q[rn]), d(cpu.q[rd]), out;
        int wide = bytes * 2;
        int lanes = (q ? 16 : 8) / wide;
        for (int i = 0; i < lanes; ++i) {
            int64_t x = a.get(i * 2, bytes, !u), y = a.get(i * 2 + 1, bytes, !u);
            out.set(i, wide, (uint64_t)d.get(i, wide, false) + (uint64_t)(x + y));
        }
        std::memset(out.b + lanes * wide, 0, 16 - lanes * wide);
        out.store(cpu.q[rd]);
        cpu.pc = next;
        return true;
    }
    if (opcode == 0x03 || opcode == 0x07) {
        if (misc_vector && size == 3 && !q) return false;
        Lanes a(cpu.q[rn]), d(cpu.q[rd]), out;
        int lanes = misc_scalar ? 1 : (q ? 16 : 8) / bytes;
        int bits = bytes * 8;
        int64_t smax = bits == 64 ? INT64_MAX : (((int64_t)1 << (bits - 1)) - 1);
        int64_t smin = -smax - 1;
        uint64_t umax = bits == 64 ? ~0ull : ((1ull << bits) - 1);
        for (int i = 0; i < lanes; ++i) {
            uint64_t r;
            if (opcode == 0x07) {
                /* SQABS and SQNEG: the most negative value has no positive
                   counterpart and saturates. */
                int64_t x = a.get(i, bytes, true);
                if (x == smin) r = (uint64_t)smax;
                else r = (uint64_t)(u ? -x : (x < 0 ? -x : x));
            } else if (!u) {
                /* SUQADD: a signed accumulator plus an unsigned operand. */
                int64_t acc = d.get(i, bytes, true);
                uint64_t add = (uint64_t)a.get(i, bytes, false);
                if (bits < 64) {
                    int64_t sum = acc + (int64_t)add;
                    r = (uint64_t)(sum > smax ? smax : sum);
                } else if (acc >= 0) {
                    r = add > (uint64_t)INT64_MAX - (uint64_t)acc ? (uint64_t)INT64_MAX : (uint64_t)acc + add;
                } else {
                    uint64_t magnitude = (uint64_t)(-(acc + 1)) + 1;
                    if (add >= magnitude) {
                        uint64_t difference = add - magnitude;
                        r = difference > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : difference;
                    } else {
                        r = (uint64_t)acc + add;
                    }
                }
            } else {
                /* USQADD: an unsigned accumulator plus a signed operand. */
                uint64_t acc = (uint64_t)d.get(i, bytes, false);
                int64_t add = a.get(i, bytes, true);
                if (add < 0) {
                    uint64_t magnitude = (uint64_t)(-(add + 1)) + 1;
                    r = magnitude > acc ? 0 : acc - magnitude;
                } else {
                    uint64_t sum = acc + (uint64_t)add;
                    r = (sum < acc || sum > umax) ? umax : sum;
                }
            }
            out.set(i, bytes, r);
        }
        std::memset(out.b + lanes * bytes, 0, 16 - lanes * bytes);
        out.store(cpu.q[rd]);
        cpu.pc = next;
        return true;
    }

    if (misc_vector && (opcode == 0x18 || opcode == 0x19)) {
        /* FRINTN, FRINTM (size 0x) and FRINTP, FRINTZ (size 1x) with U clear;
           FRINTA, FRINTX and FRINTI with U set. Bit 22 is the precision. */
        bool d = (size & 1) != 0;
        if (d && !q) return false;
        bool high = (size & 2) != 0;
        int mode; /* 0 nearest-even, 1 down, 2 up, 3 toward zero, 4 away */
        if (!u) mode = opcode == 0x18 ? (high ? 2 : 0) : (high ? 3 : 1);
        else if (opcode == 0x18 && !high) mode = 4;
        else if (opcode == 0x19) mode = 0; /* FRINTX, FRINTI: the current mode, nearest */
        else return false;
        int lanes = (q ? 16 : 8) / (d ? 8 : 4);
        uint8_t result[16] = {};
        for (int i = 0; i < lanes; ++i) {
            uint64_t bits = fp_lane(cpu.q[rn], i, d);
            uint64_t r;
            if (fp_nan(bits, d)) {
                r = fp_quiet(bits, d);
            } else {
                double x = d ? bits_to_double(bits) : (double)bits_to_float((uint32_t)bits);
                double y = mode == 0 ? std::nearbyint(x) : mode == 1 ? std::floor(x) : mode == 2 ? std::ceil(x)
                           : mode == 3 ? std::trunc(x) : std::round(x);
                r = d ? double_to_bits(y) : (uint64_t)float_to_bits((float)y);
            }
            fp_set_lane(result, i, d, r);
        }
        std::memcpy(&cpu.q[rd].lo, result, 8);
        std::memcpy(&cpu.q[rd].hi, result + 8, 8);
        cpu.pc = next;
        return true;
    }
    return false;
}


/* The reciprocal and reciprocal square root estimates, exactly as the
   architecture's pseudocode computes them: eight bits from a table formula,
   so results match the hardware bit for bit. */
int recip_estimate(int a) {
    a = a * 2 + 1;
    int b = (1 << 19) / a;
    return (b + 1) / 2;
}

int recip_sqrt_estimate(int a) {
    if (a < 256) {
        a = a * 2 + 1;
    } else {
        a = (a >> 1) << 1;
        a = (a + 1) * 2;
    }
    int b = 512;
    while ((int64_t)a * (b + 1) * (b + 1) < (int64_t)1 << 28) ++b;
    return (b + 1) / 2;
}

int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

uint64_t fp_recip_estimate(uint64_t operand, bool d) {
    bool sign = fp_sign(operand, d);
    if (fp_nan(operand, d)) return fp_quiet(operand, d);
    if (fp_inf(operand, d)) return sign ? (d ? 1ull << 63 : 1ull << 31) : 0;
    if (fp_zero(operand, d)) return (d ? 0x7ff0000000000000ull : 0x7f800000ull) | (sign ? (d ? 1ull << 63 : 1ull << 31) : 0);
    double magnitude = d ? std::fabs(bits_to_double(operand)) : std::fabs((double)bits_to_float((uint32_t)operand));
    if (magnitude < (d ? std::ldexp(1.0, -1024) : std::ldexp(1.0, -128)))
        return (d ? 0x7ff0000000000000ull : 0x7f800000ull) | (sign ? (d ? 1ull << 63 : 1ull << 31) : 0);
    uint64_t fraction;
    int exp;
    if (d) {
        fraction = operand & 0x000fffffffffffffull;
        exp = (int)((operand >> 52) & 0x7ff);
    } else {
        fraction = (operand & 0x7fffffull) << 29;
        exp = (int)((operand >> 23) & 0xff);
    }
    if (exp == 0) {
        if (!((fraction >> 51) & 1)) {
            exp = -1;
            fraction = (fraction << 2) & 0x000fffffffffffffull;
        } else {
            fraction = (fraction << 1) & 0x000fffffffffffffull;
        }
    }
    int scaled = (int)(0x100 | ((fraction >> 44) & 0xff));
    int result_exp = (d ? 2045 : 253) - exp;
    int estimate = recip_estimate(scaled);
    uint64_t result_fraction; /* 52 bits */
    if (result_exp == 0) {
        result_fraction = (1ull << 51) | ((uint64_t)(estimate & 0xff) << 43);
    } else if (result_exp == -1) {
        result_fraction = (1ull << 50) | ((uint64_t)(estimate & 0xff) << 42);
        result_exp = 0;
    } else {
        result_fraction = (uint64_t)(estimate & 0xff) << 44;
    }
    if (d) return ((uint64_t)sign << 63) | ((uint64_t)(result_exp & 0x7ff) << 52) | result_fraction;
    return ((uint64_t)sign << 31) | ((uint64_t)(result_exp & 0xff) << 23) | (result_fraction >> 29);
}

uint64_t fp_rsqrt_estimate(uint64_t operand, bool d) {
    bool sign = fp_sign(operand, d);
    if (fp_nan(operand, d)) return fp_quiet(operand, d);
    if (fp_zero(operand, d)) return (d ? 0x7ff0000000000000ull : 0x7f800000ull) | (sign ? (d ? 1ull << 63 : 1ull << 31) : 0);
    if (sign) return fp_default_nan(d);
    if (fp_inf(operand, d)) return 0;
    uint64_t fraction;
    int exp;
    if (d) {
        fraction = operand & 0x000fffffffffffffull;
        exp = (int)((operand >> 52) & 0x7ff);
    } else {
        fraction = (operand & 0x7fffffull) << 29;
        exp = (int)((operand >> 23) & 0xff);
    }
    if (exp == 0) {
        while (!((fraction >> 51) & 1)) {
            fraction = (fraction << 1) & 0x000fffffffffffffull;
            exp -= 1;
        }
        fraction = (fraction << 1) & 0x000fffffffffffffull;
    }
    int scaled = (exp & 1) == 0 ? (int)(0x80 | ((fraction >> 45) & 0x7f)) : (int)(0x100 | ((fraction >> 44) & 0xff));
    int result_exp = floor_div((d ? 3068 : 380) - exp, 2);
    int estimate = recip_sqrt_estimate(scaled);
    if (d) return ((uint64_t)(result_exp & 0x7ff) << 52) | ((uint64_t)(estimate & 0xff) << 44);
    return ((uint64_t)(result_exp & 0xff) << 23) | ((uint64_t)(estimate & 0xff) << 15);
}

/* Estimates and the rounding conversions, vector and scalar, from the two
   register misc group: FRECPE, FRSQRTE, URECPE, URSQRTE, and FCVTNS, FCVTMS,
   FCVTPS, FCVTAS with their unsigned forms. */
bool step_fp_estimates(GuestCpu& cpu, uint32_t insn, uint64_t next) {
    bool misc_vector = (insn & 0x9f3e0c00u) == 0x0e200800u;
    bool misc_scalar = (insn & 0xdf3e0c00u) == 0x5e200800u;
    if (!misc_vector && !misc_scalar) return false;
    int q = (insn >> 30) & 1;
    int u = (insn >> 29) & 1;
    bool a_bit = (insn >> 23) & 1;
    bool d = (insn >> 22) & 1;
    int opcode = (insn >> 12) & 0x1f;
    int rn = (insn >> 5) & 31;
    int rd = insn & 31;
    int lane_bytes = d ? 8 : 4;
    int lanes = misc_scalar ? 1 : (q ? 16 : 8) / lane_bytes;
    if (misc_vector && d && !q) return false;
    uint8_t out[16] = {};

    if (opcode == 0x1d && a_bit) { /* FRECPE, FRSQRTE */
        for (int i = 0; i < lanes; ++i) {
            uint64_t x = fp_lane(cpu.q[rn], i, d);
            fp_set_lane(out, i, d, u ? fp_rsqrt_estimate(x, d) : fp_recip_estimate(x, d));
        }
    } else if (opcode == 0x1c && a_bit && !d && misc_vector) { /* URECPE, URSQRTE on words */
        for (int i = 0; i < lanes; ++i) {
            uint32_t x = (uint32_t)fp_lane(cpu.q[rn], i, false);
            uint32_t r;
            if (!u) r = !(x >> 31) ? 0xffffffffu : (uint32_t)(recip_estimate((int)(x >> 23) & 0x1ff) & 0x1ff) << 23;
            else r = !(x >> 30) ? 0xffffffffu : (uint32_t)(recip_sqrt_estimate((int)(x >> 23) & 0x1ff) & 0x1ff) << 23;
            fp_set_lane(out, i, false, r);
        }
    } else if (opcode == 0x1a || (opcode == 0x1b && !a_bit) || (opcode == 0x1c && !a_bit)) {
        /* 0 nearest-even (N), 1 up (P), 2 down (M), 3 nearest-away (A) */
        int mode = opcode == 0x1a ? (a_bit ? 1 : 0) : opcode == 0x1b ? 2 : 3;
        for (int i = 0; i < lanes; ++i) {
            uint64_t bits = fp_lane(cpu.q[rn], i, d);
            double x = d ? bits_to_double(bits) : (double)bits_to_float((uint32_t)bits);
            double r = mode == 0 ? std::nearbyint(x) : mode == 1 ? std::ceil(x) : mode == 2 ? std::floor(x) : std::round(x);
            uint64_t result;
            if (std::isnan(r)) result = 0;
            else if (u) {
                double top = d ? 18446744073709551615.0 : 4294967295.0;
                result = r <= 0 ? 0 : r >= top ? (d ? ~0ull : 0xffffffffull) : (uint64_t)r;
            } else {
                double top = d ? 9223372036854775807.0 : 2147483647.0;
                double bottom = d ? -9223372036854775808.0 : -2147483648.0;
                int64_t v = r >= top ? (d ? INT64_MAX : INT32_MAX) : r <= bottom ? (d ? INT64_MIN : INT32_MIN) : (int64_t)r;
                result = (uint64_t)v;
            }
            fp_set_lane(out, i, d, result);
        }
    } else {
        return false;
    }
    std::memcpy(&cpu.q[rd].lo, out, 8);
    std::memcpy(&cpu.q[rd].hi, out + 8, 8);
    cpu.pc = next;
    return true;
}

}  // namespace

bool step_fp(GuestCpu& cpu, GuestMem& mem, uint32_t insn, uint64_t next) {
    if (step_shift_immediate(cpu, insn, next)) return true;
    if (step_neon_widening(cpu, insn, next)) return true;
    if (step_fp_arith(cpu, mem, insn, next)) return true;
    if (step_misc_extra(cpu, insn, next)) return true;
    if (step_fp_estimates(cpu, insn, next)) return true;
    /* Load and store of an FP or vector register: the integer encodings with
       the vector bit set. */
    /* Bits 25 and 24 say which shape this is. Only 00 and 01 are a load or a
       store; 1x is the SIMD space, which shares everything above them, so
       matching without this check swallows half of NEON. */
    if (((insn >> 27) & 7) == 7 && ((insn >> 26) & 1) == 1 && ((insn >> 24) & 3) <= 1) {
        int size = (insn >> 30) & 3;
        int opc = (insn >> 22) & 3;
        int kind = (insn >> 24) & 3;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        int bytes = (size == 0 && (opc & 2)) ? 16 : (1 << size);
        bool load = (opc & 1) != 0;
        uint64_t base = reg_or_sp(cpu, rn);
        uint64_t addr = base;
        bool writeback = false;
        uint64_t written = 0;
        if (kind == 1) {
            addr += (uint64_t)((insn >> 10) & 0xfff) * bytes;
        } else if (((insn >> 21) & 1) == 0) {
            int imm9 = (insn >> 12) & 0x1ff;
            int64_t off = (int64_t)((imm9 << 23) >> 23);
            int form = (insn >> 10) & 3;
            if (form == 0) addr += off;
            else if (form == 1 || form == 3) {
                writeback = true;
                written = base + off;
                if (form == 3) addr = written;
            } else
                return false;
        } else if (((insn >> 10) & 3) == 2) {
            int rm = (insn >> 16) & 31;
            int shift = (insn >> 12) & 1;
            int option = (insn >> 13) & 7;
            uint64_t index = reg(cpu, rm);
            if (option == 2) index = (uint64_t)(uint32_t)index;                 /* UXTW */
            else if (option == 6) index = (uint64_t)(int64_t)(int32_t)index;    /* SXTW */
            addr += index << (shift ? (bytes == 16 ? 4 : size) : 0);
        } else
            return false;
        if (load) {
            GuestVec value{};
            if (!vec_load(mem, addr, &value, bytes)) return false;
            cpu.q[rt] = value;
        } else {
            if (!vec_store(mem, addr, cpu.q[rt], bytes)) return false;
        }
        if (writeback) write_reg_or_sp(cpu, rn, written);
        cpu.pc = next;
        return true;
    }

    /* Load and store pair of FP registers. */
    /* The SIMD immediate group shares bits 29 to 27 with this one. Only modes
       1 to 3 are a real pair instruction, and that is what tells them apart,
       so the test belongs in the condition rather than inside the body. */
    if (((insn >> 27) & 7) == 5 && ((insn >> 26) & 1) == 1 && ((insn >> 23) & 7) <= 3 &&
        ((insn >> 30) & 3) != 3) {
        int opc = (insn >> 30) & 3;
        int mode = (insn >> 23) & 7;
        int load = (insn >> 22) & 1;
        int imm7 = (insn >> 15) & 0x7f;
        int rt2 = (insn >> 10) & 31;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        int bytes = opc == 0 ? 4 : opc == 1 ? 8 : 16;
        int32_t off = ((int32_t)(imm7 << 25)) >> 25;
        off *= bytes;
        uint64_t base = reg_or_sp(cpu, rn);
        uint64_t addr = base;
        if (mode == 0 || mode == 3 || mode == 2) addr = base + off; /* 0 is LDNP/STNP */
        if (load) {
            GuestVec a{};
            GuestVec b{};
            if (!vec_load(mem, addr, &a, bytes) || !vec_load(mem, addr + bytes, &b, bytes)) return false;
            cpu.q[rt] = a;
            cpu.q[rt2] = b;
        } else {
            if (!vec_store(mem, addr, cpu.q[rt], bytes) || !vec_store(mem, addr + bytes, cpu.q[rt2], bytes))
                return false;
        }
        if (mode == 1 || mode == 3) write_reg_or_sp(cpu, rn, base + off);
        cpu.pc = next;
        return true;
    }

    /* Scalar floating point, and the conversions between it and the general
       registers. Bits 30 and 29 are zero for all of these. */
    if (((insn >> 24) & 0x1f) == 0x1e && ((insn >> 29) & 3) == 0) {
        int ftype = (insn >> 22) & 3;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        /* Type 3 is half precision, which is not handled; type 2 exists only
           as FMOV to and from the upper half of a vector register. */
        bool upper_fmov = ftype == 2 && ((insn >> 10) & 0x3f) == 0 && ((insn >> 19) & 3) == 1 &&
                          ((insn >> 17) & 3) == 3;
        if (ftype == 3 || (ftype == 2 && !upper_fmov)) return false;
        if (((insn >> 21) & 1) == 0) return false;

        /* Between a vector register and a general one, and the conversions. */
        if (((insn >> 10) & 0x3f) == 0) {
            int sf = (insn >> 31) & 1;
            int rmode = (insn >> 19) & 3;
            int opcode = (insn >> 16) & 7;
            if (rmode == 0 && opcode == 2) { /* SCVTF */
                int64_t value = sf ? (int64_t)reg(cpu, rn) : (int64_t)(int32_t)reg(cpu, rn);
                write_f(cpu, rd, ftype, (double)value);
                cpu.pc = next;
                return true;
            }
            if (rmode == 0 && opcode == 3) { /* UCVTF */
                uint64_t value = sf ? reg(cpu, rn) : (uint64_t)(uint32_t)reg(cpu, rn);
                write_f(cpu, rd, ftype, (double)value);
                cpu.pc = next;
                return true;
            }
            if (rmode == 0 && opcode == 6) { /* FMOV to a general register */
                write_reg(cpu, rd, ftype ? cpu.q[rn].lo : (uint64_t)(uint32_t)cpu.q[rn].lo);
                cpu.pc = next;
                return true;
            }
            if (rmode == 0 && opcode == 7) { /* FMOV from a general register */
                cpu.q[rd].lo = ftype ? reg(cpu, rn) : (uint64_t)(uint32_t)reg(cpu, rn);
                cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
            /* The upper half of a 128-bit register, as FMOV Xd, Vn.D[1]. */
            if (rmode == 1 && opcode == 6 && sf && ftype == 2) {
                write_reg(cpu, rd, cpu.q[rn].hi);
                cpu.pc = next;
                return true;
            }
            if (rmode == 1 && opcode == 7 && sf && ftype == 2) {
                cpu.q[rd].hi = reg(cpu, rn);
                cpu.pc = next;
                return true;
            }
            /* Float to integer, in every rounding: FCVTN (nearest, ties to
               even), FCVTP (up), FCVTM (down), FCVTZ (toward zero) and FCVTA
               (nearest, ties away). Out of range saturates and NaN gives zero,
               which is the architecture's answer and not C's. */
            bool to_int = (opcode == 0 || opcode == 1) || (rmode == 0 && (opcode == 4 || opcode == 5));
            if (to_int) {
                bool is_unsigned = (opcode & 1) != 0;
                double value = read_f(cpu, rn, ftype);
                double rounded;
                if (opcode >= 4) rounded = std::round(value);
                else if (rmode == 0) rounded = std::nearbyint(value);
                else if (rmode == 1) rounded = std::ceil(value);
                else if (rmode == 2) rounded = std::floor(value);
                else rounded = std::trunc(value);
                uint64_t result;
                if (std::isnan(rounded)) {
                    result = 0;
                } else if (is_unsigned) {
                    double top = sf ? 18446744073709551615.0 : 4294967295.0;
                    result = rounded <= 0 ? 0 : rounded >= top ? (sf ? UINT64_MAX : 0xffffffffull) : (uint64_t)rounded;
                } else {
                    double top = sf ? 9223372036854775807.0 : 2147483647.0;
                    double bottom = sf ? -9223372036854775808.0 : -2147483648.0;
                    int64_t v = rounded >= top ? (sf ? INT64_MAX : INT32_MAX)
                                : rounded <= bottom ? (sf ? INT64_MIN : INT32_MIN) : (int64_t)rounded;
                    result = (uint64_t)v;
                }
                write_reg(cpu, rd, sf ? result : (uint64_t)(uint32_t)result);
                cpu.pc = next;
                return true;
            }
            return false;
        }

        /* FMOV immediate. */
        if (((insn >> 10) & 7) == 4) {
            double value = expand_fp_immediate((insn >> 13) & 0xff);
            write_f(cpu, rd, ftype, value);
            cpu.pc = next;
            return true;
        }

        /* Compare, which is the only one of these that writes the flags. */
        if (((insn >> 10) & 0xf) == 8 && ((insn >> 14) & 3) == 0) {
            int rm = (insn >> 16) & 31;
            bool with_zero = ((insn >> 3) & 1) != 0;
            double a = read_f(cpu, rn, ftype);
            double b = with_zero ? 0.0 : read_f(cpu, rm, ftype);
            if (std::isnan(a) || std::isnan(b)) {
                cpu.n = false;
                cpu.z = false;
                cpu.c = true;
                cpu.v = true;
            } else if (a == b) {
                cpu.n = false;
                cpu.z = true;
                cpu.c = true;
                cpu.v = false;
            } else if (a < b) {
                cpu.n = true;
                cpu.z = false;
                cpu.c = false;
                cpu.v = false;
            } else {
                cpu.n = false;
                cpu.z = false;
                cpu.c = true;
                cpu.v = false;
            }
            cpu.pc = next;
            return true;
        }

        /* One source. */
        if (((insn >> 10) & 0x1f) == 0x10) {
            int opcode = (insn >> 15) & 0x3f;
            /* FMOV, FABS and FNEG copy bits (the sign aside), NaNs and all:
               going through a double would quiet a signalling NaN. */
            if (opcode <= 2 && ftype <= 1) {
                uint64_t bits = cpu.q[rn].lo & (ftype ? ~0ull : 0xffffffffull);
                uint64_t sign = ftype ? 0x8000000000000000ull : 0x80000000ull;
                if (opcode == 1) bits &= ~sign;
                if (opcode == 2) bits ^= sign;
                cpu.q[rd].lo = bits;
                cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
            double a = read_f(cpu, rn, ftype);
            switch (opcode) {
                case 0: write_f(cpu, rd, ftype, a); break;                 /* FMOV */
                case 1: write_f(cpu, rd, ftype, std::fabs(a)); break;      /* FABS */
                case 2: write_f(cpu, rd, ftype, -a); break;                /* FNEG */
                case 3: write_f(cpu, rd, ftype, std::sqrt(a)); break;      /* FSQRT */
                case 4: write_f(cpu, rd, 0, a); break;                     /* FCVT to single */
                case 5: write_f(cpu, rd, 1, a); break;                     /* FCVT to double */
                case 8: write_f(cpu, rd, ftype, std::nearbyint(a)); break; /* FRINTN */
                case 9: write_f(cpu, rd, ftype, std::ceil(a)); break;      /* FRINTP */
                case 10: write_f(cpu, rd, ftype, std::floor(a)); break;    /* FRINTM */
                case 11: write_f(cpu, rd, ftype, std::trunc(a)); break;    /* FRINTZ */
                case 12: write_f(cpu, rd, ftype, std::round(a)); break;    /* FRINTA */
                case 14:
                case 15: write_f(cpu, rd, ftype, std::nearbyint(a)); break; /* FRINTX / FRINTI */
                default: return false;
            }
            cpu.pc = next;
            return true;
        }

        /* Conditional select. */
        if (((insn >> 10) & 3) == 3) {
            int rm = (insn >> 16) & 31;
            int cc = (insn >> 12) & 0xf;
            /* A copy of one register's bits: through a double would quiet a
               signalling NaN. */
            if (ftype <= 1) {
                int from = condition(cpu, cc) ? rn : rm;
                cpu.q[rd].lo = cpu.q[from].lo & (ftype ? ~0ull : 0xffffffffull);
                cpu.q[rd].hi = 0;
            } else {
                write_f(cpu, rd, ftype, condition(cpu, cc) ? read_f(cpu, rn, ftype) : read_f(cpu, rm, ftype));
            }
            cpu.pc = next;
            return true;
        }

        /* Two sources. */
        if (((insn >> 10) & 3) == 2) {
            int rm = (insn >> 16) & 31;
            int opcode = (insn >> 12) & 0xf;
            double a = read_f(cpu, rn, ftype);
            double b = read_f(cpu, rm, ftype);
            double r = 0;
            switch (opcode) {
                case 0: r = a * b; break;
                case 1: r = a / b; break;
                case 2: r = a + b; break;
                case 3: r = a - b; break;
                case 4: r = (std::isnan(a) || std::isnan(b)) ? NAN : (a > b ? a : b); break;
                case 5: r = (std::isnan(a) || std::isnan(b)) ? NAN : (a < b ? a : b); break;
                case 6: r = std::isnan(a) ? b : std::isnan(b) ? a : (a > b ? a : b); break;
                case 7: r = std::isnan(a) ? b : std::isnan(b) ? a : (a < b ? a : b); break;
                case 8: r = -(a * b); break;
                default: return false;
            }
            write_f(cpu, rd, ftype, r);
            cpu.pc = next;
            return true;
        }
        return false;
    }

    /* Multiply and add, in one instruction. */
    if (((insn >> 24) & 0x1f) == 0x1f && ((insn >> 29) & 3) == 0) {
        int ftype = (insn >> 22) & 3;
        if (ftype == 2 || ftype == 3) return false;
        int o1 = (insn >> 21) & 1;
        int o0 = (insn >> 15) & 1;
        int rm = (insn >> 16) & 31;
        int ra = (insn >> 10) & 31;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        double a = read_f(cpu, rn, ftype);
        double b = read_f(cpu, rm, ftype);
        double c = read_f(cpu, ra, ftype);
        double r = 0;
        if (!o1 && !o0) r = c + a * b;
        else if (!o1 && o0) r = c - a * b;
        else if (o1 && !o0) r = -c - a * b;
        else r = -c + a * b;
        write_f(cpu, rd, ftype, r);
        cpu.pc = next;
        return true;
    }

    /* Scalar multiply-add against one lane of another register. */
    if (((insn >> 30) & 1) == 1 && ((insn >> 24) & 0x1f) == 0x1f && ((insn >> 10) & 1) == 0) {
        int size = (insn >> 22) & 3;
        int opcode = (insn >> 12) & 0xf;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        bool doubles = size == 3;
        int high = (insn >> 11) & 1;
        int low = (insn >> 21) & 1;
        int m = (insn >> 20) & 1;
        int rm = doubles ? (int)(((insn >> 20) & 1) << 4 | ((insn >> 16) & 0xf)) : (int)((insn >> 16) & 0xf);
        int index = doubles ? high : ((high << 2) | (low << 1) | m);
        if (opcode != 1 && opcode != 5 && opcode != 9) return false;
        uint8_t src[16];
        std::memcpy(src, &cpu.q[rm].lo, 8);
        std::memcpy(src + 8, &cpu.q[rm].hi, 8);
        int lane = doubles ? 8 : 4;
        double b = 0;
        if (index * lane + lane <= 16) {
            if (doubles) {
                uint64_t bits = 0;
                std::memcpy(&bits, src + index * lane, 8);
                b = bits_to_double(bits);
            } else {
                uint32_t bits = 0;
                std::memcpy(&bits, src + index * lane, 4);
                b = bits_to_float(bits);
            }
        }
        int ftype = doubles ? 1 : 0;
        double a = read_f(cpu, rn, ftype);
        double acc = read_f(cpu, rd, ftype);
        double r = opcode == 9 ? a * b : opcode == 1 ? acc + a * b : acc - a * b;
        write_f(cpu, rd, ftype, r);
        cpu.pc = next;
        return true;
    }

    /* A whole vector multiplied by one lane of another, which is the shape
       matrix and transform code is built from. */
    if (((insn >> 24) & 0x1f) == 0x0f && ((insn >> 31) & 1) == 0 && ((insn >> 10) & 1) == 0 &&
        ((insn >> 19) & 0x1f) != 0) {
        int q = (insn >> 30) & 1;
        int u = (insn >> 29) & 1;
        int size = (insn >> 22) & 3;
        int opcode = (insn >> 12) & 0xf;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        if (!u && (opcode == 1 || opcode == 5 || opcode == 9) && (size == 2 || size == 3)) {
            bool doubles = size == 3;
            int high = (insn >> 11) & 1;
            int low = (insn >> 21) & 1;
            int m = (insn >> 20) & 1;
            int rm = doubles ? (int)((m << 4) | ((insn >> 16) & 0xf)) : (int)((m << 4) | ((insn >> 16) & 0xf));
            int index = doubles ? high : ((high << 1) | low);
            int lane = doubles ? 8 : 4;
            int lanes_bytes = q ? 16 : 8;
            uint8_t a[16], b[16], d[16], out[16] = {};
            std::memcpy(a, &cpu.q[rn].lo, 8);
            std::memcpy(a + 8, &cpu.q[rn].hi, 8);
            std::memcpy(b, &cpu.q[rm].lo, 8);
            std::memcpy(b + 8, &cpu.q[rm].hi, 8);
            std::memcpy(d, &cpu.q[rd].lo, 8);
            std::memcpy(d + 8, &cpu.q[rd].hi, 8);
            double chosen = 0;
            if (index * lane + lane <= 16) {
                if (doubles) {
                    uint64_t bits = 0;
                    std::memcpy(&bits, b + index * lane, 8);
                    chosen = bits_to_double(bits);
                } else {
                    uint32_t bits = 0;
                    std::memcpy(&bits, b + index * lane, 4);
                    chosen = bits_to_float(bits);
                }
            }
            for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                double x = 0, acc = 0;
                if (doubles) {
                    uint64_t xb = 0, ab = 0;
                    std::memcpy(&xb, a + at, 8);
                    std::memcpy(&ab, d + at, 8);
                    x = bits_to_double(xb);
                    acc = bits_to_double(ab);
                } else {
                    uint32_t xb = 0, ab = 0;
                    std::memcpy(&xb, a + at, 4);
                    std::memcpy(&ab, d + at, 4);
                    x = bits_to_float(xb);
                    acc = bits_to_float(ab);
                }
                double r = opcode == 9 ? x * chosen : opcode == 1 ? acc + x * chosen : acc - x * chosen;
                if (doubles) {
                    uint64_t rb = double_to_bits(r);
                    std::memcpy(out + at, &rb, 8);
                } else {
                    uint32_t rb = float_to_bits((float)r);
                    std::memcpy(out + at, &rb, 4);
                }
            }
            std::memcpy(&cpu.q[rd].lo, out, 8);
            std::memcpy(&cpu.q[rd].hi, out + 8, 8);
            if (!q) cpu.q[rd].hi = 0;
            cpu.pc = next;
            return true;
        }
    }

    /* Pairwise across the two halves of a register, folding it to a scalar.
       A vectorised sum ends with one of these. */
    if (((insn >> 30) & 1) == 1 && ((insn >> 24) & 0x1f) == 0x1e && ((insn >> 17) & 0x1f) == 0x18 &&
        ((insn >> 10) & 3) == 2) {
        int u = (insn >> 29) & 1;
        int size = (insn >> 22) & 3;
        int opcode = (insn >> 12) & 0x1f;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        if (!u && opcode == 0x1b) { /* ADDP, two 64-bit lanes added together */
            uint64_t result = cpu.q[rn].lo + cpu.q[rn].hi;
            cpu.q[rd].lo = result;
            cpu.q[rd].hi = 0;
            cpu.pc = next;
            return true;
        }
        if (u && (opcode == 0x0d || opcode == 0x0f || opcode == 0x0c)) {
            /* FADDP, FMAXP and FMINP over the two lanes. */
            bool doubles = (size & 1) != 0;
            double a = 0, b = 0;
            if (doubles) {
                a = bits_to_double(cpu.q[rn].lo);
                b = bits_to_double(cpu.q[rn].hi);
            } else {
                a = bits_to_float((uint32_t)cpu.q[rn].lo);
                b = bits_to_float((uint32_t)(cpu.q[rn].lo >> 32));
            }
            double r = opcode == 0x0d ? a + b : opcode == 0x0f ? (a > b ? a : b) : (a < b ? a : b);
            write_f(cpu, rd, doubles ? 1 : 0, r);
            cpu.pc = next;
            return true;
        }
    }

    /* Pulling one lane out into a scalar register, which is how float code
       gets at a single component. */
    if (((insn >> 30) & 1) == 1 && ((insn >> 21) & 0xff) == 0xf0 && ((insn >> 10) & 1) == 1 &&
        ((insn >> 11) & 0xf) == 0) {
        int imm5 = (insn >> 16) & 0x1f;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        int size = 0;
        while (size < 4 && !((imm5 >> size) & 1)) ++size;
        int lane_bytes = 1 << size;
        int index = imm5 >> (size + 1);
        uint8_t src[16];
        std::memcpy(src, &cpu.q[rn].lo, 8);
        std::memcpy(src + 8, &cpu.q[rn].hi, 8);
        uint64_t value = 0;
        if (index * lane_bytes + lane_bytes <= 16) std::memcpy(&value, src + index * lane_bytes, lane_bytes);
        cpu.q[rd].lo = value;
        cpu.q[rd].hi = 0;
        cpu.pc = next;
        return true;
    }

    /* One lane of one register, to or from memory, and LD1R which loads one
       element and fills every lane with it. */
    if (((insn >> 24) & 0x3f) == 0x0d && ((insn >> 31) & 1) == 0 && ((insn >> 21) & 1) == 0 &&
        ((insn >> 13) & 1) == 0) {
        int q = (insn >> 30) & 1;
        bool post = ((insn >> 23) & 1) != 0;
        bool load = ((insn >> 22) & 1) != 0;
        int opcode = (insn >> 13) & 7;
        int s_bit = (insn >> 12) & 1;
        int size = (insn >> 10) & 3;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        uint64_t addr = reg_or_sp(cpu, rn);
        int element = 0;
        int index = 0;
        bool replicate = false;
        if (opcode == 0) {
            element = 1;
            index = (q << 3) | (s_bit << 2) | size;
        } else if (opcode == 2) {
            element = 2;
            index = (q << 2) | (s_bit << 1) | (size >> 1);
        } else if (opcode == 4 && size == 0) {
            element = 4;
            index = (q << 1) | s_bit;
        } else if (opcode == 4 && size == 1) {
            element = 8;
            index = q;
        } else if (opcode == 6 && load) {
            element = 1 << size;
            replicate = true;
        } else
            return false;
        uint8_t* p = guest_ptr(mem, addr, element);
        if (!p) return false;
        uint8_t lanes[16];
        std::memcpy(lanes, &cpu.q[rt].lo, 8);
        std::memcpy(lanes + 8, &cpu.q[rt].hi, 8);
        if (replicate) {
            int width = q ? 16 : 8;
            for (int at = 0; at < width; at += element) std::memcpy(lanes + at, p, element);
            if (!q) std::memset(lanes + 8, 0, 8);
        } else if (load) {
            std::memcpy(lanes + index * element, p, element);
        } else {
            std::memcpy(p, lanes + index * element, element);
        }
        if (load) {
            std::memcpy(&cpu.q[rt].lo, lanes, 8);
            std::memcpy(&cpu.q[rt].hi, lanes + 8, 8);
        }
        if (post) {
            int rm = (insn >> 16) & 31;
            write_reg_or_sp(cpu, rn, addr + (rm == 31 ? (uint64_t)element : reg(cpu, rm)));
        }
        cpu.pc = next;
        return true;
    }

    /* Load and store of several registers at once, interleaved by lane. This
       is what a vectorised loop writing four bytes per pixel turns into. */
    if (((insn >> 24) & 0x3f) == 0x0c && ((insn >> 31) & 1) == 0) {
        int q = (insn >> 30) & 1;
        bool post = ((insn >> 23) & 1) != 0;
        bool load = ((insn >> 22) & 1) != 0;
        int opcode = (insn >> 12) & 0xf;
        int size = (insn >> 10) & 3;
        int rn = (insn >> 5) & 31;
        int rt = insn & 31;
        int count = 0;
        bool interleaved = true;
        switch (opcode) {
            case 0x0: count = 4; break;
            case 0x2: count = 4; interleaved = false; break;
            case 0x4: count = 3; break;
            case 0x6: count = 3; interleaved = false; break;
            case 0x7: count = 1; interleaved = false; break;
            case 0x8: count = 2; break;
            case 0xa: count = 2; interleaved = false; break;
            default: return false;
        }
        int lane = 1 << size;
        int lanes_bytes = q ? 16 : 8;
        int lanes = lanes_bytes / lane;
        uint64_t addr = reg_or_sp(cpu, rn);
        uint64_t total = (uint64_t)count * lanes_bytes;
        uint8_t regs[4][16] = {};
        if (load) {
            uint8_t* p = guest_ptr(mem, addr, total);
            if (!p) return false;
            if (interleaved) {
                for (int e = 0; e < lanes; ++e)
                    for (int r = 0; r < count; ++r) std::memcpy(regs[r] + e * lane, p + (e * count + r) * lane, lane);
            } else {
                for (int r = 0; r < count; ++r) std::memcpy(regs[r], p + r * lanes_bytes, lanes_bytes);
            }
            for (int r = 0; r < count; ++r) {
                int which = (rt + r) & 31;
                std::memcpy(&cpu.q[which].lo, regs[r], 8);
                std::memcpy(&cpu.q[which].hi, regs[r] + 8, 8);
                if (!q) cpu.q[which].hi = 0;
            }
        } else {
            for (int r = 0; r < count; ++r) {
                int which = (rt + r) & 31;
                std::memcpy(regs[r], &cpu.q[which].lo, 8);
                std::memcpy(regs[r] + 8, &cpu.q[which].hi, 8);
            }
            uint8_t* p = guest_ptr(mem, addr, total);
            if (!p) return false;
            if (interleaved) {
                for (int e = 0; e < lanes; ++e)
                    for (int r = 0; r < count; ++r) std::memcpy(p + (e * count + r) * lane, regs[r] + e * lane, lane);
            } else {
                for (int r = 0; r < count; ++r) std::memcpy(p + r * lanes_bytes, regs[r], lanes_bytes);
            }
        }
        if (post) {
            int rm = (insn >> 16) & 31;
            write_reg_or_sp(cpu, rn, addr + (rm == 31 ? total : reg(cpu, rm)));
        }
        cpu.pc = next;
        return true;
    }

    /* A small slice of NEON: enough for the shapes clang emits around ordinary
       float code. Anything else reports itself rather than doing the wrong
       thing quietly. */
    /* The same operations exist on a single element, encoded with 11110 in
       place of 01110. Treating that as a vector of one lane covers the whole
       scalar space with the code that is already here. */
    if (((insn >> 24) & 0x1f) == 0x0e || ((insn >> 24) & 0x1f) == 0x0f ||
        (((insn >> 24) & 0x1f) == 0x1e && ((insn >> 30) & 1) == 1)) {
        bool scalar = ((insn >> 24) & 0x1f) == 0x1e;
        int q = scalar ? 0 : (insn >> 30) & 1;
        int u = (insn >> 29) & 1;
        int rn = (insn >> 5) & 31;
        int rd = insn & 31;
        int lanes_bytes = q ? 16 : 8;
        /* One lane, of whatever width the size field names. */
        if (scalar) lanes_bytes = 1 << ((insn >> 22) & 3);

        /* EXT: a window sliding across two registers joined end to end. */
        if (((insn >> 29) & 1) == 1 && ((insn >> 24) & 0x1f) == 0x0e && ((insn >> 21) & 1) == 0 &&
            ((insn >> 15) & 1) == 0 && ((insn >> 10) & 1) == 0) {
            int rm = (insn >> 16) & 31;
            int index = (insn >> 11) & 0xf;
            /* The 64-bit form joins the low halves only: Vn's then Vm's. */
            uint8_t joined[32];
            std::memcpy(joined, &cpu.q[rn].lo, 8);
            if (q) {
                std::memcpy(joined + 8, &cpu.q[rn].hi, 8);
                std::memcpy(joined + 16, &cpu.q[rm].lo, 8);
                std::memcpy(joined + 24, &cpu.q[rm].hi, 8);
            } else {
                std::memcpy(joined + 8, &cpu.q[rm].lo, 8);
                std::memset(joined + 16, 0, 16);
            }
            uint8_t out[16] = {};
            for (int i = 0; i < lanes_bytes; ++i) out[i] = joined[index + i];
            std::memcpy(&cpu.q[rd].lo, out, 8);
            std::memcpy(&cpu.q[rd].hi, out + 8, 8);
            if (!q) cpu.q[rd].hi = 0;
            cpu.pc = next;
            return true;
        }

        /* MOVI / MVNI. */
        if (((insn >> 24) & 0x1f) == 0x0f && ((insn >> 19) & 0x1f) == 0 && ((insn >> 10) & 1) == 1) {
            int cmode = (insn >> 12) & 0xf;
            uint32_t imm8 = (uint32_t)(((insn >> 16) & 7) << 5 | ((insn >> 5) & 0x1f));
            bool invert = false;
            uint64_t value = expand_simd_immediate(imm8, cmode, u, &invert);
            if ((cmode & 1) && cmode < 12) {
                /* ORR and BIC (vector, immediate): combine with the register
                   rather than replace it. op picks which. */
                if (u) {
                    cpu.q[rd].lo &= ~value;
                    cpu.q[rd].hi = q ? cpu.q[rd].hi & ~value : 0;
                } else {
                    cpu.q[rd].lo |= value;
                    cpu.q[rd].hi = q ? cpu.q[rd].hi | value : 0;
                }
                cpu.pc = next;
                return true;
            }
            if (invert) value = ~value;
            cpu.q[rd].lo = value;
            cpu.q[rd].hi = q ? value : 0;
            cpu.pc = next;
            return true;
        }

        /* DUP, INS, SMOV and UMOV all sit in the copy group. */
        if (((insn >> 21) & 0xff) == 0x70 && ((insn >> 10) & 1) == 1) {
            int imm5 = (insn >> 16) & 0x1f;
            int imm4 = (insn >> 11) & 0xf;
            int opcode = imm4;
            int size = 0;
            while (size < 4 && !((imm5 >> size) & 1)) ++size;
            int lane_bytes = 1 << size;

            /* Moving one lane to another, which is how a float is placed into
               a vector without going through memory. */
            if (u) {
                int to = imm5 >> (size + 1);
                int from = imm4 >> size;
                uint8_t dst[16];
                uint8_t src[16];
                std::memcpy(dst, &cpu.q[rd].lo, 8);
                std::memcpy(dst + 8, &cpu.q[rd].hi, 8);
                std::memcpy(src, &cpu.q[rn].lo, 8);
                std::memcpy(src + 8, &cpu.q[rn].hi, 8);
                if (to * lane_bytes + lane_bytes <= 16 && from * lane_bytes + lane_bytes <= 16)
                    std::memcpy(dst + to * lane_bytes, src + from * lane_bytes, lane_bytes);
                std::memcpy(&cpu.q[rd].lo, dst, 8);
                std::memcpy(&cpu.q[rd].hi, dst + 8, 8);
                cpu.pc = next;
                return true;
            }
            if (opcode == 0) { /* DUP from a lane */
                int from = imm5 >> (size + 1);
                uint8_t src[16];
                std::memcpy(src, &cpu.q[rn].lo, 8);
                std::memcpy(src + 8, &cpu.q[rn].hi, 8);
                uint64_t value = 0;
                if (from * lane_bytes + lane_bytes <= 16) std::memcpy(&value, src + from * lane_bytes, lane_bytes);
                uint64_t packed = 0;
                for (int i = 0; i < 8 / lane_bytes; ++i) packed |= value << (i * lane_bytes * 8);
                cpu.q[rd].lo = packed;
                cpu.q[rd].hi = q ? packed : 0;
                cpu.pc = next;
                return true;
            }
            if (opcode == 1) { /* DUP from a general register */
                uint64_t value = reg(cpu, rn) & (lane_bytes == 8 ? ~0ull : ((1ull << (lane_bytes * 8)) - 1));
                uint64_t packed = 0;
                for (int i = 0; i < 8 / lane_bytes; ++i) packed |= value << (i * lane_bytes * 8);
                cpu.q[rd].lo = packed;
                cpu.q[rd].hi = q ? packed : 0;
                cpu.pc = next;
                return true;
            }
            if (opcode == 3 && q) { /* INS from a general register */
                int index = imm5 >> (size + 1);
                uint8_t bytes[16];
                std::memcpy(bytes, &cpu.q[rd].lo, 8);
                std::memcpy(bytes + 8, &cpu.q[rd].hi, 8);
                uint64_t value = reg(cpu, rn);
                if (index * lane_bytes + lane_bytes <= 16) std::memcpy(bytes + index * lane_bytes, &value, lane_bytes);
                std::memcpy(&cpu.q[rd].lo, bytes, 8);
                std::memcpy(&cpu.q[rd].hi, bytes + 8, 8);
                cpu.pc = next;
                return true;
            }
            if (opcode == 7) { /* UMOV to a general register */
                int index = imm5 >> (size + 1);
                uint8_t bytes[16];
                std::memcpy(bytes, &cpu.q[rn].lo, 8);
                std::memcpy(bytes + 8, &cpu.q[rn].hi, 8);
                uint64_t value = 0;
                if (index * lane_bytes + lane_bytes <= 16) std::memcpy(&value, bytes + index * lane_bytes, lane_bytes);
                write_reg(cpu, rd, value);
                cpu.pc = next;
                return true;
            }
        }

        /* Reductions across every lane, which is what a vectorised loop that
           folds a whole array down to one answer ends with. */
        if (((insn >> 17) & 0x1f) == 0x18 && ((insn >> 10) & 3) == 2) {
            int size = (insn >> 22) & 3;
            int opcode = (insn >> 12) & 0x1f;
            int lane = 1 << size;
            int count = lanes_bytes / lane;
            uint8_t a[16];
            std::memcpy(a, &cpu.q[rn].lo, 8);
            std::memcpy(a + 8, &cpu.q[rn].hi, 8);
            bool handled = true;
            int64_t best = 0;
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                uint64_t raw = 0;
                std::memcpy(&raw, a + i * lane, lane);
                int64_t value = u ? (int64_t)raw : (int64_t)(raw << (64 - lane * 8)) >> (64 - lane * 8);
                if (i == 0) best = value;
                if (opcode == 0x03) { /* SADDLV and UADDLV: the sum, twice as wide */
                    sum += u ? raw : (uint64_t)value;
                    continue;
                }
                if (opcode == 0x0a) best = value > best ? value : best;
                else if (opcode == 0x1a) best = value < best ? value : best;
                else if (opcode == 0x1b) sum += raw;
                else handled = false;
            }
            if (handled) {
                uint64_t result = (opcode == 0x1b || opcode == 0x03) ? sum : (uint64_t)best;
                int out_lane = opcode == 0x03 ? lane * 2 : lane;
                uint64_t mask = out_lane >= 8 ? ~0ull : ((1ull << (out_lane * 8)) - 1);
                cpu.q[rd].lo = result & mask;
                cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
        }

        /* One register against zero, and the sign flips. */
        if (((insn >> 17) & 0x1f) == 0x10 && ((insn >> 10) & 3) == 2 && ((insn >> 21) & 1) == 1) {
            int size = (insn >> 22) & 3;
            int opcode = (insn >> 12) & 0x1f;
            /* The float forms are picked out by their opcode. Bit 23 cannot
               say so, because for the integer forms it is part of size. */
            bool floating = opcode >= 0x0c && opcode <= 0x0f;
            uint8_t a[16];
            uint8_t out[16] = {};
            std::memcpy(a, &cpu.q[rn].lo, 8);
            std::memcpy(a + 8, &cpu.q[rn].hi, 8);
            bool handled = true;
            if (opcode == 0x0f || (u && opcode == 0x1f)) { /* FABS, FNEG, FSQRT */
                bool doubles = (size & 1) != 0;
                int lane = doubles ? 8 : 4;
                if (scalar) lanes_bytes = lane;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    double x = 0;
                    if (doubles) {
                        uint64_t xb = 0;
                        std::memcpy(&xb, a + at, 8);
                        x = bits_to_double(xb);
                    } else {
                        uint32_t xb = 0;
                        std::memcpy(&xb, a + at, 4);
                        x = bits_to_float(xb);
                    }
                    /* FABS and FNEG only touch the sign bit, NaNs and all: going
                       through a double would quiet a signalling NaN. */
                    if (opcode == 0x0f) {
                        if (doubles) {
                            uint64_t xb = 0;
                            std::memcpy(&xb, a + at, 8);
                            xb = u ? (xb ^ 0x8000000000000000ull) : (xb & 0x7fffffffffffffffull);
                            std::memcpy(out + at, &xb, 8);
                        } else {
                            uint32_t xb = 0;
                            std::memcpy(&xb, a + at, 4);
                            xb = u ? (xb ^ 0x80000000u) : (xb & 0x7fffffffu);
                            std::memcpy(out + at, &xb, 4);
                        }
                        continue;
                    }
                    double r = opcode == 0x1f ? std::sqrt(x) : (u ? -x : std::fabs(x));
                    if (doubles) {
                        uint64_t rb = double_to_bits(r);
                        std::memcpy(out + at, &rb, 8);
                    } else {
                        uint32_t rb = float_to_bits((float)r);
                        std::memcpy(out + at, &rb, 4);
                    }
                }
            } else if (floating && (opcode == 0x0c || opcode == 0x0d || opcode == 0x0e)) {
                bool doubles = (size & 1) != 0;
                int lane = doubles ? 8 : 4;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    double x = 0;
                    if (doubles) {
                        uint64_t xb = 0;
                        std::memcpy(&xb, a + at, 8);
                        x = bits_to_double(xb);
                    } else {
                        uint32_t xb = 0;
                        std::memcpy(&xb, a + at, 4);
                        x = bits_to_float(xb);
                    }
                    bool set = false;
                    if (opcode == 0x0c) set = u ? x >= 0 : x > 0;
                    else if (opcode == 0x0d) set = u ? x <= 0 : x == 0;
                    else set = x < 0;
                    uint64_t r = set ? ~0ull : 0;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (!floating && opcode >= 8 && opcode <= 0x0b) {
                int lane = 1 << size;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t raw = 0;
                    std::memcpy(&raw, a + at, lane);
                    int64_t x = (int64_t)(raw << (64 - lane * 8)) >> (64 - lane * 8);
                    uint64_t r = 0;
                    if (opcode == 8) r = (u ? x >= 0 : x > 0) ? ~0ull : 0;
                    else if (opcode == 9) r = (u ? x <= 0 : x == 0) ? ~0ull : 0;
                    else if (opcode == 0x0a) r = (x < 0) ? ~0ull : 0;
                    else r = u ? (uint64_t)(-x) : (uint64_t)(x < 0 ? -x : x);
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x1d || opcode == 0x1b) {
                /* SCVTF and UCVTF turn each lane into a float; FCVTZS and
                   FCVTZU turn it back. */
                bool doubles = (size & 1) != 0;
                int lane = doubles ? 8 : 4;
                if (scalar) lanes_bytes = lane;
                bool to_float = opcode == 0x1d;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t raw = 0;
                    std::memcpy(&raw, a + at, lane);
                    if (to_float) {
                        double value = u ? (double)raw
                                         : (double)(int64_t)(lane == 8 ? (int64_t)raw : (int32_t)(uint32_t)raw);
                        if (doubles) {
                            uint64_t bits = double_to_bits(value);
                            std::memcpy(out + at, &bits, 8);
                        } else {
                            uint32_t bits = float_to_bits((float)value);
                            std::memcpy(out + at, &bits, 4);
                        }
                    } else {
                        double value = doubles ? bits_to_double(raw) : bits_to_float((uint32_t)raw);
                        double whole = std::trunc(value);
                        /* Saturating, as the architecture does; NaN gives zero. */
                        uint64_t result;
                        if (std::isnan(whole)) result = 0;
                        else if (u) {
                            double top = lane == 8 ? 18446744073709551615.0 : 4294967295.0;
                            result = whole <= 0 ? 0 : whole >= top ? (lane == 8 ? ~0ull : 0xffffffffull) : (uint64_t)whole;
                        } else {
                            double top = lane == 8 ? 9223372036854775807.0 : 2147483647.0;
                            double bottom = lane == 8 ? -9223372036854775808.0 : -2147483648.0;
                            int64_t v = whole >= top ? (lane == 8 ? INT64_MAX : INT32_MAX)
                                        : whole <= bottom ? (lane == 8 ? INT64_MIN : INT32_MIN) : (int64_t)whole;
                            result = (uint64_t)v;
                        }
                        std::memcpy(out + at, &result, lane);
                    }
                }
            } else if (!floating && opcode <= 1) {
                /* REV64, REV32 and REV16: reverse the elements of this size
                   inside each container of the named width. */
                int lane = 1 << size;
                int container = opcode == 1 ? 2 : (u ? 4 : 8);
                if (lane >= container) {
                    handled = false;
                } else {
                    for (int base = 0; base + container <= lanes_bytes; base += container)
                        for (int at = 0; at < container; at += lane)
                            std::memcpy(out + base + (container - lane - at), a + base + at, lane);
                }
            } else if (!floating && opcode == 5 && !u && size == 0) { /* CNT, bits set per byte */
                for (int i = 0; i < lanes_bytes; ++i) {
                    int bits = 0;
                    for (int b = 0; b < 8; ++b) bits += (a[i] >> b) & 1;
                    out[i] = (uint8_t)bits;
                }
            } else if (!floating && opcode == 5 && u && size == 1) { /* RBIT per byte */
                for (int i = 0; i < lanes_bytes; ++i) {
                    uint8_t r = 0;
                    for (int b = 0; b < 8; ++b)
                        if ((a[i] >> b) & 1) r |= (uint8_t)(1 << (7 - b));
                    out[i] = r;
                }
            } else if (!floating && opcode == 4) { /* CLS when U is clear, CLZ when set */
                int lane = 1 << size;
                int width = lane * 8;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t v = 0;
                    std::memcpy(&v, a + at, lane);
                    int count = 0;
                    if (u) {
                        for (int bit = width - 1; bit >= 0 && !((v >> bit) & 1); --bit) ++count;
                    } else {
                        int top = (int)((v >> (width - 1)) & 1);
                        for (int bit = width - 2; bit >= 0 && (int)((v >> bit) & 1) == top; --bit) ++count;
                    }
                    uint64_t r = (uint64_t)count;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (!floating && opcode == 2) { /* SADDLP and UADDLP: add neighbours, widen */
                int lane = 1 << size;
                int wide = lane * 2;
                for (int at = 0; at + wide <= lanes_bytes; at += wide) {
                    uint64_t x = 0, y = 0;
                    std::memcpy(&x, a + at, lane);
                    std::memcpy(&y, a + at + lane, lane);
                    int shift = 64 - lane * 8;
                    uint64_t r = u ? x + y
                                   : (uint64_t)(((int64_t)(x << shift) >> shift) + ((int64_t)(y << shift) >> shift));
                    std::memcpy(out + at, &r, wide);
                }
            } else if ((opcode == 0x17 || opcode == 0x16) && !u && !scalar) {
                /* FCVTL widens half to single or single to double, from the
                   low half of the source (the high half for FCVTL2). FCVTN
                   narrows the other way into the low or high half. */
                auto half_to_float = [](uint16_t h) -> float {
                    uint32_t sign = (uint32_t)(h >> 15) << 31, exp = (h >> 10) & 0x1f, man = h & 0x3ff;
                    uint32_t bits;
                    if (exp == 0) {
                        float v = std::ldexp((float)man, -24);
                        return sign ? -v : v;
                    }
                    if (exp == 31) bits = sign | 0x7f800000u | (man << 13);
                    else bits = sign | ((exp + 112) << 23) | (man << 13);
                    return bits_to_float(bits);
                };
                auto float_to_half = [](float f) -> uint16_t {
                    uint32_t bits = float_to_bits(f);
                    uint16_t sign = (uint16_t)((bits >> 16) & 0x8000);
                    float mag = std::fabs(f);
                    if (std::isnan(f)) return (uint16_t)(sign | 0x7e00);
                    if (mag >= 65520.0f) return (uint16_t)(sign | 0x7c00);
                    if (mag < 6.103515625e-05f) return (uint16_t)(sign | (uint16_t)std::nearbyint(mag * 16777216.0f));
                    int e;
                    float m = std::frexp(mag, &e); /* mag = m * 2^e, m in [0.5,1) */
                    uint32_t mant = (uint32_t)std::nearbyint((m * 2.0f - 1.0f) * 1024.0f);
                    int exp = e - 1 + 15;
                    if (mant == 1024) {
                        mant = 0;
                        ++exp;
                    }
                    return (uint16_t)(sign | (exp << 10) | mant);
                };
                bool doubles = (size & 1) != 0;
                if (opcode == 0x17) {
                    const uint8_t* from = a + (q ? 8 : 0);
                    if (doubles) {
                        for (int i = 0; i < 2; ++i) {
                            uint32_t raw;
                            std::memcpy(&raw, from + i * 4, 4);
                            uint64_t bits = double_to_bits((double)bits_to_float(raw));
                            std::memcpy(out + i * 8, &bits, 8);
                        }
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            uint16_t raw;
                            std::memcpy(&raw, from + i * 2, 2);
                            uint32_t bits = float_to_bits(half_to_float(raw));
                            std::memcpy(out + i * 4, &bits, 4);
                        }
                    }
                } else {
                    uint8_t narrowed[8] = {};
                    if (doubles) {
                        for (int i = 0; i < 2; ++i) {
                            uint64_t raw;
                            std::memcpy(&raw, a + i * 8, 8);
                            uint32_t bits = float_to_bits((float)bits_to_double(raw));
                            std::memcpy(narrowed + i * 4, &bits, 4);
                        }
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            uint32_t raw;
                            std::memcpy(&raw, a + i * 4, 4);
                            uint16_t h = float_to_half(bits_to_float(raw));
                            std::memcpy(narrowed + i * 2, &h, 2);
                        }
                    }
                    if (q) {
                        std::memcpy(out, &cpu.q[rd].lo, 8);
                        std::memcpy(out + 8, narrowed, 8);
                    } else {
                        std::memcpy(out, narrowed, 8);
                    }
                }
                lanes_bytes = 16;
                q = true; /* every byte of the result was computed above */
            } else if (!floating && opcode == 0x12 && !u) {
                /* XTN: keep the low half of each element, packed into the low
                   half of the result, or the high half for the '2' form. */
                int lane = 1 << size; /* the narrow size */
                int count = 8 / lane;
                uint8_t narrowed[8] = {};
                for (int i = 0; i < count; ++i) std::memcpy(narrowed + i * lane, a + i * lane * 2, lane);
                if (q) {
                    std::memcpy(out, &cpu.q[rd].lo, 8);
                    std::memcpy(out + 8, narrowed, 8);
                } else {
                    std::memcpy(out, narrowed, 8);
                }
                lanes_bytes = 16; /* both halves are written as computed above */
            } else if (u && opcode == 5 && size == 0) { /* NOT */
                for (int i = 0; i < lanes_bytes; ++i) out[i] = (uint8_t)~a[i];
            } else {
                handled = false;
            }
            if (handled) {
                std::memcpy(&cpu.q[rd].lo, out, 8);
                std::memcpy(&cpu.q[rd].hi, out + 8, 8);
                if (!q) cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
        }

        /* UZP, ZIP and TRN, which the vectoriser uses to narrow lanes. */
        if (((insn >> 21) & 1) == 0 && ((insn >> 15) & 1) == 0 && ((insn >> 10) & 3) == 2 &&
            ((insn >> 29) & 1) == 0) {
            int size = (insn >> 22) & 3;
            int opcode = (insn >> 12) & 7;
            int rm = (insn >> 16) & 31;
            int lane = 1 << size;
            int count = lanes_bytes / lane;
            uint8_t a[16];
            uint8_t b[16];
            uint8_t out[16] = {};
            std::memcpy(a, &cpu.q[rn].lo, 8);
            std::memcpy(a + 8, &cpu.q[rn].hi, 8);
            std::memcpy(b, &cpu.q[rm].lo, 8);
            std::memcpy(b + 8, &cpu.q[rm].hi, 8);
            bool handled = true;
            if (opcode == 1 || opcode == 5) { /* UZP1 takes the even lanes, UZP2 the odd */
                int start = opcode == 1 ? 0 : 1;
                for (int i = 0; i < count; ++i) {
                    const uint8_t* source = i < count / 2 ? a : b;
                    int index = (i < count / 2 ? i : i - count / 2) * 2 + start;
                    std::memcpy(out + i * lane, source + index * lane, lane);
                }
            } else if (opcode == 3 || opcode == 7) { /* ZIP1 and ZIP2 */
                int base = opcode == 3 ? 0 : count / 2;
                for (int i = 0; i < count / 2; ++i) {
                    std::memcpy(out + (i * 2) * lane, a + (base + i) * lane, lane);
                    std::memcpy(out + (i * 2 + 1) * lane, b + (base + i) * lane, lane);
                }
            } else if (opcode == 2 || opcode == 6) { /* TRN1 and TRN2 */
                int start = opcode == 2 ? 0 : 1;
                for (int i = 0; i < count / 2; ++i) {
                    std::memcpy(out + (i * 2) * lane, a + (i * 2 + start) * lane, lane);
                    std::memcpy(out + (i * 2 + 1) * lane, b + (i * 2 + start) * lane, lane);
                }
            } else {
                handled = false;
            }
            if (handled) {
                std::memcpy(&cpu.q[rd].lo, out, 8);
                std::memcpy(&cpu.q[rd].hi, out + 8, 8);
                if (!q) cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
        }

        /* Three registers of the same shape. */
        if (((insn >> 21) & 1) == 1 && ((insn >> 10) & 1) == 1) {
            int size = (insn >> 22) & 3;
            int opcode = (insn >> 11) & 0x1f;
            int rm = (insn >> 16) & 31;
            uint8_t a[16];
            uint8_t b[16];
            uint8_t out[16] = {};
            std::memcpy(a, &cpu.q[rn].lo, 8);
            std::memcpy(a + 8, &cpu.q[rn].hi, 8);
            std::memcpy(b, &cpu.q[rm].lo, 8);
            std::memcpy(b + 8, &cpu.q[rm].hi, 8);
            bool handled = false;
            if (opcode == 0x03) { /* The logical group, chosen by size */
                handled = true;
                uint8_t d[16];
                std::memcpy(d, &cpu.q[rd].lo, 8);
                std::memcpy(d + 8, &cpu.q[rd].hi, 8);
                for (int i = 0; i < lanes_bytes; ++i) {
                    if (!u && size == 0) out[i] = a[i] & b[i];        /* AND */
                    else if (!u && size == 1) out[i] = a[i] & ~b[i];  /* BIC */
                    else if (!u && size == 2) out[i] = a[i] | b[i];   /* ORR, which is how MOV is written */
                    else if (!u && size == 3) out[i] = a[i] | ~b[i];  /* ORN */
                    else if (size == 0) out[i] = a[i] ^ b[i];         /* EOR */
                    else if (size == 1) out[i] = (uint8_t)((d[i] & a[i]) | (~d[i] & b[i])); /* BSL */
                    /* BIT inserts Vn where Vm is set, BIF where it is clear. */
                    else if (size == 2) out[i] = (uint8_t)((d[i] & ~b[i]) | (a[i] & b[i])); /* BIT */
                    else out[i] = (uint8_t)((d[i] & b[i]) | (a[i] & ~b[i]));                /* BIF */
                }
            } else if (opcode == 0x13 && u && size == 0) { /* PMUL: carryless, per byte */
                handled = true;
                for (int i = 0; i < lanes_bytes; ++i) {
                    uint32_t r = 0;
                    for (int bit = 0; bit < 8; ++bit)
                        if ((b[i] >> bit) & 1) r ^= (uint32_t)a[i] << bit;
                    out[i] = (uint8_t)r;
                }
            } else if (opcode == 0x13 && !u) { /* MUL */
                handled = true;
                int lane = 1 << size;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t x = 0, y = 0;
                    std::memcpy(&x, a + at, lane);
                    std::memcpy(&y, b + at, lane);
                    uint64_t r = x * y;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x11) { /* CMTST when U is clear, CMEQ when set */
                handled = true;
                int lane = 1 << size;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t x = 0, y = 0;
                    std::memcpy(&x, a + at, lane);
                    std::memcpy(&y, b + at, lane);
                    bool set = u ? x == y : (x & y) != 0;
                    uint64_t r = set ? ~0ull : 0;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x19 && !u) { /* FMLA and FMLS */
                handled = true;
                bool doubles = (size & 1) != 0;
                bool subtract = (size & 2) != 0;
                int lane = doubles ? 8 : 4;
                uint8_t d[16];
                std::memcpy(d, &cpu.q[rd].lo, 8);
                std::memcpy(d + 8, &cpu.q[rd].hi, 8);
                std::memcpy(out, d, 16);
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    if (doubles) {
                        uint64_t xb = 0, yb = 0, zb = 0;
                        std::memcpy(&xb, a + at, 8);
                        std::memcpy(&yb, b + at, 8);
                        std::memcpy(&zb, d + at, 8);
                        double p = bits_to_double(xb) * bits_to_double(yb);
                        double r = subtract ? bits_to_double(zb) - p : bits_to_double(zb) + p;
                        uint64_t rb = double_to_bits(r);
                        std::memcpy(out + at, &rb, 8);
                    } else {
                        uint32_t xb = 0, yb = 0, zb = 0;
                        std::memcpy(&xb, a + at, 4);
                        std::memcpy(&yb, b + at, 4);
                        std::memcpy(&zb, d + at, 4);
                        float p = bits_to_float(xb) * bits_to_float(yb);
                        float r = subtract ? bits_to_float(zb) - p : bits_to_float(zb) + p;
                        uint32_t rb = float_to_bits(r);
                        std::memcpy(out + at, &rb, 4);
                    }
                }
            } else if (opcode == 0x00 || opcode == 0x01 || opcode == 0x02 || opcode == 0x04 || opcode == 0x05 ||
                       opcode == 0x08 || opcode == 0x09 || opcode == 0x0a || opcode == 0x0b || opcode == 0x0e ||
                       opcode == 0x0f || opcode == 0x12 || opcode == 0x14 || opcode == 0x15 || opcode == 0x16 ||
                       (opcode == 0x17 && !u)) {
                /* The rest of the integer three-same group: halving and
                   saturating add and subtract, shifts by a register, absolute
                   difference, multiply-accumulate, the pairwise forms and the
                   doubling high multiplies. Each lane is widened to 64 bits,
                   signed or not as U says, worked on, then narrowed. */
                handled = true;
                int lane = 1 << size;
                int width = lane * 8;
                bool pairwise = opcode == 0x14 || opcode == 0x15 || opcode == 0x17;
                if (pairwise && scalar) handled = false;
                uint8_t d[16];
                std::memcpy(d, &cpu.q[rd].lo, 8);
                std::memcpy(d + 8, &cpu.q[rd].hi, 8);
                const int64_t smax = width == 64 ? INT64_MAX : (((int64_t)1 << (width - 1)) - 1);
                const int64_t smin = width == 64 ? INT64_MIN : -((int64_t)1 << (width - 1));
                const uint64_t umax = width == 64 ? UINT64_MAX : ((1ull << width) - 1);
                auto read_lane = [&](const uint8_t* from, int at, bool is_signed) -> uint64_t {
                    uint64_t raw = 0;
                    std::memcpy(&raw, from + at, lane);
                    if (is_signed && width < 64) {
                        int shift = 64 - width;
                        return (uint64_t)((int64_t)(raw << shift) >> shift);
                    }
                    return raw;
                };
                auto clamp_signed = [&](int64_t v, bool overflow_high, bool overflow_low) -> uint64_t {
                    if (overflow_high) return (uint64_t)smax;
                    if (overflow_low) return (uint64_t)smin;
                    return (uint64_t)(v > smax ? smax : v < smin ? smin : v);
                };
                /* A shift by the signed low byte of the other operand: left
                   when positive, right when negative. */
                auto shift_by = [&](uint64_t x, int amount, bool is_signed, bool round, bool saturate) -> uint64_t {
                    if (amount >= 0) {
                        if (x == 0) return 0;
                        if (!saturate) return amount >= 64 ? 0 : x << amount;
                        if (is_signed) {
                            int64_t sx = (int64_t)x;
                            if (amount >= 63) return (uint64_t)(sx < 0 ? smin : smax);
                            int64_t r = (int64_t)((uint64_t)sx << amount);
                            if ((r >> amount) != sx || r > smax || r < smin) return (uint64_t)(sx < 0 ? smin : smax);
                            return (uint64_t)r;
                        }
                        if (amount >= width) return umax;
                        uint64_t r = x << amount;
                        if ((r >> amount) != x || r > umax) return umax;
                        return r;
                    }
                    int by = -amount;
                    if (is_signed) {
                        int64_t sx = (int64_t)x;
                        /* Past the width, a plain shift leaves only the sign;
                           a rounding one leaves zero. */
                        if (by > 64) return round ? 0 : (uint64_t)(sx < 0 ? -1 : 0);
                        if (round) {
                            if (by == 64) return 0;
                            int64_t r = sx >> by;
                            return (uint64_t)(r + ((sx >> (by - 1)) & 1));
                        }
                        return (uint64_t)(by >= 64 ? (sx < 0 ? -1 : 0) : sx >> by);
                    }
                    if (by > 64) return 0;
                    if (round) {
                        uint64_t r = by == 64 ? 0 : x >> by;
                        return r + ((x >> (by - 1)) & 1);
                    }
                    return by >= 64 ? 0 : x >> by;
                };
                int lanes = lanes_bytes / lane;
                for (int i = 0; i < lanes && handled; ++i) {
                    int at = i * lane;
                    uint64_t x, y;
                    if (pairwise) {
                        /* Neighbouring pairs of a, then of b. */
                        int half = lanes / 2;
                        const uint8_t* from = i < half ? a : b;
                        int base = (i < half ? i : i - half) * 2 * lane;
                        x = read_lane(from, base, !u);
                        y = read_lane(from, base + lane, !u);
                    } else {
                        x = read_lane(a, at, !u);
                        y = read_lane(b, at, !u);
                    }
                    uint64_t r = 0;
                    switch (opcode) {
                    case 0x00: case 0x02: case 0x04: { /* [SU]HADD, [SU]RHADD, [SU]HSUB */
                        /* Halving in 64 bits cannot overflow for lanes up to 32
                           bits; for 64 it is done by halves. */
                        bool sub = opcode == 0x04;
                        int carry = opcode == 0x02 ? 1 : 0;
                        if (!u) {
                            int64_t sx = (int64_t)x, sy = (int64_t)y;
                            int64_t h = (sx >> 1) + (sub ? -(sy >> 1) : (sy >> 1));
                            int64_t low = (sx & 1) + (sub ? -(sy & 1) : (sy & 1)) + carry;
                            r = (uint64_t)(h + (low >> 1));
                        } else {
                            uint64_t h = (x >> 1) + (sub ? (uint64_t)0 - (y >> 1) : (y >> 1));
                            int64_t low = (int64_t)(x & 1) + (sub ? -(int64_t)(y & 1) : (int64_t)(y & 1)) + carry;
                            r = h + (uint64_t)(low >> 1);
                        }
                        break;
                    }
                    case 0x01: case 0x05: { /* SQADD/UQADD, SQSUB/UQSUB */
                        bool sub = opcode == 0x05;
                        if (!u) {
                            int64_t sx = (int64_t)x, sy = (int64_t)y;
                            int64_t v = (int64_t)(sub ? (uint64_t)sx - (uint64_t)sy : (uint64_t)sx + (uint64_t)sy);
                            bool over = sub ? (((sx ^ sy) & (sx ^ v)) < 0) : ((~(sx ^ sy) & (sx ^ v)) < 0);
                            if (width == 64 && over) r = sx < 0 ? (uint64_t)INT64_MIN : (uint64_t)INT64_MAX;
                            else r = clamp_signed(v, false, false);
                        } else if (sub) {
                            r = x < y ? 0 : x - y;
                        } else {
                            uint64_t v = x + y;
                            r = (v < x || v > umax) ? umax : v;
                        }
                        break;
                    }
                    case 0x08: case 0x09: case 0x0a: case 0x0b: { /* [SU]SHL, [SU]QSHL, [SU]RSHL, [SU]QRSHL */
                        int amount = (int8_t)(uint8_t)y;
                        bool round = opcode == 0x0a || opcode == 0x0b;
                        bool saturate = opcode == 0x09 || opcode == 0x0b;
                        r = shift_by(x, amount, !u, round, saturate);
                        break;
                    }
                    case 0x0e: case 0x0f: { /* [SU]ABD, [SU]ABA */
                        uint64_t diff = !u ? (uint64_t)((int64_t)x > (int64_t)y ? (int64_t)x - (int64_t)y
                                                                              : (int64_t)y - (int64_t)x)
                                           : (x > y ? x - y : y - x);
                        r = diff;
                        if (opcode == 0x0f) {
                            uint64_t acc = 0;
                            std::memcpy(&acc, d + at, lane);
                            r = acc + diff;
                        }
                        break;
                    }
                    case 0x12: { /* MLA, MLS */
                        uint64_t acc = 0;
                        std::memcpy(&acc, d + at, lane);
                        r = u ? acc - x * y : acc + x * y;
                        break;
                    }
                    case 0x14: /* SMAXP/UMAXP */
                        r = !u ? ((int64_t)x > (int64_t)y ? x : y) : (x > y ? x : y);
                        break;
                    case 0x15: /* SMINP/UMINP */
                        r = !u ? ((int64_t)x < (int64_t)y ? x : y) : (x < y ? x : y);
                        break;
                    case 0x16: { /* SQDMULH, SQRDMULH: the high half of twice the product */
                        if (width != 16 && width != 32) {
                            handled = false;
                            break;
                        }
                        int64_t sx = (int64_t)read_lane(a, at, true), sy = (int64_t)read_lane(b, at, true);
                        if (sx == smin && sy == smin) {
                            r = (uint64_t)smax;
                            break;
                        }
                        int64_t product = sx * sy * 2;
                        if (u) product += (int64_t)1 << (width - 1);
                        r = (uint64_t)(product >> width);
                        break;
                    }
                    case 0x17: /* ADDP */
                        r = x + y;
                        break;
                    }
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x06 || opcode == 0x07 || opcode == 0x0c || opcode == 0x0d) {
                /* CMGT/CMHI, CMGE/CMHS, and MAX/MIN, signed unless U is set. */
                handled = true;
                int lane = 1 << size;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t x = 0, y = 0;
                    std::memcpy(&x, a + at, lane);
                    std::memcpy(&y, b + at, lane);
                    int shift = 64 - lane * 8;
                    int64_t sx = (int64_t)(x << shift) >> shift;
                    int64_t sy = (int64_t)(y << shift) >> shift;
                    bool greater = u ? x > y : sx > sy;
                    bool equal = x == y;
                    uint64_t r = 0;
                    if (opcode == 0x06) r = greater ? ~0ull : 0;
                    else if (opcode == 0x07) r = (greater || equal) ? ~0ull : 0;
                    else if (opcode == 0x0c) r = greater ? x : y;
                    else r = greater ? y : x;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x1c || opcode == 0x1d || opcode == 0x1e) {
                /* FCMEQ, FCMGE, FCMGT, FACGE, FACGT, and FMAX/FMIN. Bit 23
                   picks between each pair; bit 22 is the precision. */
                handled = true;
                bool doubles = (size & 1) != 0;
                bool high = (size & 2) != 0;
                int lane = doubles ? 8 : 4;
                if (scalar) lanes_bytes = lane;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    double x = 0, y = 0;
                    if (doubles) {
                        uint64_t xb = 0, yb = 0;
                        std::memcpy(&xb, a + at, 8);
                        std::memcpy(&yb, b + at, 8);
                        x = bits_to_double(xb);
                        y = bits_to_double(yb);
                    } else {
                        uint32_t xb = 0, yb = 0;
                        std::memcpy(&xb, a + at, 4);
                        std::memcpy(&yb, b + at, 4);
                        x = bits_to_float(xb);
                        y = bits_to_float(yb);
                    }
                    uint64_t mask = 0;
                    bool is_value = false;
                    double value = 0;
                    if (opcode == 0x1c) {
                        bool set = !u ? x == y : (high ? x > y : x >= y);
                        mask = set ? ~0ull : 0;
                    } else if (opcode == 0x1d && u) {
                        bool set = high ? std::fabs(x) > std::fabs(y) : std::fabs(x) >= std::fabs(y);
                        mask = set ? ~0ull : 0;
                    } else if (opcode == 0x1e && !u) {
                        is_value = true;
                        value = high ? (x < y ? x : y) : (x > y ? x : y);
                    } else {
                        handled = false;
                    }
                    if (is_value) {
                        if (doubles) mask = double_to_bits(value);
                        else mask = float_to_bits((float)value);
                    }
                    std::memcpy(out + at, &mask, lane);
                }
            } else if (opcode == 0x10) { /* ADD or SUB */
                handled = true;
                int lane = 1 << size;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    uint64_t x = 0, y = 0;
                    std::memcpy(&x, a + at, lane);
                    std::memcpy(&y, b + at, lane);
                    uint64_t r = u ? x - y : x + y;
                    std::memcpy(out + at, &r, lane);
                }
            } else if (opcode == 0x1a || opcode == 0x1b || opcode == 0x1f || (scalar && opcode == 0x1a)) {
                /* FADD / FSUB, FMUL, FDIV, and the scalar absolute difference. */
                handled = true;
                bool doubles = (size & 1) != 0;
                int lane = doubles ? 8 : 4;
                if (scalar) lanes_bytes = lane;
                for (int at = 0; at + lane <= lanes_bytes; at += lane) {
                    double x = 0, y = 0;
                    if (doubles) {
                        uint64_t xb = 0, yb = 0;
                        std::memcpy(&xb, a + at, 8);
                        std::memcpy(&yb, b + at, 8);
                        x = bits_to_double(xb);
                        y = bits_to_double(yb);
                    } else {
                        uint32_t xb = 0, yb = 0;
                        std::memcpy(&xb, a + at, 4);
                        std::memcpy(&yb, b + at, 4);
                        x = bits_to_float(xb);
                        y = bits_to_float(yb);
                    }
                    double r = 0;
                    if (opcode == 0x1a && u) r = std::fabs(x - y); /* FABD */
                    else if (opcode == 0x1a) r = (size & 2) ? x - y : x + y;
                    else if (opcode == 0x1b) r = x * y;
                    else r = x / y;
                    if (doubles) {
                        uint64_t rb = double_to_bits(r);
                        std::memcpy(out + at, &rb, 8);
                    } else {
                        uint32_t rb = float_to_bits((float)r);
                        std::memcpy(out + at, &rb, 4);
                    }
                }
            }
            if (handled) {
                std::memcpy(&cpu.q[rd].lo, out, 8);
                std::memcpy(&cpu.q[rd].hi, out + 8, 8);
                if (!q) cpu.q[rd].hi = 0;
                cpu.pc = next;
                return true;
            }
        }
    }

    return false;
}
