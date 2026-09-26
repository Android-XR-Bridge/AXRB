/* The ARMv8 cryptographic extension: AES, SHA1 and SHA256, which the CPU
   information given to the guest advertises and which libraries such as
   OpenSSL probe for and then use. Written from the pseudocode in the Arm
   Architecture Reference Manual (AESE, SHA1C, SHA256H and their helpers).
   A 128-bit register is two 64-bit halves, element e being bits 32e..32e+31. */
#include "cpu.h"

#include <cstring>

namespace {

struct V128 {
    uint32_t w[4];
    uint8_t* bytes() { return reinterpret_cast<uint8_t*>(w); }
};

V128 get(const GuestCpu& cpu, int r) {
    V128 v;
    std::memcpy(&v.w[0], &cpu.q[r].lo, 8);
    std::memcpy(&v.w[2], &cpu.q[r].hi, 8);
    return v;
}
void put(GuestCpu& cpu, int r, const V128& v) {
    std::memcpy(&cpu.q[r].lo, &v.w[0], 8);
    std::memcpy(&cpu.q[r].hi, &v.w[2], 8);
}
uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

/* ---- AES ---- */

const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9,
    0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f,
    0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07,
    0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3,
    0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58,
    0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3,
    0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f,
    0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac,
    0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a,
    0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70,
    0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
    0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

uint8_t inverse_sbox(uint8_t v) {
    static uint8_t table[256];
    static bool made = [] {
        for (int i = 0; i < 256; ++i) table[kSbox[i]] = (uint8_t)i;
        return true;
    }();
    (void)made;
    return table[v];
}
uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}
/* The state is column-major: byte i is row i % 4, column i / 4. */
void shift_rows(uint8_t* s, bool inverse) {
    uint8_t t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            if (!inverse) t[r + 4 * c] = s[r + 4 * ((c + r) % 4)];
            else t[r + 4 * ((c + r) % 4)] = s[r + 4 * c];
        }
    std::memcpy(s, t, 16);
}
void mix_columns(uint8_t* s, bool inverse) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* col = s + 4 * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        if (!inverse) {
            col[0] = (uint8_t)(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
            col[1] = (uint8_t)(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
            col[2] = (uint8_t)(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
            col[3] = (uint8_t)(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
        } else {
            col[0] = (uint8_t)(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
            col[1] = (uint8_t)(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
            col[2] = (uint8_t)(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
            col[3] = (uint8_t)(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
        }
    }
}

/* ---- SHA ---- */

uint32_t choose(uint32_t x, uint32_t y, uint32_t z) { return ((y ^ z) & x) ^ z; }
uint32_t parity(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
uint32_t majority(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | ((x | y) & z); }

/* SHA256hash from the manual: returns X for SHA256H, Y for SHA256H2. */
V128 sha256_hash(V128 x, V128 y, const V128& w, bool part1) {
    for (int e = 0; e < 4; ++e) {
        uint32_t chs = choose(y.w[0], y.w[1], y.w[2]);
        uint32_t maj = majority(x.w[0], x.w[1], x.w[2]);
        uint32_t t1 = y.w[3] + (ror(y.w[0], 6) ^ ror(y.w[0], 11) ^ ror(y.w[0], 25)) + chs + w.w[e];
        x.w[3] = t1 + x.w[3];
        y.w[3] = t1 + (ror(x.w[0], 2) ^ ror(x.w[0], 13) ^ ror(x.w[0], 22)) + maj;
        /* Y:X rotated left by 32 bits, as one 256-bit value. */
        uint32_t x_top = x.w[3], y_top = y.w[3];
        x.w[3] = x.w[2]; x.w[2] = x.w[1]; x.w[1] = x.w[0]; x.w[0] = y_top;
        y.w[3] = y.w[2]; y.w[2] = y.w[1]; y.w[1] = y.w[0]; y.w[0] = x_top;
    }
    return part1 ? x : y;
}

}  // namespace

bool step_crypto(GuestCpu& cpu, uint32_t insn, uint64_t next) {
    const int rd = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
    /* AESE, AESD, AESMC, AESIMC */
    if ((insn & 0xffffcc00u) == 0x4e284800u) {
        int op = (insn >> 12) & 3; /* 0 AESE, 1 AESD, 2 AESMC, 3 AESIMC */
        V128 v = get(cpu, rn);
        uint8_t* s = v.bytes();
        if (op <= 1) {
            V128 key = get(cpu, rd);
            for (int i = 0; i < 16; ++i) s[i] ^= key.bytes()[i];
            shift_rows(s, op == 1);
            for (int i = 0; i < 16; ++i) s[i] = op == 0 ? kSbox[s[i]] : inverse_sbox(s[i]);
        } else {
            mix_columns(s, op == 3);
        }
        put(cpu, rd, v);
        cpu.pc = next;
        return true;
    }
    /* SHA1C, SHA1P, SHA1M, SHA1SU0, SHA256H, SHA256H2, SHA256SU1 */
    if ((insn & 0xffe08c00u) == 0x5e000000u) {
        int opc = (insn >> 12) & 7;
        V128 d = get(cpu, rd), n = get(cpu, rn), m = get(cpu, rm), result{};
        if (opc <= 2) {
            V128 x = d;
            uint32_t y = n.w[0];
            for (int e = 0; e < 4; ++e) {
                uint32_t t = opc == 0 ? choose(x.w[1], x.w[2], x.w[3])
                             : opc == 1 ? parity(x.w[1], x.w[2], x.w[3])
                                        : majority(x.w[1], x.w[2], x.w[3]);
                y = y + rol(x.w[0], 5) + t + m.w[e];
                x.w[1] = rol(x.w[1], 30);
                /* Y:X (160 bits) rotated left by 32. */
                uint32_t top = x.w[3];
                x.w[3] = x.w[2]; x.w[2] = x.w[1]; x.w[1] = x.w[0]; x.w[0] = y;
                y = top;
            }
            result = x;
        } else if (opc == 3) { /* SHA1SU0 */
            result.w[0] = d.w[2] ^ d.w[0] ^ m.w[0];
            result.w[1] = d.w[3] ^ d.w[1] ^ m.w[1];
            result.w[2] = n.w[0] ^ d.w[2] ^ m.w[2];
            result.w[3] = n.w[1] ^ d.w[3] ^ m.w[3];
        } else if (opc == 4) {
            result = sha256_hash(d, n, m, true);
        } else if (opc == 5) {
            result = sha256_hash(n, d, m, false);
        } else if (opc == 6) { /* SHA256SU1 */
            uint32_t t0[4] = {n.w[1], n.w[2], n.w[3], m.w[0]};
            auto sigma1 = [](uint32_t e) { return ror(e, 17) ^ ror(e, 19) ^ (e >> 10); };
            result.w[0] = sigma1(m.w[2]) + d.w[0] + t0[0];
            result.w[1] = sigma1(m.w[3]) + d.w[1] + t0[1];
            result.w[2] = sigma1(result.w[0]) + d.w[2] + t0[2];
            result.w[3] = sigma1(result.w[1]) + d.w[3] + t0[3];
        } else {
            return false;
        }
        put(cpu, rd, result);
        cpu.pc = next;
        return true;
    }
    /* SHA1H, SHA1SU1, SHA256SU0 */
    if ((insn & 0xfffe0c00u) == 0x5e280800u) {
        int opcode = (insn >> 12) & 0x1f;
        V128 d = get(cpu, rd), n = get(cpu, rn), result{};
        if (opcode == 0) { /* SHA1H: the rest of the register is zeroed */
            result.w[0] = rol(n.w[0], 30);
        } else if (opcode == 1) { /* SHA1SU1 */
            uint32_t t[4] = {d.w[0] ^ n.w[1], d.w[1] ^ n.w[2], d.w[2] ^ n.w[3], d.w[3]};
            result.w[0] = rol(t[0], 1);
            result.w[1] = rol(t[1], 1);
            result.w[2] = rol(t[2], 1);
            result.w[3] = rol(t[3], 1) ^ rol(t[0], 2);
        } else if (opcode == 2) { /* SHA256SU0 */
            uint32_t t[4] = {d.w[1], d.w[2], d.w[3], n.w[0]};
            for (int e = 0; e < 4; ++e) result.w[e] = (ror(t[e], 7) ^ ror(t[e], 18) ^ (t[e] >> 3)) + d.w[e];
        } else {
            return false;
        }
        put(cpu, rd, result);
        cpu.pc = next;
        return true;
    }
    return false;
}
