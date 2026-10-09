// ETC2/EAC -> BC block transcoding for the Android Vulkan layer (header-only, host-testable). ASTC (astc_decode.h)
// is decoded to pixels and re-encoded with encode_bc3, since its block footprint differs from BC's 4x4.
// NVIDIA desktop GPUs sample neither ETC2 nor ASTC, so gfxstream keeps every such texture twice: the original
// blocks plus an RGBA8 (or R16/RG16) decompressed copy, 5-9x the native size. ETC2 and BC share the 4x4 block
// grid and bytes per block, so each block is decoded and re-encoded in place:
//   ETC2 RGB -> BC1, ETC2 RGB A1 -> BC1 (punch-through alpha), ETC2 RGBA -> BC3, EAC R11 -> BC4, EAC RG11 -> BC5.
// Pixel arrays are row-major (index y*4+x); ETC stores its indices column-major, BC row-major.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace axrb::texture {

enum class Codec : uint8_t { None, Etc2Rgb, Etc2RgbA1, Etc2Rgba, EacR, EacRSigned, EacRg, EacRgSigned, Astc };

inline uint32_t block_bytes(Codec c) {
    return c == Codec::Etc2Rgba || c == Codec::EacRg || c == Codec::EacRgSigned ? 16 : 8;
}

namespace detail {

inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }
inline int ext4(int v) { return v * 17; }
inline int ext5(int v) { return v << 3 | v >> 2; }
inline int ext6(int v) { return v << 2 | v >> 4; }
inline int ext7(int v) { return v << 1 | v >> 6; }
inline uint64_t big_endian(const uint8_t* s, int n) { uint64_t b = 0; for (int i = 0; i < n; ++i) b = b << 8 | s[i]; return b; }

constexpr int kEtc1Modifiers[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
constexpr int kEtc2Distances[8] = {3, 6, 11, 16, 23, 32, 41, 64};
constexpr int kEacModifiers[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12},
    {-2, -4, -6, -13, 1, 3, 5, 12}, {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10}, {-2, -6, -8, -10, 1, 5, 7, 9},
    {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},
    {-3, -5, -7, -9, 2, 4, 6, 8}};

}  // namespace detail

