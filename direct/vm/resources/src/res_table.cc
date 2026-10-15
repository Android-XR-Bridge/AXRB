#include "arun/res_table.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace arun::res {
namespace {

constexpr std::uint16_t kStringPoolType = 0x0001;
constexpr std::uint16_t kTableType = 0x0002;
constexpr std::uint16_t kPackageType = 0x0200;
constexpr std::uint16_t kTypeType = 0x0201;
constexpr std::uint16_t kTypeSpecType = 0x0202;

constexpr std::uint8_t kTypeFlagSparse = 0x01;
constexpr std::uint8_t kTypeFlagOffset16 = 0x02;
constexpr std::uint16_t kEntryFlagComplex = 0x0001;
constexpr std::uint16_t kEntryFlagPublic = 0x0002;
constexpr std::uint16_t kEntryFlagCompact = 0x0008;
constexpr std::uint32_t kNoEntry = 0xffffffffu;

// ResTable_config masks.
constexpr std::uint8_t kLayoutDirMask = 0xC0;
constexpr std::uint8_t kScreenSizeMask = 0x0F;
constexpr std::uint8_t kScreenLongMask = 0x30;
constexpr std::uint8_t kScreenSizeNormal = 0x02;
constexpr std::uint8_t kScreenRoundMask = 0x03;
constexpr std::uint8_t kWideGamutMask = 0x03;
constexpr std::uint8_t kHdrMask = 0x0C;
constexpr std::uint8_t kUiModeTypeMask = 0x0F;
constexpr std::uint8_t kUiModeNightMask = 0x30;
constexpr std::uint8_t kKeysHiddenMask = 0x03;
constexpr std::uint8_t kKeysHiddenNo = 0x01;
constexpr std::uint8_t kKeysHiddenSoft = 0x03;
constexpr std::uint8_t kNavHiddenMask = 0x0C;

Status Malformed(std::string what) { return Err(ReasonCode::kManifestMalformed, "resources.arsc: " + what); }

std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t U32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct Chunk {
  std::uint16_t type = 0;
  std::uint16_t header_size = 0;
  std::span<const std::uint8_t> bytes;
};

// Splits `data` into chunks; every chunk lies within `data`.
Status Chunks(std::span<const std::uint8_t> data, std::vector<Chunk>* out) {
  std::size_t pos = 0;
  while (pos < data.size()) {
    if (data.size() - pos < 8) return Malformed("chunk header truncated");
    const std::uint16_t type = U16(data.data() + pos);
    const std::uint16_t header_size = U16(data.data() + pos + 2);
    const std::uint32_t size = U32(data.data() + pos + 4);
    if (header_size < 8 || size < header_size || size > data.size() - pos) {
      return Malformed(std::format("chunk 0x{:04x} at {} has a bad size", type, pos));
    }
    out->push_back(Chunk{type, header_size, data.subspan(pos, size)});
    pos += (size + 3u) & ~3u;
    if (out->size() > 1u << 20) return Malformed("too many chunks");
  }
  return Status::Ok();
}

// ResTable_config::unpackLanguageOrRegion.
std::string UnpackCode(const std::uint8_t in[2], char base) {
  if (in[0] == 0 && in[1] == 0) return {};
  if (in[0] & 0x80) {
    const int first = in[1] & 0x1f;
    const int second = ((in[1] & 0xe0) >> 5) + ((in[0] & 0x03) << 3);
    const int third = (in[0] & 0x7c) >> 2;
    return {static_cast<char>(first + base), static_cast<char>(second + base), static_cast<char>(third + base)};
  }
  std::string s;
  for (int i = 0; i < 2; ++i) {
    if (in[i] != 0) s.push_back(static_cast<char>(in[i]));
  }
  return s;
}

std::string FixedString(const std::uint8_t* p, std::size_t n) {
  std::string s;
  for (std::size_t i = 0; i < n && p[i] != 0; ++i) s.push_back(static_cast<char>(p[i]));
  return s;
}

}  // namespace

// --- Config ------------------------------------------------------------------------

