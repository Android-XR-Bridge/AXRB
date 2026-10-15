#pragma once

#include "kernel.h"
#include "linux.h"
#include "vfs.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace axrb {

// An open file description (Linux "struct file"): shared by dup'd descriptors.
class OpenFile : public std::enable_shared_from_this<OpenFile> {
public:
    virtual ~OpenFile() = default;
    // Linux flush is called for each descriptor close, even while a blocked
    // ioctl still holds the open file description alive.
    virtual void on_close() {}

    // Guest-visible operations; results are byte counts or -errno.
    virtual int64_t read(GuestThread& t, uint64_t buf, uint64_t len);
    virtual int64_t write(GuestThread& t, uint64_t buf, uint64_t len);
    virtual int64_t pread(GuestThread& t, uint64_t buf, uint64_t len, int64_t offset);
    virtual int64_t pwrite(GuestThread& t, uint64_t buf, uint64_t len, int64_t offset);
    // Gathering write: datagram sockets need the whole vector in one message;
    // by default the pieces are written one after another.
    virtual int64_t writev(GuestThread& t, const std::vector<linux_abi::kiovec>& vecs);
    virtual int64_t lseek(int64_t offset, int whence);
    virtual int64_t stat(linux_abi::kstat& st);
    virtual int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg);
    virtual int64_t getdents64(uint64_t buf, uint64_t len);
    virtual int64_t truncate(int64_t length);
    virtual int64_t sync() { return 0; }
    virtual int64_t fallocate(int mode, int64_t offset, int64_t length);
    // Readiness for poll/epoll (POLLIN, POLLOUT, ...).
    virtual uint32_t poll_mask() { return linux_abi::POLLIN_ | linux_abi::POLLOUT_; }
    // Where to register to be woken when readiness may change (null: never changes).
    virtual WaitQueue* wait_queue() { return nullptr; }
    // A Windows event signalled on readiness change, for sockets.
    virtual HANDLE readiness_event() { return nullptr; }

    // Host-level access for mmap.
    virtual int64_t pread_host(void* buf, uint64_t len, uint64_t offset);
    virtual int64_t pwrite_host(const void* buf, uint64_t len, uint64_t offset);
    virtual bool mappable() const { return false; }
    // Objects that live in guest memory already (the property area): mmap
    // returns this address instead of making a copy.
    virtual uint64_t fixed_mapping() const { return 0; }
    // Device-managed mappings (e.g. Binder's receive arena).
    virtual bool has_device_mapping() const { return false; }
    virtual int64_t map_device(uint64_t length, uint32_t prot, uint32_t flags, uint64_t offset) {
        return -linux_abi::ENODEV_;
    }

    const std::string& guest_path() const { return path_; }
    void set_guest_path(std::string path) { path_ = std::move(path); }
    uint64_t inode() const { return inode_; }
    uint32_t status_flags = 0;  // O_APPEND, O_NONBLOCK, access mode
    bool nonblocking() const { return (status_flags & linux_abi::O_NONBLOCK_) != 0; }

protected:
    std::string path_;
    uint64_t inode_ = 0;
    void assign_inode(const std::string& key);
};

// A host file (or directory) reached through a mount.
class HostFile : public OpenFile {
public:
    HostFile(HANDLE handle, std::wstring host_path, bool directory, bool read_only);
    ~HostFile() override;
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t pread(GuestThread& t, uint64_t buf, uint64_t len, int64_t offset) override;
    int64_t pwrite(GuestThread& t, uint64_t buf, uint64_t len, int64_t offset) override;
    int64_t lseek(int64_t offset, int whence) override;
    int64_t stat(linux_abi::kstat& st) override;
    int64_t getdents64(uint64_t buf, uint64_t len) override;
    int64_t truncate(int64_t length) override;
    int64_t sync() override;
    int64_t fallocate(int mode, int64_t offset, int64_t length) override;
    int64_t pread_host(void* buf, uint64_t len, uint64_t offset) override;
    int64_t pwrite_host(const void* buf, uint64_t len, uint64_t offset) override;
    bool mappable() const override { return !directory_; }
    HANDLE handle() const { return handle_; }
    const std::wstring& host_path() const { return host_path_; }

private:
    void load_directory();
    HANDLE handle_;
    std::wstring host_path_;
    bool directory_;
    bool read_only_;
    std::mutex lock_;
    int64_t position_ = 0;
    // Directory listing state.
    struct Entry {
        std::string name;
        uint8_t type;
        uint64_t inode;
    };
    std::vector<Entry> entries_;
    bool listed_ = false;
};

// Read-only content generated at open time (/proc, /sys, synthetic dirs).
class MemoryFile : public OpenFile {
public:
    MemoryFile(std::string content, bool directory = false, std::vector<std::pair<std::string, uint8_t>> children = {});
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t pread(GuestThread& t, uint64_t buf, uint64_t len, int64_t offset) override;
    int64_t lseek(int64_t offset, int whence) override;
    int64_t stat(linux_abi::kstat& st) override;
    int64_t getdents64(uint64_t buf, uint64_t len) override;
    int64_t pread_host(void* buf, uint64_t len, uint64_t offset) override;
    bool mappable() const override { return !directory_; }

private:
    std::mutex lock_;
    std::string content_;
    bool directory_;
    std::vector<std::pair<std::string, uint8_t>> children_;
    int64_t position_ = 0;
};

