#pragma once
// ASTC LDR block decoding, for GPUs without ASTC: Quest games ship nearly all
// their textures as ASTC, which desktop GPUs cannot sample.
// Ported from the decoder in CUE4Parse (Apache-2.0), itself from Ryujinx and
// before that FasTC (Pavel Krajcevski), following the ASTC specification,
// section C.2 (via experimental/quest-bridge/guest/astc.cpp). Differences from
// that code: a fixed luminance delta bug in endpoint mode 1 (the spec clamps
// with min), sRGB endpoint expansion, and blocks that are invalid or HDR
// decode to magenta, as the specification asks, instead of throwing.
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace axrb::texture::astc {


struct Bits {
    uint64_t lo = 0, hi = 0;
    int pos = 0;
    int bit(int i) const { return i < 64 ? (int)((lo >> i) & 1) : i < 128 ? (int)((hi >> (i - 64)) & 1) : 0; }
    /* n bits from position at, n <= 32. */
    int peek(int at, int n) const {
        if (n <= 0 || at >= 128) return 0;
        uint64_t word;
        if (at >= 64) word = hi >> (at - 64);
        else if (at == 0) word = lo;
        else word = (lo >> at) | (hi << (64 - at));
        return (int)(word & ((1ull << n) - 1));
    }
    int read(int n) {
        int value = peek(pos, n);
        pos += n;
        return value;
    }
};

inline int bits_of(int v, int start, int end) { return (v >> start) & ((1 << (end - start + 1)) - 1); }
inline int bit_of(int v, int i) { return (v >> i) & 1; }

inline int replicate(int value, int bits, int to) {
    if (bits == 0 || to == 0) return 0;
    int temp = value & ((1 << bits) - 1);
    int result = temp, length = bits;
    while (length < to) {
        int comp = 0;
        if (bits > to - length) {
            int shift = to - length;
            comp = bits - shift;
            bits = shift;
        }
        result <<= bits;
        result |= temp >> comp;
        length += bits;
    }
    return result;
}

inline int popcount(int v) {
    int n = 0;
    for (; v; ++n) v &= v - 1;
    return n;
}

enum Encoding { JustBits, Quint, Trit };
struct Encoded {
    Encoding encoding = JustBits;
    int bits = 0;
    int value = 0;  /* the plain bits */
    int extra = 0;  /* the trit or quint */
};

inline Encoded encoding_for_uncached(int max_value) {
    while (max_value > 0) {
        int check = max_value + 1;
        if ((check & (check - 1)) == 0) return {JustBits, popcount(max_value)};
        if (check % 3 == 0 && ((check / 3) & ((check / 3) - 1)) == 0) return {Trit, popcount(check / 3 - 1)};
        if (check % 5 == 0 && ((check / 5) & ((check / 5) - 1)) == 0) return {Quint, popcount(check / 5 - 1)};
        --max_value;
    }
    return {JustBits, 0};
}

/* encoding_for for every value an ASTC block can ask for, computed once. */
inline Encoded encoding_for(int max_value) {
    struct Table {
        Encoded entries[256];
        Table() {
            for (int i = 0; i < 256; ++i) entries[i] = encoding_for_uncached(i);
        }
    };
    static const Table table;
    return table.entries[max_value & 255];
}

inline int bit_length(const Encoded& e, int count) {
    int total = e.bits * count;
    if (e.encoding == Trit) total += (count * 8 + 4) / 5;
    else if (e.encoding == Quint) total += (count * 7 + 2) / 3;
    return total;
}

inline void decode_trits(Bits& in, Encoded* out, int& n, int bits) {
    int m[5], t[5];
    m[0] = in.read(bits);
    int T = in.read(2);
    m[1] = in.read(bits);
    T |= in.read(2) << 2;
    m[2] = in.read(bits);
    T |= in.read(1) << 4;
    m[3] = in.read(bits);
    T |= in.read(2) << 5;
    m[4] = in.read(bits);
    T |= in.read(1) << 7;
    int C;
    if (bits_of(T, 2, 4) == 7) {
        C = (bits_of(T, 5, 7) << 2) | bits_of(T, 0, 1);
        t[4] = t[3] = 2;
    } else {
        C = bits_of(T, 0, 4);
        if (bits_of(T, 5, 6) == 3) {
            t[4] = 2;
            t[3] = bit_of(T, 7);
        } else {
            t[4] = bit_of(T, 7);
            t[3] = bits_of(T, 5, 6);
        }
    }
    if (bits_of(C, 0, 1) == 3) {
        t[2] = 2;
        t[1] = bit_of(C, 4);
        t[0] = (bit_of(C, 3) << 1) | (bit_of(C, 2) & ~bit_of(C, 3) & 1);
    } else if (bits_of(C, 2, 3) == 3) {
        t[2] = 2;
        t[1] = 2;
        t[0] = bits_of(C, 0, 1);
    } else {
        t[2] = bit_of(C, 4);
        t[1] = bits_of(C, 2, 3);
        t[0] = (bit_of(C, 1) << 1) | (bit_of(C, 0) & ~bit_of(C, 1) & 1);
    }
    for (int i = 0; i < 5; ++i) out[n++] = {Trit, bits, m[i], t[i]};
}

inline void decode_quints(Bits& in, Encoded* out, int& n, int bits) {
    int m[3], q[3];
    m[0] = in.read(bits);
    int Q = in.read(3);
    m[1] = in.read(bits);
    Q |= in.read(2) << 3;
    m[2] = in.read(bits);
    Q |= in.read(2) << 5;
    if (bits_of(Q, 1, 2) == 3 && bits_of(Q, 5, 6) == 0) {
        q[0] = q[1] = 4;
        q[2] = (bit_of(Q, 0) << 2) | ((bit_of(Q, 4) & ~bit_of(Q, 0) & 1) << 1) | (bit_of(Q, 3) & ~bit_of(Q, 0) & 1);
    } else {
        int C;
        if (bits_of(Q, 1, 2) == 3) {
            q[2] = 4;
            C = (bits_of(Q, 3, 4) << 3) | ((~bits_of(Q, 5, 6) & 3) << 1) | bit_of(Q, 0);
        } else {
            q[2] = bits_of(Q, 5, 6);
            C = bits_of(Q, 0, 4);
        }
        if (bits_of(C, 0, 2) == 5) {
            q[1] = 4;
            q[0] = bits_of(C, 3, 4);
        } else {
            q[1] = bits_of(C, 3, 4);
            q[0] = bits_of(C, 0, 2);
        }
    }
    for (int i = 0; i < 3; ++i) out[n++] = {Quint, bits, m[i], q[i]};
}

/* Decodes count values; out needs room for count + 4. Returns how many. */
inline int decode_sequence(Encoded* out, Bits& in, int max_value, int count) {
    Encoded e = encoding_for(max_value);
    int n = 0;
    while (n < count) {
        if (e.encoding == Quint) decode_quints(in, out, n, e.bits);
        else if (e.encoding == Trit) decode_trits(in, out, n, e.bits);
        else out[n++] = {JustBits, e.bits, in.read(e.bits), 0};
    }
    return n;
}

struct Params {
    int width = 0, height = 0, max_weight = 0;
    bool dual = false, error = false, void_ldr = false, void_hdr = false;
    int packed_bits() const { return bit_length(encoding_for(max_weight), width * height * (dual ? 2 : 1)); }
    int weight_count() const { return width * height * (dual ? 2 : 1); }
};

inline Params block_params(Bits& in) {
    Params p;
    int mode = in.read(11);
    if ((mode & 0x1ff) == 0x1fc) {
        if (mode & 0x200) p.void_hdr = true;
        else p.void_ldr = true;
        if (!(mode & 0x400) || in.read(1) == 0) p.error = true;
        return p;
    }
    if ((mode & 0xf) == 0 || ((mode & 0x3) == 0 && (mode & 0x1c0) == 0x1c0)) {
        p.error = true;
        return p;
    }
    int layout;
    if (mode & 0x3) {
        if (mode & 0x8) layout = (mode & 0x4) ? ((mode & 0x100) ? 4 : 3) : 2;
        else layout = (mode & 0x4) ? 1 : 0;
    } else {
        if (mode & 0x100) layout = (mode & 0x80) ? ((mode & 0x20) ? 8 : 7) : 9;
        else layout = (mode & 0x80) ? 6 : 5;
    }
    int R = (mode >> 4) & 1;
    if (layout < 5) R |= (mode & 0x3) << 1;
    else R |= (mode & 0xc) >> 1;
    int A = (mode >> 5) & 0x3;
    switch (layout) {
        case 0: p.width = ((mode >> 7) & 3) + 4; p.height = A + 2; break;
        case 1: p.width = ((mode >> 7) & 3) + 8; p.height = A + 2; break;
        case 2: p.width = A + 2; p.height = ((mode >> 7) & 3) + 8; break;
        case 3: p.width = A + 2; p.height = ((mode >> 7) & 1) + 6; break;
        case 4: p.width = ((mode >> 7) & 1) + 2; p.height = A + 2; break;
        case 5: p.width = 12; p.height = A + 2; break;
        case 6: p.width = A + 2; p.height = 12; break;
        case 7: p.width = 6; p.height = 10; break;
        case 8: p.width = 10; p.height = 6; break;
        default: p.width = A + 6; p.height = ((mode >> 9) & 3) + 6; break;
    }
    bool D = layout != 9 && (mode & 0x400);
    bool H = layout != 9 && (mode & 0x200);
    if (R < 2) {
        p.error = true;
        return p;
    }
    static const int high[] = {9, 11, 15, 19, 23, 31}, low[] = {1, 2, 3, 4, 5, 7};
    p.max_weight = H ? high[R - 2] : low[R - 2];
    p.dual = D;
    return p;
}

inline int unquantize_weight(const Encoded& e) {
    int A = replicate(e.value & 1, 1, 7), B = 0, C = 0, D = 0, result = 0;
    if (e.encoding == JustBits) {
        result = replicate(e.value, e.bits, 6);
    } else if (e.encoding == Trit) {
        D = e.extra;
        switch (e.bits) {
            case 0: { static const int r[] = {0, 32, 63}; result = r[D]; break; }
            case 1: C = 50; break;
            case 2: { C = 23; int b = (e.value >> 1) & 1; B = (b << 6) | (b << 2) | b; break; }
            default: { C = 11; int cb = (e.value >> 1) & 3; B = (cb << 5) | cb; break; }
        }
    } else {
        D = e.extra;
        switch (e.bits) {
            case 0: { static const int r[] = {0, 16, 32, 47, 63}; result = r[D]; break; }
            case 1: C = 28; break;
            default: { C = 13; int b = (e.value >> 1) & 1; B = (b << 6) | (b << 1); break; }
        }
    }
    if (e.encoding != JustBits && e.bits > 0) {
        result = D * C + B;
        result ^= A;
        result = (A & 0x20) | (result >> 2);
    }
    if (result > 32) result += 1;
    return result;
}

inline void decode_color_values(int* out, Bits& in, const int* modes, int partitions, int bits_available) {
    int count = 0;
    for (int i = 0; i < partitions; ++i) count += ((modes[i] >> 2) + 1) << 1;
    /* The largest range whose encoding of count values fits the bits, as the
       lowest value with that encoding; a pure function of (count, bits),
       computed once for all of them. */
    struct Ranges {
        uint8_t entries[19][129];
        Ranges() {
            for (int n = 0; n <= 18; ++n)
                for (int bits = 0; bits <= 128; ++bits) {
                    int range = 256;
                    while (--range > 0) {
                        Encoded e = encoding_for(range);
                        if (bit_length(e, n) <= bits) {
                            while (--range > 0) {
                                Encoded next = encoding_for(range);
                                if (next.encoding != e.encoding || next.bits != e.bits) break;
                            }
                            ++range;
                            break;
                        }
                    }
                    entries[n][bits] = (uint8_t)range;
                }
        }
    };
    static const Ranges ranges;
    int range = count <= 18 && bits_available >= 0 && bits_available <= 128 ? ranges.entries[count][bits_available] : 0;
    Encoded values[40];
    int n = decode_sequence(values, in, range, std::min(count, 36));
    int written = 0;
    for (int i = 0; i < n && written < 32; ++i) {
        const Encoded& e = values[i];
        int A = replicate(e.value & 1, 1, 9), B = 0, C = 0, D = e.extra;
        if (e.encoding == JustBits) {
            out[written++] = replicate(e.value, e.bits, 8);
            continue;
        }
        int v = e.value;
        if (e.encoding == Trit) {
            switch (e.bits) {
                case 1: C = 204; break;
                case 2: { C = 93; int b = (v >> 1) & 1; B = (b << 8) | (b << 4) | (b << 2) | (b << 1); break; }
                case 3: { C = 44; int cb = (v >> 1) & 3; B = (cb << 7) | (cb << 2) | cb; break; }
                case 4: { C = 22; int dcb = (v >> 1) & 7; B = (dcb << 6) | dcb; break; }
                case 5: { C = 11; int edcb = (v >> 1) & 0xf; B = (edcb << 5) | (edcb >> 2); break; }
                default: { C = 5; int fedcb = (v >> 1) & 0x1f; B = (fedcb << 4) | (fedcb >> 4); break; }
            }
        } else {
            switch (e.bits) {
                case 1: C = 113; break;
                case 2: { C = 54; int b = (v >> 1) & 1; B = (b << 8) | (b << 3) | (b << 2); break; }
                case 3: { C = 26; int cb = (v >> 1) & 3; B = (cb << 7) | (cb << 1) | (cb >> 1); break; }
                case 4: { C = 13; int dcb = (v >> 1) & 7; B = (dcb << 6) | (dcb >> 1); break; }
                default: { C = 6; int edcb = (v >> 1) & 0xf; B = (edcb << 5) | (edcb >> 3); break; }
            }
        }
        int T = D * C + B;
        T ^= A;
        out[written++] = (A & 0x80) | (T >> 2);
    }
}

struct Pixel {
    int c[4];  /* a, r, g, b, as the original indexes them */
};
inline Pixel px(int a, int r, int g, int b) { return {{a, r, g, b}}; }
inline Pixel blue_contract(int a, int r, int g, int b) { return px(a, (r + b) >> 1, (g + b) >> 1, b); }
inline void clamp(Pixel& p) {
    for (int& v : p.c) v = std::min(255, std::max(0, v));
}
inline void bit_transfer_signed(int& a, int& b) {
    b >>= 1;
    b |= a & 0x80;
    a >>= 1;
    a &= 0x3f;
    if (a & 0x20) a -= 0x40;
}

inline bool endpoints(Pixel* ep, const int* v, int mode) {
    switch (mode) {
        case 0: ep[0] = px(255, v[0], v[0], v[0]); ep[1] = px(255, v[1], v[1], v[1]); return true;
        case 1: {
            int l0 = (v[0] >> 2) | (v[1] & 0xc0);
            int l1 = std::min(l0 + (v[1] & 0x3f), 0xff);
            ep[0] = px(255, l0, l0, l0);
            ep[1] = px(255, l1, l1, l1);
            return true;
        }
        case 4: ep[0] = px(v[2], v[0], v[0], v[0]); ep[1] = px(v[3], v[1], v[1], v[1]); return true;
        case 5: {
            int a0 = v[0], a1 = v[1], a2 = v[2], a3 = v[3];
            bit_transfer_signed(a1, a0);
            bit_transfer_signed(a3, a2);
            ep[0] = px(a2, a0, a0, a0);
            ep[1] = px(a2 + a3, a0 + a1, a0 + a1, a0 + a1);
            clamp(ep[0]);
            clamp(ep[1]);
            return true;
        }
        case 6:
            ep[0] = px(255, v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8);
            ep[1] = px(255, v[0], v[1], v[2]);
            return true;
        case 8:
            if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
                ep[0] = px(255, v[0], v[2], v[4]);
                ep[1] = px(255, v[1], v[3], v[5]);
            } else {
                ep[0] = blue_contract(255, v[1], v[3], v[5]);
                ep[1] = blue_contract(255, v[0], v[2], v[4]);
            }
            return true;
        case 9: {
            int a[6];
            std::memcpy(a, v, sizeof(a));
            bit_transfer_signed(a[1], a[0]);
            bit_transfer_signed(a[3], a[2]);
            bit_transfer_signed(a[5], a[4]);
            if (a[1] + a[3] + a[5] >= 0) {
                ep[0] = px(255, a[0], a[2], a[4]);
                ep[1] = px(255, a[0] + a[1], a[2] + a[3], a[4] + a[5]);
            } else {
                ep[0] = blue_contract(255, a[0] + a[1], a[2] + a[3], a[4] + a[5]);
                ep[1] = blue_contract(255, a[0], a[2], a[4]);
            }
            clamp(ep[0]);
            clamp(ep[1]);
            return true;
        }
        case 10:
            ep[0] = px(v[4], v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8);
            ep[1] = px(v[5], v[0], v[1], v[2]);
            return true;
        case 12:
            if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
                ep[0] = px(v[6], v[0], v[2], v[4]);
                ep[1] = px(v[7], v[1], v[3], v[5]);
            } else {
                ep[0] = blue_contract(v[7], v[1], v[3], v[5]);
                ep[1] = blue_contract(v[6], v[0], v[2], v[4]);
            }
            return true;
        case 13: {
            int a[8];
            std::memcpy(a, v, sizeof(a));
            bit_transfer_signed(a[1], a[0]);
            bit_transfer_signed(a[3], a[2]);
            bit_transfer_signed(a[5], a[4]);
            bit_transfer_signed(a[7], a[6]);
            if (a[1] + a[3] + a[5] >= 0) {
                ep[0] = px(a[6], a[0], a[2], a[4]);
                ep[1] = px(a[7] + a[6], a[0] + a[1], a[2] + a[3], a[4] + a[5]);
            } else {
                ep[0] = blue_contract(a[6] + a[7], a[0] + a[1], a[2] + a[3], a[4] + a[5]);
                ep[1] = blue_contract(a[6], a[0], a[2], a[4]);
            }
            clamp(ep[0]);
            clamp(ep[1]);
            return true;
        }
        default: return false;  /* HDR */
    }
}