Config ParseConfig(std::span<const std::uint8_t> bytes) {
  std::uint8_t c[64] = {};
  std::memcpy(c, bytes.data(), std::min<std::size_t>(bytes.size(), sizeof(c)));
  Config k;
  k.mcc = U16(c + 4);
  k.mnc = U16(c + 6);
  k.language = UnpackCode(c + 8, 'a');
  k.country = UnpackCode(c + 10, '0');
  k.orientation = c[12];
  k.touchscreen = c[13];
  k.density = U16(c + 14);
  k.keyboard = c[16];
  k.navigation = c[17];
  k.input_flags = c[18];
  k.screen_width = U16(c + 20);
  k.screen_height = U16(c + 22);
  k.sdk_version = U16(c + 24);
  k.minor_version = U16(c + 26);
  k.screen_layout = c[28];
  k.ui_mode = c[29];
  k.smallest_screen_width_dp = U16(c + 30);
  k.screen_width_dp = U16(c + 32);
  k.screen_height_dp = U16(c + 34);
  k.locale_script = FixedString(c + 36, 4);
  k.locale_variant = FixedString(c + 40, 8);
  k.screen_layout2 = c[48];
  k.color_mode = c[49];
  return k;
}

bool Config::Match(const Config& d) const {
  if (mcc != 0 && mcc != d.mcc) return false;
  if (mnc != 0 && mnc != d.mnc) return false;
  if (!language.empty()) {
    if (language != d.language) return false;
    if (!country.empty() && country != d.country) return false;
    if (!locale_variant.empty() && locale_variant != d.locale_variant) return false;
  }
  if ((screen_layout & kLayoutDirMask) != 0 && (screen_layout & kLayoutDirMask) != (d.screen_layout & kLayoutDirMask)) {
    return false;
  }
  if ((screen_layout & kScreenSizeMask) != 0 && (screen_layout & kScreenSizeMask) > (d.screen_layout & kScreenSizeMask)) {
    return false;
  }
  if ((screen_layout & kScreenLongMask) != 0 && (screen_layout & kScreenLongMask) != (d.screen_layout & kScreenLongMask)) {
    return false;
  }
  if ((screen_layout2 & kScreenRoundMask) != 0 &&
      (screen_layout2 & kScreenRoundMask) != (d.screen_layout2 & kScreenRoundMask)) {
    return false;
  }
  if ((color_mode & kWideGamutMask) != 0 && (color_mode & kWideGamutMask) != (d.color_mode & kWideGamutMask)) {
    return false;
  }
  if ((color_mode & kHdrMask) != 0 && (color_mode & kHdrMask) != (d.color_mode & kHdrMask)) return false;
  if ((ui_mode & kUiModeTypeMask) != 0 && (ui_mode & kUiModeTypeMask) != (d.ui_mode & kUiModeTypeMask)) return false;
  if ((ui_mode & kUiModeNightMask) != 0 && (ui_mode & kUiModeNightMask) != (d.ui_mode & kUiModeNightMask)) {
    return false;
  }
  if (smallest_screen_width_dp != 0 && smallest_screen_width_dp > d.smallest_screen_width_dp) return false;
  if (screen_width_dp != 0 && screen_width_dp > d.screen_width_dp) return false;
  if (screen_height_dp != 0 && screen_height_dp > d.screen_height_dp) return false;
  if (orientation != 0 && orientation != d.orientation) return false;
  if (touchscreen != 0 && touchscreen != d.touchscreen) return false;
  const std::uint8_t keys = input_flags & kKeysHiddenMask;
  if (keys != 0) {
    const std::uint8_t dkeys = d.input_flags & kKeysHiddenMask;
    // "keysexposed" also matches a device whose only keyboard is the soft one.
    if (keys != dkeys && !(keys == kKeysHiddenNo && dkeys == kKeysHiddenSoft)) return false;
  }
  if ((input_flags & kNavHiddenMask) != 0 && (input_flags & kNavHiddenMask) != (d.input_flags & kNavHiddenMask)) {
    return false;
  }
  if (keyboard != 0 && keyboard != d.keyboard) return false;
  if (navigation != 0 && navigation != d.navigation) return false;
  if (screen_width != 0 && screen_width > d.screen_width) return false;
  if (screen_height != 0 && screen_height > d.screen_height) return false;
  if (sdk_version != 0 && sdk_version > d.sdk_version) return false;
  if (minor_version != 0 && minor_version != d.minor_version) return false;
  return true;
}

