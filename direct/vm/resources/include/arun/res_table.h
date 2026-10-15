// resources.arsc: one compiled resource table (an application's, or the
// runtime's own framework package), with configuration matching.
//
// Ownership: ResourceTable owns a copy of the table bytes; every view it hands
//   out borrows from it and lives as long as the table.
// Thread affinity: const after Load; concurrent lookups are safe.
// Error propagation: Load returns Status (kManifestMalformed for malformed
//   input); lookups return std::nullopt for "no such resource".
//
// Hostile input: every chunk, offset, count and index is bounds-checked against
// the chunk that contains it before use. Entries are decoded lazily, so a
// malformed entry is reported when looked up, never read out of bounds.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arun/res_string_pool.h"
#include "arun/status.h"

namespace arun::res {

// android::Res_value data types.
enum : std::uint8_t {
  kTypeNull = 0x00,
  kTypeReference = 0x01,
  kTypeAttribute = 0x02,
  kTypeString = 0x03,
  kTypeFloat = 0x04,
  kTypeDimension = 0x05,
  kTypeFraction = 0x06,
  kTypeDynamicReference = 0x07,
  kTypeDynamicAttribute = 0x08,
  kTypeIntDec = 0x10,
  kTypeIntHex = 0x11,
  kTypeIntBoolean = 0x12,
  kTypeIntColorArgb8 = 0x1c,
  kTypeIntColorRgb8 = 0x1d,
  kTypeIntColorArgb4 = 0x1e,
  kTypeIntColorRgb4 = 0x1f,
};

// Res_value::DATA_NULL_*: a null value that means "undefined" vs "@null".
constexpr std::uint32_t kDataNullUndefined = 0;
constexpr std::uint32_t kDataNullEmpty = 1;

struct Value {
  std::uint8_t type = kTypeNull;
  std::uint32_t data = 0;
};

// ResTable_config, normalised: languages and countries unpacked to lowercase /
// uppercase ASCII ("" when unset).
struct Config {
  static constexpr std::uint16_t kDensityDefault = 0;
  static constexpr std::uint16_t kDensityMedium = 160;
  static constexpr std::uint16_t kDensityAny = 0xfffe;
  static constexpr std::uint16_t kDensityNone = 0xffff;

  std::uint16_t mcc = 0, mnc = 0;
  std::string language, country;  // "en", "US"
  std::uint8_t orientation = 0, touchscreen = 0;
  std::uint16_t density = 0;
  std::uint8_t keyboard = 0, navigation = 0, input_flags = 0;
  std::uint16_t screen_width = 0, screen_height = 0;
  std::uint16_t sdk_version = 0, minor_version = 0;
  std::uint8_t screen_layout = 0, ui_mode = 0;
  std::uint16_t smallest_screen_width_dp = 0, screen_width_dp = 0, screen_height_dp = 0;
  std::string locale_script, locale_variant;
  std::uint8_t screen_layout2 = 0, color_mode = 0;

  // True if a resource in this configuration may be used on `device`.
  bool Match(const Config& device) const;
  // True if this (matching) configuration is a better choice for `requested`
  // than `other` (also matching). Follows ResTable_config::isBetterThan.
  bool IsBetterThan(const Config& other, const Config& requested) const;
  // "default", or aapt-style qualifiers ("de-rDE-xhdpi-v21").
  std::string ToString() const;
};

// Decodes a ResTable_config record of `size` bytes (bounds already checked).
Config ParseConfig(std::span<const std::uint8_t> bytes);

// One entry of a resolved lookup.
struct Entry {
  std::uint32_t key = 0;          // index into the package's key strings
  bool complex = false;           // a bag (style, array, plurals, attr)
  bool is_public = false;
  Value value;                    // simple entries
  std::uint32_t parent = 0;       // bags: parent bag resource id (0 = none)
  std::uint32_t count = 0;        // bags: number of (name, value) pairs
  std::span<const std::uint8_t> map;  // bags: count * 12 bytes, ResTable_map
  const Config* config = nullptr;
  std::uint32_t spec_flags = 0;   // ResTable_typeSpec flags: configurations this varies with

  // Bag pair `i` (i < count): the attribute/key id and its value.
  std::pair<std::uint32_t, Value> BagPair(std::uint32_t i) const;
};

class ResourceTable {
 public:
  static Result<std::unique_ptr<ResourceTable>> Load(std::span<const std::uint8_t> bytes);

  // The value string pool (Value::data of kTypeString values indexes it).
  const axml::StringPool& strings() const { return strings_; }

  // Package ids this table defines (0x01 framework, 0x7f application).
  std::vector<std::uint8_t> PackageIds() const;
  std::string PackageName(std::uint8_t package_id) const;

  // Best entry for `id` on `device`, or nullopt if the resource does not
  // exist or no configuration matches. Status errors are malformed entries.
  Result<std::optional<Entry>> Find(std::uint32_t id, const Config& device) const;

  // "package:type/entry", or nullopt for an unknown id.
  std::optional<std::string> Name(std::uint32_t id) const;
  // Resource id for (package, type, entry) names, or 0.
  std::uint32_t Identifier(std::string_view package, std::string_view type, std::string_view entry) const;

  // Every configuration the table has variants for (for diagnostics/tests).
  std::vector<std::string> Configurations() const;

 private:
  struct Variant {
    Config config;
    std::span<const std::uint8_t> chunk;
    std::uint32_t entries_start = 0;
    std::uint32_t entry_count = 0;
    std::uint32_t offsets_at = 0;  // offset of the index array inside chunk
    std::uint8_t flags = 0;
  };
  struct Type {
    std::uint8_t id = 0;
    std::vector<std::uint32_t> spec_flags;
    std::vector<Variant> variants;
  };
  struct Package {
    std::uint8_t id = 0;
    std::string name;
    axml::StringPool type_strings;
    axml::StringPool key_strings;
    std::uint32_t type_id_offset = 0;
    std::vector<Type> types;  // index = type id - 1
  };

  ResourceTable() = default;
  Status ParsePackage(std::span<const std::uint8_t> chunk);
  const Package* FindPackage(std::uint8_t id) const;
  // Offset of entry `index` in `v`, or nullopt for NO_ENTRY.
  Result<std::optional<std::uint32_t>> EntryOffset(const Variant& v, std::uint32_t index) const;
  Result<Entry> DecodeEntry(const Variant& v, std::uint32_t offset) const;

  std::vector<std::uint8_t> bytes_;
  axml::StringPool strings_;
  std::vector<Package> packages_;

  mutable std::mutex names_lock_;
  mutable std::unordered_map<std::string, std::uint32_t> names_;  // "pkg:type/entry" -> id
  mutable bool names_built_ = false;
};

}  // namespace arun::res