inline uint32_t hash52(uint32_t v) {
    v ^= v >> 15; v -= v << 17; v += v << 7; v += v << 4;
    v ^= v >> 5; v += v << 16; v ^= v >> 7; v ^= v >> 3;
    v ^= v << 6; v ^= v >> 17;
    return v;
}

/* The partition selection function (spec C.2.21), with the per-seed part
   computed once per block rather than once per texel. */
struct PartitionHash {
    int count = 1;
    bool small = false;
    uint32_t rn = 0;
    uint8_t s[13] = {};
    PartitionHash(int seed, int partitions, bool small_block) : count(partitions), small(small_block) {
        if (count == 1) return;
        seed += (count - 1) * 1024;
        rn = hash52((uint32_t)seed);
        s[1] = rn & 0xf; s[2] = (rn >> 4) & 0xf; s[3] = (rn >> 8) & 0xf; s[4] = (rn >> 12) & 0xf;
        s[5] = (rn >> 16) & 0xf; s[6] = (rn >> 20) & 0xf; s[7] = (rn >> 24) & 0xf; s[8] = (rn >> 28) & 0xf;
        s[9] = (rn >> 18) & 0xf; s[10] = (rn >> 22) & 0xf; s[11] = (rn >> 26) & 0xf;
        s[12] = ((rn >> 30) | (rn << 2)) & 0xf;
        for (int i = 1; i <= 12; ++i) s[i] = (uint8_t)(s[i] * s[i]);
        int sh1, sh2;
        if (seed & 1) {
            sh1 = (seed & 2) ? 4 : 5;
            sh2 = count == 3 ? 6 : 5;
        } else {
            sh1 = count == 3 ? 6 : 5;
            sh2 = (seed & 2) ? 4 : 5;
        }
        int sh3 = (seed & 0x10) ? sh1 : sh2;
        s[1] >>= sh1; s[2] >>= sh2; s[3] >>= sh1; s[4] >>= sh2;
        s[5] >>= sh1; s[6] >>= sh2; s[7] >>= sh1; s[8] >>= sh2;
        s[9] >>= sh3; s[10] >>= sh3; s[11] >>= sh3; s[12] >>= sh3;
    }
    int at(int x, int y) const {
        if (count == 1) return 0;
        if (small) {
            x <<= 1;
            y <<= 1;
        }
        int a = (s[1] * x + s[2] * y + (int)(rn >> 14)) & 0x3f;
        int b = (s[3] * x + s[4] * y + (int)(rn >> 10)) & 0x3f;
        int c = (s[5] * x + s[6] * y + (int)(rn >> 6)) & 0x3f;
        int d = (s[7] * x + s[8] * y + (int)(rn >> 2)) & 0x3f;
        if (count < 4) d = 0;
        if (count < 3) c = 0;
        if (a >= b && a >= c && a >= d) return 0;
        if (b >= c && b >= d) return 1;
        if (c >= d) return 2;
        return 3;
    }
};

