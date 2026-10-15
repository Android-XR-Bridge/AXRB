#include "memory.h"

#include "files.h"
#include "linux.h"
#include "log.h"

#include <windows.h>

#include <atomic>

#include <algorithm>
#include <cstring>
#include <vector>

namespace axrb {
using namespace linux_abi;

namespace {
// Android arm64 user space historically had 39 address bits, and some engine
// code still packs pointers assuming that; keep every guest mapping below it.
constexpr uint64_t kPreferredBase = 0x0000001000000000ull;  // 64 GiB
constexpr uint64_t kPreferredEnd = 0x0000007f00000000ull;   // just under 512 GiB
constexpr uint64_t kMinimumArena = 0x0000004000000000ull;   // 256 GiB
constexpr uint64_t kEagerCommitLimit = 2ull << 20;
constexpr uint64_t kEagerFileLimit = 64ull << 20;
constexpr uint64_t kChunk = 64 * 1024;
constexpr uint64_t kFileChunk = 1ull << 20;  // file content read per lock release

bool committed(uint64_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof info)) return false;
    return info.State == MEM_COMMIT;
}

// Visits maximal runs of pages in [start, end) by commit state.
template <class F>
void for_each_run(uint64_t start, uint64_t end, F&& visit) {
    uint64_t at = start;
    while (at < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info)) break;
        uint64_t run_end = std::min<uint64_t>(end, reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize);
        visit(at, run_end, info.State == MEM_COMMIT, info.Protect);
        at = run_end;
    }
}

}  // namespace

uint32_t host_protection(uint32_t prot) {
    // Guest code is never executed natively, so PROT_EXEC only means readable.
    if (prot & PROT_WRITE_) return PAGE_READWRITE;
    if (prot & (PROT_READ_ | PROT_EXEC_)) return PAGE_READONLY;
    return PAGE_NOACCESS;
}

AddressSpace& address_space() {
    static AddressSpace space;
    return space;
}

bool AddressSpace::init(bool low_memory) {
    void* got = VirtualAlloc(reinterpret_cast<void*>(kPreferredBase), kPreferredEnd - kPreferredBase, MEM_RESERVE,
                             PAGE_NOACCESS);
    if (got) {
        base_ = kPreferredBase;
        end_ = kPreferredEnd;
    } else {
        // Something already lives there: take the largest free span below 2^39,
        // then anywhere, as long as it is big enough to be useful.
        uint64_t best = 0, best_size = 0;
        for (uint64_t at = 0x10000; at < 0x00007ff000000000ull;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info)) break;
            uint64_t region = reinterpret_cast<uint64_t>(info.BaseAddress), size = info.RegionSize;
            if (info.State == MEM_FREE && size > best_size && (region + size <= kPreferredEnd || best_size < kMinimumArena)) {
                best = (region + 0xffffffull) & ~0xffffffull;
                best_size = region + size - best;
            }
            at = region + size;
        }
        if (best_size < kMinimumArena / 4) {
            LOGE("memory", "no free address range for the guest arena");
            return false;
        }
        best_size = std::min<uint64_t>(best_size, kPreferredEnd - kPreferredBase);
        got = VirtualAlloc(reinterpret_cast<void*>(best), best_size, MEM_RESERVE, PAGE_NOACCESS);
        if (!got) return false;
        base_ = best;
        end_ = best + best_size;
    }
    mmap_top_ = end_;
    // ART uses compressed 32-bit heap references. Reserve free low address
    // ranges separately, without ever replacing Windows-owned mappings.
    if (low_memory) {
        for (uint64_t at = 0x10000; at < 0x100000000ull;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info)) break;
            uint64_t stop = std::min<uint64_t>(reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize, 0x100000000ull);
            uint64_t start = (at + kChunk - 1) & ~(kChunk - 1);
            if (info.State == MEM_FREE && stop > start && VirtualAlloc(reinterpret_cast<void*>(start), stop - start, MEM_RESERVE, PAGE_NOACCESS)) {
                low_reservations_.emplace_back(start, stop);
                LOGI("memory", "guest low arena %llx-%llx", (unsigned long long)start, (unsigned long long)stop);
            }
            if (stop <= at) break;
            at = stop;
        }
    }
    LOGI("memory", "guest arena %llx-%llx (%llu GiB)", (unsigned long long)base_, (unsigned long long)end_,
         (unsigned long long)((end_ - base_) >> 30));
    return true;
}

