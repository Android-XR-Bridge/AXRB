#include "arun/vm/dex_file.h"

#include <cstring>
#include <format>

namespace arun::vm {
namespace {

constexpr std::uint32_t kHeaderSize = 0x70;
constexpr std::uint32_t kEndianConstant = 0x12345678;

Status Malformed(std::string msg) { return Err(ReasonCode::kDexMalformed, std::move(msg)); }

// Bounds-checked cursor over untrusted bytes.
class Reader {
 public:
  Reader(std::span<const std::uint8_t> bytes, std::size_t pos) : bytes_(bytes), pos_(pos) {}

  bool ok() const { return ok_; }
  std::size_t pos() const { return pos_; }

  std::uint8_t U1() {
    if (!ok_ || pos_ >= bytes_.size()) return Fail();
    return bytes_[pos_++];
  }
  std::uint32_t Uleb() {
    std::uint32_t result = 0;
    for (int i = 0; i < 5; ++i) {
      const std::uint8_t b = U1();
      if (!ok_) return 0;
      if (i == 4 && (b & 0xf0) != 0) return Fail();  // more than 32 bits
      result |= static_cast<std::uint32_t>(b & 0x7f) << (7 * i);
      if ((b & 0x80) == 0) return result;
    }
    return Fail();
  }
  std::int32_t Sleb() {
    std::uint32_t result = 0;
    int shift = 0;
    std::uint8_t b = 0;
    for (int i = 0; i < 5; ++i) {
      b = U1();
      if (!ok_) return 0;
      result |= static_cast<std::uint32_t>(b & 0x7f) << shift;
      shift += 7;
      if ((b & 0x80) == 0) break;
      if (i == 4) return static_cast<std::int32_t>(Fail());
    }
    if (shift < 32 && (b & 0x40) != 0) result |= ~0u << shift;
    return static_cast<std::int32_t>(result);
  }
  // uleb128p1: value - 1, so 0 encodes NO_INDEX.
  std::uint32_t UlebP1() { return Uleb() - 1; }