// /dev/null, /dev/zero, /dev/urandom, /dev/random, /dev/tty...
class DeviceFile : public OpenFile {
public:
    explicit DeviceFile(std::string kind) : kind_(std::move(kind)) {}
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t pread(GuestThread& t, uint64_t buf, uint64_t len, int64_t) override { return read(t, buf, len); }
    int64_t stat(linux_abi::kstat& st) override;
    int64_t lseek(int64_t, int) override { return 0; }
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override;
    int64_t pread_host(void* buf, uint64_t len, uint64_t) override;
    bool mappable() const override { return kind_ == "zero"; }

private:
    std::string kind_;
};

// stdout/stderr: lines go to the log.
class ConsoleFile : public OpenFile {
public:
    explicit ConsoleFile(int which) : which_(which) {}
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t stat(linux_abi::kstat& st) override;
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override;

private:
    int which_;
};

// Byte pipe shared by a read end and a write end (also socketpair halves).
struct PipeBuffer {
    std::mutex lock;
    std::deque<uint8_t> bytes;
    bool writer_closed = false;
    bool reader_closed = false;
    size_t capacity = 65536;
    WaitQueue waiters;
};

class PipeFile : public OpenFile {
public:
    PipeFile(std::shared_ptr<PipeBuffer> in, std::shared_ptr<PipeBuffer> out, bool socket);
    ~PipeFile() override;
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t stat(linux_abi::kstat& st) override;
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override;
    uint32_t poll_mask() override;
    WaitQueue* wait_queue() override;
    void shutdown(int how);
    bool is_socket() const { return socket_; }

private:
    std::shared_ptr<PipeBuffer> in_;   // what we read
    std::shared_ptr<PipeBuffer> out_;  // what we write
    bool socket_;
    // A combined queue: waiters on either direction.
    WaitQueue queue_;
};

class EventFile : public OpenFile {
public:
    EventFile(uint64_t initial, bool semaphore) : counter_(initial), semaphore_(semaphore) {}
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t stat(linux_abi::kstat& st) override;
    uint32_t poll_mask() override;
    WaitQueue* wait_queue() override { return &waiters_; }

private:
    std::mutex lock_;
    uint64_t counter_;
    bool semaphore_;
    WaitQueue waiters_;
};

class TimerFile : public OpenFile {
public:
    explicit TimerFile(int clock) : clock_(clock) {}
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t stat(linux_abi::kstat& st) override;
    uint32_t poll_mask() override;
    WaitQueue* wait_queue() override { return &waiters_; }
    int64_t settime(int flags, const linux_abi::kitimerspec& value, linux_abi::kitimerspec* old);
    int64_t gettime(linux_abi::kitimerspec& out);

private:
    uint64_t expirations_locked();
    std::mutex lock_;
    int clock_;
    int64_t next_ns_ = 0;      // absolute monotonic ns of the next expiry (0 = disarmed)
    int64_t interval_ns_ = 0;
    WaitQueue waiters_;
};

class EpollFile : public OpenFile {
public:
    struct Interest {
        std::weak_ptr<OpenFile> file;
        uint32_t events;
        uint64_t data;
        bool disabled = false;  // EPOLLONESHOT fired
    };
    int64_t ctl(int op, int fd, const linux_abi::kepoll_event* event);
    int64_t wait(GuestThread& t, uint64_t events, int max, int64_t deadline_ticks);
    int64_t stat(linux_abi::kstat& st) override;
    uint32_t poll_mask() override;

private:
    std::mutex lock_;
    std::map<int, Interest> interests_;
};

// The process's descriptor table.
class FdTable {
public:
    int install(std::shared_ptr<OpenFile> file, bool cloexec, int minimum = 0);
    int install_at(int fd, std::shared_ptr<OpenFile> file, bool cloexec);
    std::shared_ptr<OpenFile> get(int fd);
    int close(int fd);
    bool cloexec(int fd);
    void set_cloexec(int fd, bool value);
    std::vector<int> list();

private:
    std::mutex lock_;
    std::vector<std::shared_ptr<OpenFile>> files_;
    std::vector<bool> cloexec_;
};

FdTable& fds();

// Device nodes with a host implementation beyond DeviceFile's.
void register_device(const std::string& name, std::function<std::shared_ptr<OpenFile>()> make);
std::shared_ptr<OpenFile> make_device(const std::string& name);

// Opens a guest path (absolute) the way openat would.
int64_t open_path(GuestThread& t, const std::string& path, uint32_t flags, uint32_t mode, std::shared_ptr<OpenFile>& out);
// Resolves a path argument relative to a directory descriptor.
int64_t path_at(int dirfd, uint64_t path_addr, std::string& out, bool allow_empty = false);
int64_t stat_path(const std::string& path, bool follow, linux_abi::kstat& st);
void fill_stat_from_attributes(linux_abi::kstat& st, const WIN32_FILE_ATTRIBUTE_DATA& data, const std::string& key);
int64_t errno_from_win32(DWORD error);
uint64_t inode_for(const std::string& key);

}  // namespace axrb
