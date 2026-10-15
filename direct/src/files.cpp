#include "files.h"

#include "cpu.h"
#include "log.h"
#include "memory.h"

#include <bcrypt.h>
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstring>
#include <set>

namespace axrb {
using namespace linux_abi;

// ---------------------------------------------------------------------------
// Helpers

int64_t errno_from_win32(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:
        case ERROR_INVALID_DRIVE: return -ENOENT_;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION: return -EACCES_;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS: return -EEXIST_;
        case ERROR_DIR_NOT_EMPTY: return -ENOTEMPTY_;
        case ERROR_DIRECTORY: return -ENOTDIR_;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return -ENOSPC_;
        case ERROR_INVALID_HANDLE: return -EBADF_;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY: return -ENOMEM_;
        case ERROR_FILENAME_EXCED_RANGE: return -ENAMETOOLONG_;
        case ERROR_NOT_SAME_DEVICE: return -EXDEV_;
        case ERROR_WRITE_PROTECT: return -EROFS_;
        case ERROR_NOACCESS: return -EFAULT_;
        case ERROR_BROKEN_PIPE: return -EPIPE_;
        default: return -EIO_;
    }
}

uint64_t inode_for(const std::string& key) {
    // FNV-1a: stable across opens, distinct enough for directory listings.
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    return (h & 0x7fffffffffffull) | 1;
}

void OpenFile::assign_inode(const std::string& key) { inode_ = inode_for(key); }

static int64_t filetime_to_unix_ns(const FILETIME& ft) {
    uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>((t - 116444736000000000ull) * 100);
}

void fill_stat_from_attributes(kstat& st, const WIN32_FILE_ATTRIBUTE_DATA& data, const std::string& key) {
    std::memset(&st, 0, sizeof st);
    bool dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st.st_dev = 0xfd01;
    st.st_ino = inode_for(key);
    st.st_mode = dir ? (S_IFDIR_ | 0771) : (S_IFREG_ | 0660);
    st.st_nlink = dir ? 2 : 1;
    st.st_uid = process().uid;
    st.st_gid = process().gid;
    st.st_size = dir ? 4096 : static_cast<int64_t>((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    st.st_blksize = 4096;
    st.st_blocks = (st.st_size + 511) / 512;
    int64_t m = filetime_to_unix_ns(data.ftLastWriteTime), a = filetime_to_unix_ns(data.ftLastAccessTime),
            c = filetime_to_unix_ns(data.ftCreationTime);
    st.st_mtime_sec = m / 1000000000;
    st.st_mtime_nsec = static_cast<uint64_t>(m % 1000000000);
    st.st_atime_sec = a / 1000000000;
    st.st_atime_nsec = static_cast<uint64_t>(a % 1000000000);
    st.st_ctime_sec = std::max(m, c) / 1000000000;
    st.st_ctime_nsec = static_cast<uint64_t>(std::max(m, c) % 1000000000);
}

static void fill_simple_stat(kstat& st, uint32_t mode, int64_t size, uint64_t ino) {
    std::memset(&st, 0, sizeof st);
    st.st_dev = 0x3;
    st.st_ino = ino;
    st.st_mode = mode;
    st.st_nlink = (mode & S_IFMT_) == S_IFDIR_ ? 2 : 1;
    st.st_uid = (mode & S_IFMT_) == S_IFCHR_ ? 0 : process().uid;
    st.st_gid = (mode & S_IFMT_) == S_IFCHR_ ? 0 : process().gid;
    st.st_size = size;
    st.st_blksize = 4096;
    st.st_blocks = (size + 511) / 512;
    int64_t now = realtime_ns();
    st.st_mtime_sec = st.st_atime_sec = st.st_ctime_sec = now / 1000000000;
}

static bool guest_buffer_ready(uint64_t buf, uint64_t len, bool write) {
    if (!len) return true;
    if (!address_space().populate(buf, len)) return false;
    return address_space().check(buf, len, write ? PROT_WRITE_ : PROT_READ_) ||
           !address_space().overlaps_guest(buf, len);
}

// ---------------------------------------------------------------------------
// OpenFile defaults

int64_t OpenFile::read(GuestThread&, uint64_t, uint64_t) { return -EINVAL_; }
int64_t OpenFile::write(GuestThread&, uint64_t, uint64_t) { return -EINVAL_; }
int64_t OpenFile::pread(GuestThread&, uint64_t, uint64_t, int64_t) { return -ESPIPE_; }
int64_t OpenFile::pwrite(GuestThread&, uint64_t, uint64_t, int64_t) { return -ESPIPE_; }
int64_t OpenFile::lseek(int64_t, int) { return -ESPIPE_; }
int64_t OpenFile::stat(kstat& st) {
    fill_simple_stat(st, S_IFREG_ | 0600, 0, inode_);
    return 0;
}
int64_t OpenFile::ioctl(GuestThread&, uint64_t, uint64_t) { return -ENOTTY_; }
int64_t OpenFile::getdents64(uint64_t, uint64_t) { return -ENOTDIR_; }
int64_t OpenFile::truncate(int64_t) { return -EINVAL_; }
int64_t OpenFile::fallocate(int, int64_t, int64_t) { return -EOPNOTSUPP_; }
int64_t OpenFile::pread_host(void*, uint64_t, uint64_t) { return -ENODEV_; }
int64_t OpenFile::pwrite_host(const void*, uint64_t, uint64_t) { return -ENODEV_; }

// ---------------------------------------------------------------------------
// HostFile

HostFile::HostFile(HANDLE handle, std::wstring host_path, bool directory, bool read_only)
    : handle_(handle), host_path_(std::move(host_path)), directory_(directory), read_only_(read_only) {}

HostFile::~HostFile() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
}

// Regular files are opened for overlapped I/O: Windows serializes every
// read and write on a synchronous handle, and engines read one package file
// from several threads at once. Each call still waits for its own transfer.
static int64_t transfer(HANDLE handle, bool write, void* buf, uint64_t len, uint64_t offset) {
    struct Event {
        HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Event() { if (handle) CloseHandle(handle); }
    };
    thread_local Event event;
    uint64_t done = 0;
    while (done < len) {
        OVERLAPPED ov{};
        uint64_t at = offset + done;
        ov.Offset = static_cast<DWORD>(at);
        ov.OffsetHigh = static_cast<DWORD>(at >> 32);
        ov.hEvent = event.handle;
        DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(len - done, 64ull << 20)), moved = 0;
        uint8_t* data = static_cast<uint8_t*>(buf) + done;
        BOOL ok = write ? WriteFile(handle, data, chunk, nullptr, &ov) : ReadFile(handle, data, chunk, nullptr, &ov);
        if (ok || GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(handle, &ov, &moved, TRUE);
        if (!ok) {
            DWORD error = GetLastError();
            if (!write && error == ERROR_HANDLE_EOF) break;
            return done ? static_cast<int64_t>(done) : errno_from_win32(error);
        }
        done += moved;
        if (write ? !moved : moved < chunk) break;
    }
    return static_cast<int64_t>(done);
}

int64_t HostFile::pread_host(void* buf, uint64_t len, uint64_t offset) {
    if (directory_) return -EISDIR_;
    return transfer(handle_, false, buf, len, offset);
}

int64_t HostFile::pwrite_host(const void* buf, uint64_t len, uint64_t offset) {
    if (directory_) return -EISDIR_;
    if (read_only_) return -EBADF_;
    return transfer(handle_, true, const_cast<void*>(buf), len, offset);
}

int64_t HostFile::read(GuestThread& t, uint64_t buf, uint64_t len) {
    std::lock_guard<std::mutex> held(lock_);
    int64_t got = pread(t, buf, len, position_);
    if (got > 0) position_ += got;
    return got;
}

int64_t HostFile::write(GuestThread& t, uint64_t buf, uint64_t len) {
    std::lock_guard<std::mutex> held(lock_);
    if (status_flags & O_APPEND_) {
        LARGE_INTEGER size{};
        GetFileSizeEx(handle_, &size);
        position_ = size.QuadPart;
    }
    int64_t wrote = pwrite(t, buf, len, position_);
    if (wrote > 0) position_ += wrote;
    return wrote;
}

int64_t HostFile::pread(GuestThread&, uint64_t buf, uint64_t len, int64_t offset) {
    if (directory_) return -EISDIR_;
    if ((status_flags & O_ACCMODE_) == O_WRONLY_) return -EBADF_;
    if (offset < 0) return -EINVAL_;
    if (!guest_buffer_ready(buf, len, true)) return -EFAULT_;
    return pread_host(reinterpret_cast<void*>(buf), len, static_cast<uint64_t>(offset));
}

int64_t HostFile::pwrite(GuestThread&, uint64_t buf, uint64_t len, int64_t offset) {
    if (directory_) return -EISDIR_;
    if ((status_flags & O_ACCMODE_) == O_RDONLY_) return -EBADF_;
    if (offset < 0) return -EINVAL_;
    if (!guest_buffer_ready(buf, len, false)) return -EFAULT_;
    return pwrite_host(reinterpret_cast<const void*>(buf), len, static_cast<uint64_t>(offset));
}

int64_t HostFile::lseek(int64_t offset, int whence) {
    std::lock_guard<std::mutex> held(lock_);
    int64_t base;
    switch (whence) {
        case 0: base = 0; break;
        case 1: base = position_; break;
        case 2: {
            if (directory_) {
                base = 0;
                break;
            }
            LARGE_INTEGER size{};
            GetFileSizeEx(handle_, &size);
            base = size.QuadPart;
            break;
        }
        case 3:   // SEEK_DATA
        case 4: { // SEEK_HOLE: no sparse files, data everywhere
            LARGE_INTEGER size{};
            GetFileSizeEx(handle_, &size);
            if (offset >= size.QuadPart) return -ENXIO_;
            position_ = whence == 3 ? offset : size.QuadPart;
            return position_;
        }
        default: return -EINVAL_;
    }
    if (base + offset < 0) return -EINVAL_;
    position_ = base + offset;
    if (directory_ && position_ == 0) listed_ = false;
    return position_;
}

int64_t HostFile::stat(kstat& st) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info)) return errno_from_win32(GetLastError());
    WIN32_FILE_ATTRIBUTE_DATA data{};
    data.dwFileAttributes = info.dwFileAttributes;
    data.ftCreationTime = info.ftCreationTime;
    data.ftLastAccessTime = info.ftLastAccessTime;
    data.ftLastWriteTime = info.ftLastWriteTime;
    data.nFileSizeHigh = info.nFileSizeHigh;
    data.nFileSizeLow = info.nFileSizeLow;
    fill_stat_from_attributes(st, data, path_);
    st.st_nlink = info.nNumberOfLinks;
    if (read_only_) {
        st.st_uid = st.st_gid = 0;
        st.st_mode = directory_ ? (S_IFDIR_ | 0755) : (S_IFREG_ | 0644);
    }
    return 0;
}

