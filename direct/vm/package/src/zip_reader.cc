#include "arun/zip_reader.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include "arun/log.h"

#pragma comment(lib, "bcrypt.lib")

namespace arun::pkg {

// Implemented in inflate.cc.
Status InflateRaw(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);

namespace {

constexpr std::uint32_t kEocdSignature = 0x06054b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kZip64EocdSignature = 0x06064b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;

constexpr std::size_t kEocdMinSize = 22;
constexpr std::size_t kMaxCommentSize = 0xFFFF;

// All multi-byte ZIP fields are little-endian. Reading through these helpers
// keeps every access bounds-checked in one place.
bool InBounds(std::size_t total, std::uint64_t offset, std::uint64_t size) {
  if (offset > total) return false;
  return size <= static_cast<std::uint64_t>(total) - offset;
}

std::uint16_t Read16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t Read32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t Read64(const std::uint8_t* p) {
  return static_cast<std::uint64_t>(Read32(p)) |
         (static_cast<std::uint64_t>(Read32(p + 4)) << 32);
}

// Standard ZIP CRC-32 (IEEE 802.3 polynomial, reflected). Every archive entry
// carries one, which makes it a free, archive-supplied correctness check on the
// decompressor -- far stronger evidence than "the test expectations matched".
std::uint32_t Crc32(std::span<const std::uint8_t> data) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace

bool IsSafeRelativeArchiveName(std::string_view name) {
  if (name.empty() || name.size() > 4096) return false;
  if (name.find('\0') != std::string_view::npos) return false;
  if (name.find('\\') != std::string_view::npos) return false;  // host separator
  if (name.front() == '/') return false;                        // absolute
  // Drive-relative or drive-absolute ("C:", "C:/...").
  if (name.size() >= 2 && name[1] == ':') return false;
  if (name.rfind("//", 0) == 0) return false;  // UNC-ish

  std::size_t start = 0;
  while (start <= name.size()) {
    const std::size_t slash = name.find('/', start);
    const std::string_view part =
        name.substr(start, slash == std::string_view::npos ? std::string_view::npos
                                                           : slash - start);
    if (part == ".." ) return false;
    if (!part.empty()) {
      if (part.back() == '.' || part.back() == ' ') return false;  // Windows strips these
      // Windows device names, with or without an extension.
      std::string upper;
      for (char c : part.substr(0, part.find('.'))) {
        upper += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
      }
      static constexpr const char* kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1",
                                                  "COM2", "COM3", "COM4", "COM5", "COM6",
                                                  "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                                  "LPT3", "LPT4", "LPT5", "LPT6", "LPT7",
                                                  "LPT8", "LPT9"};
      for (const char* r : kReserved) {
        if (upper == r) return false;
      }
      // Characters Windows cannot represent in a path.
      for (char c : part) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20) return false;
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
          return false;
        }
      }
    }
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return true;
}

Result<ZipReader> ZipReader::Open(const std::wstring& path, ZipLimits limits) {
  ARUN_ASSIGN_OR_RETURN(elf::MappedFile file, elf::MappedFile::Open(path));
  ZipReader r;
  r.bytes_ = file.bytes();
  r.file_ = std::move(file);
  r.limits_ = limits;
  const std::size_t slash = path.find_last_of(L"\\/");
  const std::wstring leaf = slash == std::wstring::npos ? path : path.substr(slash + 1);
  r.name_.assign(leaf.begin(), leaf.end());
  ARUN_RETURN_IF_ERROR(r.ParseCentralDirectory());
  return r;
}

Result<ZipReader> ZipReader::FromBytes(std::span<const std::uint8_t> bytes, std::string name,
                                       ZipLimits limits) {
  ZipReader r;
  r.bytes_ = bytes;
  r.name_ = std::move(name);
  r.limits_ = limits;
  ARUN_RETURN_IF_ERROR(r.ParseCentralDirectory());
  return r;
}

