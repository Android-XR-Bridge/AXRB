#include "arun/elf_image.h"

#include <windows.h>

namespace arun::elf {

MappedFile::~MappedFile() { Close(); }

MappedFile::MappedFile(MappedFile&& o) noexcept
    : data_(o.data_), size_(o.size_), mapping_(o.mapping_), file_(o.file_) {
  o.data_ = nullptr;
  o.size_ = 0;
  o.mapping_ = nullptr;
  o.file_ = nullptr;
}

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
  if (this != &o) {
    Close();
    data_ = o.data_;
    size_ = o.size_;
    mapping_ = o.mapping_;
    file_ = o.file_;
    o.data_ = nullptr;
    o.size_ = 0;
    o.mapping_ = nullptr;
    o.file_ = nullptr;
  }
  return *this;
}

void MappedFile::Close() {
  if (data_ != nullptr) UnmapViewOfFile(data_);
  if (mapping_ != nullptr) CloseHandle(mapping_);
  if (file_ != nullptr && file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  data_ = nullptr;
  size_ = 0;
  mapping_ = nullptr;
  file_ = nullptr;
}

Result<MappedFile> MappedFile::Open(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return Win32Err("CreateFileW", GetLastError()).WithContext("opening ELF file");
  }

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size)) {
    const DWORD e = GetLastError();
    CloseHandle(file);
    return Win32Err("GetFileSizeEx", e);
  }
  if (size.QuadPart == 0) {
    CloseHandle(file);
    return Err(ReasonCode::kElfMalformed, "file is empty");
  }

  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) {
    const DWORD e = GetLastError();
    CloseHandle(file);
    return Win32Err("CreateFileMappingW", e);
  }

  void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (view == nullptr) {
    const DWORD e = GetLastError();
    CloseHandle(mapping);
    CloseHandle(file);
    return Win32Err("MapViewOfFile", e);
  }

  MappedFile m;
  m.file_ = file;
  m.mapping_ = mapping;
  m.data_ = static_cast<const std::uint8_t*>(view);
  m.size_ = static_cast<std::size_t>(size.QuadPart);
  return m;
}

}  // namespace arun::elf
