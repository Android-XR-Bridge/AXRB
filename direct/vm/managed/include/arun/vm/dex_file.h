// A parsed, validated DEX file (spec 14.3; ADR 0004).
//
// Ownership: DexFile owns its bytes. Everything returned by reference or view
//   points into those bytes and lives as long as the DexFile.
// Thread affinity: immutable after Open(); safe to read from any thread.
// Error propagation: Result/Status with dex_* reason codes.
//
// Validation policy: Open() checks every table's bounds, every cross-table index,
// and every string's modified-UTF-8 encoding before returning. After that, the
// accessors below index tables directly -- the invariant is established once,
// here, not re-derived at each use. Class data and code items are validated when
// first read (ReadClassData / ReadCodeItem), because most classes in a large app
// are never touched.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arun/status.h"

namespace arun::vm {

constexpr std::uint32_t kDexNoIndex = 0xffffffffu;

// Access flags (DEX spec "access_flags definitions").
enum AccessFlags : std::uint32_t {
  kAccPublic = 0x1,
  kAccPrivate = 0x2,
  kAccProtected = 0x4,
  kAccStatic = 0x8,
  kAccFinal = 0x10,
  kAccSynchronized = 0x20,
  kAccVolatile = 0x40,
  kAccBridge = 0x40,
  kAccTransient = 0x80,
  kAccVarargs = 0x80,
  kAccNative = 0x100,
  kAccInterface = 0x200,
  kAccAbstract = 0x400,
  kAccStrict = 0x800,
  kAccSynthetic = 0x1000,
  kAccAnnotation = 0x2000,
  kAccEnum = 0x4000,
  kAccConstructor = 0x10000,
  kAccDeclaredSynchronized = 0x20000,
};

struct FieldId {
  std::uint16_t class_idx;
  std::uint16_t type_idx;
  std::uint32_t name_idx;
};

struct MethodId {
  std::uint16_t class_idx;
  std::uint16_t proto_idx;
  std::uint32_t name_idx;
};

struct ProtoId {
  std::uint32_t shorty_idx;
  std::uint32_t return_type_idx;
  std::uint32_t parameters_off;
};

struct ClassDef {
  std::uint32_t class_idx;
  std::uint32_t access_flags;
  std::uint32_t superclass_idx;
  std::uint32_t interfaces_off;
  std::uint32_t source_file_idx;
  std::uint32_t annotations_off;
  std::uint32_t class_data_off;
  std::uint32_t static_values_off;
};

struct EncodedField {
  std::uint32_t field_idx;
  std::uint32_t access_flags;
};

struct EncodedMethod {
  std::uint32_t method_idx;
  std::uint32_t access_flags;
  std::uint32_t code_off;  // 0 for abstract/native
};

struct ClassData {
  std::vector<EncodedField> static_fields;
  std::vector<EncodedField> instance_fields;
  std::vector<EncodedMethod> direct_methods;
  std::vector<EncodedMethod> virtual_methods;
};

struct TryItem {
  std::uint32_t start_addr;
  std::uint16_t insn_count;
  std::uint16_t handler_off;
};

struct CatchHandler {
  std::uint32_t type_idx;  // kDexNoIndex for catch-all
  std::uint32_t address;
};

// A validated code item. `insns` is in 16-bit code units.
struct CodeItem {
  std::uint16_t registers_size = 0;
  std::uint16_t ins_size = 0;
  std::uint16_t outs_size = 0;
  std::uint32_t debug_info_off = 0;
  std::span<const std::uint16_t> insns;
  std::span<const TryItem> tries;
  std::span<const std::uint8_t> handlers;  // encoded_catch_handler_list
};

enum class EncodedValueType : std::uint8_t {
  kByte = 0x00,
  kShort = 0x02,
  kChar = 0x03,
  kInt = 0x04,
  kLong = 0x06,
  kFloat = 0x10,
  kDouble = 0x11,
  kMethodType = 0x15,
  kMethodHandle = 0x16,
  kString = 0x17,
  kType = 0x18,
  kField = 0x19,
  kMethod = 0x1a,
  kEnum = 0x1b,
  kArray = 0x1c,
  kAnnotation = 0x1d,
  kNull = 0x1e,
  kBoolean = 0x1f,
};

struct EncodedValue {
  EncodedValueType type = EncodedValueType::kNull;
  // Numeric payload, already sign/zero-extended per type; floats/doubles as raw
  // bits. For kString/kType/kField/kMethod/kEnum it is the index.
  std::uint64_t bits = 0;
};

class DexFile {
 public:
  // Takes ownership of `bytes`. `location` is for diagnostics only.
  static Result<std::unique_ptr<DexFile>> Open(std::vector<std::uint8_t> bytes,
                                               std::string location);