// One ETC2 RGB (or RGB8A1 when punchthrough) block -> 16 RGBA8 pixels.
inline void decode_etc2_rgb(const uint8_t* s, uint8_t out[16][4], bool punchthrough) {
    using namespace detail;
    const uint64_t b = big_endian(s, 8);
    auto bits = [b](int hi, int lo) { return static_cast<int>((b >> lo) & ((1ull << (hi - lo + 1)) - 1)); };
    const bool flag = bits(33, 33);
    const bool diff = punchthrough || flag;
    const bool opaque = !punchthrough || flag;  // RGB8A1 reuses the diff bit as "opaque".
    const uint32_t lsb = bits(15, 0), msb = bits(31, 16);
    auto index = [&](int x, int y) { const int i = x * 4 + y; return static_cast<int>((msb >> i & 1) << 1 | (lsb >> i & 1)); };
    auto put = [&](int x, int y, int r, int g, int bl, int a) {
        uint8_t* p = out[y * 4 + x]; p[0] = clamp255(r); p[1] = clamp255(g); p[2] = clamp255(bl); p[3] = static_cast<uint8_t>(a);
    };
    auto paint = [&](const int colors[4][3]) {
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            const int i = index(x, y);
            if (!opaque && i == 2) put(x, y, 0, 0, 0, 0);
            else put(x, y, colors[i][0], colors[i][1], colors[i][2], 255);
        }
    };
    int base[2][3];
    if (diff) {
        const int r = bits(63, 59), g = bits(55, 51), bl = bits(47, 43);
        auto delta = [&](int hi) { const int d = bits(hi, hi - 2); return d >= 4 ? d - 8 : d; };
        const int r2 = r + delta(58), g2 = g + delta(50), b2 = bl + delta(42);
        if (r2 < 0 || r2 > 31) {  // T mode
            const int c1[3] = {ext4(bits(60, 59) << 2 | bits(57, 56)), ext4(bits(55, 52)), ext4(bits(51, 48))};
            const int c2[3] = {ext4(bits(47, 44)), ext4(bits(43, 40)), ext4(bits(39, 36))};
            const int d = kEtc2Distances[bits(35, 34) << 1 | bits(32, 32)];
            const int colors[4][3] = {{c1[0], c1[1], c1[2]}, {c2[0] + d, c2[1] + d, c2[2] + d},
                                      {c2[0], c2[1], c2[2]}, {c2[0] - d, c2[1] - d, c2[2] - d}};
            paint(colors);
            return;
        }
        if (g2 < 0 || g2 > 31) {  // H mode
            const int r1 = bits(62, 59), g1 = bits(58, 56) << 1 | bits(52, 52);
            const int b1 = bits(51, 51) << 3 | bits(49, 47);
            const int rr = bits(46, 43), gg = bits(42, 39), bb = bits(38, 35);
            const int order = (r1 << 8 | g1 << 4 | b1) >= (rr << 8 | gg << 4 | bb) ? 1 : 0;
            const int d = kEtc2Distances[bits(34, 34) << 2 | bits(32, 32) << 1 | order];
            const int c1[3] = {ext4(r1), ext4(g1), ext4(b1)}, c2[3] = {ext4(rr), ext4(gg), ext4(bb)};
            const int colors[4][3] = {{c1[0] + d, c1[1] + d, c1[2] + d}, {c1[0] - d, c1[1] - d, c1[2] - d},
                                      {c2[0] + d, c2[1] + d, c2[2] + d}, {c2[0] - d, c2[1] - d, c2[2] - d}};
            paint(colors);
            return;
        }
        if (b2 < 0 || b2 > 31) {  // Planar mode (always opaque)
            const int ro = ext6(bits(62, 57)), go = ext7(bits(56, 56) << 6 | bits(54, 49));
            const int bo = ext6(bits(48, 48) << 5 | bits(44, 43) << 3 | bits(41, 39));
            const int rh = ext6(bits(38, 34) << 1 | bits(32, 32)), gh = ext7(bits(31, 25)), bh = ext6(bits(24, 19));
            const int rv = ext6(bits(18, 13)), gv = ext7(bits(12, 6)), bv = ext6(bits(5, 0));
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
                put(x, y, (x * (rh - ro) + y * (rv - ro) + 4 * ro + 2) >> 2, (x * (gh - go) + y * (gv - go) + 4 * go + 2) >> 2,
                    (x * (bh - bo) + y * (bv - bo) + 4 * bo + 2) >> 2, 255);
            return;
        }
        base[0][0] = ext5(r); base[0][1] = ext5(g); base[0][2] = ext5(bl);
        base[1][0] = ext5(r2); base[1][1] = ext5(g2); base[1][2] = ext5(b2);
    } else {
        base[0][0] = ext4(bits(63, 60)); base[1][0] = ext4(bits(59, 56));
        base[0][1] = ext4(bits(55, 52)); base[1][1] = ext4(bits(51, 48));
        base[0][2] = ext4(bits(47, 44)); base[1][2] = ext4(bits(43, 40));
    }
    const int tables[2] = {bits(39, 37), bits(36, 34)};
    const bool flip = bits(32, 32);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const int sub = flip ? y >= 2 : x >= 2, i = index(x, y);
        const int a = kEtc1Modifiers[tables[sub]][0], bb = kEtc1Modifiers[tables[sub]][1];
        if (!opaque && i == 2) { put(x, y, 0, 0, 0, 0); continue; }
        const int m = !opaque && i == 0 ? 0 : i == 0 ? a : i == 1 ? bb : i == 2 ? -a : -bb;
        put(x, y, base[sub][0] + m, base[sub][1] + m, base[sub][2] + m, 255);
    }
}

// EAC 8-bit alpha block (first half of an ETC2 RGBA block) -> 16 values.
inline void decode_eac_alpha(const uint8_t* s, uint8_t out[16]) {
    const int baseValue = s[0], multiplier = s[1] >> 4, table = s[1] & 15;
    const uint64_t bits = detail::big_endian(s + 2, 6);
    for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) {
        const int i = x * 4 + y, index = static_cast<int>(bits >> (45 - 3 * i) & 7);
        out[y * 4 + x] = detail::clamp255(baseValue + detail::kEacModifiers[table][index] * multiplier);
    }
}