void HostFile::load_directory() {
    entries_.clear();
    entries_.push_back({".", DT_DIR_, inode_for(path_)});
    entries_.push_back({"..", DT_DIR_, inode_for(Vfs::normalize(path_, ".."))});
    std::set<std::string> seen;
    WIN32_FIND_DATAW found{};
    std::wstring pattern = host_path_ + (host_path_.back() == L'\\' ? L"*" : L"\\*");
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            std::wstring wname = found.cFileName;
            if (wname == L"." || wname == L"..") continue;
            std::string name = unescape_component(wname);
            uint8_t type = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR_ : DT_REG_;
            entries_.push_back({name, type, inode_for(path_ + "/" + name)});
            seen.insert(name);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    for (auto& [name, type] : vfs().virtual_children(path_))
        if (!seen.count(name)) entries_.push_back({name, type, inode_for(path_ + "/" + name)});
    listed_ = true;
}

static int64_t emit_dirents(uint64_t buf, uint64_t len, int64_t& position,
                            const std::function<bool(size_t, std::string&, uint8_t&, uint64_t&)>& entry_at) {
    uint64_t used = 0;
    std::vector<uint8_t> out;
    for (;;) {
        std::string name;
        uint8_t type;
        uint64_t ino;
        if (!entry_at(static_cast<size_t>(position), name, type, ino)) break;
        size_t reclen = (19 + name.size() + 1 + 7) & ~size_t(7);
        if (used + reclen > len) {
            if (!used) return -EINVAL_;
            break;
        }
        size_t at = out.size();
        out.resize(at + reclen, 0);
        int64_t next = position + 1;
        uint16_t rl = static_cast<uint16_t>(reclen);
        std::memcpy(&out[at], &ino, 8);
        std::memcpy(&out[at + 8], &next, 8);
        std::memcpy(&out[at + 16], &rl, 2);
        out[at + 18] = type;
        std::memcpy(&out[at + 19], name.data(), name.size());
        used += reclen;
        ++position;
    }
    if (used && !guest_write(buf, out.data(), used)) return -EFAULT_;
    return static_cast<int64_t>(used);
}

int64_t HostFile::getdents64(uint64_t buf, uint64_t len) {
    if (!directory_) return -ENOTDIR_;
    std::lock_guard<std::mutex> held(lock_);
    if (!listed_ || position_ == 0) load_directory();
    return emit_dirents(buf, len, position_, [&](size_t i, std::string& name, uint8_t& type, uint64_t& ino) {
        if (i >= entries_.size()) return false;
        name = entries_[i].name;
        type = entries_[i].type;
        ino = entries_[i].inode;
        return true;
    });
}

int64_t HostFile::truncate(int64_t length) {
    if (directory_) return -EISDIR_;
    if (read_only_) return -EROFS_;
    FILE_END_OF_FILE_INFO info{};
    info.EndOfFile.QuadPart = length;
    if (!SetFileInformationByHandle(handle_, FileEndOfFileInfo, &info, sizeof info)) return errno_from_win32(GetLastError());
    return 0;
}

int64_t HostFile::sync() {
    if (!directory_ && !read_only_) FlushFileBuffers(handle_);
    return 0;
}

int64_t HostFile::fallocate(int mode, int64_t offset, int64_t length) {
    if (mode != 0 && mode != 1) return -EOPNOTSUPP_;  // only plain and KEEP_SIZE
    if (read_only_) return -EBADF_;
    if (mode == 1) return 0;
    LARGE_INTEGER size{};
    GetFileSizeEx(handle_, &size);
    if (offset + length > size.QuadPart) return truncate(offset + length);
    return 0;
}

// ---------------------------------------------------------------------------
// MemoryFile

MemoryFile::MemoryFile(std::string content, bool directory, std::vector<std::pair<std::string, uint8_t>> children)
    : content_(std::move(content)), directory_(directory), children_(std::move(children)) {}

int64_t MemoryFile::pread_host(void* buf, uint64_t len, uint64_t offset) {
    if (offset >= content_.size()) return 0;
    uint64_t n = std::min<uint64_t>(len, content_.size() - offset);
    std::memcpy(buf, content_.data() + offset, n);
    return static_cast<int64_t>(n);
}

int64_t MemoryFile::pread(GuestThread&, uint64_t buf, uint64_t len, int64_t offset) {
    if (directory_) return -EISDIR_;
    if (offset < 0) return -EINVAL_;
    if (static_cast<uint64_t>(offset) >= content_.size()) return 0;
    uint64_t n = std::min<uint64_t>(len, content_.size() - offset);
    if (!guest_write(buf, content_.data() + offset, n)) return -EFAULT_;
    return static_cast<int64_t>(n);
}

int64_t MemoryFile::read(GuestThread& t, uint64_t buf, uint64_t len) {
    std::lock_guard<std::mutex> held(lock_);
    int64_t got = pread(t, buf, len, position_);
    if (got > 0) position_ += got;
    return got;
}

int64_t MemoryFile::write(GuestThread&, uint64_t, uint64_t len) {
    // /proc/self/... writes (oom_score_adj, timerslack_ns) are accepted and ignored.
    return static_cast<int64_t>(len);
}

int64_t MemoryFile::lseek(int64_t offset, int whence) {
    std::lock_guard<std::mutex> held(lock_);
    int64_t base = whence == 0 ? 0 : whence == 1 ? position_ : whence == 2 ? static_cast<int64_t>(content_.size()) : -1;
    if (base < 0 || base + offset < 0) return -EINVAL_;
    position_ = base + offset;
    return position_;
}

int64_t MemoryFile::stat(kstat& st) {
    if (directory_) fill_simple_stat(st, S_IFDIR_ | 0555, 0, inode_);
    else fill_simple_stat(st, S_IFREG_ | 0444, path_.rfind("/proc/", 0) == 0 ? 0 : static_cast<int64_t>(content_.size()), inode_);
    return 0;
}

int64_t MemoryFile::getdents64(uint64_t buf, uint64_t len) {
    if (!directory_) return -ENOTDIR_;
    std::lock_guard<std::mutex> held(lock_);
    return emit_dirents(buf, len, position_, [&](size_t i, std::string& name, uint8_t& type, uint64_t& ino) {
        if (i == 0) {
            name = ".";
            type = DT_DIR_;
        } else if (i == 1) {
            name = "..";
            type = DT_DIR_;
        } else if (i - 2 < children_.size()) {
            name = children_[i - 2].first;
            type = children_[i - 2].second;
        } else {
            return false;
        }
        ino = inode_for(path_ + "/" + name);
        return true;
    });
}

// ---------------------------------------------------------------------------
// Devices and the console

int64_t DeviceFile::pread_host(void* buf, uint64_t len, uint64_t) {
    std::memset(buf, 0, len);
    return static_cast<int64_t>(len);
}