bool Config::IsBetterThan(const Config& o, const Config& r) const {
  if (mcc != o.mcc && r.mcc != 0) return mcc != 0;
  if (mnc != o.mnc && r.mnc != 0) return mnc != 0;

  // Locale: a configuration naming the requested language (then country, then
  // variant) beats one that does not. Both matched, so any value set equals r's.
  if (!r.language.empty()) {
    if (language != o.language) return !language.empty();
    if (country != o.country && !r.country.empty()) return !country.empty();
    if (locale_variant != o.locale_variant && !r.locale_variant.empty()) return !locale_variant.empty();
  }

  if (((screen_layout ^ o.screen_layout) & kLayoutDirMask) != 0 && (r.screen_layout & kLayoutDirMask) != 0) {
    return (screen_layout & kLayoutDirMask) > (o.screen_layout & kLayoutDirMask);
  }
  if (smallest_screen_width_dp != o.smallest_screen_width_dp) {
    return smallest_screen_width_dp > o.smallest_screen_width_dp;
  }
  if (screen_width_dp != o.screen_width_dp || screen_height_dp != o.screen_height_dp) {
    int mine = 0, other = 0;
    if (r.screen_width_dp != 0) {
      mine += r.screen_width_dp - screen_width_dp;
      other += r.screen_width_dp - o.screen_width_dp;
    }
    if (r.screen_height_dp != 0) {
      mine += r.screen_height_dp - screen_height_dp;
      other += r.screen_height_dp - o.screen_height_dp;
    }
    if (mine != other) return mine < other;
  }
  if (((screen_layout ^ o.screen_layout) & kScreenSizeMask) != 0 && (r.screen_layout & kScreenSizeMask) != 0) {
    int my = screen_layout & kScreenSizeMask, their = o.screen_layout & kScreenSizeMask;
    int fixed_my = my, fixed_their = their;
    if ((r.screen_layout & kScreenSizeMask) >= kScreenSizeNormal) {
      if (fixed_my == 0) fixed_my = kScreenSizeNormal;
      if (fixed_their == 0) fixed_their = kScreenSizeNormal;
    }
    if (fixed_my == fixed_their) return my != 0;
    return fixed_my > fixed_their;
  }
  if (((screen_layout ^ o.screen_layout) & kScreenLongMask) != 0 && (r.screen_layout & kScreenLongMask) != 0) {
    return (screen_layout & kScreenLongMask) != 0;
  }
  if (((screen_layout2 ^ o.screen_layout2) & kScreenRoundMask) != 0 && (r.screen_layout2 & kScreenRoundMask) != 0) {
    return (screen_layout2 & kScreenRoundMask) != 0;
  }
  if (((color_mode ^ o.color_mode) & kHdrMask) != 0 && (r.color_mode & kHdrMask) != 0) {
    return (color_mode & kHdrMask) != 0;
  }
  if (((color_mode ^ o.color_mode) & kWideGamutMask) != 0 && (r.color_mode & kWideGamutMask) != 0) {
    return (color_mode & kWideGamutMask) != 0;
  }
  if (orientation != o.orientation && r.orientation != 0) return orientation != 0;
  if (((ui_mode ^ o.ui_mode) & kUiModeTypeMask) != 0 && (r.ui_mode & kUiModeTypeMask) != 0) {
    return (ui_mode & kUiModeTypeMask) != 0;
  }
  if (((ui_mode ^ o.ui_mode) & kUiModeNightMask) != 0 && (r.ui_mode & kUiModeNightMask) != 0) {
    return (ui_mode & kUiModeNightMask) != 0;
  }
  if (density != o.density) {
    const int mine = density != 0 ? density : kDensityMedium;
    const int theirs = o.density != 0 ? o.density : kDensityMedium;
    // anydpi (vector drawables) beats scaling any bucket.
    if (mine == kDensityAny) return true;
    if (theirs == kDensityAny) return false;
    int req = r.density;
    if (req == 0 || req == kDensityAny) req = kDensityMedium;
    int h = mine, l = theirs;
    bool i_am_bigger = true;
    if (l > h) {
      std::swap(l, h);
      i_am_bigger = false;
    }
    if (req >= h) return i_am_bigger;   // both too small: the bigger one scales up less
    if (l >= req) return !i_am_bigger;  // both too big: the smaller one scales down less
    // Scaling down is considered twice as good as scaling up.
    return ((2 * l) - req) * h > req * req ? !i_am_bigger : i_am_bigger;
  }
  if (touchscreen != o.touchscreen && r.touchscreen != 0) return touchscreen != 0;
  const int keys = input_flags & kKeysHiddenMask, okeys = o.input_flags & kKeysHiddenMask;
  if (keys != okeys) {
    const int rkeys = r.input_flags & kKeysHiddenMask;
    if (rkeys != 0) {
      if (keys == 0) return false;
      if (okeys == 0) return true;
      if (rkeys == keys) return true;
      if (rkeys == okeys) return false;
    }
  }
  const int nav = input_flags & kNavHiddenMask, onav = o.input_flags & kNavHiddenMask;
  if (nav != onav && (r.input_flags & kNavHiddenMask) != 0) {
    if (nav == 0) return false;
    if (onav == 0) return true;
  }
  if (keyboard != o.keyboard && r.keyboard != 0) return keyboard != 0;
  if (navigation != o.navigation && r.navigation != 0) return navigation != 0;
  if (screen_width != o.screen_width || screen_height != o.screen_height) {
    int mine = 0, other = 0;
    if (r.screen_width != 0) {
      mine += r.screen_width - screen_width;
      other += r.screen_width - o.screen_width;
    }
    if (r.screen_height != 0) {
      mine += r.screen_height - screen_height;
      other += r.screen_height - o.screen_height;
    }
    if (mine != other) return mine < other;
  }
  if (sdk_version != o.sdk_version && r.sdk_version != 0) return sdk_version > o.sdk_version;
  if (minor_version != o.minor_version && r.minor_version != 0) return minor_version != 0;
  return false;
}