 private:
  std::uint8_t Fail() {
    ok_ = false;
    return 0;
  }
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_;
  bool ok_ = true;
};

std::uint32_t Adler32(const std::uint8_t* data, std::size_t len) {
  std::uint32_t a = 1, b = 0;
  constexpr std::uint32_t kMod = 65521;
  while (len > 0) {
    const std::size_t chunk = len < 5552 ? len : 5552;  // keeps b below 2^32
    for (std::size_t i = 0; i < chunk; ++i) {
      a += data[i];
      b += a;
    }
    a %= kMod;
    b %= kMod;
    data += chunk;
    len -= chunk;
  }
  return (b << 16) | a;
}

bool InBounds(std::size_t total, std::uint64_t off, std::uint64_t size) {
  return off <= total && size <= total - off;
}

template <typename T>
T Load(const std::uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}

// Validates one MUTF-8 string starting at `p` (bounded by `end`). Returns the byte
// length (excluding the NUL) and the UTF-16 unit count through out-params.
bool ValidateMutf8(const std::uint8_t* p, const std::uint8_t* end, std::uint32_t* byte_len,
                   std::uint32_t* utf16_len) {
  const std::uint8_t* start = p;
  std::uint32_t units = 0;
  while (p < end) {
    const std::uint8_t c = *p;
    if (c == 0) {
      *byte_len = static_cast<std::uint32_t>(p - start);
      *utf16_len = units;
      return true;
    }
    if (c < 0x80) {
      p += 1;
    } else if ((c & 0xe0) == 0xc0) {
      if (end - p < 2 || (p[1] & 0xc0) != 0x80) return false;
      p += 2;
    } else if ((c & 0xf0) == 0xe0) {
      if (end - p < 3 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80) return false;
      p += 3;
    } else {
      return false;  // 4-byte forms are not used by MUTF-8 (surrogates are split)
    }
    ++units;
  }
  return false;  // unterminated
}

bool IsValidDescriptorChar(char c) {
  return c != '\0';
}

// Structural check of a type descriptor: [*(primitive|L...;)
bool IsValidTypeDescriptor(std::string_view d) {
  std::size_t i = 0;
  while (i < d.size() && d[i] == '[') ++i;
  if (i > 255) return false;
  if (i == d.size()) return false;
  const char c = d[i];
  if (c == 'L') {
    if (d.back() != ';' || d.size() - i < 3) return false;
    for (std::size_t k = i + 1; k + 1 < d.size(); ++k) {
      if (d[k] == ';' || d[k] == '.' || !IsValidDescriptorChar(d[k])) return false;
    }
    return true;
  }
  if (i + 1 != d.size()) return false;
  switch (c) {
    case 'Z': case 'B': case 'S': case 'C': case 'I': case 'J': case 'F': case 'D':
      return true;
    case 'V':
      return i == 0;  // void is valid only as a bare return type
    default:
      return false;
  }
}

}  // namespace

Result<std::unique_ptr<DexFile>> DexFile::Open(std::vector<std::uint8_t> bytes,
                                                std::string location) {
  auto dex = std::unique_ptr<DexFile>(new DexFile());
  dex->bytes_ = std::move(bytes);
  dex->location_ = std::move(location);
  Status s = dex->Parse();
  if (!s.ok()) return s.WithContext(dex->location_);
  return dex;
}

Status DexFile::Parse() {
  const std::size_t n = bytes_.size();
  const std::uint8_t* b = bytes_.data();
  if (n < kHeaderSize) return Malformed(std::format("{} bytes is smaller than a DEX header", n));
  if (std::memcmp(b, "dex\n", 4) != 0 || b[7] != 0) return Malformed("missing DEX magic");
  const char v[4] = {static_cast<char>(b[4]), static_cast<char>(b[5]), static_cast<char>(b[6]), 0};
  if (std::strcmp(v, "035") == 0) version_ = 35;
  else if (std::strcmp(v, "037") == 0) version_ = 37;
  else if (std::strcmp(v, "038") == 0) version_ = 38;
  else if (std::strcmp(v, "039") == 0) version_ = 39;
  else {
    return Err(ReasonCode::kDexUnsupported,
               std::format("DEX version '{}' is not implemented by this runtime (035-039 are); "
                           "040/041 are not emitted by d8 for apps, see "
                           "docs/android-version-support.md",
                           v));
  }

  const std::uint32_t checksum = Load<std::uint32_t>(b + 8);
  const std::uint32_t file_size = Load<std::uint32_t>(b + 32);
  const std::uint32_t header_size = Load<std::uint32_t>(b + 36);
  const std::uint32_t endian = Load<std::uint32_t>(b + 40);
  if (file_size != n) {
    return Malformed(std::format("header file_size {} but {} bytes present", file_size, n));
  }
  if (header_size != kHeaderSize) {
    return Malformed(std::format("header_size {:#x}, expected 0x70", header_size));
  }
  if (endian != kEndianConstant) return Malformed("unsupported endianness tag");
  if (Adler32(b + 12, n - 12) != checksum) {
    return Malformed("adler32 checksum mismatch: the DEX is corrupt");
  }

  auto table = [&](std::uint32_t size_off, std::uint32_t entry, const char* what,
                   std::uint32_t* count, std::uint32_t* offset) -> Status {
    *count = Load<std::uint32_t>(b + size_off);
    *offset = Load<std::uint32_t>(b + size_off + 4);
    if (*count != 0 &&
        !InBounds(n, *offset, static_cast<std::uint64_t>(*count) * entry)) {
      return Malformed(std::format("{} table ({} entries at {:#x}) exceeds the file", what,
                                   *count, *offset));
    }
    if (*count != 0 && (*offset % 4) != 0) {
      return Malformed(std::format("{} table is not 4-byte aligned", what));
    }
    return Status::Ok();
  };

  std::uint32_t strings_off, types_off, protos_off, fields_off, methods_off, classes_off;
  ARUN_RETURN_IF_ERROR(table(56, 4, "string_ids", &num_strings_, &strings_off));
  ARUN_RETURN_IF_ERROR(table(64, 4, "type_ids", &num_types_, &types_off));
  ARUN_RETURN_IF_ERROR(table(72, 12, "proto_ids", &num_protos_, &protos_off));
  ARUN_RETURN_IF_ERROR(table(80, 8, "field_ids", &num_fields_, &fields_off));
  ARUN_RETURN_IF_ERROR(table(88, 8, "method_ids", &num_methods_, &methods_off));
  ARUN_RETURN_IF_ERROR(table(96, 32, "class_defs", &num_class_defs_, &classes_off));
  if (num_types_ > 65536 || num_protos_ > 65536) {
    return Malformed("type_ids/proto_ids exceed the 16-bit index space");
  }

  // Strings: every one validated now, so later decoding can trust them.
  strings_.resize(num_strings_);
  for (std::uint32_t i = 0; i < num_strings_; ++i) {
    const std::uint32_t off = Load<std::uint32_t>(b + strings_off + 4ull * i);
    if (off >= n) return Malformed(std::format("string {} data offset out of range", i));
    Reader r(bytes_, off);
    const std::uint32_t declared = r.Uleb();
    if (!r.ok()) return Malformed(std::format("string {} length is malformed", i));
    std::uint32_t byte_len = 0, units = 0;
    if (!ValidateMutf8(b + r.pos(), b + n, &byte_len, &units)) {
      return Malformed(std::format("string {} is not valid modified UTF-8", i));
    }
    if (units != declared) {
      return Malformed(std::format("string {} declares {} UTF-16 units but encodes {}", i,
                                   declared, units));
    }
    strings_[i] = StringEntry{static_cast<std::uint32_t>(r.pos()), byte_len, units};
  }

  type_descriptor_idx_.resize(num_types_);
  for (std::uint32_t i = 0; i < num_types_; ++i) {
    const std::uint32_t s = Load<std::uint32_t>(b + types_off + 4ull * i);
    if (s >= num_strings_) return Malformed(std::format("type {} names string {}", i, s));
    type_descriptor_idx_[i] = s;
    if (!IsValidTypeDescriptor(StringMutf8(s))) {
      return Malformed(std::format("type {} has invalid descriptor '{}'", i, StringMutf8(s)));
    }
  }

  protos_.resize(num_protos_);
  for (std::uint32_t i = 0; i < num_protos_; ++i) {
    const std::uint8_t* p = b + protos_off + 12ull * i;
    ProtoId& pr = protos_[i];
    pr.shorty_idx = Load<std::uint32_t>(p);
    pr.return_type_idx = Load<std::uint32_t>(p + 4);
    pr.parameters_off = Load<std::uint32_t>(p + 8);
    if (pr.shorty_idx >= num_strings_ || pr.return_type_idx >= num_types_) {
      return Malformed(std::format("proto {} has an out-of-range index", i));
    }
    if (pr.parameters_off != 0) {
      if (pr.parameters_off % 4 != 0 || !InBounds(n, pr.parameters_off, 4)) {
        return Malformed(std::format("proto {} parameter list offset is invalid", i));
      }
      const std::uint32_t count = Load<std::uint32_t>(b + pr.parameters_off);
      if (!InBounds(n, pr.parameters_off + 4ull, 2ull * count)) {
        return Malformed(std::format("proto {} parameter list exceeds the file", i));
      }
      for (std::uint16_t t : ProtoParameters(i)) {
        if (t >= num_types_) return Malformed(std::format("proto {} parameter type invalid", i));
      }
    }
    // The shorty must agree with the full signature.
    const std::string_view shorty = StringMutf8(pr.shorty_idx);
    const auto params = ProtoParameters(i);
    if (shorty.size() != params.size() + 1) {
      return Malformed(std::format("proto {} shorty '{}' disagrees with its parameters", i,
                                   shorty));
    }
    auto shorty_of = [](std::string_view d) { return d[0] == '[' ? 'L' : d[0]; };
    if (shorty[0] != shorty_of(TypeDescriptor(pr.return_type_idx))) {
      return Malformed(std::format("proto {} shorty return type mismatch", i));
    }
    for (std::size_t k = 0; k < params.size(); ++k) {
      const std::string_view pd = TypeDescriptor(params[k]);
      if (pd == "V" || shorty[k + 1] != shorty_of(pd)) {
        return Malformed(std::format("proto {} shorty parameter mismatch", i));
      }
    }
  }

  fields_.resize(num_fields_);
  for (std::uint32_t i = 0; i < num_fields_; ++i) {
    const std::uint8_t* p = b + fields_off + 8ull * i;
    FieldId& f = fields_[i];
    f.class_idx = Load<std::uint16_t>(p);
    f.type_idx = Load<std::uint16_t>(p + 2);
    f.name_idx = Load<std::uint32_t>(p + 4);
    if (f.class_idx >= num_types_ || f.type_idx >= num_types_ || f.name_idx >= num_strings_ ||
        TypeDescriptor(f.type_idx) == "V") {
      return Malformed(std::format("field {} is invalid", i));
    }
  }

  methods_.resize(num_methods_);
  for (std::uint32_t i = 0; i < num_methods_; ++i) {
    const std::uint8_t* p = b + methods_off + 8ull * i;
    MethodId& m = methods_[i];
    m.class_idx = Load<std::uint16_t>(p);
    m.proto_idx = Load<std::uint16_t>(p + 2);
    m.name_idx = Load<std::uint32_t>(p + 4);
    if (m.class_idx >= num_types_ || m.proto_idx >= num_protos_ || m.name_idx >= num_strings_) {
      return Malformed(std::format("method {} has an out-of-range index", i));
    }
  }

  class_defs_.resize(num_class_defs_);
  for (std::uint32_t i = 0; i < num_class_defs_; ++i) {
    const std::uint8_t* p = b + classes_off + 32ull * i;
    ClassDef& c = class_defs_[i];
    c.class_idx = Load<std::uint32_t>(p);
    c.access_flags = Load<std::uint32_t>(p + 4);
    c.superclass_idx = Load<std::uint32_t>(p + 8);
    c.interfaces_off = Load<std::uint32_t>(p + 12);
    c.source_file_idx = Load<std::uint32_t>(p + 16);
    c.annotations_off = Load<std::uint32_t>(p + 20);
    c.class_data_off = Load<std::uint32_t>(p + 24);
    c.static_values_off = Load<std::uint32_t>(p + 28);
    if (c.class_idx >= num_types_ ||
        (c.superclass_idx != kDexNoIndex && c.superclass_idx >= num_types_) ||
        (c.source_file_idx != kDexNoIndex && c.source_file_idx >= num_strings_)) {
      return Malformed(std::format("class_def {} has an out-of-range index", i));
    }
    const std::string_view desc = TypeDescriptor(c.class_idx);
    if (desc[0] != 'L') return Malformed(std::format("class_def {} is not a class type", i));
    if (c.interfaces_off != 0) {
      if (c.interfaces_off % 4 != 0 || !InBounds(n, c.interfaces_off, 4)) {
        return Malformed(std::format("class_def {} interface list offset invalid", i));
      }
      const std::uint32_t count = Load<std::uint32_t>(b + c.interfaces_off);
      if (!InBounds(n, c.interfaces_off + 4ull, 2ull * count)) {
        return Malformed(std::format("class_def {} interface list exceeds the file", i));
      }
      for (std::uint16_t t : ClassInterfaces(c)) {
        if (t >= num_types_) return Malformed(std::format("class_def {} interface invalid", i));
      }
    }
    if (c.class_data_off != 0 && c.class_data_off >= n) {
      return Malformed(std::format("class_def {} class_data offset out of range", i));
    }
    if (!class_def_by_descriptor_.emplace(desc, i).second) {
      return Malformed(std::format("class '{}' is defined twice", desc));
    }
  }
  return Status::Ok();
}

std::string_view DexFile::StringMutf8(std::uint32_t idx) const {
  const StringEntry& e = strings_[idx];
  return std::string_view(reinterpret_cast<const char*>(bytes_.data() + e.data_off), e.byte_len);
}

std::uint32_t DexFile::StringUtf16Length(std::uint32_t idx) const { return strings_[idx].utf16_len; }

std::u16string DexFile::StringUtf16(std::uint32_t idx) const {
  return DecodeMutf8(StringMutf8(idx));
}

std::string_view DexFile::TypeDescriptor(std::uint32_t type_idx) const {
  return StringMutf8(type_descriptor_idx_[type_idx]);
}

std::string_view DexFile::ProtoShorty(std::uint32_t proto_idx) const {
  return StringMutf8(protos_[proto_idx].shorty_idx);
}

std::span<const std::uint16_t> DexFile::ProtoParameters(std::uint32_t proto_idx) const {
  const std::uint32_t off = protos_[proto_idx].parameters_off;
  if (off == 0) return {};
  const std::uint32_t count = Load<std::uint32_t>(bytes_.data() + off);
  return {reinterpret_cast<const std::uint16_t*>(bytes_.data() + off + 4), count};
}

std::string DexFile::ProtoSignature(std::uint32_t proto_idx) const {
  std::string out = "(";
  for (std::uint16_t t : ProtoParameters(proto_idx)) out += TypeDescriptor(t);
  out += ")";
  out += TypeDescriptor(protos_[proto_idx].return_type_idx);
  return out;
}

std::optional<std::uint32_t> DexFile::FindClassDef(std::string_view descriptor) const {
  auto it = class_def_by_descriptor_.find(descriptor);
  if (it == class_def_by_descriptor_.end()) return std::nullopt;
  return it->second;
}

std::span<const std::uint16_t> DexFile::ClassInterfaces(const ClassDef& def) const {
  if (def.interfaces_off == 0) return {};
  const std::uint32_t count = Load<std::uint32_t>(bytes_.data() + def.interfaces_off);
  return {reinterpret_cast<const std::uint16_t*>(bytes_.data() + def.interfaces_off + 4), count};
}

Result<ClassData> DexFile::ReadClassData(const ClassDef& def) const {
  ClassData data;
  if (def.class_data_off == 0) return data;
  Reader r(bytes_, def.class_data_off);
  const std::uint32_t sizes[4] = {r.Uleb(), r.Uleb(), r.Uleb(), r.Uleb()};
  if (!r.ok()) return Malformed("class_data header is truncated");
  // Every entry takes at least two bytes; reject counts the file cannot hold
  // before reserving memory for them.
  const std::uint64_t total = static_cast<std::uint64_t>(sizes[0]) + sizes[1] + sizes[2] + sizes[3];
  if (total * 2 > bytes_.size()) return Malformed("class_data member counts exceed the file");

  auto read_fields = [&](std::uint32_t count, std::vector<EncodedField>& out) -> Status {
    std::uint32_t idx = 0;
    for (std::uint32_t k = 0; k < count; ++k) {
      const std::uint32_t diff = r.Uleb();
      const std::uint32_t flags = r.Uleb();
      if (!r.ok()) return Malformed("class_data field list is truncated");
      if (k > 0 && diff == 0) return Malformed("class_data lists a field twice");
      idx += diff;
      if (idx >= num_fields_) return Malformed("class_data field index out of range");
      out.push_back(EncodedField{idx, flags});
    }
    return Status::Ok();
  };
  auto read_methods = [&](std::uint32_t count, std::vector<EncodedMethod>& out) -> Status {
    std::uint32_t idx = 0;
    for (std::uint32_t k = 0; k < count; ++k) {
      const std::uint32_t diff = r.Uleb();
      const std::uint32_t flags = r.Uleb();
      const std::uint32_t code = r.Uleb();
      if (!r.ok()) return Malformed("class_data method list is truncated");
      if (k > 0 && diff == 0) return Malformed("class_data lists a method twice");
      idx += diff;
      if (idx >= num_methods_) return Malformed("class_data method index out of range");
      const bool needs_code = (flags & (kAccAbstract | kAccNative)) == 0;
      if (needs_code != (code != 0)) {
        return Malformed(std::format("method {} code presence contradicts its access flags",
                                     idx));
      }
      out.push_back(EncodedMethod{idx, flags, code});
    }
    return Status::Ok();
  };
  ARUN_RETURN_IF_ERROR(read_fields(sizes[0], data.static_fields));
  ARUN_RETURN_IF_ERROR(read_fields(sizes[1], data.instance_fields));
  ARUN_RETURN_IF_ERROR(read_methods(sizes[2], data.direct_methods));
  ARUN_RETURN_IF_ERROR(read_methods(sizes[3], data.virtual_methods));
  return data;
}

Result<CodeItem> DexFile::ReadCodeItem(std::uint32_t code_off) const {
  const std::size_t n = bytes_.size();
  if (code_off % 4 != 0 || !InBounds(n, code_off, 16)) {
    return Malformed(std::format("code item offset {:#x} is invalid", code_off));
  }
  const std::uint8_t* p = bytes_.data() + code_off;
  CodeItem c;
  c.registers_size = Load<std::uint16_t>(p);
  c.ins_size = Load<std::uint16_t>(p + 2);
  c.outs_size = Load<std::uint16_t>(p + 4);
  const std::uint16_t tries_size = Load<std::uint16_t>(p + 6);
  c.debug_info_off = Load<std::uint32_t>(p + 8);
  const std::uint32_t insns_size = Load<std::uint32_t>(p + 12);
  if (c.ins_size > c.registers_size) return Malformed("code item has more ins than registers");
  if (insns_size == 0) return Malformed("code item has no instructions");
  if (!InBounds(n, code_off + 16ull, 2ull * insns_size)) {
    return Malformed("code item instructions exceed the file");
  }
  c.insns = {reinterpret_cast<const std::uint16_t*>(p + 16), insns_size};
  if (tries_size != 0) {
    std::uint64_t tries_off = code_off + 16ull + 2ull * insns_size;
    if (insns_size % 2 != 0) tries_off += 2;  // padding
    if (!InBounds(n, tries_off, 8ull * tries_size)) return Malformed("try items exceed the file");
    c.tries = {reinterpret_cast<const TryItem*>(bytes_.data() + tries_off), tries_size};
    const std::uint64_t handlers_off = tries_off + 8ull * tries_size;
    if (handlers_off > n) return Malformed("catch handler list exceeds the file");
    c.handlers = std::span<const std::uint8_t>(bytes_).subspan(static_cast<std::size_t>(handlers_off));
    for (const TryItem& t : c.tries) {
      if (static_cast<std::uint64_t>(t.start_addr) + t.insn_count > insns_size) {
        return Malformed("try item covers code outside the method");
      }
      if (t.handler_off >= c.handlers.size()) return Malformed("try item handler offset invalid");
    }
  }
  return c;
}

Result<std::vector<CatchHandler>> DexFile::FindCatchHandlers(const CodeItem& code,
                                                             std::uint32_t dex_pc) const {
  std::vector<CatchHandler> out;
  for (const TryItem& t : code.tries) {
    if (dex_pc < t.start_addr || dex_pc >= t.start_addr + t.insn_count) continue;
    Reader r(code.handlers, t.handler_off);
    const std::int32_t size = r.Sleb();
    if (!r.ok() || size > 65536 || size < -65536) return Malformed("catch handler is malformed");
    const std::int32_t count = size < 0 ? -size : size;
    for (std::int32_t k = 0; k < count; ++k) {
      const std::uint32_t type_idx = r.Uleb();
      const std::uint32_t addr = r.Uleb();
      if (!r.ok() || type_idx >= num_types_ || addr >= code.insns.size()) {
        return Malformed("catch handler entry is malformed");
      }
      out.push_back(CatchHandler{type_idx, addr});
    }
    if (size <= 0) {
      const std::uint32_t addr = r.Uleb();
      if (!r.ok() || addr >= code.insns.size()) return Malformed("catch-all address invalid");
      out.push_back(CatchHandler{kDexNoIndex, addr});
    }
    return out;  // try items do not overlap; the first match is the only one
  }
  return out;
}

Result<std::vector<EncodedValue>> DexFile::ReadEncodedArray(std::uint32_t offset) const {
  std::vector<EncodedValue> out;
  if (offset == 0) return out;
  if (offset >= bytes_.size()) return Malformed("encoded array offset out of range");
  Reader r(bytes_, offset);
  const std::uint32_t size = r.Uleb();
  if (!r.ok() || size > bytes_.size()) return Malformed("encoded array size invalid");
  for (std::uint32_t i = 0; i < size; ++i) {
    const std::uint8_t header = r.U1();
    if (!r.ok()) return Malformed("encoded array truncated");
    const auto type = static_cast<EncodedValueType>(header & 0x1f);
    const std::uint32_t arg = header >> 5;
    EncodedValue v;
    v.type = type;
    auto read_bytes = [&](std::uint32_t count, bool sign_extend) -> std::uint64_t {
      std::uint64_t value = 0;
      for (std::uint32_t k = 0; k < count; ++k) value |= static_cast<std::uint64_t>(r.U1()) << (8 * k);
      if (sign_extend && count < 8 && (value >> (8 * count - 1)) & 1) value |= ~0ull << (8 * count);
      return value;
    };
    switch (type) {
      case EncodedValueType::kByte:
        if (arg != 0) return Malformed("encoded byte has a bad size");
        v.bits = read_bytes(1, true);
        break;
      case EncodedValueType::kShort:
      case EncodedValueType::kInt:
      case EncodedValueType::kLong: {
        const std::uint32_t max = type == EncodedValueType::kShort ? 1 : type == EncodedValueType::kInt ? 3 : 7;
        if (arg > max) return Malformed("encoded integer has a bad size");
        v.bits = read_bytes(arg + 1, true);
        break;
      }
      case EncodedValueType::kChar:
        if (arg > 1) return Malformed("encoded char has a bad size");
        v.bits = read_bytes(arg + 1, false);
        break;
      case EncodedValueType::kFloat:
      case EncodedValueType::kDouble: {
        const std::uint32_t width = type == EncodedValueType::kFloat ? 4 : 8;
        if (arg + 1 > width) return Malformed("encoded floating value has a bad size");
        // Bytes are the HIGH-order bytes of the value, zero-extended to the right.
        v.bits = read_bytes(arg + 1, false) << (8 * (width - (arg + 1)));
        break;
      }
      case EncodedValueType::kString:
      case EncodedValueType::kType:
      case EncodedValueType::kField:
      case EncodedValueType::kMethod:
      case EncodedValueType::kEnum:
      case EncodedValueType::kMethodType:
      case EncodedValueType::kMethodHandle: {
        if (arg > 3) return Malformed("encoded index has a bad size");
        v.bits = read_bytes(arg + 1, false);
        const std::uint64_t limit =
            type == EncodedValueType::kString ? num_strings_
            : type == EncodedValueType::kType ? num_types_
            : type == EncodedValueType::kMethod ? num_methods_
            : (type == EncodedValueType::kField || type == EncodedValueType::kEnum) ? num_fields_
            : 0xffffffffull;
        if (v.bits >= limit) return Malformed("encoded value index out of range");
        break;
      }
      case EncodedValueType::kNull:
        if (arg != 0) return Malformed("encoded null has a bad size");
        break;
      case EncodedValueType::kBoolean:
        if (arg > 1) return Malformed("encoded boolean has a bad value");
        v.bits = arg;
        break;
      case EncodedValueType::kArray:
      case EncodedValueType::kAnnotation:
        // Nested arrays/annotations never appear in static_values of classes this
        // runtime initialises; they are reported rather than half-parsed.
        return Err(ReasonCode::kDexUnsupported,
                   "nested encoded array/annotation in a static initialiser list");
      default:
        return Malformed(std::format("unknown encoded value type {:#x}", header & 0x1f));
    }
    if (!r.ok()) return Malformed("encoded value truncated");
    out.push_back(v);
  }
  return out;
}

std::optional<std::uint32_t> DexFile::LineNumber(const CodeItem& code,
                                                 std::uint32_t dex_pc) const {
  if (code.debug_info_off == 0 || code.debug_info_off >= bytes_.size()) return std::nullopt;
  Reader r(bytes_, code.debug_info_off);
  std::int64_t line = r.Uleb();
  const std::uint32_t params = r.Uleb();
  if (!r.ok() || params > 65535) return std::nullopt;
  for (std::uint32_t i = 0; i < params; ++i) r.Uleb();
  std::uint32_t address = 0;
  std::optional<std::uint32_t> best;
  for (int guard = 0; guard < 1'000'000 && r.ok(); ++guard) {
    const std::uint8_t op = r.U1();
    switch (op) {
      case 0x00: return best;  // DBG_END_SEQUENCE
      case 0x01: address += r.Uleb(); break;
      case 0x02: line += r.Sleb(); break;
      case 0x03: r.Uleb(); r.Uleb(); r.Uleb(); break;
      case 0x04: r.Uleb(); r.Uleb(); r.Uleb(); r.Uleb(); break;
      case 0x05: case 0x06: r.Uleb(); break;
      case 0x07: case 0x08: break;
      case 0x09: r.Uleb(); break;
      default: {
        const std::uint32_t adjusted = op - 0x0a;
        line += -4 + static_cast<std::int32_t>(adjusted % 15);
        address += adjusted / 15;
        if (address > dex_pc) return best;
        if (line > 0) best = static_cast<std::uint32_t>(line);
        break;
      }
    }
  }
  return best;
}

std::string_view DexFile::SourceFile(const ClassDef& def) const {
  if (def.source_file_idx == kDexNoIndex) return {};
  return StringMutf8(def.source_file_idx);
}

std::u16string DecodeMutf8(std::string_view s) {
  std::u16string out;
  out.reserve(s.size());
  const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
  const auto* end = p + s.size();
  while (p < end) {
    const std::uint8_t c = *p;
    if (c < 0x80) {
      out.push_back(c);
      p += 1;
    } else if ((c & 0xe0) == 0xc0 && end - p >= 2) {
      out.push_back(static_cast<char16_t>(((c & 0x1f) << 6) | (p[1] & 0x3f)));
      p += 2;
    } else if ((c & 0xf0) == 0xe0 && end - p >= 3) {
      out.push_back(static_cast<char16_t>(((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f)));
      p += 3;
    } else {
      out.push_back(u'�');
      p += 1;
    }
  }
  return out;
}

std::string Utf16ToUtf8(std::u16string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    std::uint32_t cp = s[i];
    if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (s[i + 1] - 0xdc00);
      ++i;
    } else if (cp >= 0xd800 && cp <= 0xdfff) {
      cp = 0xfffd;
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }
  return out;
}

std::u16string Utf8ToUtf16(std::string_view s) {
  std::u16string out;
  out.reserve(s.size());
  const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
  const auto* end = p + s.size();
  while (p < end) {
    std::uint32_t cp;
    const std::uint8_t c = *p;
    int len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
    if (len == 0 || end - p < len) {
      out.push_back(u'�');
      ++p;
      continue;
    }
    if (len == 1) cp = c;
    else if (len == 2) cp = ((c & 0x1f) << 6) | (p[1] & 0x3f);
    else if (len == 3) cp = ((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
    else cp = ((c & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
    p += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xd800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xdc00 + (cp & 0x3ff)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

}  // namespace arun::vm