int64_t DeviceFile::read(GuestThread&, uint64_t buf, uint64_t len) {
    if (kind_ == "null") return 0;
    if (!guest_buffer_ready(buf, len, true)) return -EFAULT_;
    if (kind_ == "zero") {
        std::vector<uint8_t> zeros(static_cast<size_t>(std::min<uint64_t>(len, 1 << 20)));
        uint64_t done = 0;
        while (done < len) {
            uint64_t n = std::min<uint64_t>(zeros.size(), len - done);
            if (!guest_write(buf + done, zeros.data(), n)) return -EFAULT_;
            done += n;
        }
        return static_cast<int64_t>(len);
    }
    if (kind_ == "urandom" || kind_ == "random") {
        std::vector<uint8_t> bytes(static_cast<size_t>(std::min<uint64_t>(len, 1 << 20)));
        BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!guest_write(buf, bytes.data(), bytes.size())) return -EFAULT_;
        return static_cast<int64_t>(bytes.size());
    }
    return 0;
}

int64_t DeviceFile::write(GuestThread&, uint64_t, uint64_t len) { return static_cast<int64_t>(len); }

int64_t DeviceFile::stat(kstat& st) {
    uint64_t rdev = kind_ == "null" ? 0x103 : kind_ == "zero" ? 0x105 : kind_ == "random" ? 0x108 : 0x109;
    fill_simple_stat(st, S_IFCHR_ | 0666, 0, inode_);
    st.st_rdev = rdev;
    return 0;
}

int64_t DeviceFile::ioctl(GuestThread&, uint64_t request, uint64_t) {
    if (request == FIONBIO_ || request == FIOCLEX_ || request == FIONCLEX_) return 0;
    return -ENOTTY_;
}

int64_t ConsoleFile::read(GuestThread&, uint64_t, uint64_t) { return 0; }

int64_t ConsoleFile::write(GuestThread&, uint64_t buf, uint64_t len) {
    std::string text(static_cast<size_t>(std::min<uint64_t>(len, 1 << 20)), '\0');
    if (!guest_read(buf, text.data(), text.size())) return -EFAULT_;
    log_raw(text.data(), text.size());
    return static_cast<int64_t>(len);
}

int64_t ConsoleFile::stat(kstat& st) {
    fill_simple_stat(st, S_IFIFO_ | 0600, 0, 0x100 + which_);
    return 0;
}

int64_t ConsoleFile::ioctl(GuestThread&, uint64_t, uint64_t) { return -ENOTTY_; }

// ---------------------------------------------------------------------------
// Pipes

PipeFile::PipeFile(std::shared_ptr<PipeBuffer> in, std::shared_ptr<PipeBuffer> out, bool socket)
    : in_(std::move(in)), out_(std::move(out)), socket_(socket) {}

PipeFile::~PipeFile() {
    if (out_) {
        std::lock_guard<std::mutex> held(out_->lock);
        out_->writer_closed = true;
        out_->waiters.wake_all();
    }
    if (in_) {
        std::lock_guard<std::mutex> held(in_->lock);
        in_->reader_closed = true;
        in_->waiters.wake_all();
    }
}

WaitQueue* PipeFile::wait_queue() { return in_ ? &in_->waiters : (out_ ? &out_->waiters : nullptr); }

uint32_t PipeFile::poll_mask() {
    uint32_t mask = 0;
    if (in_) {
        std::lock_guard<std::mutex> held(in_->lock);
        if (!in_->bytes.empty()) mask |= POLLIN_ | POLLRDNORM_;
        if (in_->writer_closed) mask |= POLLHUP_ | (socket_ ? POLLIN_ | POLLRDHUP_ : 0);
    }
    if (out_) {
        std::lock_guard<std::mutex> held(out_->lock);
        if (out_->reader_closed) mask |= POLLERR_ | (socket_ ? POLLHUP_ : 0);
        else if (out_->bytes.size() < out_->capacity) mask |= POLLOUT_ | POLLWRNORM_;
    }
    return mask;
}

int64_t PipeFile::read(GuestThread& t, uint64_t buf, uint64_t len) {
    if (!in_) return -EBADF_;
    if (!len) return 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> held(in_->lock);
            if (!in_->bytes.empty()) {
                size_t n = static_cast<size_t>(std::min<uint64_t>(len, in_->bytes.size()));
                std::vector<uint8_t> chunk(in_->bytes.begin(), in_->bytes.begin() + static_cast<ptrdiff_t>(n));
                if (!guest_write(buf, chunk.data(), n)) return -EFAULT_;
                in_->bytes.erase(in_->bytes.begin(), in_->bytes.begin() + static_cast<ptrdiff_t>(n));
                in_->waiters.wake_all();  // writers waiting for space
                return static_cast<int64_t>(n);
            }
            if (in_->writer_closed) return 0;
        }
        if (nonblocking()) return -EAGAIN_;
        in_->waiters.add(&t);
        int64_t r = block_until(t, [&] {
            std::lock_guard<std::mutex> held(in_->lock);
            return !in_->bytes.empty() || in_->writer_closed;
        }, 0, "pipe read");
        in_->waiters.remove(&t);
        if (r < 0) return r;
    }
}

int64_t PipeFile::write(GuestThread& t, uint64_t buf, uint64_t len) {
    if (!out_) return -EBADF_;
    std::vector<uint8_t> data(static_cast<size_t>(len));
    if (len && !guest_read(buf, data.data(), data.size())) return -EFAULT_;
    uint64_t done = 0;
    while (done < len) {
        {
            std::lock_guard<std::mutex> held(out_->lock);
            if (out_->reader_closed) {
                if (done) return static_cast<int64_t>(done);
                ksiginfo info{};
                info.si_signo = SIGPIPE_;
                info.si_code = SI_USER_;
                send_signal(t, SIGPIPE_, info);
                return -EPIPE_;
            }
            size_t space = out_->capacity > out_->bytes.size() ? out_->capacity - out_->bytes.size() : 0;
            if (space) {
                size_t n = static_cast<size_t>(std::min<uint64_t>(space, len - done));
                out_->bytes.insert(out_->bytes.end(), data.begin() + static_cast<ptrdiff_t>(done),
                                   data.begin() + static_cast<ptrdiff_t>(done + n));
                done += n;
                out_->waiters.wake_all();
                continue;
            }
        }
        if (nonblocking()) return done ? static_cast<int64_t>(done) : -EAGAIN_;
        out_->waiters.add(&t);
        int64_t r = block_until(t, [&] {
            std::lock_guard<std::mutex> held(out_->lock);
            return out_->reader_closed || out_->bytes.size() < out_->capacity;
        }, 0, "pipe write");
        out_->waiters.remove(&t);
        if (r < 0) return done ? static_cast<int64_t>(done) : r;
    }
    return static_cast<int64_t>(done);
}

void PipeFile::shutdown(int how) {
    if ((how == SHUT_WR_ || how == SHUT_RDWR_) && out_) {
        std::lock_guard<std::mutex> held(out_->lock);
        out_->writer_closed = true;
        out_->waiters.wake_all();
    }
    if ((how == SHUT_RD_ || how == SHUT_RDWR_) && in_) {
        std::lock_guard<std::mutex> held(in_->lock);
        in_->reader_closed = true;
        in_->waiters.wake_all();
    }
}

int64_t PipeFile::stat(kstat& st) {
    fill_simple_stat(st, (socket_ ? S_IFSOCK_ : S_IFIFO_) | 0600, 0, inode_ ? inode_ : reinterpret_cast<uint64_t>(this) >> 4);
    return 0;
}

int64_t PipeFile::ioctl(GuestThread&, uint64_t request, uint64_t arg) {
    if (request == FIONREAD_ && in_) {
        std::lock_guard<std::mutex> held(in_->lock);
        int32_t n = static_cast<int32_t>(in_->bytes.size());
        return guest_write(arg, &n, 4) ? 0 : -EFAULT_;
    }
    if (request == FIONBIO_) {
        int32_t on = 0;
        guest_read(arg, &on, 4);
        if (on) status_flags |= O_NONBLOCK_;
        else status_flags &= ~O_NONBLOCK_;
        return 0;
    }
    return -ENOTTY_;
}

// ---------------------------------------------------------------------------
// eventfd

uint32_t EventFile::poll_mask() {
    std::lock_guard<std::mutex> held(lock_);
    uint32_t mask = POLLOUT_ | POLLWRNORM_;
    if (counter_) mask |= POLLIN_ | POLLRDNORM_;
    return mask;
}

int64_t EventFile::read(GuestThread& t, uint64_t buf, uint64_t len) {
    if (len < 8) return -EINVAL_;
    for (;;) {
        {
            std::lock_guard<std::mutex> held(lock_);
            if (counter_) {
                uint64_t value = semaphore_ ? 1 : counter_;
                counter_ -= value;
                if (!guest_write(buf, &value, 8)) return -EFAULT_;
                waiters_.wake_all();
                return 8;
            }
        }
        if (nonblocking()) return -EAGAIN_;
        waiters_.add(&t);
        int64_t r = block_until(t, [&] {
            std::lock_guard<std::mutex> held(lock_);
            return counter_ != 0;
        }, 0, "eventfd read");
        waiters_.remove(&t);
        if (r < 0) return r;
    }
}

int64_t EventFile::write(GuestThread&, uint64_t buf, uint64_t len) {
    if (len < 8) return -EINVAL_;
    uint64_t value = 0;
    if (!guest_read(buf, &value, 8)) return -EFAULT_;
    if (value == ~0ull) return -EINVAL_;
    std::lock_guard<std::mutex> held(lock_);
    if (counter_ + value < counter_ || counter_ + value == ~0ull) return -EAGAIN_;
    counter_ += value;
    waiters_.wake_all();
    return 8;
}