std::string Config::ToString() const {
  std::vector<std::string> q;
  if (mcc != 0) q.push_back(std::format("mcc{}", mcc));
  if (mnc != 0) q.push_back(std::format("mnc{}", mnc));
  if (!language.empty()) q.push_back(language);
  if (!country.empty()) q.push_back("r" + country);
  if ((screen_layout & kLayoutDirMask) == 0x40) q.push_back("ldltr");
  if ((screen_layout & kLayoutDirMask) == 0x80) q.push_back("ldrtl");
  if (smallest_screen_width_dp != 0) q.push_back(std::format("sw{}dp", smallest_screen_width_dp));
  if (screen_width_dp != 0) q.push_back(std::format("w{}dp", screen_width_dp));
  if (screen_height_dp != 0) q.push_back(std::format("h{}dp", screen_height_dp));
  static const char* const kSizes[] = {"", "small", "normal", "large", "xlarge"};
  if (const int s = screen_layout & kScreenSizeMask; s > 0 && s < 5) q.push_back(kSizes[s]);
  if ((screen_layout & kScreenLongMask) == 0x10) q.push_back("notlong");
  if ((screen_layout & kScreenLongMask) == 0x20) q.push_back("long");
  if (orientation == 1) q.push_back("port");
  if (orientation == 2) q.push_back("land");
  if ((ui_mode & kUiModeNightMask) == 0x10) q.push_back("notnight");
  if ((ui_mode & kUiModeNightMask) == 0x20) q.push_back("night");
  switch (density) {
    case 0: break;
    case 120: q.push_back("ldpi"); break;
    case 160: q.push_back("mdpi"); break;
    case 213: q.push_back("tvdpi"); break;
    case 240: q.push_back("hdpi"); break;
    case 320: q.push_back("xhdpi"); break;
    case 480: q.push_back("xxhdpi"); break;
    case 640: q.push_back("xxxhdpi"); break;
    case kDensityAny: q.push_back("anydpi"); break;
    case kDensityNone: q.push_back("nodpi"); break;
    default: q.push_back(std::format("{}dpi", density)); break;
  }
  if (sdk_version != 0) q.push_back(std::format("v{}", sdk_version));
  if (q.empty()) return "default";
  std::string s;
  for (const std::string& p : q) {
    if (!s.empty()) s += '-';
    s += p;
  }
  return s;
}

std::pair<std::uint32_t, Value> Entry::BagPair(std::uint32_t i) const {
  const std::uint8_t* p = map.data() + 12ull * i;
  return {U32(p), Value{p[7], U32(p + 8)}};
}

