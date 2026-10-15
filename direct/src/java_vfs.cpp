// java.io for the managed runtime: Androitainer's vfs::FileSystem interface,
// implemented on the guest's file system. Handles are guest file descriptors,
// so a FileDescriptor that Java code hands to native code (AssetFileDescriptor,
// ParcelFileDescriptor) is a real descriptor there too.

#include "arun/vfs.h"

#include "files.h"
#include "kernel.h"
#include "log.h"
#include "linux.h"
#include "memory.h"

#include <cstring>

namespace arun::vfs {
using namespace axrb::linux_abi;

namespace {

axrb::GuestThread& self() {
    axrb::GuestThread* t = axrb::current_thread();
    if (!t) axrb::fatal("java.io used from a thread that is not a guest thread");
    return *t;
}

int64_t sys(uint64_t nr, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0) {
    return axrb::do_syscall(self(), nr, a0, a1, a2, a3, a4, 0);
}

uint64_t ptr(const void* p) { return reinterpret_cast<uint64_t>(p); }

constexpr uint64_t kOpenat = 56, kClose = 57, kLseek = 62, kMkdirat = 34, kUnlinkat = 35, kRenameat = 38,
                   kFaccessat = 48, kNewfstatat = 79, kGetdents64 = 61, kIoctl = 29;
const uint64_t kCwd = static_cast<uint64_t>(static_cast<int64_t>(AT_FDCWD_));

}  // namespace

void FileSystem::AddMount(std::string, std::filesystem::path, bool) {}

int FileSystem::Resolve(std::string_view, bool, std::filesystem::path*) const { return kENOENT; }

int FileSystem::Open(std::string_view guest, const OpenFlags& flags, int* handle) {
    std::string path(guest);
    uint32_t mode = flags.write ? O_WRONLY_ : O_RDONLY_;
    if (flags.write) {
        if (flags.append) mode |= O_APPEND_;
        else if (flags.truncate) mode |= O_TRUNC_;
        if (flags.create) mode |= O_CREAT_;
        if (flags.exclusive) mode |= O_EXCL_;
    }
    int64_t fd = sys(kOpenat, kCwd, ptr(path.c_str()), mode | O_CLOEXEC_, 0666);
    if (fd < 0) return static_cast<int>(-fd);
    *handle = static_cast<int>(fd);
    return 0;
}

int FileSystem::Read(int handle, std::span<std::uint8_t> out, std::size_t* n) {
    auto file = axrb::fds().get(handle);
    if (!file) return kEBADF;
    int64_t r = file->read(self(), ptr(out.data()), out.size());
    if (r < 0) return static_cast<int>(-r);
    *n = static_cast<std::size_t>(r);
    return 0;
}

int FileSystem::Write(int handle, std::span<const std::uint8_t> data) {
    auto file = axrb::fds().get(handle);
    if (!file) return kEBADF;
    size_t done = 0;
    while (done < data.size()) {
        int64_t r = file->write(self(), ptr(data.data() + done), data.size() - done);
        if (r < 0) return static_cast<int>(-r);
        if (r == 0) return kEIO;
        done += static_cast<size_t>(r);
    }
    return 0;
}

int FileSystem::Available(int handle, std::int64_t* bytes) {
    auto file = axrb::fds().get(handle);
    if (!file) return kEBADF;
    kstat st{};
    if (file->stat(st) == 0 && (st.st_mode & S_IFMT_) == S_IFREG_) {
        int64_t at = file->lseek(0, 1);
        *bytes = at >= 0 ? std::max<int64_t>(0, st.st_size - at) : 0;
        return 0;
    }
    int32_t n = 0;
    *bytes = sys(kIoctl, static_cast<uint64_t>(handle), FIONREAD_, ptr(&n)) == 0 ? n : 0;
    return 0;
}

int FileSystem::Skip(int handle, std::int64_t n, std::int64_t* skipped) {
    auto file = axrb::fds().get(handle);
    if (!file) return kEBADF;
    int64_t at = file->lseek(0, 1);
    if (at >= 0) {
        int64_t to = file->lseek(n, 1);
        if (to < 0) return static_cast<int>(-to);
        *skipped = to - at;
        return 0;
    }
    // Not seekable: read and discard.
    std::vector<std::uint8_t> sink(static_cast<size_t>(std::min<int64_t>(n, 65536)));
    int64_t total = 0;
    while (total < n) {
        int64_t r = file->read(self(), ptr(sink.data()), static_cast<uint64_t>(std::min<int64_t>(n - total, sink.size())));
        if (r <= 0) break;
        total += r;
    }
    *skipped = total;
    return 0;
}

int FileSystem::Close(int handle) {
    int64_t r = sys(kClose, static_cast<uint64_t>(handle));
    return r < 0 ? static_cast<int>(-r) : 0;
}

bool FileSystem::Access(std::string_view guest, int mode) const {
    std::string path(guest);
    return sys(kFaccessat, kCwd, ptr(path.c_str()), static_cast<uint64_t>(mode)) == 0;
}

int FileSystem::Stat(std::string_view guest, StatInfo* info) const {
    std::string path(guest);
    kstat st{};
    int64_t r = sys(kNewfstatat, kCwd, ptr(path.c_str()), ptr(&st), 0);
    if (r < 0) return static_cast<int>(-r);
    info->is_directory = (st.st_mode & S_IFMT_) == S_IFDIR_;
    info->size = static_cast<uint64_t>(st.st_size);
    info->mtime_ms = st.st_mtime_sec * 1000 + static_cast<int64_t>(st.st_mtime_nsec / 1000000);
    return 0;
}

int FileSystem::Mkdir(std::string_view guest) {
    std::string path(guest);
    int64_t r = sys(kMkdirat, kCwd, ptr(path.c_str()), 0771);
    return r < 0 ? static_cast<int>(-r) : 0;
}

int FileSystem::Remove(std::string_view guest) {
    std::string path(guest);
    int64_t r = sys(kUnlinkat, kCwd, ptr(path.c_str()), 0);
    if (r == -EISDIR_) r = sys(kUnlinkat, kCwd, ptr(path.c_str()), AT_REMOVEDIR_);
    return r < 0 ? static_cast<int>(-r) : 0;
}

int FileSystem::Rename(std::string_view from, std::string_view to) {
    std::string a(from), b(to);
    int64_t r = sys(kRenameat, kCwd, ptr(a.c_str()), kCwd, ptr(b.c_str()));
    return r < 0 ? static_cast<int>(-r) : 0;
}

int FileSystem::List(std::string_view guest, std::vector<std::string>* names) const {
    std::string path(guest);
    int64_t fd = sys(kOpenat, kCwd, ptr(path.c_str()), O_RDONLY_ | O_DIRECTORY_ | O_CLOEXEC_, 0);
    if (fd < 0) return static_cast<int>(-fd);
    std::vector<uint8_t> buffer(32768);
    for (;;) {
        int64_t n = sys(kGetdents64, static_cast<uint64_t>(fd), ptr(buffer.data()), buffer.size());
        if (n <= 0) break;
        for (int64_t at = 0; at < n;) {
            uint16_t reclen;
            std::memcpy(&reclen, buffer.data() + at + 16, 2);
            const char* name = reinterpret_cast<const char*>(buffer.data() + at + 19);
            if (std::strcmp(name, ".") != 0 && std::strcmp(name, "..") != 0) names->push_back(name);
            at += reclen;
        }
    }
    sys(kClose, static_cast<uint64_t>(fd));
    return 0;
}

int FileSystem::ReadFile(std::string_view guest, std::vector<std::uint8_t>* bytes) const {
    std::string path(guest);
    int64_t fd = sys(kOpenat, kCwd, ptr(path.c_str()), O_RDONLY_ | O_CLOEXEC_, 0);
    if (fd < 0) return static_cast<int>(-fd);
    auto file = axrb::fds().get(static_cast<int>(fd));
    bytes->clear();
    std::uint8_t chunk[65536];
    for (;;) {
        int64_t r = file->read(self(), ptr(chunk), sizeof chunk);
        if (r <= 0) break;
        bytes->insert(bytes->end(), chunk, chunk + r);
    }
    sys(kClose, static_cast<uint64_t>(fd));
    return 0;
}

}  // namespace arun::vfs
