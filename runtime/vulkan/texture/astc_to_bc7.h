#pragma once
// ASTC LDR to BC7 transcoding. Desktop GPUs sample BC7 natively at 8 bits per
// texel; the emulator's host renderer otherwise keeps each ASTC texture as
// RGBA8 (32 bits per texel) plus the compressed original.
//
// The BC7 encoder is Binomial's bc7e (bc7e/bc7e.ispc, prebuilt by
// scripts/build/build_bc7e.py) at its "ultrafast" setting, which searches all
// BC7 modes, including mode 4's separate alpha indices: most game textures
// carry an independent alpha channel (smoothness, masks).
//
// A region is transcoded in bands of texel rows. A band starts on a row that
// both ASTC block rows and BC7 block rows (4 texels) start on, so bands are
// independent and can run on different threads.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numeric>
#include <vector>
#include "astc_decoder.h"
#include "bc7e/bc7e_ispc.h"

namespace axrb::texture {

// Texel rows per band unit for an ASTC block height.
inline int band_rows(int blockHeight) { return std::lcm(blockHeight, 4); }

// Encoder settings: perceptual error weighting for sRGB color, equal channel
// weights for UNORM data (normal maps, masks).
inline const ispc::bc7e_compress_block_params* bc7_settings(bool srgb) {
    static std::once_flag once;
    static ispc::bc7e_compress_block_params perceptual, linear;
    std::call_once(once, [] {
        ispc::bc7e_compress_block_init();
        ispc::bc7e_compress_block_params_init_ultrafast(&perceptual, true);
        ispc::bc7e_compress_block_params_init_ultrafast(&linear, false);
    });
    return srgb ? &perceptual : &linear;
}

struct AstcRegion {
    const uint8_t* blocks;   // first ASTC block of the region
    size_t rowBytes;         // bytes from one ASTC block row to the next
    int blockWidth, blockHeight;
    bool srgb;
    int width, height;       // texels
};
struct Bc7Output {
    uint8_t* blocks;         // first BC7 block of the region
    size_t rowBytes;         // bytes from one BC7 block row to the next
};

// Transcodes texel rows [first, last) of the region. first is a multiple of
// band_rows(blockHeight); last is too, or the region height.
inline void astc_to_bc7(const AstcRegion& in, const Bc7Output& out, int first, int last) {
    const int bw = in.blockWidth, bh = in.blockHeight;
    const int columns = (in.width + 3) / 4 * 4;          // texels, padded to whole BC7 blocks
    const int rows = (last - first + 3) / 4 * 4;
    std::vector<uint32_t> strip(size_t(columns) * rows);
    uint32_t texels[144];
    const int blocksAcross = (in.width + bw - 1) / bw;
    for (int by = first / bh; by * bh < last; ++by) {
        const uint8_t* row = in.blocks + size_t(by) * in.rowBytes;
        for (int bx = 0; bx < blocksAcross; ++bx) {
            astc::decode_block(row + size_t(bx) * 16, bw, bh, in.srgb, texels);
            for (int ty = 0; ty < bh; ++ty) {
                const int y = by * bh + ty;
                if (y < first || y >= last) continue;
                const int x = bx * bw, count = std::min(bw, in.width - x);
                std::memcpy(&strip[size_t(y - first) * columns + x], texels + ty * bw, size_t(count) * 4);
            }
        }
    }
    // Partial blocks at the right and bottom edges repeat the last texel.
    for (int y = 0; y < last - first; ++y)
        for (int x = in.width; x < columns; ++x) strip[size_t(y) * columns + x] = strip[size_t(y) * columns + in.width - 1];
    for (int y = last - first; y < rows; ++y)
        std::memcpy(&strip[size_t(y) * columns], &strip[size_t(last - first - 1) * columns], size_t(columns) * 4);
    // bc7e encodes a row of blocks per call, several blocks per SIMD pass.
    const ispc::bc7e_compress_block_params* settings = bc7_settings(in.srgb);
    std::vector<uint32_t> pixels(size_t(columns / 4) * 16);
    for (int y = 0; y < rows; y += 4) {
        for (int x = 0; x < columns; x += 4)
            for (int i = 0; i < 4; ++i) std::memcpy(&pixels[size_t(x / 4) * 16 + i * 4], &strip[size_t(y + i) * columns + x], 16);
        uint8_t* target = out.blocks + size_t((first + y) / 4) * out.rowBytes;
        ispc::bc7e_compress_blocks(uint32_t(columns / 4), reinterpret_cast<uint64_t*>(target), pixels.data(), settings);
    }
}

} // namespace axrb::texture