// --- ResourceTable -------------------------------------------------------------------

Result<std::unique_ptr<ResourceTable>> ResourceTable::Load(std::span<const std::uint8_t> input) {
  std::unique_ptr<ResourceTable> t(new ResourceTable());
  t->bytes_.assign(input.begin(), input.end());
  const std::span<const std::uint8_t> bytes(t->bytes_);
  if (bytes.size() < 12 || U16(bytes.data()) != kTableType) return Malformed("not a resource table");
  const std::uint16_t header_size = U16(bytes.data() + 2);
  const std::uint32_t size = U32(bytes.data() + 4);
  if (header_size < 12 || size < header_size || size > bytes.size()) return Malformed("table header");
  std::vector<Chunk> chunks;
  ARUN_RETURN_IF_ERROR(Chunks(bytes.subspan(header_size, size - header_size), &chunks));
  bool have_pool = false;
  for (const Chunk& c : chunks) {
    if (c.type == kStringPoolType) {
      if (have_pool) return Malformed("second value string pool");
      ARUN_RETURN_IF_ERROR(t->strings_.Load(c.bytes));
      have_pool = true;
    } else if (c.type == kPackageType) {
      ARUN_RETURN_IF_ERROR(t->ParsePackage(c.bytes));
    }
  }
  if (!have_pool) return Malformed("no value string pool");
  if (t->packages_.empty()) return Malformed("no package");
  return t;
}

Status ResourceTable::ParsePackage(std::span<const std::uint8_t> chunk) {
  const std::uint16_t header_size = U16(chunk.data() + 2);
  if (header_size < 284 || chunk.size() < header_size) return Malformed("package header truncated");
  Package pkg;
  const std::uint32_t id = U32(chunk.data() + 8);
  if (id > 0xff) return Malformed(std::format("package id 0x{:x}", id));
  pkg.id = static_cast<std::uint8_t>(id);
  std::u16string name;
  for (int i = 0; i < 128; ++i) {
    const char16_t ch = static_cast<char16_t>(U16(chunk.data() + 12 + 2 * i));
    if (ch == 0) break;
    name.push_back(ch);
  }
  pkg.name = axml::Utf16To8(name);
  const std::uint32_t type_strings = U32(chunk.data() + 268);
  const std::uint32_t key_strings = U32(chunk.data() + 276);
  if (header_size >= 288) pkg.type_id_offset = U32(chunk.data() + 284);
  if (pkg.type_id_offset > 0xff) return Malformed("type id offset");
  for (const Package& p : packages_) {
    if (p.id == pkg.id) return Malformed(std::format("package 0x{:02x} defined twice", pkg.id));
  }

  std::vector<Chunk> chunks;
  ARUN_RETURN_IF_ERROR(Chunks(chunk.subspan(header_size), &chunks));
  bool have_types = false, have_keys = false;
  for (const Chunk& c : chunks) {
    const std::size_t offset = static_cast<std::size_t>(c.bytes.data() - chunk.data());
    switch (c.type) {
      case kStringPoolType:
        if (offset == type_strings) {
          ARUN_RETURN_IF_ERROR(pkg.type_strings.Load(c.bytes));
          have_types = true;
        } else if (offset == key_strings) {
          ARUN_RETURN_IF_ERROR(pkg.key_strings.Load(c.bytes));
          have_keys = true;
        }
        break;
      case kTypeSpecType: {
        if (c.header_size < 16) return Malformed("type spec header");
        const std::uint8_t type_id = c.bytes[8];
        const std::uint32_t count = U32(c.bytes.data() + 12);
        if (type_id == 0) return Malformed("type id 0");
        if (count > (c.bytes.size() - c.header_size) / 4) return Malformed("type spec flags out of bounds");
        if (pkg.types.size() < type_id) pkg.types.resize(type_id);
        Type& t = pkg.types[type_id - 1];
        t.id = type_id;
        t.spec_flags.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) t.spec_flags[i] = U32(c.bytes.data() + c.header_size + 4ull * i);
        break;
      }
      case kTypeType: {
        if (c.header_size < 24 || c.bytes.size() < 24) return Malformed("type header");
        const std::uint8_t type_id = c.bytes[8];
        if (type_id == 0 || type_id > pkg.types.size() || pkg.types[type_id - 1].id == 0) {
          return Malformed(std::format("type 0x{:02x} without a type spec", type_id));
        }
        Variant v;
        v.flags = c.bytes[9];
        v.entry_count = U32(c.bytes.data() + 12);
        v.entries_start = U32(c.bytes.data() + 16);
        const std::uint32_t config_size = U32(c.bytes.data() + 20);
        if (config_size < 4 || 20ull + config_size > c.header_size) return Malformed("type config size");
        v.config = ParseConfig(c.bytes.subspan(20, config_size));
        v.offsets_at = c.header_size;
        const std::uint64_t width = (v.flags & (kTypeFlagSparse | kTypeFlagOffset16)) == kTypeFlagOffset16 ? 2 : 4;
        if (static_cast<std::uint64_t>(v.offsets_at) + width * v.entry_count > c.bytes.size() ||
            v.entries_start > c.bytes.size()) {
          return Malformed("type entries out of bounds");
        }
        if ((v.flags & kTypeFlagSparse) == 0 && v.entry_count > pkg.types[type_id - 1].spec_flags.size()) {
          return Malformed("type has more entries than its spec");
        }
        v.chunk = c.bytes;
        pkg.types[type_id - 1].variants.push_back(std::move(v));
        break;
      }
      default:
        break;  // library, overlayable, staged alias: not needed for lookups
    }
  }
  if (!have_types || !have_keys) return Malformed("package without type or key strings");
  packages_.push_back(std::move(pkg));
  return Status::Ok();
}

