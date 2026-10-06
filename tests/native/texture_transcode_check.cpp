// Offline check for tools/texture_transcode.h, driven by tests/texture_transcode_check.py:
//   texture_transcode_check <codec> <width> <height> <blocks.bin> <decoded.rgba> <transcoded.bin>
// Writes our decode of the source blocks (RGBA8, row-major image; EAC values in R, signed ones offset by 127)
// and the transcoded BC blocks, then prints the transcode rate.
#include "../../runtime/vulkan/texture/etc2_to_bc.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace axrb::texture;

int main(int argc, char** argv) {
    if (argc != 7) { std::fprintf(stderr, "usage: codec width height blocks decoded transcoded\n"); return 2; }
    const std::string name = argv[1];
    const Codec codec = name == "etc2" ? Codec::Etc2Rgb : name == "etc2a1" ? Codec::Etc2RgbA1 : name == "etc2a8" ? Codec::Etc2Rgba
        : name == "eacr" ? Codec::EacR : name == "eacr_signed" ? Codec::EacRSigned : name == "eacrg" ? Codec::EacRg
        : name == "eacrg_signed" ? Codec::EacRgSigned : Codec::None;
    if (codec == Codec::None) { std::fprintf(stderr, "unknown codec\n"); return 2; }
    const int width = std::atoi(argv[2]), height = std::atoi(argv[3]), bx = width / 4, by = height / 4;
    std::ifstream in(argv[4], std::ios::binary);
    const std::vector<uint8_t> blocks((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const uint32_t size = block_bytes(codec);
    if (blocks.size() != static_cast<size_t>(bx) * by * size) { std::fprintf(stderr, "size mismatch\n"); return 2; }

    std::vector<uint8_t> decoded(static_cast<size_t>(width) * height * 4), transcoded(blocks.size());
    for (int b = 0; b < bx * by; ++b) {
        const uint8_t* s = blocks.data() + static_cast<size_t>(b) * size;
        uint8_t px[16][4];
        int values[16];
        switch (codec) {
        case Codec::Etc2Rgb: case Codec::Etc2RgbA1: decode_etc2_rgb(s, px, codec == Codec::Etc2RgbA1); break;
        case Codec::Etc2Rgba: {
            uint8_t alpha[16];
            decode_etc2_rgb(s + 8, px, false); decode_eac_alpha(s, alpha);
            for (int i = 0; i < 16; ++i) px[i][3] = alpha[i];
            break;
        }
        default: {
            const bool isSigned = codec == Codec::EacRSigned || codec == Codec::EacRgSigned;
            const bool rg = codec == Codec::EacRg || codec == Codec::EacRgSigned;
            std::memset(px, 0, sizeof(px));
            decode_eac_r11(s, values, isSigned);
            for (int i = 0; i < 16; ++i) { px[i][0] = static_cast<uint8_t>(values[i] + (isSigned ? 127 : 0)); px[i][3] = 255; }
            if (rg) {
                decode_eac_r11(s + 8, values, isSigned);
                for (int i = 0; i < 16; ++i) px[i][1] = static_cast<uint8_t>(values[i] + (isSigned ? 127 : 0));
            }
        }
        }
        const int x0 = (b % bx) * 4, y0 = (b / bx) * 4;
        for (int i = 0; i < 16; ++i) std::memcpy(&decoded[(static_cast<size_t>(y0 + i / 4) * width + x0 + i % 4) * 4], px[i], 4);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < bx * by; ++b)
        transcode_block(codec, blocks.data() + static_cast<size_t>(b) * size, transcoded.data() + static_cast<size_t>(b) * size);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream(argv[5], std::ios::binary).write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    std::ofstream(argv[6], std::ios::binary).write(reinterpret_cast<const char*>(transcoded.data()), transcoded.size());
    std::printf("%s: %d blocks in %.3f s, %.1f Mpix/s\n", name.c_str(), bx * by, seconds, bx * by * 16 / seconds / 1e6);
    return 0;
}
