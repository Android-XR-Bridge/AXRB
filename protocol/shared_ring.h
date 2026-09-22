#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace axrb::protocol {

// Lock-free SPSC (Single Producer, Single Consumer) ring buffer for frame
// metadata transport. Replaces TCP for the GPU frame descriptor channel,
// eliminating kernel transitions and the ACK round-trip.
//
// Memory layout:
//   [CacheLine0]  write_index (atomic, written by producer)
//   [CacheLine1]  read_index  (atomic, written by consumer)
//   [CacheLine2+] entries[Capacity]
//
// Each entry is 256 bytes, aligned to cache line size.
// Capacity entries provides buffering for pipeline overlap.

constexpr uint32_t kRingCapacity = 16;           // Must be power of 2
constexpr uint32_t kRingEntrySize = 256;         // Bytes per entry (cache-line aligned)
constexpr uint32_t kRingHeaderSize = 128;        // Two cache lines for indices
constexpr uint32_t kRingTotalSize = kRingHeaderSize + kRingCapacity * kRingEntrySize;
constexpr uint64_t kRingMagic = 0x52494E4758425241ULL; // "AXRBNGR"

struct RingEntry {
    uint8_t data[kRingEntrySize];
};

// Shared memory header - written once at initialization.
struct RingHeader {
    uint64_t magic = kRingMagic;
    uint32_t capacity = kRingCapacity;
    uint32_t entry_size = kRingEntrySize;
    std::atomic<uint64_t> write_index{0};  // Next slot to write (producer)
    std::atomic<uint64_t> read_index{0};   // Next slot to read (consumer)
    uint8_t padding[96];                    // Pad to 128 bytes (2 cache lines)
};

static_assert(sizeof(RingHeader) == 128);
static_assert(offsetof(RingHeader, write_index) == 16);
static_assert(offsetof(RingHeader, read_index) == 80);

class SharedRingBuffer {
public:
    SharedRingBuffer() = default;
    ~SharedRingBuffer() { close(); }

    // Non-copyable, non-movable (owns shared memory mapping)
    SharedRingBuffer(const SharedRingBuffer&) = delete;
    SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;

    // Create a new shared ring buffer (producer side).
    // On Windows, creates a named file mapping.
    // On Linux/Android, creates a file in the specified path.
    bool create(const char* name) {
        close();
        name_ = name;
#if defined(_WIN32)
        mapping_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, kRingTotalSize, name);
        if (!mapping_) return false;
        data_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kRingTotalSize));
        if (!data_) { CloseHandle(mapping_); mapping_ = nullptr; return false; }
#else
        fd_ = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) return false;
        if (ftruncate(fd_, kRingTotalSize) != 0) { ::close(fd_); fd_ = -1; return false; }
        data_ = static_cast<uint8_t*>(mmap(nullptr, kRingTotalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (data_ == MAP_FAILED) { ::close(fd_); fd_ = -1; data_ = nullptr; return false; }
#endif
        header_ = reinterpret_cast<RingHeader*>(data_);
        entries_ = reinterpret_cast<RingEntry*>(data_ + kRingHeaderSize);
        // Initialize header
        header_->magic = kRingMagic;
        header_->capacity = kRingCapacity;
        header_->entry_size = kRingEntrySize;
        header_->write_index.store(0, std::memory_order_relaxed);
        header_->read_index.store(0, std::memory_order_relaxed);
        owner_ = true;
        return true;
    }

    // Open an existing shared ring buffer (consumer side).
    bool open(const char* name) {
        close();
        name_ = name;
#if defined(_WIN32)
        mapping_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!mapping_) return false;
        data_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kRingTotalSize));
        if (!data_) { CloseHandle(mapping_); mapping_ = nullptr; return false; }
#else
        fd_ = shm_open(name, O_RDWR, 0666);
        if (fd_ < 0) return false;
        data_ = static_cast<uint8_t*>(mmap(nullptr, kRingTotalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (data_ == MAP_FAILED) { ::close(fd_); fd_ = -1; data_ = nullptr; return false; }
#endif
        header_ = reinterpret_cast<RingHeader*>(data_);
        entries_ = reinterpret_cast<RingEntry*>(data_ + kRingHeaderSize);
        // Validate magic
        if (header_->magic != kRingMagic) { close(); return false; }
        owner_ = false;
        return true;
    }

    void close() {
#if defined(_WIN32)
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        mapping_ = nullptr;
#else
        if (data_) munmap(data_, kRingTotalSize);
        if (fd_ >= 0) ::close(fd_);
        if (owner_ && !name_.empty()) shm_unlink(name_.c_str());
        fd_ = -1;
#endif
        data_ = nullptr;
        header_ = nullptr;
        entries_ = nullptr;
        name_.clear();
    }

    // Producer: write an entry to the ring buffer.
    // Returns true if the entry was written, false if the buffer is full.
    bool write(const void* data, uint32_t size) {
        if (!header_ || size > kRingEntrySize) return false;
        const uint64_t write = header_->write_index.load(std::memory_order_relaxed);
        const uint64_t read = header_->read_index.load(std::memory_order_acquire);
        if (write - read >= kRingCapacity) return false; // Buffer full
        auto& entry = entries_[write & (kRingCapacity - 1)];
        std::memcpy(entry.data, data, size);
        if (size < kRingEntrySize) std::memset(entry.data + size, 0, kRingEntrySize - size);
        header_->write_index.store(write + 1, std::memory_order_release);
        return true;
    }

    // Consumer: read an entry from the ring buffer.
    // Returns true if an entry was read, false if the buffer is empty.
    bool read(void* data, uint32_t size) {
        if (!header_ || size > kRingEntrySize) return false;
        const uint64_t read = header_->read_index.load(std::memory_order_relaxed);
        const uint64_t write = header_->write_index.load(std::memory_order_acquire);
        if (read >= write) return false; // Buffer empty
        const auto& entry = entries_[read & (kRingCapacity - 1)];
        std::memcpy(data, entry.data, size);
        header_->read_index.store(read + 1, std::memory_order_release);
        return true;
    }

    // Consumer: peek at the next entry without consuming it.
    bool peek(void* data, uint32_t size) const {
        if (!header_ || size > kRingEntrySize) return false;
        const uint64_t read = header_->read_index.load(std::memory_order_relaxed);
        const uint64_t write = header_->write_index.load(std::memory_order_acquire);
        if (read >= write) return false;
        const auto& entry = entries_[read & (kRingCapacity - 1)];
        std::memcpy(data, entry.data, size);
        return true;
    }

    // Number of entries available to read.
    uint64_t available() const {
        if (!header_) return 0;
        return header_->write_index.load(std::memory_order_acquire) -
               header_->read_index.load(std::memory_order_relaxed);
    }

    // Number of free slots available to write.
    uint64_t free_slots() const {
        if (!header_) return 0;
        return kRingCapacity - (header_->write_index.load(std::memory_order_relaxed) -
                                header_->read_index.load(std::memory_order_acquire));
    }

    bool is_valid() const { return header_ != nullptr; }

private:
    std::string name_;
    uint8_t* data_ = nullptr;
    RingHeader* header_ = nullptr;
    RingEntry* entries_ = nullptr;
    bool owner_ = false;
#if defined(_WIN32)
    HANDLE mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace axrb::protocol