// EAC R11 block -> 16 values scaled to 8 bits (unsigned 0..255, or signed -127..127 stored as int).
inline void decode_eac_r11(const uint8_t* s, int out[16], bool isSigned) {
    const int multiplier = s[1] >> 4, table = s[1] & 15;
    const uint64_t bits = detail::big_endian(s + 2, 6);
    for (int x = 0; x < 4; ++x) for (int y = 0; y < 4; ++y) {
        const int i = x * 4 + y, index = static_cast<int>(bits >> (45 - 3 * i) & 7);
        const int modifier = detail::kEacModifiers[table][index];
        if (isSigned) {
            const int baseValue = std::max(-127, static_cast<int>(static_cast<int8_t>(s[0])));
            int v = baseValue * 8 + (multiplier ? modifier * multiplier * 8 : modifier);
            v = std::clamp(v, -1023, 1023);
            out[y * 4 + x] = (v * 127 + (v < 0 ? -511 : 511)) / 1023;
        } else {
            int v = s[0] * 8 + 4 + (multiplier ? modifier * multiplier * 8 : modifier);
            v = std::clamp(v, 0, 2047);
            out[y * 4 + x] = (v * 255 + 1023) / 2047;
        }
    }
}

namespace detail {

inline int dist2(const int* a, const int* b) {
    const int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return dr * dr + dg * dg + db * db;
}
inline void unpack565(uint16_t v, int c[3]) { c[0] = ext5(v >> 11); c[1] = ext6(v >> 5 & 63); c[2] = ext5(v & 31); }
inline uint16_t quantize565(const float c[3]) {
    auto q = [](float v, int levels) { return std::clamp(static_cast<int>(v * levels / 255.0f + 0.5f), 0, levels); };
    return static_cast<uint16_t>(q(c[0], 31) << 11 | q(c[1], 63) << 5 | q(c[2], 31));
}

// Best (a, b) endpoint pair so that a 4-color block's index 2, (2a+b)/3, reproduces an 8-bit value.
struct SingleColorTable {
    uint8_t five[256][2], six[256][2];
    SingleColorTable() {
        auto build = [](uint8_t (*table)[2], int levels, int (*expand)(int)) {
            for (int v = 0; v < 256; ++v) {
                int best = 1 << 30;
                for (int a = 0; a < levels; ++a) for (int b = 0; b < levels; ++b) {
                    const int err = std::abs((2 * expand(a) + expand(b)) / 3 - v) * 4 + std::abs(a - b);
                    if (err < best) { best = err; table[v][0] = static_cast<uint8_t>(a); table[v][1] = static_cast<uint8_t>(b); }
                }
            }
        };
        build(five, 32, ext5);
        build(six, 64, ext6);
    }
};
inline const SingleColorTable& single_color_table() { static const SingleColorTable table; return table; }

struct Bc1Fit { uint16_t c0 = 0, c1 = 0; uint8_t index[16] = {}; int error = 1 << 30; };

// Palette and nearest-index assignment for endpoints (c0, c1); threeColor = punch-through mode.
inline Bc1Fit evaluate_bc1(uint16_t c0, uint16_t c1, const uint8_t px[16][4], const bool* transparent, bool threeColor) {
    int p[4][3];
    unpack565(c0, p[0]); unpack565(c1, p[1]);
    for (int k = 0; k < 3; ++k) {
        if (threeColor) { p[2][k] = (p[0][k] + p[1][k]) / 2; p[3][k] = 0; }
        else { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
    }
    Bc1Fit fit{c0, c1, {}, 0};
    if (!threeColor) {
        // The 4-colour palette lies on a line (p1, p3, p2, p0 in order along p0 - p1), so the nearest entry follows
        // from each pixel's projection: compare it with the midpoints between neighbouring entries.
        const int dir[3] = {p[0][0] - p[1][0], p[0][1] - p[1][1], p[0][2] - p[1][2]};
        auto dot = [&](const int* c) { return c[0] * dir[0] + c[1] * dir[1] + c[2] * dir[2]; };
        const int s0 = dot(p[0]), s1 = dot(p[1]), s2 = dot(p[2]), s3 = dot(p[3]);
        const int lowMid = s1 + s3, middle = s3 + s2, highMid = s2 + s0;
        for (int i = 0; i < 16; ++i) {
            const int c[3] = {px[i][0], px[i][1], px[i][2]};
            const int d = 2 * dot(c);
            const int index = d < middle ? (d <= lowMid ? 1 : 3) : (d < highMid ? 2 : 0);
            fit.index[i] = static_cast<uint8_t>(index);
            fit.error += dist2(c, p[index]);
        }
        return fit;
    }
    const int choices = 3;
    for (int i = 0; i < 16; ++i) {
        if (transparent && transparent[i]) { fit.index[i] = 3; continue; }
        const int c[3] = {px[i][0], px[i][1], px[i][2]};
        int best = dist2(c, p[0]), bestIndex = 0;
        for (int k = 1; k < choices; ++k) { const int e = dist2(c, p[k]); if (e < best) { best = e; bestIndex = k; } }
        fit.index[i] = static_cast<uint8_t>(bestIndex);
        fit.error += best;
    }
    return fit;
}

// Least-squares endpoints for fixed indices; returns false when the system is degenerate.
inline bool refine_bc1(const Bc1Fit& fit, const uint8_t px[16][4], const bool* transparent, bool threeColor, uint16_t& c0, uint16_t& c1) {
    // Weight of endpoint 0 per index, in units of 1/scale (thirds for 4 colours, halves for 3), summed as integers.
    static constexpr int fourWeights[4] = {3, 0, 2, 1}, threeWeights[3] = {2, 0, 1};
    const int scale = threeColor ? 2 : 3;
    int aa = 0, bb = 0, ab = 0, ax[3] = {}, bx[3] = {};
    for (int i = 0; i < 16; ++i) {
        if (transparent && transparent[i]) continue;
        const int a = threeColor ? threeWeights[fit.index[i]] : fourWeights[fit.index[i]], b = scale - a;
        aa += a * a; bb += b * b; ab += a * b;
        for (int k = 0; k < 3; ++k) { ax[k] += a * px[i][k]; bx[k] += b * px[i][k]; }
    }
    const int det = aa * bb - ab * ab;
    if (!det) return false;
    const float f = static_cast<float>(scale) / static_cast<float>(det);
    float e0[3], e1[3];
    for (int k = 0; k < 3; ++k) { e0[k] = f * static_cast<float>(ax[k] * bb - bx[k] * ab); e1[k] = f * static_cast<float>(bx[k] * aa - ax[k] * ab); }
    c0 = quantize565(e0); c1 = quantize565(e1);
    return true;
}

// Encodes the RGB of 16 pixels as a BC1 color block. allowTransparent: pixels with alpha < 128 become index 3
// of the 3-color mode (BC1 RGBA). Otherwise the 4-color mode is used (also valid for BC3's color half).
inline void encode_bc1(const uint8_t px[16][4], bool allowTransparent, uint8_t* out) {
    bool transparent[16] = {};
    int opaque = 0;
    for (int i = 0; i < 16; ++i) { transparent[i] = allowTransparent && px[i][3] < 128; opaque += !transparent[i]; }
    const bool threeColor = opaque < 16;
    auto write = [out](uint16_t c0, uint16_t c1, const uint8_t index[16]) {
        uint32_t bits = 0;
        for (int i = 0; i < 16; ++i) bits |= static_cast<uint32_t>(index[i]) << (2 * i);
        out[0] = static_cast<uint8_t>(c0); out[1] = static_cast<uint8_t>(c0 >> 8);
        out[2] = static_cast<uint8_t>(c1); out[3] = static_cast<uint8_t>(c1 >> 8);
        for (int k = 0; k < 4; ++k) out[4 + k] = static_cast<uint8_t>(bits >> (8 * k));
    };
    if (!opaque) { const uint8_t all[16] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}; write(0, 0, all); return; }

    // Mean and covariance of the opaque pixels, from integer sums (cov = sum(xy) - sum(x) sum(y) / n).
    int sum[3] = {}, products[6] = {}, first = -1;
    bool solid = true;
    for (int i = 0; i < 16; ++i) {
        if (transparent[i]) continue;
        const int r = px[i][0], g = px[i][1], b = px[i][2];
        if (first < 0) first = i;
        else if (r != px[first][0] || g != px[first][1] || b != px[first][2]) solid = false;
        sum[0] += r; sum[1] += g; sum[2] += b;
        products[0] += r * r; products[1] += r * g; products[2] += r * b;
        products[3] += g * g; products[4] += g * b; products[5] += b * b;
    }
    const float inverseCount = 1.0f / static_cast<float>(opaque);
    const float mean[3] = {sum[0] * inverseCount, sum[1] * inverseCount, sum[2] * inverseCount};
    if (solid && !threeColor) {
        const auto& t = single_color_table();
        const uint16_t c0 = static_cast<uint16_t>(t.five[px[first][0]][0] << 11 | t.six[px[first][1]][0] << 5 | t.five[px[first][2]][0]);
        const uint16_t c1 = static_cast<uint16_t>(t.five[px[first][0]][1] << 11 | t.six[px[first][1]][1] << 5 | t.five[px[first][2]][1]);
        uint8_t index[16];
        if (c0 > c1) { std::fill(index, index + 16, uint8_t{2}); write(c0, c1, index); }
        else if (c0 < c1) { std::fill(index, index + 16, uint8_t{3}); write(c1, c0, index); }
        else { std::fill(index, index + 16, uint8_t{0}); write(c0, c1, index); }
        return;
    }
    static constexpr int pairs[6][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}};
    float cov[6];
    for (int k = 0; k < 6; ++k) cov[k] = static_cast<float>(products[k]) - static_cast<float>(sum[pairs[k][0]]) * mean[pairs[k][1]];
    // Principal axis by power iteration, started from the covariance row of the widest channel (close already).
    const int widest = cov[0] >= cov[3] && cov[0] >= cov[5] ? 0 : cov[3] >= cov[5] ? 1 : 2;
    static constexpr int rows[3][3] = {{0, 1, 2}, {1, 3, 4}, {2, 4, 5}};
    float axis[3] = {cov[rows[widest][0]], cov[rows[widest][1]], cov[rows[widest][2]]};
    for (int it = 0; it < 4; ++it) {
        const float n[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
                            cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                            cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
        const float len = std::max(std::fabs(n[0]), std::max(std::fabs(n[1]), std::fabs(n[2])));
        if (len < 1e-6f) break;
        const float inverse = 1.0f / len;
        for (int k = 0; k < 3; ++k) axis[k] = n[k] * inverse;
    }
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < 16; ++i) {
        if (transparent[i]) continue;
        const float t = (px[i][0] - mean[0]) * axis[0] + (px[i][1] - mean[1]) * axis[1] + (px[i][2] - mean[2]) * axis[2];
        lo = std::min(lo, t); hi = std::max(hi, t);
    }
    const float norm = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    float e0[3], e1[3];
    for (int k = 0; k < 3; ++k) {
        const float scale = norm > 1e-12f ? axis[k] / norm : 0.0f;
        e0[k] = std::clamp(mean[k] + scale * hi, 0.0f, 255.0f);
        e1[k] = std::clamp(mean[k] + scale * lo, 0.0f, 255.0f);
    }
    const bool* mask = threeColor ? transparent : nullptr;
    Bc1Fit best = evaluate_bc1(quantize565(e0), quantize565(e1), px, mask, threeColor);
    // One least-squares pass: a second one measured no gain.
    if (best.error > 0) {
        uint16_t c0, c1;
        if (refine_bc1(best, px, mask, threeColor, c0, c1)) {
            const Bc1Fit next = evaluate_bc1(c0, c1, px, mask, threeColor);
            if (next.error < best.error) best = next;
        }
    }
    // Endpoint order selects the mode: c0 > c1 is 4-color, c0 <= c1 is 3-color with transparent index 3.
    uint16_t c0 = best.c0, c1 = best.c1;
    uint8_t index[16];
    std::memcpy(index, best.index, 16);
    if (threeColor) {
        if (c0 > c1) { std::swap(c0, c1); for (auto& i : index) if (i < 2) i ^= 1; }
    } else if (c0 < c1) {
        std::swap(c0, c1); for (auto& i : index) i ^= 1;  // 0<->1, 2<->3
    } else if (c0 == c1) {
        std::fill(index, index + 16, uint8_t{0});
    }
    write(c0, c1, index);
}

