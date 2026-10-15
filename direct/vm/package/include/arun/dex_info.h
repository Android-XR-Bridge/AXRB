// DEX container inspection and runtime-compatibility gating (spec 13.1, 14.3).
//
// Ownership: DexInfo is a value; it borrows nothing after Parse returns.
// Thread affinity: none (immutable value type).
// Error propagation: Status/Result with package_* reason codes.
// Pointers: none retained.
//
// This module exists to answer one question precisely, before anything executes:
// *can the selected managed runtime run this DEX at all?* Spec 14.3 requires
// detecting unsupported DEX format/opcodes BEFORE claiming a launch succeeded.
// Getting this wrong means an app fails deep inside class loading with a useless
// error instead of at inspection time with an actionable one.
#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "arun/status.h"

namespace arun::pkg {

// DEX format versions, as defined by the AOSP dex-format specification.
// The introducing Android release is recorded because it, not the app's own
// target SDK, is what a runtime must implement.
enum class DexVersion : std::uint32_t {
  kUnknown = 0,
  k035 = 35,  // pre-Android 7.0 baseline
  k037 = 37,  // Android 7.0: default interface methods, invoke adjustment
  k038 = 38,  // Android 8.0: invoke-polymorphic, invoke-custom, method handles
  k039 = 39,  // Android 9.0: const-method-handle, const-method-type
  k040 = 40,  // Android 10:  extended SimpleNames character set
  k041 = 41,  // Android 16:  container format (experimental upstream)
};

const char* DexVersionName(DexVersion v);
// Android API level that introduced this format version.
int DexVersionIntroducedInApi(DexVersion v);

struct DexHeaderInfo {
  DexVersion version = DexVersion::kUnknown;
  std::uint32_t raw_version = 0;      // numeric value from the magic, e.g. 39
  std::uint32_t checksum = 0;
  std::uint32_t file_size = 0;
  std::uint32_t header_size = 0;
  std::uint32_t endian_tag = 0;
  std::uint32_t string_ids_size = 0;
  std::uint32_t type_ids_size = 0;
  std::uint32_t proto_ids_size = 0;
  std::uint32_t field_ids_size = 0;
  std::uint32_t method_ids_size = 0;
  std::uint32_t class_defs_size = 0;
  std::uint32_t data_size = 0;
  // Present only from 038 onwards; their presence proves method handles are used.
  bool has_call_site_ids = false;
  bool has_method_handles = false;
  std::uint32_t call_site_ids_size = 0;
  std::uint32_t method_handles_size = 0;
};

struct DexInfo {
  std::string entry_name;      // e.g. "classes.dex", "classes2.dex"
  DexHeaderInfo header;
  std::uint64_t byte_size = 0;

  // Parses and validates a DEX header. Does NOT verify the whole container:
  // full verification belongs to the managed runtime, not to inspection.
  static Result<DexInfo> Parse(std::span<const std::uint8_t> bytes, std::string entry_name);
};

// What a managed runtime must provide to execute a given DEX version. Populated
// from the AOSP dex-format specification; see docs/android-version-support.md.
struct DexVersionRequirements {
  DexVersion version = DexVersion::kUnknown;
  int introduced_in_api = 0;
  std::vector<std::string> required_features;
};

DexVersionRequirements RequirementsFor(DexVersion v);

// The DEX capability of a candidate managed runtime. Deliberately explicit: a
// runtime declares the highest format it implements, and inspection refuses
// anything above it rather than discovering the problem during class loading.
struct RuntimeDexCapability {
  std::string runtime_name = "none";
  DexVersion max_supported = DexVersion::kUnknown;
  bool multidex = false;
  // invoke-polymorphic, invoke-custom, const-method-handle, const-method-type.
  // A runtime can accept DEX 038/039 containers without these: the opcodes are
  // only a problem if a method that uses them actually executes.
  bool method_handles = false;
};

// The capability of the runtime this build actually has: the in-tree DEX
// interpreter (ADR 0004). It parses DEX 035-039 and executes everything except
// the method-handle family, which throws UnsupportedOperationException if
// reached. Multidex loading exists but no fixture exercises it, so it is not
// claimed. Raise these values only when a test proves the new capability.
RuntimeDexCapability CurrentRuntimeDexCapability();

// Checks a DEX against a runtime capability. Returns a specific, actionable
// status rather than a bare bool.
Status CheckDexSupported(const DexInfo& dex, const RuntimeDexCapability& runtime);

// Aggregated view over an APK's DEX files.
struct DexInventory {
  std::vector<DexInfo> files;
  DexVersion highest_version = DexVersion::kUnknown;
  bool multidex = false;

  // Lowest Android API level whose runtime could load every DEX here.
  int minimum_runtime_api() const;
};

}  // namespace arun::pkg
