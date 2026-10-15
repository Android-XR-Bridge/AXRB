// Raw DEFLATE decompression (RFC 1951), for ZIP/APK entries.
//
// Ownership: pure function over caller-provided buffers.
// Thread affinity: none; no global state.
// Error propagation: Status with package_malformed / resource_exhausted.
// Pointers: host pointers into caller buffers only.
//
// Why this exists rather than a library call: Windows ships no raw-DEFLATE
// decompressor. The Compression API (compressapi.h) offers MSZIP, XPRESS,
// XPRESS_HUFF and LZMS -- MSZIP is a *framed* variant, not the raw stream ZIP
// stores -- and neither the Windows SDK nor the MSVC toolset ships zlib. The NDK
// ships zlib only for Android targets. Vendoring a compression library for one
// function would add a dependency, a licence obligation and an SBOM entry to the
// runner's smallest layer, so this is implemented directly.
//
// Input is hostile (spec 13.3). Every table index, every distance, every copy is
// bounds-checked, output is capped at the size the caller declared, and there is
// no unbounded loop: a corrupt stream terminates with an error rather than
// spinning or reading out of bounds.

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <vector>

#include "arun/status.h"

namespace arun::pkg {
namespace {

constexpr int kMaxBits = 15;
constexpr int kLengthCodes = 29;
constexpr int kLiteralCodes = 288;
constexpr int kDistanceCodes = 30;

// Canonical Huffman decoding table built from code lengths.
struct HuffmanTable {
  // count[n] = number of codes of length n; symbol[] = symbols in canonical order.
  std::array<std::uint16_t, kMaxBits + 1> count{};
  std::vector<std::uint16_t> symbol;

  Status Build(const std::uint8_t* lengths, std::size_t n) {
    count.fill(0);
    for (std::size_t i = 0; i < n; ++i) {
      if (lengths[i] > kMaxBits) {
        return Err(ReasonCode::kPackageMalformed, "deflate: code length exceeds 15 bits");
      }
      ++count[lengths[i]];
    }
    if (count[0] == n) {
      // An all-zero table is legal only if it is never used; leave it empty and
      // let any actual decode attempt fail.
      symbol.clear();
      return Status::Ok();
    }

    // Verify the code set is not over-subscribed (a classic corrupt-stream case).
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
      left <<= 1;
      left -= count[len];
      if (left < 0) {
        return Err(ReasonCode::kPackageMalformed, "deflate: over-subscribed Huffman code");
      }
    }

    std::array<std::uint16_t, kMaxBits + 1> offsets{};
    offsets[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) {
      offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + count[len]);
    }
    symbol.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
      if (lengths[i] != 0) symbol[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
    }
    return Status::Ok();
  }
};

class BitReader {
 public:
  explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

  // Reads `n` bits LSB-first. Sets `ok_` to false on exhaustion rather than
  // reading past the buffer.
  std::uint32_t Bits(int n) {
    while (bit_count_ < n) {
      if (position_ >= data_.size()) {
        ok_ = false;
        return 0;
      }
      bit_buffer_ |= static_cast<std::uint64_t>(data_[position_++]) << bit_count_;
      bit_count_ += 8;
    }
    const std::uint32_t value = static_cast<std::uint32_t>(bit_buffer_ & ((1ull << n) - 1));
    bit_buffer_ >>= n;
    bit_count_ -= n;
    return value;
  }

  void AlignToByte() {
    const int drop = bit_count_ % 8;
    bit_buffer_ >>= drop;
    bit_count_ -= drop;
  }

  // Copies `n` bytes directly (stored blocks). Must be byte-aligned first.
  bool CopyBytes(std::uint8_t* out, std::size_t n) {
    // Drain whole bytes still sitting in the bit buffer.
    while (n > 0 && bit_count_ >= 8) {
      *out++ = static_cast<std::uint8_t>(bit_buffer_ & 0xff);
      bit_buffer_ >>= 8;
      bit_count_ -= 8;
      --n;
    }
    if (n > data_.size() - position_) {
      ok_ = false;
      return false;
    }
    std::memcpy(out, data_.data() + position_, n);
    position_ += n;
    return true;
  }