// BC4 block from 16 values in 0..255 (or signed -127..127 when isSigned).
inline void encode_bc4(const int values[16], bool isSigned, uint8_t* out) {
    const int offset = isSigned ? 127 : 0;
    int v[16], lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) { v[i] = values[i] + offset; lo = std::min(lo, v[i]); hi = std::max(hi, v[i]); }
    struct Fit { int e0, e1; uint8_t index[16]; int error; };
    auto evaluate = [&](int e0, int e1) {
        int palette[8] = {e0, e1};
        if (e0 > e1) for (int k = 2; k < 8; ++k) palette[k] = ((8 - k) * e0 + (k - 1) * e1 + 3) / 7;
        else {
            for (int k = 2; k < 6; ++k) palette[k] = ((6 - k) * e0 + (k - 1) * e1 + 2) / 5;
            palette[6] = isSigned ? -1000 : 0; palette[7] = isSigned ? -1000 : 255;  // signed: never pick the extremes
        }
        Fit fit{e0, e1, {}, 0};
        for (int i = 0; i < 16; ++i) {
            int best = 1 << 30, bestIndex = 0;
            for (int k = 0; k < 8; ++k) { const int e = (v[i] - palette[k]) * (v[i] - palette[k]); if (e < best) { best = e; bestIndex = k; } }
            fit.index[i] = static_cast<uint8_t>(bestIndex); fit.error += best;
        }
        return fit;
    };
    Fit best{hi, lo, {}, 0};  // constant (e.g. opaque alpha): index 0 everywhere
    if (hi != lo) best = evaluate(hi, lo);
    if (hi != lo && !isSigned) {
        // Blocks that hit 0 or 255 can use the 6-value mode, whose indices 6/7 are exactly 0 and 255.
        int innerLo = 255, innerHi = 0;
        for (int x : v) if (x != 0 && x != 255) { innerLo = std::min(innerLo, x); innerHi = std::max(innerHi, x); }
        if (innerLo <= innerHi && (lo == 0 || hi == 255)) {
            const Fit six = evaluate(innerLo, innerHi);
            if (six.error < best.error) best = six;
        }
    }
    const int e0 = best.e0 - offset, e1 = best.e1 - offset;
    out[0] = static_cast<uint8_t>(e0); out[1] = static_cast<uint8_t>(e1);
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= static_cast<uint64_t>(best.index[i]) << (3 * i);
    for (int k = 0; k < 6; ++k) out[2 + k] = static_cast<uint8_t>(bits >> (8 * k));
}

}  // namespace detail

