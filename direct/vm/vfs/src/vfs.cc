#include "arun/vfs.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>

namespace arun::vfs {
namespace {

int ErrnoFromWin32(DWORD e) {
  switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
      return kENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      return kEACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return kEEXIST;
    case ERROR_DIR_NOT_EMPTY:
      return kENOTEMPTY;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      return kENOSPC;
    case ERROR_DIRECTORY:
      return kENOTDIR;
    case ERROR_FILENAME_EXCED_RANGE:
      return kENAMETOOLONG;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
      return kEINVAL;
    case ERROR_TOO_MANY_OPEN_FILES:
      return kEMFILE;
    default:
      return kEIO;
  }
}

bool IsReservedDeviceName(std::string_view name) {
  // CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension.
  const std::size_t dot = name.find('.');
  std::string base(name.substr(0, dot));
  for (char& c : base) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  static constexpr std::array<std::string_view, 4> kPlain = {"CON", "PRN", "AUX", "NUL"};
  for (std::string_view r : kPlain) {
    if (base == r) return true;
  }
  if (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' &&
      base[3] <= '9') {
    return true;
  }
  return false;
}

// A guest path component Windows can store without reinterpreting it.
int CheckComponent(std::string_view c) {
  if (c.size() > 255) return kENAMETOOLONG;
  for (unsigned char ch : c) {
    if (ch < 0x20) return kEINVAL;
    switch (ch) {
      case '<': case '>': case ':': case '"': case '\\': case '|': case '?': case '*':
        return kEINVAL;
      default:
        break;
    }
  }
  if (c.back() == '.' || c.back() == ' ') return kEINVAL;
  if (IsReservedDeviceName(c)) return kEINVAL;
  return 0;
}

std::wstring Wide(const std::filesystem::path& p) {
  // Extended-length form so deep application directories are not cut off at
  // MAX_PATH.
  std::wstring s = p.wstring();
  if (s.starts_with(L"\\\\?\\")) return s;
  return L"\\\\?\\" + s;
}

std::int64_t FileTimeToUnixMs(const FILETIME& ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<std::int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

struct HandleCloser {
  void operator()(void* h) const {
    if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
};

}  // namespace

std::string ErrnoDescription(int err) {
  switch (err) {
    case kENOENT: return "ENOENT (No such file or directory)";
    case kEIO: return "EIO (I/O error)";
    case kEBADF: return "EBADF (Bad file descriptor)";
    case kEACCES: return "EACCES (Permission denied)";
    case kEEXIST: return "EEXIST (File exists)";
    case kENOTDIR: return "ENOTDIR (Not a directory)";
    case kEISDIR: return "EISDIR (Is a directory)";
    case kEINVAL: return "EINVAL (Invalid argument)";
    case kEMFILE: return "EMFILE (Too many open files)";
    case kENOSPC: return "ENOSPC (No space left on device)";
    case kEROFS: return "EROFS (Read-only file system)";
    case kENAMETOOLONG: return "ENAMETOOLONG (File name too long)";
    case kENOTEMPTY: return "ENOTEMPTY (Directory not empty)";
    default: return "errno " + std::to_string(err);
  }
}

bool NormalizeGuestPath(std::string_view in, std::string* out) {
  if (in.empty() || in[0] != '/') return false;
  if (in.find('\0') != std::string_view::npos) return false;
  std::vector<std::string_view> parts;
  std::size_t i = 0;
  while (i < in.size()) {
    while (i < in.size() && in[i] == '/') ++i;
    std::size_t j = i;
    while (j < in.size() && in[j] != '/') ++j;
    std::string_view c = in.substr(i, j - i);
    i = j;
    if (c.empty() || c == ".") continue;
    if (c == "..") {
      if (parts.empty()) return false;
      parts.pop_back();
      continue;
    }
    parts.push_back(c);
  }
  out->clear();
  for (std::string_view p : parts) {
    out->push_back('/');
    out->append(p);
  }
  if (out->empty()) *out = "/";
  return true;
}

}  // namespace arun::vfs
