// Ownership: value type, no allocation beyond the message string.
// Thread affinity: none (immutable after construction).
// Error propagation: every fallible internal API returns Status or Result<T>.
// Pointers: none held.
//
// Reason codes are stable, machine-readable identifiers. The UI and the JSON CLI
// surface (spec 21.1) must switch on `ReasonCode`, never on the English message.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arun {

// Stable numeric values: they appear in JSON output and process exit codes.
// Never renumber an existing entry; append only.
enum class ReasonCode : std::uint32_t {
  kOk = 0,

  // Generic
  kInvalidArgument = 1,
  kNotImplemented = 2,      // honestly unimplemented; never returned as success
  kUnsupported = 3,         // understood, deliberately out of profile
  kInternal = 4,
  kNotFound = 5,
  kAlreadyExists = 6,
  kPermissionDenied = 7,
  kResourceExhausted = 8,
  kBusy = 9,

  // Host OS
  kHostOsError = 20,
  kHostMemoryError = 21,

  // ELF loader (spec 8)
  kElfMalformed = 40,
  kElfUnsupportedClass = 41,     // not ELF64 little-endian x86-64
  kElfUnsupportedType = 42,      // not ET_DYN
  kElfUnsupportedRelocation = 43,
  kElfUnresolvedSymbol = 44,
  kElfMissingDependency = 45,
  kElfTextRelocation = 46,
  kElfExecutableStack = 47,
  kElfBoundsViolation = 48,
  kElfDuplicateLoad = 49,

  // Guest memory (spec 10.4)
  kMemReservationFailed = 60,
  kMemCommitFailed = 61,
  kMemProtectFailed = 62,
  kMemRegionOverlap = 63,
  kMemBadAlignment = 64,

  // Guest ABI / execution (spec 9, 10)
  kAbiUnsupportedSignature = 80,
  kAbiThunkFailure = 81,
  kTlsUnavailable = 82,
  kGuestFault = 83,

  // Package / manifest (spec 13)
  kPackageMalformed = 100,
  kPackageSignatureInvalid = 101,
  kPackageAbiIncompatible = 102,
  kPackageUnsupportedContainer = 103,
  kManifestMalformed = 104,        // binary AndroidManifest (AXML) or resources.arsc
  kManifestNoLauncher = 105,       // no MAIN/LAUNCHER activity to start

  // Managed runtime (DEX interpreter, ADR 0004)
  kDexMalformed = 120,             // structurally invalid DEX; rejected before execution
  kDexUnsupported = 121,           // valid DEX using a format/opcode this runtime lacks
  kVmClassNotFound = 122,
  kVmLinkageError = 123,           // unresolvable method/field, incompatible change
  kVmUncaughtException = 124,      // a Java exception escaped to the host boundary
  kVmStackOverflow = 125,
  kVmOutOfMemory = 126,
};

const char* ReasonCodeName(ReasonCode code);

class [[nodiscard]] Status {
 public:
  Status() = default;  // ok
  Status(ReasonCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status Ok() { return Status{}; }

  bool ok() const { return code_ == ReasonCode::kOk; }
  explicit operator bool() const { return ok(); }

  ReasonCode code() const { return code_; }
  const std::string& message() const { return message_; }

  // "kElfMalformed: <message>" — for logs, never for programmatic dispatch.
  std::string ToString() const;

  // Adds outer context while preserving the original reason code.
  Status WithContext(std::string_view context) const;

 private:
  ReasonCode code_ = ReasonCode::kOk;
  std::string message_;
};

// Convenience constructors.
Status Err(ReasonCode code, std::string message);
// Captures GetLastError() at the call site.
Status Win32Err(std::string_view what, unsigned long last_error);

template <typename T>
class [[nodiscard]] Result {
 public:
  Result(T value) : value_(std::move(value)) {}
  Result(Status status) : status_(std::move(status)) {
    // An ok Status carries no value; that is a programming error.
  }

  bool ok() const { return status_.ok() && value_.has_value(); }
  explicit operator bool() const { return ok(); }

  const Status& status() const { return status_; }
  ReasonCode code() const { return status_.code(); }

  T& operator*() { return *value_; }
  const T& operator*() const { return *value_; }
  T* operator->() { return &*value_; }
  const T* operator->() const { return &*value_; }

  T value_or(T fallback) const { return value_ ? *value_ : std::move(fallback); }

 private:
  Status status_;
  std::optional<T> value_;
};

}  // namespace arun

// Propagate an error out of the current function.
#define ARUN_RETURN_IF_ERROR(expr)                 \
  do {                                             \
    ::arun::Status _arun_st = (expr);              \
    if (!_arun_st.ok()) return _arun_st;           \
  } while (0)

// Two levels of indirection are required: `a##__LINE__` pastes the literal text
// "__LINE__" instead of the line number, which silently collapses every use in a
// function to one name.
#define ARUN_CONCAT_INNER(a, b) a##b
#define ARUN_CONCAT(a, b) ARUN_CONCAT_INNER(a, b)

#define ARUN_ASSIGN_OR_RETURN_IMPL(tmp, decl, expr) \
  auto tmp = (expr);                                \
  if (!(tmp).ok()) return (tmp).status();           \
  decl = std::move(*(tmp))

// Assign or propagate. `decl` is the full declaration, e.g. `auto x`.
#define ARUN_ASSIGN_OR_RETURN(decl, expr) \
  ARUN_ASSIGN_OR_RETURN_IMPL(ARUN_CONCAT(_arun_r_, __LINE__), decl, expr)
