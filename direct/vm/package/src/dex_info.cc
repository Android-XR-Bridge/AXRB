#include "arun/dex_info.h"

#include <cstring>
#include <format>

#include "arun/log.h"

namespace arun::pkg {
namespace {

constexpr std::uint32_t kExpectedEndianTag = 0x12345678;
constexpr std::uint32_t kReverseEndianTag = 0x78563412;
constexpr std::size_t kMinHeaderSize = 112;  // DEX 035 header_item

std::uint32_t Read32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

const char* DexVersionName(DexVersion v) {
  switch (v) {
    case DexVersion::k035: return "035";
    case DexVersion::k037: return "037";
    case DexVersion::k038: return "038";
    case DexVersion::k039: return "039";
    case DexVersion::k040: return "040";
    case DexVersion::k041: return "041";
    case DexVersion::kUnknown: break;
  }
  return "unknown";
}

int DexVersionIntroducedInApi(DexVersion v) {
  switch (v) {
    case DexVersion::k035: return 1;   // the long-standing baseline
    case DexVersion::k037: return 24;  // Android 7.0
    case DexVersion::k038: return 26;  // Android 8.0
    case DexVersion::k039: return 28;  // Android 9.0
    case DexVersion::k040: return 29;  // Android 10
    case DexVersion::k041: return 36;  // Android 16, experimental upstream
    case DexVersion::kUnknown: break;
  }
  return 0;
}

DexVersionRequirements RequirementsFor(DexVersion v) {
  DexVersionRequirements r;
  r.version = v;
  r.introduced_in_api = DexVersionIntroducedInApi(v);
  switch (v) {
    case DexVersion::k041:
      r.required_features.push_back(
          "container format: multiple logical DEX files in one physical file");
      [[fallthrough]];
    case DexVersion::k040:
      r.required_features.push_back("extended SimpleNames character set");
      [[fallthrough]];
    case DexVersion::k039:
      r.required_features.push_back("const-method-handle opcode");
      r.required_features.push_back("const-method-type opcode");
      [[fallthrough]];
    case DexVersion::k038:
      r.required_features.push_back("invoke-polymorphic opcode");
      r.required_features.push_back("invoke-custom opcode");
      r.required_features.push_back("method_handle_item and call_site_id_item sections");
      r.required_features.push_back(
          "java.lang.invoke core library support (MethodHandle, MethodType, CallSite)");
      [[fallthrough]];
    case DexVersion::k037:
      r.required_features.push_back("default interface methods");
      r.required_features.push_back("adjusted invoke-* semantics for interface defaults");
      [[fallthrough]];
    case DexVersion::k035:
      r.required_features.push_back("base DEX 035 instruction set and container layout");
      break;
    case DexVersion::kUnknown:
      break;
  }
  return r;
}

RuntimeDexCapability CurrentRuntimeDexCapability() {
  // The in-tree interpreter (ADR 0004). 039 is the highest version its parser
  // accepts, and tests/integration runs unmodified 035/037/038/039 fixture APKs
  // end to end. Method handles are NOT implemented (see method_handles), and
  // multidex is unproven. Do not raise these values to make a caller proceed:
  // raise them when a test proves the capability.
  return RuntimeDexCapability{"arun-interpreter (ADR 0004)", DexVersion::k039, false, false};
}

Result<DexInfo> DexInfo::Parse(std::span<const std::uint8_t> bytes, std::string entry_name) {
  if (bytes.size() < kMinHeaderSize) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("'{}' is {} bytes, too small for a DEX header", entry_name,
                           bytes.size()));
  }