int64_t EventFile::stat(kstat& st) {
    fill_simple_stat(st, 0600, 0, reinterpret_cast<uint64_t>(this) >> 4);
    return 0;
}

// ---------------------------------------------------------------------------
// timerfd

uint64_t TimerFile::expirations_locked() {
    if (!next_ns_) return 0;
    int64_t now = clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns();
    if (now < next_ns_) return 0;
    if (!interval_ns_) {
        next_ns_ = 0;
        return 1;
    }
    uint64_t count = static_cast<uint64_t>((now - next_ns_) / interval_ns_) + 1;
    next_ns_ += static_cast<int64_t>(count) * interval_ns_;
    return count;
}

uint32_t TimerFile::poll_mask() {
    std::lock_guard<std::mutex> held(lock_);
    if (!next_ns_) return 0;
    int64_t now = clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns();
    return now >= next_ns_ ? (POLLIN_ | POLLRDNORM_) : 0;
}

int64_t TimerFile::read(GuestThread& t, uint64_t buf, uint64_t len) {
    if (len < 8) return -EINVAL_;
    for (;;) {
        int64_t wait_ns = -1;
        {
            std::lock_guard<std::mutex> held(lock_);
            uint64_t count = expirations_locked();
            if (count) return guest_write(buf, &count, 8) ? 8 : -EFAULT_;
            if (next_ns_) wait_ns = next_ns_ - (clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns());
        }
        if (nonblocking()) return -EAGAIN_;
        int64_t deadline = wait_ns < 0 ? 0 : ticks_now() + std::max<int64_t>(1, wait_ns * ticks_per_second() / 1000000000);
        int64_t r = block_until(t, [&] { return poll_mask() != 0; }, deadline, "timerfd read");
        if (r == -EINTR_) return r;
    }
}

int64_t TimerFile::settime(int flags, const kitimerspec& value, kitimerspec* old) {
    std::lock_guard<std::mutex> held(lock_);
    if (old) {
        int64_t now = clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns();
        int64_t left = next_ns_ ? std::max<int64_t>(0, next_ns_ - now) : 0;
        old->it_value = {left / 1000000000, left % 1000000000};
        old->it_interval = {interval_ns_ / 1000000000, interval_ns_ % 1000000000};
    }
    int64_t v = value.it_value.tv_sec * 1000000000 + value.it_value.tv_nsec;
    interval_ns_ = value.it_interval.tv_sec * 1000000000 + value.it_interval.tv_nsec;
    if (!v) next_ns_ = 0;
    else if (flags & TIMER_ABSTIME_) next_ns_ = v;
    else next_ns_ = (clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns()) + v;
    waiters_.wake_all();
    return 0;
}

int64_t TimerFile::gettime(kitimerspec& out) {
    std::lock_guard<std::mutex> held(lock_);
    int64_t now = clock_ == CLOCK_REALTIME_ ? realtime_ns() : monotonic_ns();
    int64_t left = next_ns_ ? std::max<int64_t>(0, next_ns_ - now) : 0;
    out.it_value = {left / 1000000000, left % 1000000000};
    out.it_interval = {interval_ns_ / 1000000000, interval_ns_ % 1000000000};
    return 0;
}

int64_t TimerFile::stat(kstat& st) {
    fill_simple_stat(st, 0600, 0, reinterpret_cast<uint64_t>(this) >> 4);
    return 0;
}

// ---------------------------------------------------------------------------
// epoll

int64_t EpollFile::ctl(int op, int fd, const kepoll_event* event) {
    auto file = fds().get(fd);
    if (!file) return -EBADF_;
    if (file.get() == this) return -EINVAL_;
    std::lock_guard<std::mutex> held(lock_);
    auto it = interests_.find(fd);
    switch (op) {
        case EPOLL_CTL_ADD_:
            if (it != interests_.end() && !it->second.file.expired()) return -EEXIST_;
            interests_[fd] = {file, event->events, event->data, false};
            return 0;
        case EPOLL_CTL_MOD_:
            if (it == interests_.end()) return -ENOENT_;
            it->second.events = event->events;
            it->second.data = event->data;
            it->second.disabled = false;
            return 0;
        case EPOLL_CTL_DEL_:
            if (it == interests_.end()) return -ENOENT_;
            interests_.erase(it);
            return 0;
        default: return -EINVAL_;
    }
}

uint32_t EpollFile::poll_mask() {
    std::lock_guard<std::mutex> held(lock_);
    for (auto& [fd, interest] : interests_) {
        auto file = interest.file.lock();
        if (file && !interest.disabled && (file->poll_mask() & (interest.events | POLLERR_ | POLLHUP_))) return POLLIN_;
    }
    return 0;
}

int64_t EpollFile::wait(GuestThread& t, uint64_t events, int max, int64_t deadline) {
    if (max <= 0) return -EINVAL_;
    std::vector<kepoll_event> ready;
    auto collect = [&]() {
        ready.clear();
        std::lock_guard<std::mutex> held(lock_);
        for (auto it = interests_.begin(); it != interests_.end();) {
            auto file = it->second.file.lock();
            if (!file) {
                it = interests_.erase(it);
                continue;
            }
            Interest& in = it->second;
            uint32_t mask = file->poll_mask() & (in.events | EPOLLERR_ | EPOLLHUP_);
            if (mask && !in.disabled && static_cast<int>(ready.size()) < max) {
                ready.push_back({mask, 0, in.data});
                if (in.events & EPOLLONESHOT_) in.disabled = true;
            }
            ++it;
        }
        return !ready.empty();
    };
    // Register with everything watched so readiness changes wake us.
    std::vector<std::shared_ptr<OpenFile>> watched;
    std::vector<HANDLE> handles;
    {
        std::lock_guard<std::mutex> held(lock_);
        for (auto& [fd, in] : interests_)
            if (auto file = in.file.lock()) watched.push_back(file);
    }
    for (auto& file : watched) {
        if (auto* q = file->wait_queue()) q->add(&t);
        if (HANDLE h = file->readiness_event()) handles.push_back(h);
    }
    int64_t r = 0;
    if (!collect()) {
        if (deadline == -1) r = -ETIMEDOUT_;
        else r = block_until(t, collect, deadline, "epoll_wait");
    }
    for (auto& file : watched)
        if (auto* q = file->wait_queue()) q->remove(&t);
    (void)handles;
    if (r == -EINTR_) return r;
    if (ready.empty()) return 0;
    if (!guest_write(events, ready.data(), ready.size() * sizeof(kepoll_event))) return -EFAULT_;
    return static_cast<int64_t>(ready.size());
}

int64_t EpollFile::stat(kstat& st) {
    fill_simple_stat(st, 0600, 0, reinterpret_cast<uint64_t>(this) >> 4);
    return 0;
}

// ---------------------------------------------------------------------------
// Descriptor table

FdTable& fds() {
    static FdTable table;
    return table;
}

int FdTable::install(std::shared_ptr<OpenFile> file, bool cloexec, int minimum) {
    std::lock_guard<std::mutex> held(lock_);
    for (size_t fd = static_cast<size_t>(std::max(0, minimum));; ++fd) {
        if (fd >= 65536) return static_cast<int>(-EMFILE_);
        if (fd >= files_.size()) {
            files_.resize(fd + 1);
            cloexec_.resize(fd + 1);
        }
        if (!files_[fd]) {
            files_[fd] = std::move(file);
            cloexec_[fd] = cloexec;
            return static_cast<int>(fd);
        }
    }
}

int FdTable::install_at(int fd, std::shared_ptr<OpenFile> file, bool cloexec) {
    if (fd < 0 || fd >= 65536) return static_cast<int>(-EBADF_);
    std::shared_ptr<OpenFile> old;
    {
        std::lock_guard<std::mutex> held(lock_);
        if (static_cast<size_t>(fd) >= files_.size()) {
            files_.resize(static_cast<size_t>(fd) + 1);
            cloexec_.resize(static_cast<size_t>(fd) + 1);
        }
        old = std::move(files_[fd]);
        files_[fd] = std::move(file);
        cloexec_[fd] = cloexec;
    }
    if (old) old->on_close();
    return fd;  // `old` closes outside the lock
}

std::shared_ptr<OpenFile> FdTable::get(int fd) {
    std::lock_guard<std::mutex> held(lock_);
    if (fd < 0 || static_cast<size_t>(fd) >= files_.size()) return nullptr;
    return files_[fd];
}

int FdTable::close(int fd) {
    std::shared_ptr<OpenFile> old;
    {
        std::lock_guard<std::mutex> held(lock_);
        if (fd < 0 || static_cast<size_t>(fd) >= files_.size() || !files_[fd]) return static_cast<int>(-EBADF_);
        old = std::move(files_[fd]);
        files_[fd].reset();
        cloexec_[fd] = false;
    }
    old->on_close();
    return 0;
}