bool AddressSpace::owns_range(uint64_t start, uint64_t end) const {
    if (end <= start) return false;
    if (start >= base_ && end <= end_) return true;
    for (auto [lo, hi] : low_reservations_) if (start >= lo && end <= hi) return true;
    return false;
}

bool AddressSpace::overlaps_guest(uint64_t addr, uint64_t len) const {
    addr = untag(addr);
    if (!len) return false;
    const uint64_t end = addr + len;
    if (end <= addr) return true;  // invalid buffers must not take the host path
    if (addr < end_ && end > base_) return true;
    for (auto [lo, hi] : low_reservations_) if (addr < hi && end > lo) return true;
    return false;
}

bool AddressSpace::range_free_locked(uint64_t start, uint64_t end) const {
    if (!owns_range(start, end)) return false;
    auto it = vmas_.upper_bound(start);
    if (it != vmas_.begin()) {
        auto prev = std::prev(it);
        if (prev->second.end > start) return false;
    }
    return it == vmas_.end() || it->first >= end;
}

uint64_t AddressSpace::find_free_locked(uint64_t length, uint64_t hint) const {
    if (hint && range_free_locked(page_down(hint), page_down(hint) + length)) return page_down(hint);
    // A hint below 4 GiB asks for low memory (ART's arm64 allocator probes
    // for it page by page, unmapping every high result): take the first gap
    // at or above the hint there.
    if (hint && hint < (1ull << 32)) {
        for (auto [lo, hi] : low_reservations_) {
            uint64_t at = std::max(lo, page_down(hint));
            if (at >= hi) continue;
            auto it = vmas_.upper_bound(at);
            if (it != vmas_.begin() && std::prev(it)->second.end > at) at = std::prev(it)->second.end;
            while (at + length <= hi) {
                if (it == vmas_.end() || it->first >= at + length) return at;
                at = std::max(at, it->second.end);
                ++it;
            }
        }
    }
    // Top-down first fit, like Linux's mmap_base allocator, starting below
    // the previous allocation (Linux's old free_area_cache) rather than
    // walking every mapping from the top each time; then from the top.
    for (uint64_t from : {mmap_top_, end_}) {
        uint64_t top = from;
        for (auto it = std::make_reverse_iterator(vmas_.lower_bound(from)); it != vmas_.rend(); ++it) {
            if (it->second.start < base_) break;
            if (it->second.end <= top && top - it->second.end >= length) return top - length;
            top = std::min(top, it->second.start);
        }
        if (top >= base_ && top - base_ >= length) return top - length;
        if (from == end_) break;
    }
    return 0;
}

void AddressSpace::split_locked(uint64_t at) {
    auto it = vmas_.upper_bound(at);
    if (it == vmas_.begin()) return;
    --it;
    Vma& vma = it->second;
    if (at <= vma.start || at >= vma.end) return;
    Vma tail = vma;
    tail.start = at;
    if (tail.file) tail.offset = vma.offset + (at - vma.start);
    vma.end = at;
    vmas_.emplace(at, std::move(tail));
}

void AddressSpace::unmap_locked(uint64_t start, uint64_t end) {
    // Freed space above the allocation cursor is found again from there.
    if (end > mmap_top_ && start < end_) mmap_top_ = std::min(end, end_);
    split_locked(start);
    split_locked(end);
    auto it = vmas_.lower_bound(start);
    while (it != vmas_.end() && it->first < end) {
        Vma& vma = it->second;
        if (vma.file && (vma.flags & MAP_SHARED_) && (vma.prot & PROT_WRITE_)) {
            // Write back what was touched; the file is the shared object.
            for_each_run(vma.start, vma.end, [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
                if (is_committed) vma.file->pwrite_host(reinterpret_cast<const void*>(a), b - a, vma.offset + (a - vma.start));
            });
        }
        VirtualFree(reinterpret_cast<void*>(vma.start), vma.end - vma.start, MEM_DECOMMIT);
        it = vmas_.erase(it);
    }
}

