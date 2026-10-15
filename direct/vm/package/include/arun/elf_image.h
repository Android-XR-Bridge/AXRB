// Read-only mapped host files (from Androitainer's ELF module; only the part
// the zip reader uses).
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "arun/status.h"

namespace arun::elf {

// Read-only view of a host file. Owns the mapping.
class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&&) noexcept;
  MappedFile& operator=(MappedFile&&) noexcept;

  static Result<MappedFile> Open(const std::wstring& path);

  std::span<const std::uint8_t> bytes() const { return {data_, size_}; }
  bool valid() const { return data_ != nullptr; }

 private:
  void Close();
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  void* mapping_ = nullptr;  // HANDLE
  void* file_ = nullptr;     // HANDLE
};

}  // namespace arun::elf