Status ZipReader::ParseCentralDirectory() {
  const std::size_t size = bytes_.size();
  if (size < kEocdMinSize) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("archive is {} bytes, too small to contain an EOCD record", size));
  }

  // Scan backwards for the end-of-central-directory record.
  const std::size_t scan_limit = std::min(size, kEocdMinSize + kMaxCommentSize);
  std::optional<std::size_t> eocd_offset;
  for (std::size_t back = kEocdMinSize; back <= scan_limit; ++back) {
    const std::size_t candidate = size - back;
    if (Read32(bytes_.data() + candidate) == kEocdSignature) {
      // Validate that the declared comment length actually reaches the file end,
      // which disambiguates a real EOCD from the signature appearing in data.
      const std::uint16_t comment_len = Read16(bytes_.data() + candidate + 20);
      if (candidate + kEocdMinSize + comment_len == size) {
        eocd_offset = candidate;
        break;
      }
    }
  }
  if (!eocd_offset.has_value()) {
    return Err(ReasonCode::kPackageMalformed, "no end-of-central-directory record found");
  }

  const std::uint8_t* eocd = bytes_.data() + *eocd_offset;
  std::uint64_t entry_count = Read16(eocd + 10);
  std::uint64_t cd_size = Read32(eocd + 12);
  std::uint64_t cd_offset = Read32(eocd + 16);

  // ZIP64: the 32-bit fields saturate and the real values live in a separate record.
  const bool needs_zip64 = (entry_count == 0xFFFF) || (cd_size == 0xFFFFFFFF) ||
                           (cd_offset == 0xFFFFFFFF);
  if (needs_zip64) {
    if (*eocd_offset < 20) {
      return Err(ReasonCode::kPackageMalformed, "ZIP64 indicated but no locator fits");
    }
    const std::uint8_t* locator = bytes_.data() + *eocd_offset - 20;
    if (Read32(locator) != kZip64LocatorSignature) {
      return Err(ReasonCode::kPackageMalformed, "ZIP64 indicated but locator signature missing");
    }
    const std::uint64_t z64_offset = Read64(locator + 8);
    if (!InBounds(bytes_.size(), z64_offset, 56)) {
      return Err(ReasonCode::kPackageMalformed, "ZIP64 EOCD offset is outside the archive");
    }
    const std::uint8_t* z64 = bytes_.data() + z64_offset;
    if (Read32(z64) != kZip64EocdSignature) {
      return Err(ReasonCode::kPackageMalformed, "ZIP64 EOCD signature missing");
    }
    entry_count = Read64(z64 + 32);
    cd_size = Read64(z64 + 40);
    cd_offset = Read64(z64 + 48);
  }

  if (entry_count > limits_.max_entries) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("archive declares {} entries, above the {} limit", entry_count,
                           limits_.max_entries));
  }
  if (!InBounds(bytes_.size(), cd_offset, cd_size)) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("central directory [{:#x},+{:#x}) is outside the {} byte archive",
                           cd_offset, cd_size, bytes_.size()));
  }

  entries_.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(entry_count, 65536)));

  std::uint64_t cursor = cd_offset;
  const std::uint64_t cd_end = cd_offset + cd_size;
  std::uint64_t total_uncompressed = 0;

  for (std::uint64_t i = 0; i < entry_count; ++i) {
    if (!InBounds(bytes_.size(), cursor, 46) || cursor + 46 > cd_end) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("central directory entry {} is truncated", i));
    }
    const std::uint8_t* h = bytes_.data() + cursor;
    if (Read32(h) != kCentralSignature) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("central directory entry {} has a bad signature", i));
    }

    const std::uint16_t method = Read16(h + 10);
    const std::uint32_t crc = Read32(h + 16);
    std::uint64_t comp_size = Read32(h + 20);
    std::uint64_t uncomp_size = Read32(h + 24);
    const std::uint16_t name_len = Read16(h + 28);
    const std::uint16_t extra_len = Read16(h + 30);
    const std::uint16_t comment_len = Read16(h + 32);
    std::uint64_t local_offset = Read32(h + 42);

    const std::uint64_t header_size =
        46ull + name_len + extra_len + comment_len;
    if (cursor + header_size > cd_end) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("central directory entry {} overruns the directory", i));
    }
    if (name_len > limits_.max_name_length) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("entry {} has a {}-byte name, above the limit", i, name_len));
    }

    ZipEntry entry;
    entry.name.assign(reinterpret_cast<const char*>(h + 46), name_len);

    // ZIP64 extra field, when any 32-bit size field saturated.
    if (comp_size == 0xFFFFFFFF || uncomp_size == 0xFFFFFFFF || local_offset == 0xFFFFFFFF) {
      const std::uint8_t* extra = h + 46 + name_len;
      std::uint64_t pos = 0;
      bool found = false;
      while (pos + 4 <= extra_len) {
        const std::uint16_t tag = Read16(extra + pos);
        const std::uint16_t len = Read16(extra + pos + 2);
        if (pos + 4 + len > extra_len) break;
        if (tag == 0x0001) {
          const std::uint8_t* z = extra + pos + 4;
          std::uint64_t consumed = 0;
          if (uncomp_size == 0xFFFFFFFF && consumed + 8 <= len) {
            uncomp_size = Read64(z + consumed);
            consumed += 8;
          }
          if (comp_size == 0xFFFFFFFF && consumed + 8 <= len) {
            comp_size = Read64(z + consumed);
            consumed += 8;
          }
          if (local_offset == 0xFFFFFFFF && consumed + 8 <= len) {
            local_offset = Read64(z + consumed);
            consumed += 8;
          }
          found = true;
          break;
        }
        pos += 4ull + len;
      }
      if (!found) {
        return Err(ReasonCode::kPackageMalformed,
                   std::format("entry '{}' saturates a 32-bit field but has no ZIP64 extra",
                               entry.name));
      }
    }

    if (method != static_cast<std::uint16_t>(CompressionMethod::kStore) &&
        method != static_cast<std::uint16_t>(CompressionMethod::kDeflate)) {
      return Err(ReasonCode::kPackageUnsupportedContainer,
                 std::format("entry '{}' uses compression method {}; only store and deflate "
                             "are supported",
                             entry.name, method));
    }
    if (uncomp_size > limits_.max_entry_uncompressed) {
      return Err(ReasonCode::kResourceExhausted,
                 std::format("entry '{}' declares {} uncompressed bytes, above the limit",
                             entry.name, uncomp_size));
    }
    // Decompression-bomb guard: a huge expansion from a tiny input.
    if (comp_size > 0 && uncomp_size / comp_size > limits_.max_compression_ratio) {
      return Err(ReasonCode::kResourceExhausted,
                 std::format("entry '{}' has a compression ratio of {}:1, above the {}:1 limit",
                             entry.name, uncomp_size / comp_size,
                             limits_.max_compression_ratio));
    }
    total_uncompressed += uncomp_size;
    if (total_uncompressed > limits_.max_total_uncompressed) {
      return Err(ReasonCode::kResourceExhausted,
                 "archive expands to more than the total uncompressed limit");
    }
    if (!InBounds(bytes_.size(), local_offset, 30)) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("entry '{}' local header offset {:#x} is outside the archive",
                             entry.name, local_offset));
    }

    entry.method = static_cast<CompressionMethod>(method);
    entry.crc32 = crc;
    entry.compressed_size = comp_size;
    entry.uncompressed_size = uncomp_size;
    entry.local_header_offset = local_offset;
    entry.is_directory = !entry.name.empty() && entry.name.back() == '/';

    ARUN_RETURN_IF_ERROR(ResolveDataOffset(entry));
    entries_.push_back(std::move(entry));
    cursor += header_size;
  }

  ARUN_LOG_DEBUG(log::Category::kInstall, "{}: {} entries, {} bytes", name_, entries_.size(),
                 bytes_.size());
  return Status::Ok();
}