bool AddressSpace::fill_locked(const Vma& vma, uint64_t start, uint64_t end, const uint8_t* content) {
    // Lock-free readers (read_complete) retry rather than see a committed
    // page before its content.
    fill_sequence_.fetch_add(1, std::memory_order_acq_rel);
    struct Done {
        std::atomic<uint64_t>& sequence;
        ~Done() { sequence.fetch_add(1, std::memory_order_release); }
    } done{fill_sequence_};
    // Commit writable first so file content can be placed, then apply the
    // mapping's protection.
    if (!VirtualAlloc(reinterpret_cast<void*>(start), end - start, MEM_COMMIT, PAGE_READWRITE)) {
        LOGE("memory", "commit %llx-%llx failed: %lu", (unsigned long long)start, (unsigned long long)end,
             GetLastError());
        return false;
    }
    if (content) {
        std::memcpy(reinterpret_cast<void*>(start), content, end - start);
    } else if (vma.file) {
        uint64_t file_offset = vma.offset + (start - vma.start);
        int64_t got = vma.file->pread_host(reinterpret_cast<void*>(start), end - start, file_offset);
        if (got < 0) got = 0;
        // Bytes past the end of the file read as zero, as on Linux; a fresh
        // commit is already zero.
    }
    DWORD old = 0;
    uint32_t protection = host_protection(vma.prot);
    if (protection != PAGE_READWRITE) VirtualProtect(reinterpret_cast<void*>(start), end - start, protection, &old);
    return true;
}

int AddressSpace::fill_file(std::unique_lock<std::shared_mutex>& held, uint64_t start, uint64_t end) {
    // [start, end) lies in one file mapping. Its content is read with the
    // lock released, so a disk read does not stall every other thread's
    // faults and mappings; the pages are committed after taking it again.
    auto it = std::prev(vmas_.upper_bound(start));
    const std::shared_ptr<OpenFile> file = it->second.file;
    const uint64_t offset = it->second.offset + (start - it->second.start);
    thread_local std::vector<uint8_t> content;
    content.resize(end - start);
    held.unlock();
    int64_t got = file->pread_host(content.data(), end - start, offset);
    got = std::max<int64_t>(got, 0);
    // Bytes past the end of the file read as zero, as on Linux.
    std::memset(content.data() + got, 0, end - start - got);
    held.lock();
    // 0 if the mapping changed meanwhile and the caller should look again.
    it = vmas_.upper_bound(start);
    if (it == vmas_.begin()) return 0;
    const Vma& vma = std::prev(it)->second;
    if (vma.file != file || vma.end < end || vma.offset + (start - vma.start) != offset || vma.prot == PROT_NONE_)
        return 0;
    bool ok = true;
    for_each_run(start, end, [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
        if (!is_committed) ok = fill_locked(vma, a, b, content.data() + (a - start)) && ok;
    });
    return ok ? 1 : -1;
}

int64_t AddressSpace::map_locked(uint64_t addr, uint64_t length, uint32_t prot, uint32_t flags,
                                 std::shared_ptr<OpenFile> file, uint64_t offset, const std::string& name) {
    length = page_up(length);
    if (!length) return -EINVAL_;
    uint64_t start;
    if (flags & (MAP_FIXED_ | MAP_FIXED_NOREPLACE_)) {
        if (addr & (kPageSize - 1)) return -EINVAL_;
        start = addr;
        if (!owns_range(start, start + length)) {
            LOGW("memory", "fixed mapping %llx+%llx outside the guest arena", (unsigned long long)addr,
                 (unsigned long long)length);
            return -ENOMEM_;
        }
        if ((flags & MAP_FIXED_NOREPLACE_) && !(flags & MAP_FIXED_) && !range_free_locked(start, start + length))
            return -EEXIST_;
        unmap_locked(start, start + length);
    } else {
        start = find_free_locked(length, addr);
        if (!start) return -ENOMEM_;
    }
    Vma vma;
    vma.start = start;
    vma.end = start + length;
    vma.prot = prot;
    vma.flags = flags & ~(MAP_FIXED_ | MAP_FIXED_NOREPLACE_);
    vma.file = std::move(file);
    vma.offset = offset;
    vma.name = name;
    if (vma.file && vma.name.empty()) vma.name = vma.file->guest_path();
    if (!(flags & (MAP_FIXED_ | MAP_FIXED_NOREPLACE_)) && start >= base_) mmap_top_ = start;
    auto inserted = vmas_.emplace(start, std::move(vma)).first;
    const Vma& placed = inserted->second;
    // Small file mappings are read eagerly by mmap(), outside the lock.
    if (prot != PROT_NONE_ && !placed.file && ((flags & MAP_POPULATE_) || length <= kEagerCommitLimit))
        fill_locked(placed, placed.start, placed.end);
    return static_cast<int64_t>(start);
}