inline uint8_t reverse_byte(uint8_t b) {
    return (uint8_t)((((b * 0x80200802ull) & 0x0884422110ull) * 0x0101010101ull) >> 32);
}

inline bool decode(const uint8_t* block, int bw, int bh, bool srgb, uint32_t* out) {
    Bits in;
    std::memcpy(&in.lo, block, 8);
    std::memcpy(&in.hi, block + 8, 8);
    Params p = block_params(in);
    if (p.error || p.void_hdr) return false;
    if (p.void_ldr) {
        in.read(52);
        uint32_t r = (uint32_t)in.read(16), g = (uint32_t)in.read(16), b = (uint32_t)in.read(16),
                 a = (uint32_t)in.read(16);
        uint32_t rgba = (r >> 8) | (g & 0xff00) | ((b & 0xff00) << 8) | ((a & 0xff00) << 16);
        for (int i = 0; i < bw * bh; ++i) out[i] = rgba;
        return true;
    }
    if (p.width > bw || p.height > bh) return false;
    int partitions = in.read(2) + 1;
    if (partitions == 4 && p.dual) return false;
    int modes[4] = {0, 0, 0, 0};
    int partition_index = 0, base_cem = 0;
    if (partitions == 1) {
        modes[0] = in.read(4);
    } else {
        partition_index = in.read(10);
        base_cem = in.read(6);
    }
    int base_mode = base_cem & 3;
    int weight_bits = p.packed_bits();
    if (weight_bits > 96) return false;
    int remaining = 128 - weight_bits - in.pos;
    int extra_cem_bits = 0;
    if (base_mode) extra_cem_bits = partitions == 2 ? 2 : partitions == 3 ? 5 : partitions == 4 ? 8 : 0;
    remaining -= extra_cem_bits;
    int plane_bits = p.dual ? 2 : 0;
    remaining -= plane_bits;
    if (remaining < 0) return false;
    int color_bits = remaining;
    Bits color;
    {
        int start = in.pos;
        /* The 128-bit block shifted down by start, cut to color_bits. */
        uint64_t lo = in.lo, hi = in.hi;
        if (start >= 64) {
            lo = hi >> (start - 64);
            hi = 0;
        } else if (start > 0) {
            lo = (lo >> start) | (hi << (64 - start));
            hi >>= start;
        }
        if (color_bits < 64) {
            lo &= color_bits ? (~0ull >> (64 - color_bits)) : 0;
            hi = 0;
        } else if (color_bits < 128) {
            hi &= color_bits > 64 ? (~0ull >> (128 - color_bits)) : 0;
        }
        color.lo = lo;
        color.hi = hi;
        in.pos += color_bits;
    }
    int plane_index = in.read(plane_bits);
    if (base_mode) {
        int extra = in.read(extra_cem_bits);
        int temp = ((extra << 6) | base_cem) >> 2;
        int C[4], M[4];
        for (int i = 0; i < partitions; ++i) { C[i] = temp & 1; temp >>= 1; }
        for (int i = 0; i < partitions; ++i) { M[i] = temp & 3; temp >>= 2; }
        for (int i = 0; i < partitions; ++i) modes[i] = ((base_mode - (C[i] ? 0 : 1)) << 2) | M[i];
    } else if (partitions > 1) {
        for (int i = 0; i < partitions; ++i) modes[i] = base_cem >> 2;
    }
    int count = 0;
    for (int i = 0; i < partitions; ++i) count += ((modes[i] >> 2) + 1) << 1;
    if (count > 18) return false;
    int values[40] = {};
    decode_color_values(values, color, modes, partitions, color_bits);
    Pixel ep[4][2];
    int at = 0;
    for (int i = 0; i < partitions; ++i) {
        if (!endpoints(ep[i], values + at, modes[i])) return false;
        at += ((modes[i] >> 2) + 1) << 1;
    }
    /* The weights run from the top of the block down, bit reversed. */
    uint8_t w[16];
    for (int i = 0; i < 16; ++i) w[i] = reverse_byte(block[15 - i]);
    int clear_start = (weight_bits >> 3) + 1;
    w[clear_start - 1] &= (uint8_t)((1 << (weight_bits % 8)) - 1);
    for (int i = clear_start; i < 16; ++i) w[i] = 0;
    Bits weights_in;
    std::memcpy(&weights_in.lo, w, 8);
    std::memcpy(&weights_in.hi, w + 8, 8);
    Encoded encoded[160];
    int weight_count = decode_sequence(encoded, weights_in, p.max_weight, p.weight_count());
    int unq[2][144] = {};
    {
        int index = 0;
        for (int i = 0; i < weight_count; ++i) {
            unq[0][index] = unquantize_weight(encoded[i]);
            if (p.dual) {
                if (++i >= weight_count) break;
                unq[1][index] = unquantize_weight(encoded[i]);
            }
            if (++index >= p.width * p.height) break;
        }
    }
    int weights[2][144];
    int Ds = (1024 + bw / 2) / (bw - 1), Dt = (1024 + bh / 2) / (bh - 1);
    int grid = p.width * p.height;
    for (int plane = 0; plane < (p.dual ? 2 : 1); ++plane)
        for (int t = 0; t < bh; ++t)
            for (int s = 0; s < bw; ++s) {
                int gs = (Ds * s * (p.width - 1) + 32) >> 6, gt = (Dt * t * (p.height - 1) + 32) >> 6;
                int js = gs >> 4, fs = gs & 0xf, jt = gt >> 4, ft = gt & 0xf;
                int w11 = (fs * ft + 8) >> 4, w10 = ft - w11, w01 = fs - w11, w00 = 16 - fs - ft + w11;
                int v0 = js + jt * p.width;
                int p00 = v0 < grid ? unq[plane][v0] : 0;
                int p01 = v0 + 1 < grid ? unq[plane][v0 + 1] : 0;
                int p10 = v0 + p.width < grid ? unq[plane][v0 + p.width] : 0;
                int p11 = v0 + p.width + 1 < grid ? unq[plane][v0 + p.width + 1] : 0;
                weights[plane][t * bw + s] = (p00 * w00 + p01 * w01 + p10 * w10 + p11 * w11 + 8) >> 4;
            }
    /* Endpoints per partition and channel, clamped and expanded once: sRGB
       decoding interpolates the 8-bit endpoints directly, alpha included, as
       ARM's reference decoder does; linear decoding replicates them to 16
       bits (e * 257) and keeps the top byte of the result. */
    int lo[4][4], hi[4][4];
    for (int part = 0; part < partitions; ++part)
        for (int comp = 0; comp < 4; ++comp) {
            int e0 = std::min(255, std::max(0, ep[part][0].c[comp])), e1 = std::min(255, std::max(0, ep[part][1].c[comp]));
            lo[part][comp] = srgb ? e0 : e0 * 257;
            hi[part][comp] = srgb ? e1 : e1 * 257;
        }
    const int dual_comp = p.dual ? (plane_index + 1) & 3 : -1;
    const PartitionHash hash(partition_index, partitions, bw * bh < 31);
    for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i) {
            const int part = hash.at(i, j), texel = j * bw + i;
            int c[4];
            for (int comp = 0; comp < 4; ++comp) {
                const int weight = weights[comp == dual_comp ? 1 : 0][texel];
                const int f = lo[part][comp] * (64 - weight) + hi[part][comp] * weight + 32;
                c[comp] = srgb ? f >> 6 : (f >> 6) >> 8;
            }
            /* c is a, r, g, b; out is RGBA8 in memory order. */
            out[texel] = (uint32_t)c[1] | ((uint32_t)c[2] << 8) | ((uint32_t)c[3] << 16) | ((uint32_t)c[0] << 24);
        }
    return true;
}


// The block size of a Vulkan ASTC LDR format (VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
// 157, through VK_FORMAT_ASTC_12x12_SRGB_BLOCK, 184, in UNORM/SRGB pairs), or
// false for any other format.
inline bool block_size(uint32_t vkFormat, int* blockWidth, int* blockHeight, bool* srgb) {
    static const int dims[14][2] = {{4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
                                    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};
    if (vkFormat < 157 || vkFormat > 184) return false;
    const int index = int(vkFormat - 157);
    *blockWidth = dims[index / 2][0];
    *blockHeight = dims[index / 2][1];
    *srgb = index & 1;
    return true;
}

// Decodes one 16-byte block to blockWidth x blockHeight RGBA8 texels (row
// major); invalid and HDR blocks decode to magenta.
inline void decode_block(const uint8_t* block, int blockWidth, int blockHeight, bool srgb, uint32_t* texels) {
    if (!decode(block, blockWidth, blockHeight, srgb, texels))
        for (int i = 0; i < blockWidth * blockHeight; ++i) texels[i] = 0xffff00ffu;
}

} // namespace axrb::texture::astc