bool FdTable::cloexec(int fd) {
    std::lock_guard<std::mutex> held(lock_);
    return fd >= 0 && static_cast<size_t>(fd) < cloexec_.size() && cloexec_[fd];
}

void FdTable::set_cloexec(int fd, bool value) {
    std::lock_guard<std::mutex> held(lock_);
    if (fd >= 0 && static_cast<size_t>(fd) < cloexec_.size()) cloexec_[fd] = value;
}

std::vector<int> FdTable::list() {
    std::lock_guard<std::mutex> held(lock_);
    std::vector<int> out;
    for (size_t i = 0; i < files_.size(); ++i)
        if (files_[i]) out.push_back(static_cast<int>(i));
    return out;
}

// ---------------------------------------------------------------------------
// Paths

int64_t path_at(int dirfd, uint64_t path_addr, std::string& out, bool allow_empty) {
    std::string path;
    if (!guest_string(path_addr, path, 4096)) return -EFAULT_;
    if (path.empty() && !allow_empty) return -ENOENT_;
    if (!path.empty() && path[0] == '/') {
        out = Vfs::normalize("/", path);
        return 0;
    }
    std::string base;
    if (dirfd == AT_FDCWD_) {
        base = process().cwd;
    } else {
        auto dir = fds().get(dirfd);
        if (!dir) return -EBADF_;
        base = dir->guest_path();
        if (base.empty()) return -ENOTDIR_;
    }
    out = path.empty() ? base : Vfs::normalize(base, path);
    return 0;
}

namespace {
std::mutex g_devices_lock;
std::map<std::string, std::function<std::shared_ptr<OpenFile>()>> g_devices;
}  // namespace

void register_device(const std::string& name, std::function<std::shared_ptr<OpenFile>()> make) {
    std::lock_guard<std::mutex> held(g_devices_lock);
    g_devices[name] = std::move(make);
}

std::shared_ptr<OpenFile> make_device(const std::string& name) {
    {
        std::lock_guard<std::mutex> held(g_devices_lock);
        if (auto it = g_devices.find(name); it != g_devices.end()) return it->second();
    }
    return std::make_shared<DeviceFile>(name);
}

int64_t stat_path(const std::string& path, bool follow, kstat& st) {
    Resolved r;
    std::string link;
    if (!follow && vfs().readlink(path, link)) {
        fill_simple_stat(st, S_IFLNK_ | 0777, static_cast<int64_t>(link.size()), inode_for(path));
        return 0;
    }
    int e = vfs().resolve(path, follow, r);
    if (e) return e;
    switch (r.kind) {
        case Resolved::Host: {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(r.host.c_str(), GetFileExInfoStandard, &data)) return errno_from_win32(GetLastError());
            fill_stat_from_attributes(st, data, r.guest);
            if (r.read_only) {
                // Read-only mounts are the system image: root-owned, as on a device.
                st.st_uid = st.st_gid = 0;
                st.st_mode = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? (S_IFDIR_ | 0755) : (S_IFREG_ | 0644);
            }
            return 0;
        }
        case Resolved::Synthetic: {
            bool proc = r.guest.rfind("/proc/", 0) == 0;
            fill_simple_stat(st, S_IFREG_ | 0444, proc ? 0 : static_cast<int64_t>(r.reader().size()), inode_for(r.guest));
            return 0;
        }
        case Resolved::SyntheticDir:
            fill_simple_stat(st, S_IFDIR_ | 0555, 0, inode_for(r.guest));
            return 0;
        case Resolved::Device:
            return make_device(r.device)->stat(st);
        default: return -ENOENT_;
    }
}

int64_t open_path(GuestThread&, const std::string& path, uint32_t flags, uint32_t mode, std::shared_ptr<OpenFile>& out) {
    (void)mode;
    Resolved r;
    std::string link;
    if ((flags & O_NOFOLLOW_) && vfs().readlink(path, link) && !(flags & O_PATH_)) return -ELOOP_;
    int e = vfs().resolve(path, !(flags & O_NOFOLLOW_), r);
    uint32_t access = flags & O_ACCMODE_;
    bool wants_write = access != O_RDONLY_ || (flags & (O_CREAT_ | O_TRUNC_));
    if (e) {
        // Creating a file under a missing parent is ENOENT too.
        return e;
    }
    switch (r.kind) {
        case Resolved::Device:
            out = make_device(r.device);
            break;
        case Resolved::Synthetic:
            if (access != O_RDONLY_ && r.guest.rfind("/proc/", 0) != 0) return -EACCES_;
            out = std::make_shared<MemoryFile>(r.reader());
            break;
        case Resolved::SyntheticDir: {
            if (wants_write) return -EISDIR_;
            auto children = vfs().virtual_children(r.guest);
            for (auto& name : r.children)
                if (std::none_of(children.begin(), children.end(), [&](auto& c) { return c.first == name; }))
                    children.push_back({name, DT_UNKNOWN_});
            out = std::make_shared<MemoryFile>("", true, std::move(children));
            break;
        }
        case Resolved::Host: {
            DWORD attributes = GetFileAttributesW(r.host.c_str());
            bool exists = attributes != INVALID_FILE_ATTRIBUTES;
            bool directory = exists && (attributes & FILE_ATTRIBUTE_DIRECTORY);
            if ((flags & O_DIRECTORY_) && exists && !directory) return -ENOTDIR_;
            if (directory && access != O_RDONLY_) return -EISDIR_;
            if (r.read_only && (wants_write || (!exists && (flags & O_CREAT_)))) return exists ? -EROFS_ : (flags & O_CREAT_) ? -EROFS_ : -ENOENT_;
            if (!exists && !(flags & O_CREAT_)) return -ENOENT_;
            if (exists && (flags & O_CREAT_) && (flags & O_EXCL_)) return -EEXIST_;
            DWORD desired = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
            if (access == O_RDONLY_ || access == O_RDWR_) desired |= GENERIC_READ;
            if (access == O_WRONLY_ || access == O_RDWR_) desired |= GENERIC_WRITE;
            if (flags & O_PATH_) desired = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
            DWORD disposition = OPEN_EXISTING;
            if (flags & O_CREAT_) disposition = (flags & O_EXCL_) ? CREATE_NEW : (flags & O_TRUNC_) ? OPEN_ALWAYS : OPEN_ALWAYS;
            else if ((flags & O_TRUNC_) && access != O_RDONLY_) disposition = TRUNCATE_EXISTING;
            DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
            DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_OVERLAPPED);
            HANDLE h = CreateFileW(r.host.c_str(), desired, share, nullptr, disposition, flags_and_attributes, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                DWORD error = GetLastError();
                if (error == ERROR_PATH_NOT_FOUND) return -ENOENT_;
                return errno_from_win32(error);
            }
            if ((flags & O_CREAT_) && (flags & O_TRUNC_) && access != O_RDONLY_ && exists) {
                FILE_END_OF_FILE_INFO eof{};
                SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof eof);
            }
            auto file = std::make_shared<HostFile>(h, r.host, directory, r.read_only);
            out = file;
            break;
        }
        default: return -ENOENT_;
    }
    out->set_guest_path(r.guest);
    out->status_flags = flags & (O_ACCMODE_ | O_APPEND_ | O_NONBLOCK_ | O_DIRECT_ | O_DSYNC_);
    return 0;
}

// ---------------------------------------------------------------------------
// System calls

int64_t OpenFile::writev(GuestThread& t, const std::vector<kiovec>& vecs) {
    int64_t total = 0;
    for (auto& v : vecs) {
        if (!v.len) continue;
        int64_t n = write(t, v.base, v.len);
        if (n < 0) return total ? total : n;
        total += n;
        if (static_cast<uint64_t>(n) < v.len) break;
    }
    return total;
}

static int64_t do_readv(GuestThread& t, OpenFile& file, uint64_t iov, int64_t count, int64_t offset, bool write) {
    if (count < 0 || count > 1024) return -EINVAL_;
    std::vector<kiovec> vecs(static_cast<size_t>(count));
    if (count && !guest_read(iov, vecs.data(), vecs.size() * sizeof(kiovec))) return -EFAULT_;
    if (write && offset < 0) return file.writev(t, vecs);
    int64_t total = 0;
    for (auto& v : vecs) {
        if (!v.len) continue;
        int64_t n;
        if (offset >= 0) n = write ? file.pwrite(t, v.base, v.len, offset + total) : file.pread(t, v.base, v.len, offset + total);
        else n = write ? file.write(t, v.base, v.len) : file.read(t, v.base, v.len);
        if (n < 0) return total ? total : n;
        total += n;
        if (static_cast<uint64_t>(n) < v.len) break;
    }
    return total;
}