int64_t AddressSpace::mmap(uint64_t addr, uint64_t length, uint32_t prot, uint32_t flags,
                           std::shared_ptr<OpenFile> file, uint64_t offset, const char* name) {
    if (offset & (kPageSize - 1)) return -EINVAL_;
    if (!length || length > (1ull << 46)) return -EINVAL_;
    const bool eager_file = file && prot != PROT_NONE_ &&
                            ((flags & MAP_POPULATE_) || (page_up(length) <= kEagerFileLimit && !(flags & MAP_SHARED_)));
    std::unique_lock<std::shared_mutex> held(lock_);
    const int64_t result = map_locked(addr, length, prot, flags, std::move(file), offset, name ? name : "");
    held.unlock();
    if (result >= 0 && eager_file) populate(static_cast<uint64_t>(result), length);
    return result;
}

int64_t AddressSpace::munmap(uint64_t addr, uint64_t length) {
    if ((addr & (kPageSize - 1)) || !length) return -EINVAL_;
    std::unique_lock<std::shared_mutex> held(lock_);
    uint64_t end = addr + page_up(length);
    if (!owns_range(addr, end)) {
        // Not ours: only a mapping the host made could be there, and the guest
        // has no business unmapping it.
        return 0;
    }
    unmap_locked(addr, end);
    return 0;
}

int64_t AddressSpace::mprotect(uint64_t addr, uint64_t length, uint32_t prot) {
    if (addr & (kPageSize - 1)) return -EINVAL_;
    uint64_t end = addr + page_up(length);
    if (end == addr) return 0;
    std::unique_lock<std::shared_mutex> held(lock_);
    // The whole range must be mapped.
    for (uint64_t at = addr; at < end;) {
        auto it = vmas_.upper_bound(at);
        if (it == vmas_.begin()) return -ENOMEM_;
        --it;
        if (it->second.end <= at) return -ENOMEM_;
        at = it->second.end;
    }
    split_locked(addr);
    split_locked(end);
    for (auto it = vmas_.lower_bound(addr); it != vmas_.end() && it->first < end; ++it) {
        Vma& vma = it->second;
        vma.prot = prot;
        DWORD protection = host_protection(prot);
        for_each_run(vma.start, vma.end, [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
            DWORD old = 0;
            if (is_committed) VirtualProtect(reinterpret_cast<void*>(a), b - a, protection, &old);
        });
    }
    return 0;
}

int64_t AddressSpace::mremap(uint64_t old_addr, uint64_t old_size, uint64_t new_size, uint32_t flags,
                             uint64_t new_addr) {
    if (old_addr & (kPageSize - 1)) return -EINVAL_;
    old_size = page_up(old_size);
    new_size = page_up(new_size);
    if (!new_size) return -EINVAL_;
    std::unique_lock<std::shared_mutex> held(lock_);
    auto it = vmas_.upper_bound(old_addr);
    if (it == vmas_.begin()) return -EFAULT_;
    --it;
    if (it->second.end <= old_addr) return -EFAULT_;
    Vma original = it->second;
    if (old_addr + old_size > original.end) old_size = original.end - old_addr;
    if (new_size <= old_size && !(flags & MREMAP_FIXED_)) {
        unmap_locked(old_addr + new_size, old_addr + old_size);
        return static_cast<int64_t>(old_addr);
    }
    if (!(flags & MREMAP_FIXED_) && old_addr + old_size == original.end &&
        range_free_locked(old_addr + old_size, old_addr + new_size)) {
        it->second.end = old_addr + new_size;
        return static_cast<int64_t>(old_addr);
    }
    if (!(flags & MREMAP_MAYMOVE_)) return -ENOMEM_;
    uint64_t target = (flags & MREMAP_FIXED_) ? new_addr : find_free_locked(new_size, 0);
    if (!target) return -ENOMEM_;
    if (flags & MREMAP_FIXED_) unmap_locked(target, target + new_size);
    Vma moved = original;
    moved.start = target;
    moved.end = target + new_size;
    if (moved.file) moved.offset = original.offset + (old_addr - original.start);
    vmas_.emplace(target, moved);
    // Carry over what the old pages hold.
    for_each_run(old_addr, old_addr + std::min(old_size, new_size), [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
        if (!is_committed) return;
        uint64_t to = target + (a - old_addr);
        VirtualAlloc(reinterpret_cast<void*>(to), b - a, MEM_COMMIT, PAGE_READWRITE);
        std::memcpy(reinterpret_cast<void*>(to), reinterpret_cast<void*>(a), b - a);
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(to), b - a, host_protection(moved.prot), &old);
    });
    unmap_locked(old_addr, old_addr + old_size);
    return static_cast<int64_t>(target);
}