Status ZipReader::ResolveDataOffset(ZipEntry& entry) {
  // The local header repeats the name/extra lengths, and they can differ from the
  // central directory. The data starts after the LOCAL header, so it must be read
  // from there -- trusting the central directory's lengths here is a classic bug.
  const std::uint8_t* lh = bytes_.data() + entry.local_header_offset;
  if (Read32(lh) != kLocalSignature) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("entry '{}' has a bad local header signature", entry.name));
  }
  const std::uint16_t name_len = Read16(lh + 26);
  const std::uint16_t extra_len = Read16(lh + 28);
  const std::uint64_t data_offset = entry.local_header_offset + 30ull + name_len + extra_len;
  if (!InBounds(bytes_.size(), data_offset, entry.compressed_size)) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("entry '{}' data [{:#x},+{:#x}) is outside the archive", entry.name,
                           data_offset, entry.compressed_size));
  }
  entry.data_offset = data_offset;
  return Status::Ok();
}

const ZipEntry* ZipReader::Find(std::string_view entry_name) const {
  for (const ZipEntry& e : entries_) {
    if (e.name == entry_name) return &e;
  }
  return nullptr;
}

Result<std::span<const std::uint8_t>> ZipReader::RawData(const ZipEntry& entry) const {
  if (entry.data_offset == 0 && entry.compressed_size != 0) {
    return Err(ReasonCode::kInternal, "entry data offset was not resolved");
  }
  if (!InBounds(bytes_.size(), entry.data_offset, entry.compressed_size)) {
    return Err(ReasonCode::kPackageMalformed, "entry data is outside the archive");
  }
  return bytes_.subspan(static_cast<std::size_t>(entry.data_offset),
                        static_cast<std::size_t>(entry.compressed_size));
}