  // Decodes one symbol using the canonical bit-length walk.
  int Decode(const HuffmanTable& table) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      code |= static_cast<int>(Bits(1));
      if (!ok_) return -1;
      const int count = table.count[len];
      if (code - first < count) {
        const std::size_t at = static_cast<std::size_t>(index + (code - first));
        if (at >= table.symbol.size()) {
          ok_ = false;
          return -1;
        }
        return table.symbol[at];
      }
      index += count;
      first = (first + count) << 1;
      code <<= 1;
    }
    ok_ = false;  // no code matched within 15 bits
    return -1;
  }

  bool ok() const { return ok_; }
  std::size_t position() const { return position_; }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t position_ = 0;
  std::uint64_t bit_buffer_ = 0;
  int bit_count_ = 0;
  bool ok_ = true;
};

// RFC 1951 section 3.2.5 length and distance tables.
constexpr std::uint16_t kLengthBase[kLengthCodes] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[kLengthCodes] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistanceBase[kDistanceCodes] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
constexpr std::uint8_t kDistanceExtra[kDistanceCodes] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void BuildFixedTables(HuffmanTable& literals, HuffmanTable& distances) {
  std::uint8_t lengths[kLiteralCodes];
  int i = 0;
  for (; i < 144; ++i) lengths[i] = 8;
  for (; i < 256; ++i) lengths[i] = 9;
  for (; i < 280; ++i) lengths[i] = 7;
  for (; i < kLiteralCodes; ++i) lengths[i] = 8;
  (void)literals.Build(lengths, kLiteralCodes);

  std::uint8_t dist_lengths[kDistanceCodes];
  for (int d = 0; d < kDistanceCodes; ++d) dist_lengths[d] = 5;
  (void)distances.Build(dist_lengths, kDistanceCodes);
}

Status ReadDynamicTables(BitReader& reader, HuffmanTable& literals, HuffmanTable& distances) {
  const int hlit = static_cast<int>(reader.Bits(5)) + 257;
  const int hdist = static_cast<int>(reader.Bits(5)) + 1;
  const int hclen = static_cast<int>(reader.Bits(4)) + 4;
  if (!reader.ok()) return Err(ReasonCode::kPackageMalformed, "deflate: truncated block header");
  if (hlit > kLiteralCodes || hdist > kDistanceCodes + 2) {
    return Err(ReasonCode::kPackageMalformed, "deflate: implausible code counts");
  }

  static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
  std::uint8_t code_lengths[19] = {};
  for (int i = 0; i < hclen; ++i) {
    code_lengths[kOrder[i]] = static_cast<std::uint8_t>(reader.Bits(3));
  }
  if (!reader.ok()) return Err(ReasonCode::kPackageMalformed, "deflate: truncated code lengths");

  HuffmanTable code_table;
  ARUN_RETURN_IF_ERROR(code_table.Build(code_lengths, 19));

  std::vector<std::uint8_t> lengths(static_cast<std::size_t>(hlit + hdist), 0);
  std::size_t written = 0;
  while (written < lengths.size()) {
    const int symbol = reader.Decode(code_table);
    if (symbol < 0) return Err(ReasonCode::kPackageMalformed, "deflate: bad code-length symbol");

    if (symbol < 16) {
      lengths[written++] = static_cast<std::uint8_t>(symbol);
      continue;
    }
    std::uint8_t value = 0;
    int repeat = 0;
    if (symbol == 16) {
      if (written == 0) {
        return Err(ReasonCode::kPackageMalformed, "deflate: repeat with no previous length");
      }
      value = lengths[written - 1];
      repeat = 3 + static_cast<int>(reader.Bits(2));
    } else if (symbol == 17) {
      repeat = 3 + static_cast<int>(reader.Bits(3));
    } else {
      repeat = 11 + static_cast<int>(reader.Bits(7));
    }
    if (!reader.ok()) return Err(ReasonCode::kPackageMalformed, "deflate: truncated repeat");
    if (written + static_cast<std::size_t>(repeat) > lengths.size()) {
      return Err(ReasonCode::kPackageMalformed, "deflate: code-length repeat overruns the table");
    }
    for (int i = 0; i < repeat; ++i) lengths[written++] = value;
  }

  ARUN_RETURN_IF_ERROR(literals.Build(lengths.data(), static_cast<std::size_t>(hlit)));
  ARUN_RETURN_IF_ERROR(
      distances.Build(lengths.data() + hlit, static_cast<std::size_t>(hdist)));
  return Status::Ok();
}

}  // namespace