int64_t AddressSpace::madvise(uint64_t addr, uint64_t length, int advice) {
    if (addr & (kPageSize - 1)) return -EINVAL_;
    if (advice != MADV_DONTNEED_ && advice != MADV_FREE_ && advice != MADV_REMOVE_) return 0;
    uint64_t end = addr + page_up(length);
    std::unique_lock<std::shared_mutex> held(lock_);
    // DONTNEED on a private mapping: the next touch sees zeros (or the file).
    // Decommitting gives exactly that through the normal first-touch path.
    for (uint64_t at = addr; at < end;) {
        auto it = vmas_.upper_bound(at);
        if (it == vmas_.begin()) break;
        --it;
        if (it->second.end <= at) {
            auto next = vmas_.upper_bound(at);
            if (next == vmas_.end()) break;
            at = next->first;
            continue;
        }
        uint64_t b = std::min(end, it->second.end);
        if (!(it->second.flags & MAP_SHARED_)) VirtualFree(reinterpret_cast<void*>(at), b - at, MEM_DECOMMIT);
        at = b;
    }
    return 0;
}

int64_t AddressSpace::msync(uint64_t addr, uint64_t length, int) {
    std::unique_lock<std::shared_mutex> held(lock_);
    uint64_t end = addr + page_up(length);
    // Unmapped guest pages are ENOMEM, as on Linux: ART probes with msync for
    // free low memory. Addresses outside the arena belong to the host.
    if (owns_range(page_down(addr), end)) {
        for (uint64_t at = page_down(addr); at < end;) {
            auto it = vmas_.upper_bound(at);
            if (it == vmas_.begin() || std::prev(it)->second.end <= at) return -ENOMEM_;
            at = std::prev(it)->second.end;
        }
    }
    for (auto it = vmas_.lower_bound(page_down(addr)); it != vmas_.end() && it->first < end; ++it) {
        Vma& vma = it->second;
        if (!vma.file || !(vma.flags & MAP_SHARED_) || !(vma.prot & PROT_WRITE_)) continue;
        for_each_run(std::max(vma.start, addr), std::min(vma.end, end), [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
            if (is_committed) vma.file->pwrite_host(reinterpret_cast<const void*>(a), b - a, vma.offset + (a - vma.start));
        });
    }
    return 0;
}

int64_t AddressSpace::mincore(uint64_t addr, uint64_t length, uint8_t* out) {
    if (addr & (kPageSize - 1)) return -EINVAL_;
    uint64_t pages = page_up(length) / kPageSize;
    for (uint64_t i = 0; i < pages; ++i) out[i] = committed(addr + i * kPageSize) ? 1 : 0;
    return 0;
}

void AddressSpace::set_brk_base(uint64_t base) {
    std::unique_lock<std::shared_mutex> held(lock_);
    brk_base_ = brk_ = page_up(base);
}

int64_t AddressSpace::brk(uint64_t addr) {
    std::unique_lock<std::shared_mutex> held(lock_);
    if (!addr || addr < brk_base_ || addr > brk_base_ + (512ull << 20)) return static_cast<int64_t>(brk_);
    uint64_t want = page_up(addr), have = page_up(brk_);
    if (want > have) {
        if (!range_free_locked(have, want)) return static_cast<int64_t>(brk_);
        map_locked(have, want - have, PROT_READ_ | PROT_WRITE_, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, nullptr, 0,
                   "[heap]");
    } else if (want < have) {
        unmap_locked(want, have);
    }
    brk_ = addr;
    return static_cast<int64_t>(brk_);
}

void AddressSpace::name_range(uint64_t addr, uint64_t length, const std::string& name) {
    std::unique_lock<std::shared_mutex> held(lock_);
    uint64_t end = addr + page_up(length);
    split_locked(addr);
    split_locked(end);
    for (auto it = vmas_.lower_bound(addr); it != vmas_.end() && it->first < end; ++it) it->second.name = name;
}

