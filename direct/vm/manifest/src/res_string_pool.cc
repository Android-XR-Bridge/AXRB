#include "arun/res_string_pool.h"

#include <format>

namespace arun::axml {
namespace {

constexpr std::uint32_t kUtf8Flag = 1u << 8;
constexpr std::uint32_t kSpanEnd = 0xffffffffu;

Status Malformed(std::string what) { return Err(ReasonCode::kManifestMalformed, "string pool: " + what); }

std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t U32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Decodes one UTF-8 sequence starting at s[*i]; malformed input yields U+FFFD.
std::uint32_t NextUtf8(std::string_view s, std::size_t* i) {
  const auto b0 = static_cast<std::uint8_t>(s[(*i)++]);
  if (b0 < 0x80) return b0;
  int extra = b0 >= 0xf0 ? 3 : b0 >= 0xe0 ? 2 : b0 >= 0xc0 ? 1 : -1;
  if (extra < 0) return 0xfffd;
  std::uint32_t cp = b0 & (0x3f >> extra);
  for (int k = 0; k < extra; ++k) {
    if (*i >= s.size()) return 0xfffd;
    const auto b = static_cast<std::uint8_t>(s[*i]);
    if ((b & 0xc0) != 0x80) return 0xfffd;
    cp = (cp << 6) | (b & 0x3f);
    ++*i;
  }
  return cp > 0x10ffff ? 0xfffd : cp;
}

}  // namespace

void AppendUtf8(std::string* out, std::uint32_t cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out->push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

std::u16string Utf8To16(std::string_view s) {
  std::u16string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const std::uint32_t cp = NextUtf8(s, &i);
    if (cp >= 0x10000) {
      out.push_back(static_cast<char16_t>(0xd800 + ((cp - 0x10000) >> 10)));
      out.push_back(static_cast<char16_t>(0xdc00 + ((cp - 0x10000) & 0x3ff)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

std::string Utf16To8(std::u16string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t k = 0; k < s.size(); ++k) {
    std::uint32_t c = s[k];
    if (c >= 0xd800 && c <= 0xdbff && k + 1 < s.size() && s[k + 1] >= 0xdc00 && s[k + 1] <= 0xdfff) {
      c = 0x10000 + ((c - 0xd800) << 10) + (s[k + 1] - 0xdc00);
      ++k;
    } else if (c >= 0xd800 && c <= 0xdfff) {
      c = 0xfffd;  // unpaired surrogate
    }
    AppendUtf8(&out, c);
  }
  return out;
}

Status StringPool::Load(std::span<const std::uint8_t> chunk, std::uint32_t max_strings) {
  if (chunk.size() < 28) return Malformed("header truncated");
  header_size_ = U16(chunk.data() + 2);
  count_ = U32(chunk.data() + 8);
  style_count_ = U32(chunk.data() + 12);
  const std::uint32_t flags = U32(chunk.data() + 16);
  strings_start_ = U32(chunk.data() + 20);
  styles_start_ = U32(chunk.data() + 24);
  if (header_size_ < 28 || header_size_ > chunk.size()) return Malformed("header size");
  if (count_ > max_strings || style_count_ > count_) return Malformed("too many strings");
  if (static_cast<std::uint64_t>(count_) + style_count_ > (chunk.size() - header_size_) / 4) {
    return Malformed("offsets out of bounds");
  }
  if (count_ > 0 && strings_start_ >= chunk.size()) return Malformed("strings start out of bounds");
  if (style_count_ > 0 && styles_start_ >= chunk.size()) return Malformed("styles start out of bounds");
  utf8_ = (flags & kUtf8Flag) != 0;
  chunk_ = chunk;
  return Status::Ok();
}

Status StringPool::Locate(std::uint32_t index, std::size_t* at) const {
  if (index >= count_) return Malformed(std::format("index {} out of range", index));
  const std::uint64_t pos = static_cast<std::uint64_t>(strings_start_) + U32(chunk_.data() + header_size_ + 4ull * index);
  if (pos >= chunk_.size()) return Malformed(std::format("string {} out of bounds", index));
  *at = static_cast<std::size_t>(pos);
  return Status::Ok();
}

Status StringPool::Get(std::uint32_t index, std::string* out) const {
  out->clear();
  if (index == kNoString) return Status::Ok();
  std::size_t at = 0;
  ARUN_RETURN_IF_ERROR(Locate(index, &at));
  const std::span<const std::uint8_t> p = chunk_.subspan(at);
  if (utf8_) {
    // utf16 length (1-2 bytes), utf8 length (1-2 bytes), bytes, NUL.
    std::size_t i = 0;
    auto len = [&](std::uint32_t* v) -> bool {
      if (i >= p.size()) return false;
      std::uint32_t b = p[i++];
      if (b & 0x80) {
        if (i >= p.size()) return false;
        b = ((b & 0x7f) << 8) | p[i++];
      }
      *v = b;
      return true;
    };
    std::uint32_t u16len = 0, u8len = 0;
    if (!len(&u16len) || !len(&u8len)) return Malformed("UTF-8 length truncated");
    if (u8len > p.size() - i) return Malformed("UTF-8 string out of bounds");
    out->assign(reinterpret_cast<const char*>(p.data() + i), u8len);
    return Status::Ok();
  }
  std::u16string wide;
  ARUN_RETURN_IF_ERROR(Get16(index, &wide));
  *out = Utf16To8(wide);
  return Status::Ok();
}

Status StringPool::Get16(std::uint32_t index, std::u16string* out) const {
  out->clear();
  if (index == kNoString) return Status::Ok();
  if (utf8_) {
    std::string narrow;
    ARUN_RETURN_IF_ERROR(Get(index, &narrow));
    *out = Utf8To16(narrow);
    return Status::Ok();
  }
  std::size_t at = 0;
  ARUN_RETURN_IF_ERROR(Locate(index, &at));
  const std::span<const std::uint8_t> p = chunk_.subspan(at);
  if (p.size() < 2) return Malformed("UTF-16 length truncated");
  std::size_t i = 2;
  std::uint32_t n = U16(p.data());
  if (n & 0x8000) {
    if (p.size() < 4) return Malformed("UTF-16 length truncated");
    n = ((n & 0x7fff) << 16) | U16(p.data() + 2);
    i = 4;
  }
  if (n > (p.size() - i) / 2) return Malformed("UTF-16 string out of bounds");
  out->resize(n);
  for (std::uint32_t k = 0; k < n; ++k) (*out)[k] = static_cast<char16_t>(U16(p.data() + i + 2ull * k));
  return Status::Ok();
}

Status StringPool::Spans(std::uint32_t index, std::vector<StringSpan>* out) const {
  out->clear();
  if (index >= style_count_) return Status::Ok();
  const std::uint64_t pos =
      static_cast<std::uint64_t>(styles_start_) + U32(chunk_.data() + header_size_ + 4ull * (count_ + index));
  for (std::uint64_t at = pos;; at += 12) {
    if (at + 4 > chunk_.size()) return Malformed("style out of bounds");
    const std::uint32_t name = U32(chunk_.data() + at);
    if (name == kSpanEnd) break;
    if (at + 12 > chunk_.size()) return Malformed("style out of bounds");
    if (out->size() >= 4096) return Malformed("too many spans");
    out->push_back(StringSpan{name, U32(chunk_.data() + at + 4), U32(chunk_.data() + at + 8)});
  }
  return Status::Ok();
}

}  // namespace arun::axml