// BC3 block from 16 RGBA8 pixels (row-major): BC4-style alpha plus a 4-colour BC1 colour block.
inline void encode_bc3(const uint8_t px[16][4], uint8_t* out) {
    int alpha[16];
    for (int i = 0; i < 16; ++i) alpha[i] = px[i][3];
    detail::encode_bc4(alpha, false, out);
    detail::encode_bc1(px, false, out + 8);
}

// Transcodes one block; `out` receives block_bytes(codec) bytes (the same size as the input block).
inline void transcode_block(Codec codec, const uint8_t* in, uint8_t* out) {
    uint8_t px[16][4];
    int values[16];
    switch (codec) {
    case Codec::Etc2Rgb: decode_etc2_rgb(in, px, false); detail::encode_bc1(px, false, out); break;
    case Codec::Etc2RgbA1: decode_etc2_rgb(in, px, true); detail::encode_bc1(px, true, out); break;
    case Codec::Etc2Rgba: {
        uint8_t alpha[16];
        decode_eac_alpha(in, alpha);
        for (int i = 0; i < 16; ++i) values[i] = alpha[i];
        detail::encode_bc4(values, false, out);
        decode_etc2_rgb(in + 8, px, false);
        detail::encode_bc1(px, false, out + 8);
        break;
    }
    case Codec::EacR: case Codec::EacRSigned:
        decode_eac_r11(in, values, codec == Codec::EacRSigned);
        detail::encode_bc4(values, codec == Codec::EacRSigned, out);
        break;
    case Codec::EacRg: case Codec::EacRgSigned:
        for (int half = 0; half < 2; ++half) {
            decode_eac_r11(in + 8 * half, values, codec == Codec::EacRgSigned);
            detail::encode_bc4(values, codec == Codec::EacRgSigned, out + 8 * half);
        }
        break;
    case Codec::None: case Codec::Astc: break;  // ASTC is decoded per region (astc_decode.h), not per block
    }
}

}  // namespace axrb::texture
