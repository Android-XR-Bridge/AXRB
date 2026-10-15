// ResStringPool: the string table of binary XML and resources.arsc chunks.
//
// Ownership: the pool borrows the chunk bytes; the caller keeps them alive for
//   the pool's lifetime (a Document or ResourceTable owns them).
// Thread affinity: none (const after Load). Error propagation: Status/Result
//   with kManifestMalformed.
//
// Strings are decoded on demand, so a large table costs nothing until used.
// Every offset and length is checked against the chunk.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "arun/status.h"

namespace arun::axml {

// One styled run of a string (<b>, <i>, <u>, ...): tag name index and the
// inclusive character range it covers.
struct StringSpan {
  std::uint32_t name = 0;
  std::uint32_t first = 0;
  std::uint32_t last = 0;
};

class StringPool {
 public:
  static constexpr std::uint32_t kNoString = 0xffffffffu;

  // `chunk` is the whole RES_STRING_POOL_TYPE chunk (header included).
  Status Load(std::span<const std::uint8_t> chunk, std::uint32_t max_strings = 1u << 22);

  std::uint32_t size() const { return count_; }
  bool utf8() const { return utf8_; }

  // kNoString -> empty; any other bad index is an error.
  Status Get(std::uint32_t index, std::string* out) const;
  Status Get16(std::uint32_t index, std::u16string* out) const;
  // Styled runs of string `index` (empty if the string has none).
  Status Spans(std::uint32_t index, std::vector<StringSpan>* out) const;

 private:
  Status Locate(std::uint32_t index, std::size_t* at) const;

  std::span<const std::uint8_t> chunk_;
  std::uint32_t count_ = 0;
  std::uint32_t style_count_ = 0;
  std::uint32_t header_size_ = 0;
  std::uint32_t strings_start_ = 0;
  std::uint32_t styles_start_ = 0;
  bool utf8_ = false;
};

// UTF-8 <-> UTF-16 helpers shared by the resource code.
void AppendUtf8(std::string* out, std::uint32_t code_point);
std::u16string Utf8To16(std::string_view utf8);
std::string Utf16To8(std::u16string_view utf16);

}  // namespace arun::axml
