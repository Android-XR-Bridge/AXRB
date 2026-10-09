// ASTC LDR block decoder (Khronos Data Format Specification, ASTC chapter), used to transcode ASTC textures to BC3.
// 2D blocks only. HDR endpoint modes, HDR void extents and reserved encodings decode to the error colour (magenta),
// as an LDR-only decoder should. Output: block width x height RGBA8 texels, row-major.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

namespace refract::astc {

namespace detail {

struct Level { uint8_t bits, trits, quints; uint16_t count; };
// Integer sequence encodings, in increasing range. Weights use the first 12, colour endpoints all 21.
constexpr Level kLevels[21] = {
    {1, 0, 0, 2},   {0, 1, 0, 3},   {2, 0, 0, 4},   {0, 0, 1, 5},   {1, 1, 0, 6},   {3, 0, 0, 8},   {1, 0, 1, 10},
    {2, 1, 0, 12},  {4, 0, 0, 16},  {2, 0, 1, 20},  {3, 1, 0, 24},  {5, 0, 0, 32},  {3, 0, 1, 40},  {4, 1, 0, 48},
    {6, 0, 0, 64},  {4, 0, 1, 80},  {5, 1, 0, 96},  {7, 0, 0, 128}, {5, 0, 1, 160}, {6, 1, 0, 192}, {8, 0, 0, 256}};

inline int ise_bits(int count, int level) {
    const Level& l = kLevels[level];
    return l.bits * count + (l.trits ? (8 * count + 4) / 5 : 0) + (l.quints ? (7 * count + 2) / 3 : 0);
}

// Unquantisation tables: colour values to 0..255, weights to 0..64.
struct Tables {
    uint8_t color[21][256];
    uint8_t weight[12][32];
    Tables() {
        for (int level = 0; level < 21; ++level) {
            const Level& l = kLevels[level];
            for (int v = 0; v < l.count; ++v) {
                if (!l.trits && !l.quints) {  // bit replication to 8 bits
                    int out = 0;
                    for (int shift = 8 - l.bits; shift > -l.bits; shift -= l.bits) out |= shift >= 0 ? v << shift : v >> -shift;
                    color[level][v] = static_cast<uint8_t>(out & 255);
                    continue;
                }
                const int n = l.bits, m = v & ((1 << n) - 1), d = v >> n;
                const int a = (m & 1) ? 0x1FF : 0, b = m >> 1 & 1, c = m >> 2 & 1, dd = m >> 3 & 1, e = m >> 4 & 1, f = m >> 5 & 1;
                int B = 0, C = 0;
                if (l.trits) {
                    switch (n) {
                    case 1: C = 204; break;
                    case 2: C = 93; B = b << 8 | b << 4 | b << 2 | b << 1; break;
                    case 3: C = 44; B = c << 8 | b << 7 | c << 3 | b << 2 | c << 1 | b; break;
                    case 4: C = 22; B = dd << 8 | c << 7 | b << 6 | dd << 2 | c << 1 | b; break;
                    case 5: C = 11; B = e << 8 | dd << 7 | c << 6 | b << 5 | e << 1 | dd; break;
                    case 6: C = 5; B = f << 8 | e << 7 | dd << 6 | c << 5 | b << 4 | f; break;
                    }
                } else {
                    switch (n) {
                    case 1: C = 113; break;
                    case 2: C = 54; B = b << 8 | b << 3 | b << 2; break;
                    case 3: C = 26; B = c << 8 | b << 7 | c << 2 | b << 1 | c; break;
                    case 4: C = 13; B = dd << 8 | c << 7 | b << 6 | dd << 1 | c; break;
                    case 5: C = 6; B = e << 8 | dd << 7 | c << 6 | b << 5 | e; break;
                    }
                }
                int t = d * C + B;
                t ^= a;
                color[level][v] = static_cast<uint8_t>((a & 0x80) | (t >> 2 & 0x7F));
            }
        }
        for (int level = 0; level < 12; ++level) {
            const Level& l = kLevels[level];
            for (int v = 0; v < l.count; ++v) {
                int out;
                if (!l.trits && !l.quints) {
                    out = 0;
                    for (int shift = 6 - l.bits; shift > -l.bits; shift -= l.bits) out |= shift >= 0 ? v << shift : v >> -shift;
                    out &= 63;
                } else if (l.bits == 0) {
                    static constexpr uint8_t trit[3] = {0, 32, 63}, quint[5] = {0, 16, 32, 47, 63};
                    out = l.trits ? trit[v] : quint[v];
                } else {
                    const int n = l.bits, m = v & ((1 << n) - 1), d = v >> n;
                    const int a = (m & 1) ? 0x7F : 0, b = m >> 1 & 1, c = m >> 2 & 1;
                    int B = 0, C = 0;
                    if (l.trits) {
                        if (n == 1) C = 50;
                        else if (n == 2) { C = 23; B = b << 6 | b << 2 | b; }
                        else { C = 11; B = c << 6 | b << 5 | c << 1 | b; }
                    } else {
                        if (n == 1) C = 28;
                        else { C = 13; B = b << 6 | b << 1; }
                    }
                    int t = d * C + B;
                    t ^= a;
                    out = (a & 0x20) | (t >> 2);
                }
                weight[level][v] = static_cast<uint8_t>(out > 32 ? out + 1 : out);
            }
        }
    }
};
inline const Tables& tables() { static const Tables t; return t; }

// 128-bit block read LSB-first; reads past `end` return zero bits (truncated integer sequences).
struct Bits {
    uint64_t lo, hi;
    uint32_t get(int start, int count, int end = 128) const {  // count <= 32
        const int n = std::min(start + count, end) - start;
        if (n <= 0) return 0;
        const uint64_t word = start >= 64 ? hi >> (start - 64) : start ? (lo >> start | hi << (64 - start)) : lo;
        return static_cast<uint32_t>(word & ((uint64_t{1} << n) - 1));
    }
};
inline uint64_t reverse64(uint64_t v) {
    v = (v >> 1 & 0x5555555555555555ull) | (v & 0x5555555555555555ull) << 1;
    v = (v >> 2 & 0x3333333333333333ull) | (v & 0x3333333333333333ull) << 2;
    v = (v >> 4 & 0x0F0F0F0F0F0F0F0Full) | (v & 0x0F0F0F0F0F0F0F0Full) << 4;
    v = (v >> 8 & 0x00FF00FF00FF00FFull) | (v & 0x00FF00FF00FF00FFull) << 8;
    v = (v >> 16 & 0x0000FFFF0000FFFFull) | (v & 0x0000FFFF0000FFFFull) << 16;
    return v >> 32 | v << 32;
}

// Decodes `count` integers of range `level` starting at bit `start`.
inline void decode_ise(const Bits& bits, int start, int count, int level, uint8_t* out) {
    const Level& l = kLevels[level];
    const int n = l.bits, end = start + ise_bits(count, level);
    int p = start;
    auto read = [&](int k) { const uint32_t v = bits.get(p, k, end); p += k; return v; };
    if (l.trits) {
        for (int i = 0; i < count; i += 5) {
            uint32_t m[5], T = 0;
            m[0] = read(n); T |= read(2);
            m[1] = read(n); T |= read(2) << 2;
            m[2] = read(n); T |= read(1) << 4;
            m[3] = read(n); T |= read(2) << 5;
            m[4] = read(n); T |= read(1) << 7;
            int t[5], C;
            if ((T >> 2 & 7) == 7) { C = static_cast<int>((T >> 5 & 7) << 2 | (T & 3)); t[4] = 2; t[3] = 2; }
            else {
                C = static_cast<int>(T & 31);
                if ((T >> 5 & 3) == 3) { t[4] = 2; t[3] = T >> 7 & 1; }
                else { t[4] = T >> 7 & 1; t[3] = T >> 5 & 3; }
            }
            if ((C & 3) == 3) { t[2] = 2; t[1] = C >> 4 & 1; t[0] = (C >> 3 & 1) << 1 | ((C >> 2 & 1) & ~(C >> 3) & 1); }
            else if ((C >> 2 & 3) == 3) { t[2] = 2; t[1] = 2; t[0] = C & 3; }
            else { t[2] = C >> 4 & 1; t[1] = C >> 2 & 3; t[0] = (C >> 1 & 1) << 1 | ((C & 1) & ~(C >> 1) & 1); }
            for (int k = 0; k < 5 && i + k < count; ++k) out[i + k] = static_cast<uint8_t>(t[k] << n | m[k]);
        }
    } else if (l.quints) {
        for (int i = 0; i < count; i += 3) {
            uint32_t m[3], Q = 0;
            m[0] = read(n); Q |= read(3);
            m[1] = read(n); Q |= read(2) << 3;
            m[2] = read(n); Q |= read(2) << 5;
            int q[3];
            if ((Q >> 1 & 3) == 3 && (Q >> 5 & 3) == 0) {
                q[2] = static_cast<int>((Q & 1) << 2 | ((Q >> 4 & 1) & ~Q & 1) << 1 | ((Q >> 3 & 1) & ~Q & 1));
                q[1] = 4; q[0] = 4;
            } else {
                int C;
                if ((Q >> 1 & 3) == 3) { q[2] = 4; C = static_cast<int>((Q >> 3 & 3) << 3 | (~Q >> 5 & 3) << 1 | (Q & 1)); }
                else { q[2] = Q >> 5 & 3; C = static_cast<int>(Q & 31); }
                if ((C & 7) == 5) { q[1] = 4; q[0] = C >> 3 & 3; }
                else { q[1] = C >> 3 & 3; q[0] = C & 7; }
            }
            for (int k = 0; k < 3 && i + k < count; ++k) out[i + k] = static_cast<uint8_t>(q[k] << n | m[k]);
        }
    } else {
        for (int i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(read(n));
    }
}

inline uint32_t hash52(uint32_t p) {
    p ^= p >> 15; p -= p << 17; p += p << 7; p += p << 4; p ^= p >> 5;
    p += p << 16; p ^= p >> 7; p ^= p >> 3; p ^= p << 6; p ^= p >> 17;
    return p;
}
// Partition selection for one block's seed; the hash and shifts are per block, only the linear forms per texel.
struct PartitionSelector {
    uint32_t s[8], rnum;
    int partitions;
    bool small;
    PartitionSelector(int seed, int partitionCount, bool smallBlock) : partitions(partitionCount), small(smallBlock) {
        seed += (partitions - 1) * 1024;
        rnum = hash52(static_cast<uint32_t>(seed));
        for (int i = 0; i < 8; ++i) { s[i] = rnum >> (4 * i) & 15; s[i] *= s[i]; }
        int sh1, sh2;
        if (seed & 1) { sh1 = (seed & 2) ? 4 : 5; sh2 = partitions == 3 ? 6 : 5; }
        else { sh1 = partitions == 3 ? 6 : 5; sh2 = (seed & 2) ? 4 : 5; }
        for (int i = 0; i < 8; ++i) s[i] >>= (i & 1) ? sh2 : sh1;  // s[8..11] only scale z, which is 0 in 2D
    }
    int operator()(int x, int y) const {
        if (small) { x <<= 1; y <<= 1; }
        const int a = static_cast<int>((s[0] * x + s[1] * y + (rnum >> 14)) & 0x3F);
        const int b = static_cast<int>((s[2] * x + s[3] * y + (rnum >> 10)) & 0x3F);
        const int c = partitions < 3 ? 0 : static_cast<int>((s[4] * x + s[5] * y + (rnum >> 6)) & 0x3F);
        const int d = partitions < 4 ? 0 : static_cast<int>((s[6] * x + s[7] * y + (rnum >> 2)) & 0x3F);
        if (a >= b && a >= c && a >= d) return 0;
        if (b >= c && b >= d) return 1;
        if (c >= d) return 2;
        return 3;
    }
};

// Bilinear weight-grid infill for one footprint and grid size: per texel, four grid indices and their weights.
struct Infill {
    uint8_t index[144][4], weight[144][4];
    Infill(int bw, int bh, int gw, int gh) {
        const int ds = (1024 + bw / 2) / (bw - 1), dt = (1024 + bh / 2) / (bh - 1);
        for (int y = 0; y < bh; ++y) for (int x = 0; x < bw; ++x) {
            const int gs = (ds * x * (gw - 1) + 32) >> 6, gt = (dt * y * (gh - 1) + 32) >> 6;
            const int js = gs >> 4, fs = gs & 15, jt = gt >> 4, ft = gt & 15;
            const int w11 = (fs * ft + 8) >> 4, w[4] = {16 - fs - ft + w11, fs - w11, ft - w11, w11};
            const int v0 = js + jt * gw, v[4] = {v0, v0 + 1, v0 + gw, v0 + gw + 1};
            for (int k = 0; k < 4; ++k) {  // past the grid only with weight 0
                const bool inside = v[k] < gw * gh;
                index[y * bw + x][k] = static_cast<uint8_t>(inside ? v[k] : 0);
                weight[y * bw + x][k] = static_cast<uint8_t>(inside ? w[k] : 0);
            }
        }
    }
};
// Per thread, the tables of the footprint in use (a texture has one footprint and few grid sizes).
inline const Infill& infill(int bw, int bh, int gw, int gh) {
    thread_local int footprint = 0;
    thread_local std::unique_ptr<Infill> cache[13][13];
    if (footprint != (bw << 8 | bh)) {
        for (auto& row : cache) for (auto& entry : row) entry.reset();
        footprint = bw << 8 | bh;
    }
    auto& entry = cache[gw][gh];
    if (!entry) entry = std::make_unique<Infill>(bw, bh, gw, gh);
    return *entry;
}

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
inline void bit_transfer_signed(int& a, int& b) {
    b >>= 1; b |= a & 0x80; a >>= 1; a &= 0x3F;
    if (a & 0x20) a -= 0x40;
}
// LDR endpoint modes; false for HDR modes.
inline bool decode_endpoints(int mode, const uint8_t* v, int e0[4], int e1[4]) {
    auto set = [](int* e, int r, int g, int b, int a) { e[0] = clamp255(r); e[1] = clamp255(g); e[2] = clamp255(b); e[3] = clamp255(a); };
    auto contract = [&](int* e, int r, int g, int b, int a) { set(e, (r + b) >> 1, (g + b) >> 1, b, a); };
    int a0, a1, a2, a3, b0, b1, b2, b3;
    switch (mode) {
    case 0: set(e0, v[0], v[0], v[0], 255); set(e1, v[1], v[1], v[1], 255); return true;
    case 1: {
        const int l0 = (v[0] >> 2) | (v[1] & 0xC0), l1 = std::min(l0 + (v[1] & 0x3F), 255);
        set(e0, l0, l0, l0, 255); set(e1, l1, l1, l1, 255); return true;
    }
    case 4: set(e0, v[0], v[0], v[0], v[2]); set(e1, v[1], v[1], v[1], v[3]); return true;
    case 5:
        a0 = v[0]; b0 = v[1]; a1 = v[2]; b1 = v[3];
        bit_transfer_signed(b0, a0); bit_transfer_signed(b1, a1);
        set(e0, a0, a0, a0, a1); set(e1, a0 + b0, a0 + b0, a0 + b0, a1 + b1); return true;
    case 6: set(e0, v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8, 255); set(e1, v[0], v[1], v[2], 255); return true;
    case 8: case 12: {
        const int alpha0 = mode == 12 ? v[6] : 255, alpha1 = mode == 12 ? v[7] : 255;
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) { set(e0, v[0], v[2], v[4], alpha0); set(e1, v[1], v[3], v[5], alpha1); }
        else { contract(e0, v[1], v[3], v[5], alpha1); contract(e1, v[0], v[2], v[4], alpha0); }
        return true;
    }
    case 9: case 13: {
        a0 = v[0]; b0 = v[1]; a1 = v[2]; b1 = v[3]; a2 = v[4]; b2 = v[5];
        a3 = mode == 13 ? v[6] : 255; b3 = mode == 13 ? v[7] : 0;
        bit_transfer_signed(b0, a0); bit_transfer_signed(b1, a1); bit_transfer_signed(b2, a2);
        if (mode == 13) bit_transfer_signed(b3, a3);
        if (b0 + b1 + b2 >= 0) { set(e0, a0, a1, a2, a3); set(e1, a0 + b0, a1 + b1, a2 + b2, a3 + b3); }
        else { contract(e0, a0 + b0, a1 + b1, a2 + b2, a3 + b3); contract(e1, a0, a1, a2, a3); }
        return true;
    }
    case 10:
        set(e0, v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8, v[4]); set(e1, v[0], v[1], v[2], v[5]); return true;
    default: return false;
    }
}

// Why the last block on this thread decoded to the error colour (diagnostics for the offline check).
inline thread_local const char* last_error = nullptr;

inline void fill(uint8_t (*out)[4], int texels, int r, int g, int b, int a) {
    for (int i = 0; i < texels; ++i) { out[i][0] = static_cast<uint8_t>(r); out[i][1] = static_cast<uint8_t>(g); out[i][2] = static_cast<uint8_t>(b); out[i][3] = static_cast<uint8_t>(a); }
}

}  // namespace detail

// Decodes one 16-byte block of a bw x bh footprint into `out` (bw*bh texels). sRGB formats interpolate with the
// sRGB endpoint expansion; returns false (and writes magenta) for error blocks.
inline bool decode_block(const uint8_t* src, int bw, int bh, bool srgb, uint8_t (*out)[4]) {
    using namespace detail;
    const int texels = bw * bh;
    Bits bits{};
    std::memcpy(&bits.lo, src, 8); std::memcpy(&bits.hi, src + 8, 8);
    auto error = [&](const char* why) { last_error = why; fill(out, texels, 255, 0, 255, 255); return false; };
    const uint32_t mode = bits.get(0, 11);
    if ((mode & 0x1FF) == 0x1FC) {  // void extent
        if (mode & 0x200) return error("hdr void extent");
        fill(out, texels, bits.get(64, 16) >> 8, bits.get(80, 16) >> 8, bits.get(96, 16) >> 8, bits.get(112, 16) >> 8);
        return true;
    }
    int gw, gh, quant = mode >> 4 & 1, hbit = mode >> 9 & 1, dual = mode >> 10 & 1;
    const int A = mode >> 5 & 3;
    if (mode & 3) {
        quant |= (mode & 3) << 1;
        int B = mode >> 7 & 3;
        switch (mode >> 2 & 3) {
        case 0: gw = B + 4; gh = A + 2; break;
        case 1: gw = B + 8; gh = A + 2; break;
        case 2: gw = A + 2; gh = B + 8; break;
        default:
            B &= 1;
            if (mode & 0x100) { gw = B + 2; gh = A + 2; } else { gw = A + 2; gh = B + 6; }
        }
    } else {
        quant |= (mode >> 2 & 3) << 1;
        if (!(mode >> 2 & 3)) return error("reserved block mode");
        const int B = mode >> 9 & 3;
        switch (mode >> 7 & 3) {
        case 0: gw = 12; gh = A + 2; break;
        case 1: gw = A + 2; gh = 12; break;
        case 2: gw = A + 6; gh = B + 6; dual = 0; hbit = 0; break;
        default:
            if (A == 0) { gw = 6; gh = 10; } else if (A == 1) { gw = 10; gh = 6; } else return error("reserved block mode");
        }
    }
    const int weightLevel = quant - 2 + 6 * hbit;
    const int weightCount = gw * gh * (dual + 1);
    if (gw > bw || gh > bh || weightCount > 64) return error("weight grid");
    const int weightBits = ise_bits(weightCount, weightLevel);
    if (weightBits < 24 || weightBits > 96) return error("weight bits");
    const int partitions = static_cast<int>(bits.get(11, 2)) + 1;
    if (dual && partitions == 4) return error("dual plane with 4 partitions");

    int below = 128 - weightBits, colorStart, seed = 0, cem[4];
    if (partitions == 1) { cem[0] = static_cast<int>(bits.get(13, 4)); colorStart = 17; }
    else {
        seed = static_cast<int>(bits.get(13, 10)); colorStart = 29;
        const uint32_t selector = bits.get(23, 6);
        if (!(selector & 3)) for (int i = 0; i < partitions; ++i) cem[i] = static_cast<int>(selector >> 2);
        else {
            const int extra = 3 * partitions - 4;
            below -= extra;
            const uint32_t encoded = selector | bits.get(below, extra) << 6;
            const int base = static_cast<int>(selector & 3) - 1;
            for (int i = 0; i < partitions; ++i)
                cem[i] = (base + static_cast<int>(encoded >> (2 + i) & 1)) << 2 | static_cast<int>(encoded >> (2 + partitions + 2 * i) & 3);
        }
    }
    int plane2 = -1;
    if (dual) { below -= 2; plane2 = static_cast<int>(bits.get(below, 2)); }

    int valueCount = 0;
    for (int i = 0; i < partitions; ++i) valueCount += 2 * ((cem[i] >> 2) + 1);
    const int colorBits = below - colorStart;
    if (valueCount > 18 || colorBits < (13 * valueCount + 4) / 5) return error("colour bits");
    int colorLevel = 20;
    while (colorLevel > 0 && ise_bits(valueCount, colorLevel) > colorBits) --colorLevel;
    uint8_t values[18];
    decode_ise(bits, colorStart, valueCount, colorLevel, values);
    const auto& t = tables();
    for (int i = 0; i < valueCount; ++i) values[i] = t.color[colorLevel][values[i]];
    int e0[4][4], e1[4][4];
    for (int i = 0, at = 0; i < partitions; ++i) {
        if (!decode_endpoints(cem[i], values + at, e0[i], e1[i])) return error("hdr endpoints");
        at += 2 * ((cem[i] >> 2) + 1);
    }

    // Weights are stored bit-reversed from the top of the block.
    const Bits reversed{reverse64(bits.hi), reverse64(bits.lo)};
    uint8_t grid[64] = {};
    decode_ise(reversed, 0, weightCount, weightLevel, grid);
    for (int i = 0; i < weightCount; ++i) grid[i] = t.weight[weightLevel][grid[i]];

    // Endpoints as 16-bit values: sRGB formats expand c to c<<8|0x80, UNORM to c*257.
    int c0[4][4], c1[4][4];
    for (int i = 0; i < partitions; ++i) for (int c = 0; c < 4; ++c) {
        c0[i][c] = srgb ? (e0[i][c] << 8 | 0x80) : e0[i][c] * 257;
        c1[i][c] = srgb ? (e1[i][c] << 8 | 0x80) : e1[i][c] * 257;
    }
    const Infill& table = infill(bw, bh, gw, gh);
    const PartitionSelector select(seed, partitions, texels < 31);
    const int planes = dual + 1;
    for (int y = 0, i = 0; y < bh; ++y) for (int x = 0; x < bw; ++x, ++i) {
        const uint8_t* at = table.index[i];
        const uint8_t* w = table.weight[i];
        int weight[2];
        for (int p = 0; p < planes; ++p)
            weight[p] = (grid[at[0] * planes + p] * w[0] + grid[at[1] * planes + p] * w[1] +
                         grid[at[2] * planes + p] * w[2] + grid[at[3] * planes + p] * w[3] + 8) >> 4;
        const int part = partitions > 1 ? select(x, y) : 0;
        uint8_t* texel = out[i];
        for (int c = 0; c < 4; ++c) {
            const int wc = c == plane2 ? weight[1] : weight[0];
            texel[c] = static_cast<uint8_t>(((c0[part][c] * (64 - wc) + c1[part][c] * wc + 32) >> 6) >> 8);
        }
    }
    return true;
}

}  // namespace refract::astc