Result<std::vector<std::uint8_t>> ZipReader::Extract(const ZipEntry& entry) const {
  ARUN_ASSIGN_OR_RETURN(auto raw, RawData(entry));

  std::vector<std::uint8_t> out;
  if (entry.method == CompressionMethod::kStore) {
    if (entry.compressed_size != entry.uncompressed_size) {
      return Err(ReasonCode::kPackageMalformed,
                 std::format("stored entry '{}' has mismatched sizes ({} vs {})", entry.name,
                             entry.compressed_size, entry.uncompressed_size));
    }
    out.assign(raw.begin(), raw.end());
  } else {
    out.assign(static_cast<std::size_t>(entry.uncompressed_size), 0);
    ARUN_RETURN_IF_ERROR(
        InflateRaw(raw, out).WithContext(std::format("inflating '{}'", entry.name)));
  }

  // The archive told us what the bytes should hash to. Checking it turns a silent
  // decompression bug or a tampered entry into a specific, loud failure.
  // Note: this is an INTEGRITY check, not a signature check. Spec 13.2 is explicit
  // that a CRC is not signature verification.
  const std::uint32_t actual = Crc32(out);
  if (actual != entry.crc32) {
    return Err(ReasonCode::kPackageMalformed,
               std::format("entry '{}' has CRC32 {:#010x} but the directory declared {:#010x}",
                           entry.name, actual, entry.crc32));
  }
  return out;
}

Result<std::string> ZipReader::Sha256Hex() const {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
    return Err(ReasonCode::kHostOsError, "BCryptOpenAlgorithmProvider(SHA256) failed");
  }
  std::uint8_t digest[32] = {};
  const NTSTATUS st =
      BCryptHash(alg, nullptr, 0, const_cast<std::uint8_t*>(bytes_.data()),
                 static_cast<ULONG>(bytes_.size()), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(alg, 0);
  if (!BCRYPT_SUCCESS(st)) {
    return Err(ReasonCode::kHostOsError, "BCryptHash(SHA256) failed");
  }

  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (std::uint8_t b : digest) {
    hex += kDigits[b >> 4];
    hex += kDigits[b & 0x0f];
  }
  return hex;
}

}  // namespace arun::pkg