  const std::string& location() const { return location_; }
  // 35, 37, 38 or 39.
  std::uint32_t version() const { return version_; }

  std::uint32_t num_strings() const { return num_strings_; }
  std::uint32_t num_types() const { return num_types_; }
  std::uint32_t num_protos() const { return num_protos_; }
  std::uint32_t num_fields() const { return num_fields_; }
  std::uint32_t num_methods() const { return num_methods_; }
  std::uint32_t num_class_defs() const { return num_class_defs_; }

  // Modified UTF-8 bytes of a string, without the terminator. Validated at Open.
  std::string_view StringMutf8(std::uint32_t idx) const;
  // UTF-16 length recorded in the file (validated against the data at Open).
  std::uint32_t StringUtf16Length(std::uint32_t idx) const;
  // Decodes a string to UTF-16.
  std::u16string StringUtf16(std::uint32_t idx) const;

  std::string_view TypeDescriptor(std::uint32_t type_idx) const;

  const ProtoId& GetProtoId(std::uint32_t idx) const { return protos_[idx]; }
  std::string_view ProtoShorty(std::uint32_t proto_idx) const;
  std::span<const std::uint16_t> ProtoParameters(std::uint32_t proto_idx) const;
  // "(Ljava/lang/String;I)V"
  std::string ProtoSignature(std::uint32_t proto_idx) const;

  const FieldId& GetFieldId(std::uint32_t idx) const { return fields_[idx]; }
  const MethodId& GetMethodId(std::uint32_t idx) const { return methods_[idx]; }
  const ClassDef& GetClassDef(std::uint32_t idx) const { return class_defs_[idx]; }

  std::optional<std::uint32_t> FindClassDef(std::string_view descriptor) const;
  std::span<const std::uint16_t> ClassInterfaces(const ClassDef& def) const;

  // Decoded and validated on each call; callers cache the result.
  Result<ClassData> ReadClassData(const ClassDef& def) const;
  Result<CodeItem> ReadCodeItem(std::uint32_t code_off) const;
  Result<std::vector<EncodedValue>> ReadEncodedArray(std::uint32_t offset) const;

  // Handlers covering `dex_pc`, in order. Empty if the pc is not inside a try.
  Result<std::vector<CatchHandler>> FindCatchHandlers(const CodeItem& code,
                                                     std::uint32_t dex_pc) const;

  // Line number for a pc, from the debug info. Best effort, diagnostics only.
  std::optional<std::uint32_t> LineNumber(const CodeItem& code, std::uint32_t dex_pc) const;
  std::string_view SourceFile(const ClassDef& def) const;

 private:
  DexFile() = default;
  Status Parse();

  std::vector<std::uint8_t> bytes_;
  std::string location_;
  std::uint32_t version_ = 0;

  std::uint32_t num_strings_ = 0, num_types_ = 0, num_protos_ = 0, num_fields_ = 0,
                num_methods_ = 0, num_class_defs_ = 0;
  // Parsed string table: offset and length of each string's MUTF-8 data.
  struct StringEntry {
    std::uint32_t data_off;
    std::uint32_t byte_len;
    std::uint32_t utf16_len;
  };
  std::vector<StringEntry> strings_;
  std::vector<std::uint32_t> type_descriptor_idx_;
  std::vector<ProtoId> protos_;
  std::vector<FieldId> fields_;
  std::vector<MethodId> methods_;
  std::vector<ClassDef> class_defs_;
  std::unordered_map<std::string_view, std::uint32_t> class_def_by_descriptor_;
};

// Modified UTF-8 (DEX strings) -> UTF-16. Assumes validated input.
std::u16string DecodeMutf8(std::string_view mutf8);
// UTF-16 -> standard UTF-8, for logging and host APIs. Unpaired surrogates become U+FFFD.
std::string Utf16ToUtf8(std::u16string_view s);
std::u16string Utf8ToUtf16(std::string_view s);

}  // namespace arun::vm