std::vector<std::uint8_t> ResourceTable::PackageIds() const {
  std::vector<std::uint8_t> ids;
  for (const Package& p : packages_) ids.push_back(p.id);
  return ids;
}

std::string ResourceTable::PackageName(std::uint8_t package_id) const {
  const Package* p = FindPackage(package_id);
  return p == nullptr ? std::string() : p->name;
}

const ResourceTable::Package* ResourceTable::FindPackage(std::uint8_t id) const {
  for (const Package& p : packages_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

Result<std::optional<std::uint32_t>> ResourceTable::EntryOffset(const Variant& v, std::uint32_t index) const {
  const std::uint8_t* base = v.chunk.data() + v.offsets_at;
  std::uint64_t offset = kNoEntry;
  if (v.flags & kTypeFlagSparse) {
    // ResTable_sparseTypeEntry {uint16 idx; uint16 offset / 4}, sorted by idx.
    std::uint32_t lo = 0, hi = v.entry_count;
    while (lo < hi) {
      const std::uint32_t mid = lo + (hi - lo) / 2;
      const std::uint16_t idx = U16(base + 4ull * mid);
      if (idx == index) {
        offset = 4ull * U16(base + 4ull * mid + 2);
        break;
      }
      if (idx < index) lo = mid + 1; else hi = mid;
    }
  } else if (v.flags & kTypeFlagOffset16) {
    if (index >= v.entry_count) return std::optional<std::uint32_t>();
    const std::uint16_t o = U16(base + 2ull * index);
    if (o != 0xffff) offset = 4ull * o;
  } else {
    if (index >= v.entry_count) return std::optional<std::uint32_t>();
    offset = U32(base + 4ull * index);
  }
  if (offset == kNoEntry) return std::optional<std::uint32_t>();
  if (v.entries_start + offset + 8 > v.chunk.size()) return Malformed("entry offset out of bounds");
  return std::optional<std::uint32_t>(static_cast<std::uint32_t>(v.entries_start + offset));
}

Result<Entry> ResourceTable::DecodeEntry(const Variant& v, std::uint32_t at) const {
  const std::uint8_t* p = v.chunk.data() + at;
  const std::size_t avail = v.chunk.size() - at;
  Entry e;
  e.config = &v.config;
  const std::uint16_t flags = U16(p + 2);
  if (flags & kEntryFlagCompact) {
    // {uint16 key; uint16 flags (value type in the high byte); uint32 data}
    e.key = U16(p);
    e.value = Value{static_cast<std::uint8_t>(flags >> 8), U32(p + 4)};
    e.is_public = (flags & kEntryFlagPublic) != 0;
    return e;
  }
  const std::uint16_t size = U16(p);
  e.key = U32(p + 4);
  e.is_public = (flags & kEntryFlagPublic) != 0;
  if (flags & kEntryFlagComplex) {
    if (size < 16 || avail < size) return Malformed("map entry truncated");
    e.complex = true;
    e.parent = U32(p + 8);
    e.count = U32(p + 12);
    if (e.count > (avail - size) / 12) return Malformed("map entry out of bounds");
    e.map = std::span<const std::uint8_t>(p + size, 12ull * e.count);
    return e;
  }
  if (size < 8 || static_cast<std::size_t>(size) + 8 > avail) return Malformed("entry value out of bounds");
  e.value = Value{p[size + 3], U32(p + size + 4)};
  return e;
}

Result<std::optional<Entry>> ResourceTable::Find(std::uint32_t id, const Config& device) const {
  const Package* pkg = FindPackage(static_cast<std::uint8_t>(id >> 24));
  const std::uint32_t type_id = (id >> 16) & 0xff;
  const std::uint32_t index = id & 0xffff;
  if (pkg == nullptr || type_id == 0 || type_id > pkg->types.size()) return std::optional<Entry>();
  const Type& type = pkg->types[type_id - 1];
  if (index >= type.spec_flags.size()) return std::optional<Entry>();
  const Variant* best = nullptr;
  std::uint32_t best_at = 0;
  for (const Variant& v : type.variants) {
    if (!v.config.Match(device)) continue;
    if (best != nullptr && !v.config.IsBetterThan(best->config, device)) continue;
    ARUN_ASSIGN_OR_RETURN(std::optional<std::uint32_t> at, EntryOffset(v, index));
    if (!at) continue;
    best = &v;
    best_at = *at;
  }
  if (best == nullptr) return std::optional<Entry>();
  ARUN_ASSIGN_OR_RETURN(Entry e, DecodeEntry(*best, best_at));
  e.spec_flags = type.spec_flags[index];
  return std::optional<Entry>(e);
}

std::optional<std::string> ResourceTable::Name(std::uint32_t id) const {
  const Package* pkg = FindPackage(static_cast<std::uint8_t>(id >> 24));
  const std::uint32_t type_id = (id >> 16) & 0xff;
  const std::uint32_t index = id & 0xffff;
  if (pkg == nullptr || type_id == 0 || type_id > pkg->types.size()) return std::nullopt;
  const Type& type = pkg->types[type_id - 1];
  if (index >= type.spec_flags.size()) return std::nullopt;
  std::string type_name;
  if (type_id - 1 < pkg->type_id_offset ||
      !pkg->type_strings.Get(type_id - 1 - pkg->type_id_offset, &type_name).ok()) {
    return std::nullopt;
  }
  for (const Variant& v : type.variants) {
    auto at = EntryOffset(v, index);
    if (!at.ok() || !*at) continue;
    auto e = DecodeEntry(v, **at);
    if (!e.ok()) return std::nullopt;
    std::string key;
    if (!pkg->key_strings.Get(e->key, &key).ok()) return std::nullopt;
    return pkg->name + ":" + type_name + "/" + key;
  }
  return std::nullopt;
}

std::uint32_t ResourceTable::Identifier(std::string_view package, std::string_view type, std::string_view entry) const {
  std::lock_guard<std::mutex> hold(names_lock_);
  if (!names_built_) {
    names_built_ = true;
    for (const Package& pkg : packages_) {
      for (const Type& t : pkg.types) {
        if (t.id == 0) continue;
        for (std::uint32_t i = 0; i < t.spec_flags.size(); ++i) {
          const std::uint32_t id = (static_cast<std::uint32_t>(pkg.id) << 24) | (static_cast<std::uint32_t>(t.id) << 16) | i;
          if (auto n = Name(id)) names_.emplace(*n, id);
        }
      }
    }
  }
  std::string key;
  key.append(package).append(":").append(type).append("/").append(entry);
  auto it = names_.find(key);
  return it == names_.end() ? 0 : it->second;
}

std::vector<std::string> ResourceTable::Configurations() const {
  std::vector<std::string> out;
  for (const Package& pkg : packages_) {
    for (const Type& t : pkg.types) {
      for (const Variant& v : t.variants) {
        std::string s = v.config.ToString();
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
      }
    }
  }
  return out;
}

}  // namespace arun::res
