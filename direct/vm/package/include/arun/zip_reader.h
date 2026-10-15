// Safe ZIP/APK reader (spec 13.3: treat archive input as hostile data).
//
// Ownership: ZipReader owns its file mapping. Entry views borrow from it.
// Thread affinity: const after Open(); concurrent reads are safe.
// Error propagation: Status/Result with package_* reason codes.
// Pointers: host pointers into the mapped file only. Entry names are raw bytes
//   from the archive and are NEVER turned into host paths by this class.
//
// Deliberately NOT a general-purpose unzip: it refuses rather than accommodates.
// Every limit below exists because the alternative is a decompression bomb, an
// integer overflow, or a path escape.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arun/elf_image.h"  // MappedFile
#include "arun/status.h"

namespace arun::pkg {

struct ZipLimits {
  std::uint32_t max_entries = 200'000;
  std::uint64_t max_entry_uncompressed = 4ull << 30;    // 4 GiB per entry
  std::uint64_t max_total_uncompressed = 16ull << 30;   // 16 GiB total
  std::uint32_t max_compression_ratio = 200;            // per entry
  std::uint32_t max_name_length = 4096;
};

enum class CompressionMethod : std::uint16_t {
  kStore = 0,
  kDeflate = 8,
};

struct ZipEntry {
  // Raw bytes from the archive. May contain anything; not a host path.
  std::string name;
  CompressionMethod method = CompressionMethod::kStore;
  std::uint64_t compressed_size = 0;
  std::uint64_t uncompressed_size = 0;
  std::uint32_t crc32 = 0;
  std::uint64_t local_header_offset = 0;
  bool is_directory = false;
  // Offset of the entry's data, resolved from the local header. Only valid after
  // ResolveDataOffset; 0 means not yet resolved.
  std::uint64_t data_offset = 0;
};

class ZipReader {
 public:
  static Result<ZipReader> Open(const std::wstring& path, ZipLimits limits = {});
  // For in-memory archives (tests, embedded payloads).
  static Result<ZipReader> FromBytes(std::span<const std::uint8_t> bytes, std::string name,
                                     ZipLimits limits = {});

  const std::vector<ZipEntry>& entries() const { return entries_; }
  const std::string& name() const { return name_; }
  std::uint64_t archive_size() const { return bytes_.size(); }

  const ZipEntry* Find(std::string_view entry_name) const;

  // Returns the stored bytes WITHOUT decompressing. Valid only for kStore
  // entries; this is how an uncompressed asset can be exposed by offset/length
  // (spec 12.6) without materialising it.
  Result<std::span<const std::uint8_t>> RawData(const ZipEntry& entry) const;

  // Decompresses (or copies) an entry into a fresh buffer, enforcing the limits.
  Result<std::vector<std::uint8_t>> Extract(const ZipEntry& entry) const;

  // SHA-256 of the whole archive, for package identity (spec 2.3).
  Result<std::string> Sha256Hex() const;

 private:
  Status ParseCentralDirectory();
  Status ResolveDataOffset(ZipEntry& entry);

  elf::MappedFile file_;                 // empty for FromBytes
  std::span<const std::uint8_t> bytes_;
  std::string name_;
  ZipLimits limits_;
  std::vector<ZipEntry> entries_;
};

// True if the name is safe to use as a relative path component sequence, i.e. no
// traversal, no absolute form, no drive letter, no NUL, no backslash, no Windows
// device name, no trailing dot/space. Used at the HOST boundary only -- the
// virtual filesystem can represent names this rejects (spec 12.4).
bool IsSafeRelativeArchiveName(std::string_view name);

}  // namespace arun::pkg