// Diagnostic counters reported by the profiler next to the syscalls.
extern std::atomic<uint64_t> g_syscall_counts[512];

bool AddressSpace::handle_fault(uint64_t address, bool write, bool execute) {
    (void)execute;
    if (!owns_range(address, address + 1)) return false;
    g_syscall_counts[500].fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::shared_mutex> held(lock_);
    for (;;) {
        auto it = vmas_.upper_bound(address);
        if (it == vmas_.begin()) return false;
        --it;
        const Vma& vma = it->second;
        if (address >= vma.end) return false;
        if (vma.prot == PROT_NONE_) return false;
        if (write && !(vma.prot & PROT_WRITE_)) return false;
        uint64_t page = page_down(address);
        if (committed(page)) {
            g_syscall_counts[502].fetch_add(1, std::memory_order_relaxed);
            // Already there (another thread got here first). Retry only if the
            // page now permits the access; otherwise it is a genuine fault.
            MEMORY_BASIC_INFORMATION info{};
            VirtualQuery(reinterpret_cast<void*>(page), &info, sizeof info);
            if (write) return info.Protect == PAGE_READWRITE;
            return info.Protect == PAGE_READWRITE || info.Protect == PAGE_READONLY;
        }
        uint64_t start = std::max(vma.start, address & ~(kChunk - 1));
        uint64_t end = std::min(vma.end, (address & ~(kChunk - 1)) + kChunk);
        if (vma.file) {
            if (const int filled = fill_file(held, start, end)) return filled > 0;
            continue;  // the mapping changed while its file was read
        }
        bool ok = true;
        for_each_run(start, end, [&](uint64_t a, uint64_t b, bool is_committed, DWORD) {
            if (!is_committed) ok = fill_locked(vma, a, b) && ok;
        });
        return ok;
    }
}

bool AddressSpace::check(uint64_t addr, uint64_t len, uint32_t prot) const {
    addr = untag(addr);
    if (len && addr + len <= addr) return false;
    std::shared_lock<std::shared_mutex> held(lock_);
    uint64_t end = addr + len;
    for (uint64_t at = addr; at < end;) {
        auto it = vmas_.upper_bound(at);
        if (it == vmas_.begin()) return false;
        --it;
        if (it->second.end <= at) return false;
        if ((it->second.prot & prot) != prot) return false;
        at = it->second.end;
    }
    return true;
}

bool AddressSpace::any_exec(uint64_t addr, uint64_t len) const {
    std::shared_lock<std::shared_mutex> held(lock_);
    uint64_t end = addr + len;
    auto it = vmas_.upper_bound(addr);
    if (it != vmas_.begin()) --it;
    for (; it != vmas_.end() && it->first < end; ++it)
        if (it->second.end > addr && (it->second.prot & PROT_EXEC_)) return true;
    return false;
}

