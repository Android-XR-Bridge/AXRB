// Checks the ASTC decoder and the ASTC to BC7 transcoder against ARM's
// reference codec. Each argument pair is an .astc file (astcenc output) and
// astcenc's decode of it as uncompressed KTX; a name containing "_s." or
// "_s_" marks sRGB. Reports per file: texels that differ from the reference
// decode, BC7 PSNR against it, and throughput. Exits 1 on any decode mismatch.
//   texture_transcode_test <astc> <ktx> [<astc> <ktx> ...]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "../../runtime/vulkan/texture/astc_to_bc7.h"
#include "../../runtime/vulkan/texture/bc7enc/bc7decomp.h"
#include "../../runtime/vulkan/texture/bc7enc/bc7enc.c"
#include "../../runtime/vulkan/texture/bc7enc/bc7decomp.cpp"

namespace {
std::vector<uint8_t> read_file(const char* path) {
    std::vector<uint8_t> data;
    if (FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        data.resize(size_t(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        std::fclose(f);
    }
    return data;
}

// Uncompressed KTX 1 with GL_UNSIGNED_BYTE RGB or RGBA: returns RGBA8.
bool read_ktx(const char* path, int* width, int* height, std::vector<uint32_t>* rgba) {
    const std::vector<uint8_t> d = read_file(path);
    if (d.size() < 68) return false;
    uint32_t h[13];
    std::memcpy(h, d.data() + 12, sizeof(h));
    const uint32_t glType = h[1], glFormat = h[3], keyBytes = h[12];
    const int components = glFormat == 0x1908 ? 4 : glFormat == 0x1907 ? 3 : 0;
    if (glType != 0x1401 || !components) return false;
    *width = int(h[6]);
    *height = int(h[7]);
    const size_t at = 64 + keyBytes + 4, rowBytes = (size_t(*width) * components + 3) & ~size_t(3);
    if (d.size() < at + rowBytes * *height) return false;
    rgba->resize(size_t(*width) * *height);
    for (int y = 0; y < *height; ++y)
        for (int x = 0; x < *width; ++x) {
            const uint8_t* p = d.data() + at + size_t(y) * rowBytes + size_t(x) * components;
            (*rgba)[size_t(y) * *width + x] = p[0] | p[1] << 8 | p[2] << 16 | uint32_t(components == 4 ? p[3] : 255) << 24;
        }
    return true;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

int main(int argc, char** argv) {
    using namespace axrb::texture;
    init_bc7_encoder();
    int failures = 0;
    double decodeTexels = 0, decodeSeconds = 0, encodeTexels = 0, encodeSeconds = 0, psnrSum = 0, psnrMin = 99;
    int files = 0;
    // --bench <astc>...: game textures without a reference decode; the
    // decoder's own output is the reference for BC7 quality.
    const bool bench = argc > 1 && !std::strcmp(argv[1], "--bench");
    for (int arg = bench ? 2 : 1; bench ? arg < argc : arg + 1 < argc; arg += bench ? 1 : 2) {
        const std::vector<uint8_t> file = read_file(argv[arg]);
        int width = 0, height = 0;
        std::vector<uint32_t> reference;
        if (bench && file.size() >= 16) {
            width = file[7] | file[8] << 8 | file[9] << 16;
            height = file[10] | file[11] << 8 | file[12] << 16;
        }
        if (file.size() < 16 || (!bench && !read_ktx(argv[arg + 1], &width, &height, &reference))) {
            std::printf("%s: unreadable\n", argv[arg]);
            ++failures;
            continue;
        }
        const int bw = file[4], bh = file[5];
        const std::string name = argv[arg];
        const bool srgb = name.find("_s.") != std::string::npos || name.find("_s_") != std::string::npos;
        const int across = (width + bw - 1) / bw;
        const uint8_t* blocks = file.data() + 16;

        // Decoder against the reference, texel by texel.
        auto start = std::chrono::steady_clock::now();
        std::vector<uint32_t> decoded(size_t(width) * height), texels(144);
        for (int by = 0; by * bh < height; ++by)
            for (int bx = 0; bx < across; ++bx) {
                astc::decode_block(blocks + (size_t(by) * across + bx) * 16, bw, bh, srgb, texels.data());
                for (int y = 0; y < bh && by * bh + y < height; ++y)
                    for (int x = 0; x < bw && bx * bw + x < width; ++x)
                        decoded[size_t(by * bh + y) * width + bx * bw + x] = texels[y * bw + x];
            }
        decodeSeconds += seconds_since(start);
        decodeTexels += double(width) * height;
        if (bench) reference = decoded;
        size_t mismatched = 0;
        int worst = 0;
        for (size_t i = 0; i < decoded.size(); ++i)
            if (decoded[i] != reference[i]) {
                ++mismatched;
                for (int c = 0; c < 32; c += 8)
                    worst = std::max(worst, std::abs(int((decoded[i] >> c) & 255) - int((reference[i] >> c) & 255)));
            }

        // Transcode in bands, as the layer's workers do, then decode the BC7.
        const int bcAcross = (width + 3) / 4, bcDown = (height + 3) / 4;
        std::vector<uint8_t> bc7(size_t(bcAcross) * bcDown * 16);
        auto params = bc7_params(srgb);
        // Encoder exploration: BC7_PARTITIONS, BC7_UBER, BC7_LSQ override the settings.
        if (const char* v = std::getenv("BC7_PARTITIONS")) params.m_max_partitions_mode = uint32_t(std::atoi(v));
        if (const char* v = std::getenv("BC7_UBER")) params.m_uber_level = uint32_t(std::atoi(v));
        if (const char* v = std::getenv("BC7_LSQ")) params.m_try_least_squares = std::atoi(v) != 0;
        start = std::chrono::steady_clock::now();
        const int band = band_rows(bh) * 4;
        for (int y = 0; y < height; y += band)
            astc_to_bc7({blocks, size_t(across) * 16, bw, bh, srgb, width, height}, {bc7.data(), size_t(bcAcross) * 16},
                        y, std::min(height, y + band), params);
        encodeSeconds += seconds_since(start);
        encodeTexels += double(width) * height;
        double squared = 0, channel[4] = {0, 0, 0, 0};
        bool alpha = false;
        for (uint32_t texel : reference) alpha |= (texel >> 24) != 255;
        bc7decomp::color_rgba unpacked[16];
        for (int by = 0; by < bcDown; ++by)
            for (int bx = 0; bx < bcAcross; ++bx) {
                bc7decomp::unpack_bc7(&bc7[(size_t(by) * bcAcross + bx) * 16], unpacked);
                for (int y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (int x = 0; x < 4 && bx * 4 + x < width; ++x) {
                        const uint32_t r = reference[size_t(by * 4 + y) * width + bx * 4 + x];
                        for (int c = 0; c < 4; ++c) {
                            const double d = double(unpacked[y * 4 + x].m_comps[c]) - double((r >> (c * 8)) & 255);
                            squared += d * d;
                            channel[c] += d * d;
                        }
                    }
            }
        const double mse = squared / (double(width) * height * 4);
        const double psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99;
        if (!bench || std::getenv("VERBOSE"))
            std::printf("%-34s %4dx%-4d %2dx%-2d %s %s rgb_mse=%.1f a_mse=%.1f mismatched=%zu worst=%d bc7_psnr=%.2f\n", argv[arg], width, height, bw, bh,
                        srgb ? "srgb" : "unorm", alpha ? "alpha" : "opaque", (channel[0] + channel[1] + channel[2]) / (3.0 * width * height),
                        channel[3] / (double(width) * height), mismatched, worst, psnr);
        psnrSum += psnr;
        psnrMin = std::min(psnrMin, psnr);
        ++files;
        if (mismatched) ++failures;
    }
    std::printf("%d files, %.1f Mtexel: decode %.1f Mtexel/s, transcode %.1f Mtexel/s (one thread); BC7 PSNR mean %.2f min %.2f; "
                "%d file(s) with decode mismatches\n",
                files, encodeTexels / 1e6, decodeTexels / decodeSeconds / 1e6, encodeTexels / encodeSeconds / 1e6,
                files ? psnrSum / files : 0.0, psnrMin, failures);
    return failures ? 1 : 0;
}
