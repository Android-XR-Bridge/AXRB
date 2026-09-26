#pragma once
#include <cstddef>
#include <cstdint>

/* Decodes ASTC LDR blocks to RGBA8. The blocks are row_blocks to a row and
   slice_blocks to a slice; out gets width x height x depth texels. */
void astc_decode(const uint8_t* blocks, int block_w, int block_h, int width, int height, int depth,
                 size_t row_blocks, size_t slice_blocks, uint8_t* out, size_t out_row_bytes,
                 size_t out_slice_bytes);

/* The block size of a Vulkan ASTC LDR format, or false for any other format. */
bool astc_block_size(uint32_t vk_format, int* block_w, int* block_h, bool* srgb);
