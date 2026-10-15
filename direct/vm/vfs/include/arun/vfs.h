// Guest file system (spec 20): maps guest paths such as /data/user/0/<pkg> onto
// per-application host directories.
//
// This is compatibility, not a security boundary (spec 20). It still refuses
// anything that would escape a mount: guest paths are normalised lexically
// ("." and ".." resolved, repeated slashes collapsed) before the mount lookup,
// and path components Windows would reinterpret (drive colons, backslashes,
// reserved device names, trailing dots/spaces) are rejected with EINVAL
// instead of being passed to Win32.
//
// Known differences from a Linux file system, documented rather than hidden:
// names are case-insensitive (NTFS), there are no symbolic links, permissions
// are only "mount is read-only" or "mount is writable", and timestamps have
// NTFS resolution.
//
// Ownership: FileSystem owns its open handles. Thread affinity: thread-safe.
// Error propagation: Linux errno values (what Android code expects), 0 = ok.
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arun::vfs {

// Linux errno numbers.
inline constexpr int kENOENT = 2;
inline constexpr int kEIO = 5;
inline constexpr int kEBADF = 9;
inline constexpr int kEACCES = 13;
inline constexpr int kEEXIST = 17;
inline constexpr int kENOTDIR = 20;
inline constexpr int kEISDIR = 21;
inline constexpr int kEINVAL = 22;
inline constexpr int kEMFILE = 24;
inline constexpr int kENOSPC = 28;
inline constexpr int kEROFS = 30;
inline constexpr int kENAMETOOLONG = 36;
inline constexpr int kENOTEMPTY = 39;

// "ENOENT (No such file or directory)", as libcore formats it.
std::string ErrnoDescription(int err);

// Lexically normalises an absolute guest path. Returns false for relative
// paths, embedded NULs, or ".." above the root.
bool NormalizeGuestPath(std::string_view in, std::string* out);

struct OpenFlags {
  bool write = false;
  bool append = false;
  bool create = false;
  bool exclusive = false;  // with create: fail with EEXIST if present
  bool truncate = true;    // with write and !append
};

struct StatInfo {
  bool is_directory = false;
  std::uint64_t size = 0;
  std::int64_t mtime_ms = 0;  // Unix epoch milliseconds
};

class FileSystem {
 public:
  FileSystem() = default;
  ~FileSystem() = default;
  FileSystem(const FileSystem&) = delete;
  FileSystem& operator=(const FileSystem&) = delete;

  // `guest_prefix` is an absolute guest directory; `host_dir` must exist.
  void AddMount(std::string guest_prefix, std::filesystem::path host_dir, bool writable);

  // Maps a guest path to its host path. EROFS if `for_write` and the mount is
  // read-only; ENOENT if no mount covers it; EINVAL for unrepresentable names.
  int Resolve(std::string_view guest, bool for_write, std::filesystem::path* host) const;

  int Open(std::string_view guest, const OpenFlags& flags, int* handle);
  // *n == 0 means end of file.
  int Read(int handle, std::span<std::uint8_t> out, std::size_t* n);
  int Write(int handle, std::span<const std::uint8_t> data);
  int Available(int handle, std::int64_t* bytes);
  int Skip(int handle, std::int64_t n, std::int64_t* skipped);
  int Close(int handle);

  // mode: 0 exists, 4 read, 2 write (access(2) bits).
  bool Access(std::string_view guest, int mode) const;
  int Stat(std::string_view guest, StatInfo* info) const;
  int Mkdir(std::string_view guest);
  int Remove(std::string_view guest);
  int Rename(std::string_view from, std::string_view to);
  int List(std::string_view guest, std::vector<std::string>* names) const;
  int ReadFile(std::string_view guest, std::vector<std::uint8_t>* bytes) const;

 private:
  // AXRB Direct: implemented on the guest file system and descriptor table
  // (direct/src/java_vfs.cpp); handles are guest file descriptors.
};

}  // namespace arun::vfs