  // Magic is "dex\n0NN\0" -- 8 bytes, with the version as three ASCII digits.
  static constexpr std::uint8_t kMagicPrefix[4] = {'d', 'e', 'x', '\n'};
  if (std::memcmp(bytes.data(), kMagicPrefix, 4) != 0 || bytes[7] != 0) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("'{}' does not begin with a DEX magic", entry_name));
  }
  for (int i = 4; i < 7; ++i) {
    if (bytes[i] < '0' || bytes[i] > '9') {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("'{}' has a non-numeric DEX version in its magic", entry_name));
    }
  }

  DexInfo info;
  info.entry_name = std::move(entry_name);
  info.byte_size = bytes.size();

  const std::uint32_t raw = static_cast<std::uint32_t>((bytes[4] - '0') * 100 +
                                                       (bytes[5] - '0') * 10 + (bytes[6] - '0'));
  info.header.raw_version = raw;
  switch (raw) {
    case 35: info.header.version = DexVersion::k035; break;
    case 37: info.header.version = DexVersion::k037; break;
    case 38: info.header.version = DexVersion::k038; break;
    case 39: info.header.version = DexVersion::k039; break;
    case 40: info.header.version = DexVersion::k040; break;
    case 41: info.header.version = DexVersion::k041; break;
    default:
      // A version we have never heard of. Report it rather than guessing that it
      // behaves like the nearest one we know.
      return Err(ReasonCode::kPackageUnsupportedContainer,
                 std::format("'{}' declares DEX format version {:03} which this build does "
                             "not recognise",
                             info.entry_name, raw));
  }

  const std::uint8_t* h = bytes.data();
  info.header.checksum = Read32(h + 8);
  // h + 12 .. h + 32 is the 20-byte SHA-1 signature; verification is the
  // runtime's job, not inspection's.
  info.header.file_size = Read32(h + 32);
  info.header.header_size = Read32(h + 36);
  info.header.endian_tag = Read32(h + 40);

  if (info.header.endian_tag == kReverseEndianTag) {
    return Err(ReasonCode::kPackageUnsupportedContainer,
               std::format("'{}' is big-endian DEX, which this build does not support",
                           info.entry_name));
  }
  if (info.header.endian_tag != kExpectedEndianTag) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("'{}' has endian_tag {:#x}, expected {:#x}", info.entry_name,
                           info.header.endian_tag, kExpectedEndianTag));
  }
  if (info.header.header_size < kMinHeaderSize) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("'{}' declares header_size {} below the minimum {}",
                           info.entry_name, info.header.header_size, kMinHeaderSize));
  }
  // DEX 041 containers describe a logical file inside a larger physical one, so
  // file_size need not equal the buffer length there. For every other version it
  // must, and a mismatch means truncation or tampering.
  if (info.header.version != DexVersion::k041 && info.header.file_size != bytes.size()) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("'{}' declares file_size {} but is {} bytes", info.entry_name,
                           info.header.file_size, bytes.size()));
  }

  info.header.string_ids_size = Read32(h + 56);
  info.header.type_ids_size = Read32(h + 64);
  info.header.proto_ids_size = Read32(h + 72);
  info.header.field_ids_size = Read32(h + 80);
  info.header.method_ids_size = Read32(h + 88);
  info.header.class_defs_size = Read32(h + 96);
  info.header.data_size = Read32(h + 104);

  ARUN_LOG_DEBUG(log::Category::kManifest, "{}: DEX {} classes={} methods={}",
                 info.entry_name, DexVersionName(info.header.version),
                 info.header.class_defs_size, info.header.method_ids_size);
  return info;
}

Status CheckDexSupported(const DexInfo& dex, const RuntimeDexCapability& runtime) {
  if (runtime.max_supported == DexVersion::kUnknown) {
    return Err(ReasonCode::kNotImplemented,
               std::format("'{}' is DEX format {}, but this build has no managed runtime at "
                           "all (runtime='{}'): DEX execution is not implemented. See "
                           "docs/adr/0003-managed-runtime-selection.md",
                           dex.entry_name, DexVersionName(dex.header.version),
                           runtime.runtime_name));
  }
  if (static_cast<std::uint32_t>(dex.header.version) >
      static_cast<std::uint32_t>(runtime.max_supported)) {
    const DexVersionRequirements req = RequirementsFor(dex.header.version);
    std::string features;
    for (std::size_t i = 0; i < req.required_features.size(); ++i) {
      if (i != 0) features += "; ";
      features += req.required_features[i];
    }
    return Err(ReasonCode::kPackageAbiIncompatible,
               std::format("'{}' is DEX format {} (introduced in API {}), but runtime '{}' "
                           "implements at most {}. Missing: {}",
                           dex.entry_name, DexVersionName(dex.header.version),
                           req.introduced_in_api, runtime.runtime_name,
                           DexVersionName(runtime.max_supported), features));
  }
  return Status::Ok();
}

int DexInventory::minimum_runtime_api() const {
  int api = 1;
  for (const DexInfo& d : files) {
    const int needed = DexVersionIntroducedInApi(d.header.version);
    if (needed > api) api = needed;
  }
  return api;
}

}  // namespace arun::pkg