// Reads one byte of every page; a first touch commits it through the fault handler.
static bool touch_pages(uint64_t start, uint64_t end) {
    __try {
        for (uint64_t page = start; page < end; page += kPageSize) (void)*reinterpret_cast<const volatile uint8_t*>(page);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool AddressSpace::populate(uint64_t addr, uint64_t len) {
    if (!len) return true;
    addr = untag(addr);
    if (addr + len <= addr || addr + len > UINT64_MAX - (kPageSize - 1)) return false;
    uint64_t start = page_down(addr), end = page_up(addr + len);
    if (!owns_range(start, end)) return !overlaps_guest(addr, len);
    g_syscall_counts[501].fetch_add(1, std::memory_order_relaxed);
    if (end - start <= 256 * kPageSize) {
        // Small buffers (socket and file I/O on every frame) are nearly always
        // there already. Reading them finds that out without the mapping lock
        // or a Windows memory query, which waits whenever another thread is
        // committing or releasing memory. As in read_complete, a fill that ran
        // meanwhile sends this to the locked path.
        const uint64_t before = fill_sequence_.load(std::memory_order_acquire);
        if (!(before & 1) && touch_pages(start, end)) {
            std::atomic_thread_fence(std::memory_order_acquire);
            if (fill_sequence_.load(std::memory_order_relaxed) == before) return true;
        }
    }
    {
        // Pages are filled under the exclusive lock, so a committed page seen
        // under the shared lock is complete. Most calls find everything there.
        std::shared_lock<std::shared_mutex> shared(lock_);
        bool complete = true;
        for (uint64_t at = start; at < end && complete;) {
            auto it = vmas_.upper_bound(at);
            if (it == vmas_.begin()) return false;
            --it;
            if (it->second.end <= at) return false;
            uint64_t b = std::min(end, it->second.end);
            if (it->second.prot != PROT_NONE_)
                for_each_run(at, b, [&](uint64_t, uint64_t, bool is_committed, DWORD) { complete = complete && is_committed; });
            at = b;
        }
        if (complete) return true;
    }
    std::unique_lock<std::shared_mutex> held(lock_);
    for (uint64_t at = start; at < end;) {
        auto it = vmas_.upper_bound(at);
        if (it == vmas_.begin()) return false;
        --it;
        const Vma& vma = it->second;
        if (vma.end <= at) return false;
        uint64_t b = std::min(end, vma.end);
        if (vma.prot != PROT_NONE_ && vma.file) {
            // File content a chunk at a time, read without the lock.
            uint64_t hole = b;
            for_each_run(at, b, [&](uint64_t x, uint64_t, bool is_committed, DWORD) {
                if (!is_committed && hole == b) hole = x;
            });
            if (hole != b) {
                if (fill_file(held, hole, std::min(b, hole + kFileChunk)) < 0) return false;
                at = hole;  // look again: filled now, or the mapping changed
                continue;
            }
        } else if (vma.prot != PROT_NONE_) {
            for_each_run(at, b, [&](uint64_t x, uint64_t y, bool is_committed, DWORD) {
                if (!is_committed) fill_locked(vma, x, y);
            });
        }
        at = b;
    }
    return true;
}

bool AddressSpace::read_complete(uint64_t addr, void* out, size_t len) {
    // A fill that ran meanwhile may have exposed a page before its content.
    const uint64_t before = fill_sequence_.load(std::memory_order_acquire);
    if (!(before & 1) && guest_read(addr, out, len)) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (fill_sequence_.load(std::memory_order_relaxed) == before) return true;
    }
    return populate(addr, len) && guest_read(addr, out, len);
}

std::string AddressSpace::maps_text() const {
    std::shared_lock<std::shared_mutex> held(lock_);
    std::string text;
    char line[512];
    for (const auto& [start, vma] : vmas_) {
        std::snprintf(line, sizeof line, "%llx-%llx %c%c%c%c %08llx %s %llu", (unsigned long long)vma.start,
                      (unsigned long long)vma.end, (vma.prot & PROT_READ_) ? 'r' : '-', (vma.prot & PROT_WRITE_) ? 'w' : '-',
                      (vma.prot & PROT_EXEC_) ? 'x' : '-', (vma.flags & MAP_SHARED_) ? 's' : 'p',
                      (unsigned long long)vma.offset, vma.file ? "fd:01" : "00:00",
                      vma.file ? (unsigned long long)vma.file->inode() : 0ull);
        text += line;
        if (!vma.name.empty()) {
            text.append(73 - std::min<size_t>(72, std::strlen(line)), ' ');
            text += vma.name;
        }
        text += '\n';
    }
    return text;
}

uint64_t AddressSpace::mapped_bytes() const {
    std::shared_lock<std::shared_mutex> held(lock_);
    uint64_t total = 0;
    for (const auto& [start, vma] : vmas_) total += vma.end - vma.start;
    return total;
}

void AddressSpace::for_each(const std::function<void(const Vma&)>& visit) const {
    std::shared_lock<std::shared_mutex> held(lock_);
    for (const auto& [start, vma] : vmas_) visit(vma);
}

bool guest_read(uint64_t addr, void* out, size_t len) {
    if (!len) return true;
    addr = untag(addr);
    if (addr < 0x10000) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), len);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool guest_write(uint64_t addr, const void* data, size_t len) {
    if (!len) return true;
    addr = untag(addr);
    if (addr < 0x10000) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), data, len);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

static bool guest_strlen(uint64_t addr, size_t max, size_t* out) {
    __try {
        const char* p = reinterpret_cast<const char*>(addr);
        size_t n = 0;
        while (n < max && p[n]) ++n;
        *out = n;
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool guest_string(uint64_t addr, std::string& out, size_t max) {
    addr = untag(addr);
    if (addr < 0x10000) return false;
    size_t n = 0;
    if (!guest_strlen(addr, max, &n)) return false;
    out.assign(reinterpret_cast<const char*>(addr), n);
    return true;
}

}  // namespace axrb
