#include "arun/status.h"

#include <windows.h>

#include <array>
#include <cstdio>

namespace arun {
namespace {

struct Entry {
  ReasonCode code;
  const char* name;
};

constexpr std::array kNames{
    Entry{ReasonCode::kOk, "ok"},
    Entry{ReasonCode::kInvalidArgument, "invalid_argument"},
    Entry{ReasonCode::kNotImplemented, "not_implemented"},
    Entry{ReasonCode::kUnsupported, "unsupported"},
    Entry{ReasonCode::kInternal, "internal"},
    Entry{ReasonCode::kNotFound, "not_found"},
    Entry{ReasonCode::kAlreadyExists, "already_exists"},
    Entry{ReasonCode::kPermissionDenied, "permission_denied"},
    Entry{ReasonCode::kResourceExhausted, "resource_exhausted"},
    Entry{ReasonCode::kBusy, "busy"},
    Entry{ReasonCode::kHostOsError, "host_os_error"},
    Entry{ReasonCode::kHostMemoryError, "host_memory_error"},
    Entry{ReasonCode::kElfMalformed, "elf_malformed"},
    Entry{ReasonCode::kElfUnsupportedClass, "elf_unsupported_class"},
    Entry{ReasonCode::kElfUnsupportedType, "elf_unsupported_type"},
    Entry{ReasonCode::kElfUnsupportedRelocation, "elf_unsupported_relocation"},
    Entry{ReasonCode::kElfUnresolvedSymbol, "elf_unresolved_symbol"},
    Entry{ReasonCode::kElfMissingDependency, "elf_missing_dependency"},
    Entry{ReasonCode::kElfTextRelocation, "elf_text_relocation"},
    Entry{ReasonCode::kElfExecutableStack, "elf_executable_stack"},
    Entry{ReasonCode::kElfBoundsViolation, "elf_bounds_violation"},
    Entry{ReasonCode::kElfDuplicateLoad, "elf_duplicate_load"},
    Entry{ReasonCode::kMemReservationFailed, "mem_reservation_failed"},
    Entry{ReasonCode::kMemCommitFailed, "mem_commit_failed"},
    Entry{ReasonCode::kMemProtectFailed, "mem_protect_failed"},
    Entry{ReasonCode::kMemRegionOverlap, "mem_region_overlap"},
    Entry{ReasonCode::kMemBadAlignment, "mem_bad_alignment"},
    Entry{ReasonCode::kAbiUnsupportedSignature, "abi_unsupported_signature"},
    Entry{ReasonCode::kAbiThunkFailure, "abi_thunk_failure"},
    Entry{ReasonCode::kTlsUnavailable, "tls_unavailable"},
    Entry{ReasonCode::kGuestFault, "guest_fault"},
    Entry{ReasonCode::kPackageMalformed, "package_malformed"},
    Entry{ReasonCode::kPackageSignatureInvalid, "package_signature_invalid"},
    Entry{ReasonCode::kPackageAbiIncompatible, "package_abi_incompatible"},
    Entry{ReasonCode::kPackageUnsupportedContainer, "package_unsupported_container"},
    Entry{ReasonCode::kManifestMalformed, "manifest_malformed"},
    Entry{ReasonCode::kManifestNoLauncher, "manifest_no_launcher"},
    Entry{ReasonCode::kDexMalformed, "dex_malformed"},
    Entry{ReasonCode::kDexUnsupported, "dex_unsupported"},
    Entry{ReasonCode::kVmClassNotFound, "vm_class_not_found"},
    Entry{ReasonCode::kVmLinkageError, "vm_linkage_error"},
    Entry{ReasonCode::kVmUncaughtException, "vm_uncaught_exception"},
    Entry{ReasonCode::kVmStackOverflow, "vm_stack_overflow"},
    Entry{ReasonCode::kVmOutOfMemory, "vm_out_of_memory"},
};

}  // namespace

const char* ReasonCodeName(ReasonCode code) {
  for (const Entry& e : kNames) {
    if (e.code == code) return e.name;
  }
  return "unknown_reason_code";
}

std::string Status::ToString() const {
  if (ok()) return "ok";
  std::string out = ReasonCodeName(code_);
  out += ": ";
  out += message_;
  return out;
}

Status Status::WithContext(std::string_view context) const {
  if (ok()) return *this;
  std::string combined(context);
  combined += ": ";
  combined += message_;
  return Status(code_, std::move(combined));
}

Status Err(ReasonCode code, std::string message) {
  return Status(code, std::move(message));
}

Status Win32Err(std::string_view what, unsigned long last_error) {
  char* buffer = nullptr;
  const DWORD len = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&buffer), 0, nullptr);

  std::string detail;
  if (len != 0 && buffer != nullptr) {
    detail.assign(buffer, len);
    while (!detail.empty() &&
           (detail.back() == '\n' || detail.back() == '\r' || detail.back() == '.')) {
      detail.pop_back();
    }
  }
  if (buffer != nullptr) LocalFree(buffer);

  char prefix[64];
  std::snprintf(prefix, sizeof(prefix), " (win32 error %lu)", last_error);

  std::string message(what);
  if (!detail.empty()) {
    message += ": ";
    message += detail;
  }
  message += prefix;
  return Status(ReasonCode::kHostOsError, std::move(message));
}

}  // namespace arun