static int64_t remove_path(const std::string& path, bool directory) {
    Resolved r;
    int e = vfs().resolve(path, false, r);
    if (e) return e;
    if (r.kind != Resolved::Host) return -EACCES_;
    if (r.read_only) return -EROFS_;
    DWORD attributes = GetFileAttributesW(r.host.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return -ENOENT_;
    bool is_dir = attributes & FILE_ATTRIBUTE_DIRECTORY;
    if (directory && !is_dir) return -ENOTDIR_;
    if (!directory && is_dir) return -EISDIR_;
    if (is_dir) {
        if (!RemoveDirectoryW(r.host.c_str())) return errno_from_win32(GetLastError());
        return 0;
    }
    // POSIX semantics: the name goes away now, open handles keep working.
    HANDLE h = CreateFileW(r.host.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return errno_from_win32(GetLastError());
    FILE_DISPOSITION_INFO_EX info{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                  FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    BOOL ok = SetFileInformationByHandle(h, FileDispositionInfoEx, &info, sizeof info);
    if (!ok) {
        FILE_DISPOSITION_INFO legacy{TRUE};
        ok = SetFileInformationByHandle(h, FileDispositionInfo, &legacy, sizeof legacy);
    }
    DWORD error = GetLastError();
    CloseHandle(h);
    return ok ? 0 : errno_from_win32(error);
}

static int64_t rename_path(const std::string& from, const std::string& to, uint32_t flags) {
    Resolved a, b;
    int e = vfs().resolve(from, false, a);
    if (e) return e;
    e = vfs().resolve(to, false, b);
    if (e) return e;
    if (a.kind != Resolved::Host || b.kind != Resolved::Host) return -EXDEV_;
    if (a.read_only || b.read_only) return -EROFS_;
    if (a.mount != b.mount) return -EXDEV_;
    const uint32_t RENAME_NOREPLACE = 1;
    DWORD attributes = GetFileAttributesW(a.host.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return -ENOENT_;
    DWORD target_attributes = GetFileAttributesW(b.host.c_str());
    if (target_attributes != INVALID_FILE_ATTRIBUTES && (flags & RENAME_NOREPLACE)) return -EEXIST_;
    if (target_attributes != INVALID_FILE_ATTRIBUTES && (target_attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        RemoveDirectoryW(b.host.c_str());  // only succeeds when empty, as Linux requires
    }
    HANDLE h = CreateFileW(a.host.c_str(), DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return errno_from_win32(GetLastError());
    std::vector<uint8_t> buffer(sizeof(FILE_RENAME_INFO) + b.host.size() * sizeof(wchar_t) + 16);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->Flags = FILE_RENAME_FLAG_POSIX_SEMANTICS | ((flags & RENAME_NOREPLACE) ? 0 : FILE_RENAME_FLAG_REPLACE_IF_EXISTS);
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(b.host.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, b.host.data(), info->FileNameLength);
    BOOL ok = SetFileInformationByHandle(h, FileRenameInfoEx, info, static_cast<DWORD>(buffer.size()));
    DWORD error = GetLastError();
    CloseHandle(h);
    if (!ok) {
        if (MoveFileExW(a.host.c_str(), b.host.c_str(), (flags & RENAME_NOREPLACE) ? 0 : MOVEFILE_REPLACE_EXISTING)) return 0;
        return errno_from_win32(error);
    }
    return 0;
}

static int64_t poll_files(GuestThread& t, uint64_t fds_addr, uint64_t count, int64_t deadline) {
    if (count > 4096) return -EINVAL_;
    std::vector<kpollfd> polls(static_cast<size_t>(count));
    if (count && !guest_read(fds_addr, polls.data(), polls.size() * sizeof(kpollfd))) return -EFAULT_;
    std::vector<std::shared_ptr<OpenFile>> files(polls.size());
    for (size_t i = 0; i < polls.size(); ++i)
        if (polls[i].fd >= 0) files[i] = fds().get(polls[i].fd);
    int ready_count = 0;
    auto check = [&]() {
        ready_count = 0;
        for (size_t i = 0; i < polls.size(); ++i) {
            polls[i].revents = 0;
            if (polls[i].fd < 0) continue;
            if (!files[i]) {
                polls[i].revents = POLLNVAL_;
                ++ready_count;
                continue;
            }
            uint32_t mask = files[i]->poll_mask();
            polls[i].revents = static_cast<int16_t>(mask & (static_cast<uint16_t>(polls[i].events) | POLLERR_ | POLLHUP_ | POLLNVAL_));
            if (polls[i].revents) ++ready_count;
        }
        return ready_count > 0;
    };
    for (auto& f : files)
        if (f)
            if (auto* q = f->wait_queue()) q->add(&t);
    int64_t r = 0;
    if (!check() && deadline != -1) r = block_until(t, check, deadline, "poll");
    for (auto& f : files)
        if (f)
            if (auto* q = f->wait_queue()) q->remove(&t);
    if (r == -EINTR_) return r;
    if (count && !guest_write(fds_addr, polls.data(), polls.size() * sizeof(kpollfd))) return -EFAULT_;
    return ready_count;
}

int64_t sys_files(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    auto fd_arg = [&](int i) { return static_cast<int>(static_cast<int32_t>(a[i])); };
    switch (nr) {
        case 17: {  // getcwd
            std::string cwd = process().cwd;
            if (cwd.size() + 1 > a[1]) return -ERANGE_;
            if (!guest_write(a[0], cwd.c_str(), cwd.size() + 1)) return -EFAULT_;
            return static_cast<int64_t>(cwd.size() + 1);
        }
        case 23: {  // dup
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return fds().install(file, false);
        }
        case 24: {  // dup3
            int oldfd = fd_arg(0), newfd = fd_arg(1);
            if (oldfd == newfd) return -EINVAL_;
            auto file = fds().get(oldfd);
            if (!file) return -EBADF_;
            return fds().install_at(newfd, file, (a[2] & O_CLOEXEC_) != 0);
        }
        case 25: {  // fcntl
            int fd = fd_arg(0);
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            switch (static_cast<int>(a[1])) {
                case F_DUPFD_: return fds().install(file, false, static_cast<int>(a[2]));
                case F_DUPFD_CLOEXEC_: return fds().install(file, true, static_cast<int>(a[2]));
                case F_GETFD_: return fds().cloexec(fd) ? FD_CLOEXEC_ : 0;
                case F_SETFD_: fds().set_cloexec(fd, (a[2] & FD_CLOEXEC_) != 0); return 0;
                case F_GETFL_: return file->status_flags | O_LARGEFILE_;
                case F_SETFL_:
                    file->status_flags = (file->status_flags & ~(O_APPEND_ | O_NONBLOCK_)) |
                                         (static_cast<uint32_t>(a[2]) & (O_APPEND_ | O_NONBLOCK_));
                    return 0;
                case F_GETLK_:
                case F_OFD_GETLK_: {
                    kflock lock{};
                    if (!guest_read(a[2], &lock, sizeof lock)) return -EFAULT_;
                    lock.l_type = 2;  // F_UNLCK: nobody else holds locks in this process model
                    return guest_write(a[2], &lock, sizeof lock) ? 0 : -EFAULT_;
                }
                case F_SETLK_:
                case F_SETLKW_:
                case F_OFD_SETLK_:
                case F_OFD_SETLKW_: return 0;
                case F_GETPIPE_SZ_: return 65536;
                case F_SETPIPE_SZ_: return static_cast<int64_t>(a[2]);
                case F_ADD_SEALS_: return 0;
                case F_GET_SEALS_: return 0;
                case F_SETOWN_: case F_GETOWN_: case F_SETSIG_: case F_GETSIG_: return 0;
                default:
                    LOGW("files", "fcntl cmd %llu unsupported", (unsigned long long)a[1]);
                    return -EINVAL_;
            }
        }
        case 29: {  // ioctl
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            if (a[1] == FIOCLEX_) { fds().set_cloexec(fd_arg(0), true); return 0; }
            if (a[1] == FIONCLEX_) { fds().set_cloexec(fd_arg(0), false); return 0; }
            return file->ioctl(t, a[1], a[2]);
        }
        case 32: return 0;  // flock
        case 33: return -EPERM_;  // mknodat
        case 34: {  // mkdirat
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path)) return e;
            Resolved r;
            if (int e = vfs().resolve(path, true, r)) return e;
            if (r.kind != Resolved::Host) return -EEXIST_;
            if (r.read_only) return GetFileAttributesW(r.host.c_str()) != INVALID_FILE_ATTRIBUTES ? -EEXIST_ : -EROFS_;
            if (!CreateDirectoryW(r.host.c_str(), nullptr)) return errno_from_win32(GetLastError());
            return 0;
        }
        case 35: {  // unlinkat
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path)) return e;
            return remove_path(path, (a[2] & AT_REMOVEDIR_) != 0);
        }
        case 36:  // symlinkat
        case 37:  // linkat
            return -EPERM_;
        case 38:    // renameat
        case 276: { // renameat2
            std::string from, to;
            if (int64_t e = path_at(fd_arg(0), a[1], from)) return e;
            if (int64_t e = path_at(fd_arg(2), a[3], to)) return e;
            return rename_path(from, to, nr == 276 ? static_cast<uint32_t>(a[4]) : 0);
        }
        case 43:    // statfs
        case 44: {  // fstatfs
            kstatfs st{};
            st.f_type = 0xef53;  // ext4
            st.f_bsize = 4096;
            st.f_frsize = 4096;
            ULARGE_INTEGER avail{}, total{}, free_bytes{};
            GetDiskFreeSpaceExW(nullptr, &avail, &total, &free_bytes);
            st.f_blocks = total.QuadPart / 4096;
            st.f_bfree = free_bytes.QuadPart / 4096;
            st.f_bavail = avail.QuadPart / 4096;
            st.f_files = 1 << 22;
            st.f_ffree = 1 << 21;
            st.f_namelen = 255;
            return guest_write(a[1], &st, sizeof st) ? 0 : -EFAULT_;
        }
        case 45: {  // truncate
            std::string path;
            if (int64_t e = path_at(AT_FDCWD_, a[0], path)) return e;
            std::shared_ptr<OpenFile> file;
            if (int64_t e = open_path(t, path, O_WRONLY_, 0, file)) return e;
            return file->truncate(static_cast<int64_t>(a[1]));
        }
        case 46: {  // ftruncate
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->truncate(static_cast<int64_t>(a[1]));
        }
        case 47: {  // fallocate
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->fallocate(static_cast<int>(a[1]), static_cast<int64_t>(a[2]), static_cast<int64_t>(a[3]));
        }
        case 48:     // faccessat
        case 439: {  // faccessat2
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path, nr == 439 && (a[3] & AT_EMPTY_PATH_))) return e;
            kstat st;
            if (int64_t e = stat_path(path, !(nr == 439 && (a[3] & AT_SYMLINK_NOFOLLOW_)), st)) return e;
            uint32_t mode = static_cast<uint32_t>(a[2]);
            if (mode & ~7u) return -EINVAL_;
            if (mode & 2) {
                Resolved resolved;
                if (int64_t e = vfs().resolve(path, !(nr == 439 && (a[3] & AT_SYMLINK_NOFOLLOW_)), resolved)) return e;
                if (resolved.read_only) return -EROFS_;
            }
            uint32_t allowed = (st.st_mode >> (st.st_uid == process().uid ? 6 : st.st_gid == process().gid ? 3 : 0)) & 7;
            if (process().uid == 0) allowed = 6 | ((st.st_mode & 0111) ? 1 : 0);
            if (mode & ~allowed) return -EACCES_;
            return 0;
        }
        case 49: {  // chdir
            std::string path;
            if (int64_t e = path_at(AT_FDCWD_, a[0], path)) return e;
            kstat st;
            if (int64_t e = stat_path(path, true, st)) return e;
            if ((st.st_mode & S_IFMT_) != S_IFDIR_) return -ENOTDIR_;
            process().cwd = path;
            return 0;
        }
        case 50: {  // fchdir
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            process().cwd = file->guest_path();
            return 0;
        }
        case 52: case 53: case 54: case 55: return 0;  // fchmod, fchmodat, fchownat, fchown
        case 56: {  // openat
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path)) return e;
            std::shared_ptr<OpenFile> file;
            uint32_t flags = static_cast<uint32_t>(a[2]);
            int64_t e = open_path(t, path, flags, static_cast<uint32_t>(a[3]), file);
            TRACE("files", "openat %s flags %o -> %lld", path.c_str(), flags, (long long)e);
            if (e) return e;
            return fds().install(file, (flags & O_CLOEXEC_) != 0);
        }
        case 57: return fds().close(fd_arg(0));  // close
        case 59: {  // pipe2
            auto buffer = std::make_shared<PipeBuffer>();
            auto reader = std::make_shared<PipeFile>(buffer, nullptr, false);
            auto writer = std::make_shared<PipeFile>(nullptr, buffer, false);
            bool cloexec = (a[1] & O_CLOEXEC_) != 0;
            if (a[1] & O_NONBLOCK_) {
                reader->status_flags |= O_NONBLOCK_;
                writer->status_flags |= O_NONBLOCK_;
            }
            writer->status_flags |= O_WRONLY_;
            int pair[2] = {fds().install(reader, cloexec), fds().install(writer, cloexec)};
            if (!guest_write(a[0], pair, sizeof pair)) {
                fds().close(pair[0]);
                fds().close(pair[1]);
                return -EFAULT_;
            }
            return 0;
        }
        case 61: {  // getdents64
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->getdents64(a[1], a[2]);
        }
        case 62: {  // lseek
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->lseek(static_cast<int64_t>(a[1]), static_cast<int>(a[2]));
        }
        case 63: {  // read
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->read(t, a[1], a[2]);
        }
        case 64: {  // write
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->write(t, a[1], a[2]);
        }
        case 65: case 66: {  // readv, writev
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return do_readv(t, *file, a[1], static_cast<int64_t>(a[2]), -1, nr == 66);
        }
        case 67: {  // pread64
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->pread(t, a[1], a[2], static_cast<int64_t>(a[3]));
        }
        case 68: {  // pwrite64
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->pwrite(t, a[1], a[2], static_cast<int64_t>(a[3]));
        }
        case 69: case 70: {  // preadv, pwritev
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return do_readv(t, *file, a[1], static_cast<int64_t>(a[2]), static_cast<int64_t>(a[3]), nr == 70);
        }
        case 71: {  // sendfile
            auto out = fds().get(fd_arg(0));
            auto in = fds().get(fd_arg(1));
            if (!out || !in) return -EBADF_;
            int64_t offset = -1;
            if (a[2] && !guest_read(a[2], &offset, 8)) return -EFAULT_;
            uint64_t count = std::min<uint64_t>(a[3], 1 << 24);
            std::vector<uint8_t> data(static_cast<size_t>(count));
            int64_t got = offset >= 0 ? in->pread_host(data.data(), count, static_cast<uint64_t>(offset)) : -1;
            if (offset < 0) {
                // Through a scratch guest buffer would be needed for stream reads; files are the common case.
                kstat st;
                in->stat(st);
                int64_t position = in->lseek(0, 1);
                got = in->pread_host(data.data(), count, static_cast<uint64_t>(position));
                if (got > 0) in->lseek(position + got, 0);
            }
            if (got <= 0) return got;
            // Write via a host-side path when possible.
            int64_t written = out->pwrite_host(data.data(), static_cast<uint64_t>(got), static_cast<uint64_t>(out->lseek(0, 1)));
            if (written > 0) out->lseek(written, 1);
            if (offset >= 0 && a[2]) {
                offset += got;
                guest_write(a[2], &offset, 8);
            }
            return written;
        }
        case 72: {  // pselect6
            int n = fd_arg(0);
            uint64_t sets[3] = {a[1], a[2], a[3]};
            std::vector<uint64_t> bits[3];
            size_t words = static_cast<size_t>((n + 63) / 64);
            for (int s = 0; s < 3; ++s) {
                bits[s].assign(words, 0);
                if (sets[s] && words && !guest_read(sets[s], bits[s].data(), words * 8)) return -EFAULT_;
            }
            int64_t deadline = 0;
            if (a[4]) {
                ktimespec ts;
                if (!guest_read(a[4], &ts, sizeof ts)) return -EFAULT_;
                deadline = (ts.tv_sec == 0 && ts.tv_nsec == 0) ? -1 : ticks_now() + ticks_from_timespec_relative(ts);
            }
            std::vector<std::shared_ptr<OpenFile>> files(static_cast<size_t>(n));
            for (int fd = 0; fd < n; ++fd)
                for (int s = 0; s < 3; ++s)
                    if (bits[s][fd / 64] & (1ull << (fd % 64))) files[fd] = fds().get(fd);
            std::vector<uint64_t> out[3];
            int count = 0;
            auto check = [&] {
                count = 0;
                for (int s = 0; s < 3; ++s) out[s].assign(words, 0);
                for (int fd = 0; fd < n; ++fd) {
                    if (!files[fd]) continue;
                    uint32_t mask = files[fd]->poll_mask();
                    bool want[3] = {(bits[0][fd / 64] >> (fd % 64)) & 1, (bits[1][fd / 64] >> (fd % 64)) & 1,
                                    (bits[2][fd / 64] >> (fd % 64)) & 1};
                    bool got[3] = {(mask & (POLLIN_ | POLLHUP_ | POLLERR_)) != 0, (mask & (POLLOUT_ | POLLERR_)) != 0,
                                   (mask & POLLPRI_) != 0};
                    for (int s = 0; s < 3; ++s)
                        if (want[s] && got[s]) {
                            out[s][fd / 64] |= 1ull << (fd % 64);
                            ++count;
                        }
                }
                return count > 0;
            };
            for (auto& f : files)
                if (f)
                    if (auto* q = f->wait_queue()) q->add(&t);
            int64_t r = 0;
            if (!check() && deadline != -1) r = block_until(t, check, deadline, "select");
            for (auto& f : files)
                if (f)
                    if (auto* q = f->wait_queue()) q->remove(&t);
            if (r == -EINTR_) return r;
            for (int s = 0; s < 3; ++s)
                if (sets[s] && words) guest_write(sets[s], out[s].data(), words * 8);
            return count;
        }
        case 73: {  // ppoll
            int64_t deadline = 0;
            if (a[2]) {
                ktimespec ts;
                if (!guest_read(a[2], &ts, sizeof ts)) return -EFAULT_;
                deadline = (ts.tv_sec == 0 && ts.tv_nsec == 0) ? -1 : ticks_now() + ticks_from_timespec_relative(ts);
            }
            uint64_t old_mask = t.sigmask;
            if (a[3]) {
                uint64_t mask;
                if (!guest_read(a[3], &mask, 8)) return -EFAULT_;
                t.sigmask = mask & ~((1ull << (SIGKILL_ - 1)) | (1ull << (SIGSTOP_ - 1)));
            }
            int64_t r = poll_files(t, a[0], a[1], deadline);
            if (a[3] && r != -EINTR_) t.sigmask = old_mask;
            return r;
        }
        case 78: {  // readlinkat
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path)) return e;
            std::string target;
            if (!vfs().readlink(path, target)) {
                kstat st;
                if (int64_t e = stat_path(path, false, st)) return e;
                return -EINVAL_;
            }
            uint64_t n = std::min<uint64_t>(target.size(), a[3]);
            if (!guest_write(a[2], target.data(), n)) return -EFAULT_;
            return static_cast<int64_t>(n);
        }
        case 79: {  // newfstatat
            uint32_t flags = static_cast<uint32_t>(a[3]);
            kstat st;
            std::string path;
            if (int64_t e = path_at(fd_arg(0), a[1], path, (flags & AT_EMPTY_PATH_) != 0)) return e;
            std::string raw;
            guest_string(a[1], raw);
            if (raw.empty() && (flags & AT_EMPTY_PATH_)) {
                auto file = fds().get(fd_arg(0));
                if (!file) return -EBADF_;
                if (int64_t e = file->stat(st)) return e;
            } else if (int64_t e = stat_path(path, !(flags & AT_SYMLINK_NOFOLLOW_), st)) {
                TRACE("files", "stat %s -> %lld", path.c_str(), (long long)e);
                return e;
            }
            return guest_write(a[2], &st, sizeof st) ? 0 : -EFAULT_;
        }
        case 80: {  // fstat
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            kstat st;
            if (int64_t e = file->stat(st)) return e;
            return guest_write(a[1], &st, sizeof st) ? 0 : -EFAULT_;
        }
        case 81: return 0;  // sync
        case 82: case 83: {  // fsync, fdatasync
            auto file = fds().get(fd_arg(0));
            if (!file) return -EBADF_;
            return file->sync();
        }
        case 84: return 0;  // sync_file_range
        case 85: {  // timerfd_create
            auto file = std::make_shared<TimerFile>(static_cast<int>(a[0]));
            if (a[1] & O_NONBLOCK_) file->status_flags |= O_NONBLOCK_;
            return fds().install(file, (a[1] & O_CLOEXEC_) != 0);
        }
        case 86: {  // timerfd_settime
            auto timer = std::dynamic_pointer_cast<TimerFile>(fds().get(fd_arg(0)));
            if (!timer) return -EINVAL_;
            kitimerspec value{}, old{};
            if (!guest_read(a[2], &value, sizeof value)) return -EFAULT_;
            timer->settime(static_cast<int>(a[1]), value, a[3] ? &old : nullptr);
            if (a[3]) guest_write(a[3], &old, sizeof old);
            return 0;
        }
        case 87: {  // timerfd_gettime
            auto timer = std::dynamic_pointer_cast<TimerFile>(fds().get(fd_arg(0)));
            if (!timer) return -EINVAL_;
            kitimerspec value{};
            timer->gettime(value);
            return guest_write(a[1], &value, sizeof value) ? 0 : -EFAULT_;
        }
        case 88: return 0;  // utimensat
        case 19: {  // eventfd2
            auto file = std::make_shared<EventFile>(a[0] & 0xffffffffull, (a[1] & 1) != 0);
            if (a[1] & O_NONBLOCK_) file->status_flags |= O_NONBLOCK_;
            return fds().install(file, (a[1] & O_CLOEXEC_) != 0);
        }
        case 20: {  // epoll_create1
            auto file = std::make_shared<EpollFile>();
            return fds().install(file, (a[0] & O_CLOEXEC_) != 0);
        }
        case 21: {  // epoll_ctl
            auto ep = std::dynamic_pointer_cast<EpollFile>(fds().get(fd_arg(0)));
            if (!ep) return -EINVAL_;
            kepoll_event event{};
            if (static_cast<int>(a[1]) != EPOLL_CTL_DEL_ && !guest_read(a[3], &event, sizeof event)) return -EFAULT_;
            return ep->ctl(static_cast<int>(a[1]), fd_arg(2), &event);
        }
        case 22: {  // epoll_pwait
            auto ep = std::dynamic_pointer_cast<EpollFile>(fds().get(fd_arg(0)));
            if (!ep) return -EINVAL_;
            int timeout = static_cast<int>(a[3]);
            int64_t deadline = timeout < 0 ? 0 : timeout == 0 ? -1 : ticks_now() + static_cast<int64_t>(timeout) * ticks_per_second() / 1000;
            uint64_t old_mask = t.sigmask;
            if (a[4]) {
                uint64_t mask;
                if (guest_read(a[4], &mask, 8)) t.sigmask = mask;
            }
            int64_t r = ep->wait(t, a[1], static_cast<int>(a[2]), deadline);
            if (a[4] && r != -EINTR_) t.sigmask = old_mask;
            return r;
        }
        case 291: {  // statx
            std::string path;
            uint32_t flags = static_cast<uint32_t>(a[2]);
            if (int64_t e = path_at(fd_arg(0), a[1], path, (flags & AT_EMPTY_PATH_) != 0)) return e;
            kstat st;
            std::string raw;
            guest_string(a[1], raw);
            if (raw.empty() && (flags & AT_EMPTY_PATH_)) {
                auto file = fds().get(fd_arg(0));
                if (!file) return -EBADF_;
                if (int64_t e = file->stat(st)) return e;
            } else if (int64_t e = stat_path(path, !(flags & AT_SYMLINK_NOFOLLOW_), st)) {
                return e;
            }
            kstatx sx{};
            sx.stx_mask = 0x7ff;
            sx.stx_blksize = static_cast<uint32_t>(st.st_blksize);
            sx.stx_nlink = st.st_nlink;
            sx.stx_uid = st.st_uid;
            sx.stx_gid = st.st_gid;
            sx.stx_mode = static_cast<uint16_t>(st.st_mode);
            sx.stx_ino = st.st_ino;
            sx.stx_size = static_cast<uint64_t>(st.st_size);
            sx.stx_blocks = static_cast<uint64_t>(st.st_blocks);
            sx.stx_atime = {st.st_atime_sec, static_cast<uint32_t>(st.st_atime_nsec), 0};
            sx.stx_mtime = {st.st_mtime_sec, static_cast<uint32_t>(st.st_mtime_nsec), 0};
            sx.stx_ctime = {st.st_ctime_sec, static_cast<uint32_t>(st.st_ctime_nsec), 0};
            sx.stx_btime = sx.stx_ctime;
            sx.stx_dev_major = 253;
            sx.stx_dev_minor = 1;
            return guest_write(a[4], &sx, sizeof sx) ? 0 : -EFAULT_;
        }
        case 279: {  // memfd_create
            std::string name;
            guest_string(a[0], name, 256);
            wchar_t dir[MAX_PATH], file_name[MAX_PATH];
            GetTempPathW(MAX_PATH, dir);
            GetTempFileNameW(dir, L"amf", 0, file_name);
            HANDLE h = CreateFileW(file_name, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) return -ENOMEM_;
            auto file = std::make_shared<HostFile>(h, file_name, false, false);
            file->set_guest_path("/memfd:" + name + " (deleted)");
            file->status_flags = O_RDWR_;
            return fds().install(file, (a[1] & 1) != 0);
        }
        case 285: return -EXDEV_;  // copy_file_range: callers fall back to read/write
        case 436: {  // close_range
            uint32_t first = static_cast<uint32_t>(a[0]), last = static_cast<uint32_t>(a[1]);
            for (int fd : fds().list())
                if (static_cast<uint32_t>(fd) >= first && static_cast<uint32_t>(fd) <= last) {
                    if (a[2] & 4) fds().set_cloexec(fd, true);  // CLOSE_RANGE_CLOEXEC
                    else fds().close(fd);
                }
            return 0;
        }
        case 26: {  // inotify_init1: a descriptor that never reports changes
            auto file = std::make_shared<EventFile>(0, false);
            if (a[0] & O_NONBLOCK_) file->status_flags |= O_NONBLOCK_;
            return fds().install(file, (a[0] & O_CLOEXEC_) != 0);
        }
        case 27: return 1;  // inotify_add_watch
        case 28: return 0;  // inotify_rm_watch
        case 5: case 6: case 7: return -EOPNOTSUPP_;   // setxattr family
        case 8: case 9: case 10: return -ENODATA_;     // getxattr family
        case 11: case 12: case 13: return 0;           // listxattr family: none
        case 14: case 15: case 16: return -ENODATA_;   // removexattr family
        case 213: return 0;  // readahead
        case 223: return 0;  // fadvise64
        case 74: return -ENOSYS_;  // signalfd4
        default:
            handled = false;
            return 0;
    }
}

}  // namespace axrb
