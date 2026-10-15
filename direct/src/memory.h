#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace axrb {

constexpr uint64_t kPageSize = 4096;
constexpr uint64_t page_down(uint64_t v) { return v & ~(kPageSize - 1); }
constexpr uint64_t page_up(uint64_t v) { return (v + kPageSize - 1) & ~(kPageSize - 1); }
// arm64 Linux ignores the top byte of user data addresses (TBI) and Android's
// allocator tags heap pointers there; host code must strip it before use.
constexpr uint64_t untag(uint64_t address) { return address & 0x00ff'ffff'ffff'ffffull; }

class OpenFile;  // files.h

// Guest memory is identity mapped: a guest address is the host address of the
// same byte, so structures with pointers inside (Vulkan, OpenXR, JNI) cross
// the boundary unchanged. Linux mappings live inside one large reservation
// (the arena) and are committed lazily, page run by page run, on first touch:
// the vectored exception handler commits (and, for file mappings, reads)
// pages of a mapping the guest is entitled to access.
struct Vma {
    uint64_t start = 0;
    uint64_t end = 0;
    uint32_t prot = 0;      // PROT_*
    uint32_t flags = 0;     // MAP_*
    std::shared_ptr<OpenFile> file;  // file-backed mapping (content read on fault)
    uint64_t offset = 0;    // file offset of `start`
    std::string name;       // for /proc/self/maps
};

class AddressSpace {
public:
    bool init(bool low_memory = false);
    uint64_t arena_base() const { return base_; }
    uint64_t arena_end() const { return end_; }
    // Includes ART's optional low reservations. Host buffers outside all guest
    // reservations are also used by the existing native bridge.
    bool overlaps_guest(uint64_t addr, uint64_t len) const;

    // Linux semantics; results are addresses or -errno.
    int64_t mmap(uint64_t addr, uint64_t length, uint32_t prot, uint32_t flags, std::shared_ptr<OpenFile> file,
                 uint64_t offset, const char* name = nullptr);
    int64_t munmap(uint64_t addr, uint64_t length);
    int64_t mprotect(uint64_t addr, uint64_t length, uint32_t prot);
    int64_t mremap(uint64_t old_addr, uint64_t old_size, uint64_t new_size, uint32_t flags, uint64_t new_addr);
    int64_t madvise(uint64_t addr, uint64_t length, int advice);
    int64_t brk(uint64_t addr);
    int64_t msync(uint64_t addr, uint64_t length, int flags);
    int64_t mincore(uint64_t addr, uint64_t length, uint8_t* out);
    void set_brk_base(uint64_t base);
    void name_range(uint64_t addr, uint64_t length, const std::string& name);

    // Called by the vectored exception handler. True if the fault was a
    // first touch of a mapping and the access may be retried.
    bool handle_fault(uint64_t address, bool write, bool execute);

    // Whether [addr, addr+len) is mapped with at least `prot`.
    bool check(uint64_t addr, uint64_t len, uint32_t prot) const;
    // Whether any part of [addr, addr+len) is mapped executable.
    bool any_exec(uint64_t addr, uint64_t len) const;
    // Makes sure the range is committed (host code about to touch it in bulk).
    bool populate(uint64_t addr, uint64_t len);
    // populate() and guest_read(), without the lock when the pages are
    // already complete: the translator fetches guest code this way.
    bool read_complete(uint64_t addr, void* out, size_t len);

    std::string maps_text() const;
    uint64_t mapped_bytes() const;
    void for_each(const std::function<void(const Vma&)>& visit) const;

private:
    int64_t map_locked(uint64_t addr, uint64_t length, uint32_t prot, uint32_t flags, std::shared_ptr<OpenFile> file,
                       uint64_t offset, const std::string& name);
    void unmap_locked(uint64_t start, uint64_t end);
    uint64_t find_free_locked(uint64_t length, uint64_t hint) const;
    bool range_free_locked(uint64_t start, uint64_t end) const;
    void split_locked(uint64_t at);
    bool fill_locked(const Vma& vma, uint64_t start, uint64_t end, const uint8_t* content = nullptr);
    int fill_file(std::unique_lock<std::shared_mutex>& held, uint64_t start, uint64_t end);
    bool owns_range(uint64_t start, uint64_t end) const;

    uint64_t base_ = 0, end_ = 0;
    std::vector<std::pair<uint64_t, uint64_t>> low_reservations_;
    uint64_t mmap_top_ = 0;   // next top-down allocation candidate
    uint64_t brk_base_ = 0, brk_ = 0;
    mutable std::shared_mutex lock_;
    // Odd while a page is committed but its content not yet copied in.
    std::atomic<uint64_t> fill_sequence_{0};
    std::map<uint64_t, Vma> vmas_;  // keyed by start
};

AddressSpace& address_space();

// Host access to guest memory with fault containment; false on a bad pointer.
bool guest_read(uint64_t addr, void* out, size_t len);
bool guest_write(uint64_t addr, const void* data, size_t len);
// Reads a NUL-terminated string (bounded); false on a bad pointer.
bool guest_string(uint64_t addr, std::string& out, size_t max = 65536);
template <class T>
inline T* gptr(uint64_t addr) { return reinterpret_cast<T*>(untag(addr)); }

uint32_t host_protection(uint32_t prot);

}  // namespace axrb