// Decompresses a raw DEFLATE stream into `output`, which must already be sized to
// the expected uncompressed length. Producing a different length is an error: the
// ZIP central directory told us what to expect, and a mismatch means the archive
// is malformed or tampered with.
Status InflateRaw(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
  BitReader reader(input);
  std::size_t produced = 0;
  int blocks = 0;
  // A stream of empty blocks could otherwise loop indefinitely on crafted input.
  constexpr int kMaxBlocks = 1 << 20;

  for (;;) {
    if (++blocks > kMaxBlocks) {
      return Err(ReasonCode::kResourceExhausted, "deflate: too many blocks");
    }
    const std::uint32_t final_block = reader.Bits(1);
    const std::uint32_t type = reader.Bits(2);
    if (!reader.ok()) {
      return Err(ReasonCode::kPackageMalformed, "deflate: truncated stream at block header");
    }

    if (type == 0) {
      // Stored: LEN / NLEN, then raw bytes.
      reader.AlignToByte();
      const std::uint32_t len = reader.Bits(16);
      const std::uint32_t nlen = reader.Bits(16);
      if (!reader.ok()) {
        return Err(ReasonCode::kPackageMalformed, "deflate: truncated stored header");
      }
      if ((len ^ 0xFFFF) != nlen) {
        return Err(ReasonCode::kPackageMalformed, "deflate: stored block length check failed");
      }
      if (produced + len > output.size()) {
        return Err(ReasonCode::kPackageMalformed,
                   "deflate: stored block would exceed the declared output size");
      }
      if (!reader.CopyBytes(output.data() + produced, len)) {
        return Err(ReasonCode::kPackageMalformed, "deflate: truncated stored block");
      }
      produced += len;
    } else if (type == 1 || type == 2) {
      HuffmanTable literals;
      HuffmanTable distances;
      if (type == 1) {
        BuildFixedTables(literals, distances);
      } else {
        ARUN_RETURN_IF_ERROR(ReadDynamicTables(reader, literals, distances));
      }

      for (;;) {
        const int symbol = reader.Decode(literals);
        if (symbol < 0) {
          return Err(ReasonCode::kPackageMalformed, "deflate: bad literal/length symbol");
        }
        if (symbol < 256) {
          if (produced >= output.size()) {
            return Err(ReasonCode::kPackageMalformed,
                       "deflate: output exceeds the declared uncompressed size");
          }
          output[produced++] = static_cast<std::uint8_t>(symbol);
          continue;
        }
        if (symbol == 256) break;  // end of block

        const int length_index = symbol - 257;
        if (length_index >= kLengthCodes) {
          return Err(ReasonCode::kPackageMalformed, "deflate: invalid length code");
        }
        const std::size_t length =
            kLengthBase[length_index] + reader.Bits(kLengthExtra[length_index]);

        const int distance_symbol = reader.Decode(distances);
        if (distance_symbol < 0 || distance_symbol >= kDistanceCodes) {
          return Err(ReasonCode::kPackageMalformed, "deflate: invalid distance code");
        }
        const std::size_t distance =
            kDistanceBase[distance_symbol] + reader.Bits(kDistanceExtra[distance_symbol]);
        if (!reader.ok()) {
          return Err(ReasonCode::kPackageMalformed, "deflate: truncated match");
        }
        if (distance == 0 || distance > produced) {
          // Referring behind the start of the output is the classic corrupt /
          // malicious case; it must never read out of bounds.
          return Err(ReasonCode::kPackageMalformed,
                     std::format("deflate: match distance {} exceeds {} bytes produced",
                                 distance, produced));
        }
        if (produced + length > output.size()) {
          return Err(ReasonCode::kPackageMalformed,
                     "deflate: match would exceed the declared uncompressed size");
        }
        // Byte-by-byte on purpose: overlapping matches (distance < length) are
        // legal and must replicate, so memcpy/memmove would be wrong.
        for (std::size_t i = 0; i < length; ++i) {
          output[produced] = output[produced - distance];
          ++produced;
        }
      }
    } else {
      return Err(ReasonCode::kPackageMalformed, "deflate: reserved block type 3");
    }

    if (final_block) break;
  }

  if (produced != output.size()) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("deflate: produced {} bytes but the directory declared {}", produced,
                           output.size()));
  }
  return Status::Ok();
}

}  // namespace arun::pkg
