#include "binder.h"
#include "files.h"
#include "memory.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace axrb {
namespace {
using namespace linux_abi;

// Linux arm64 Binder UAPI. Keep these layouts independent of Windows ABI.
constexpr uint32_t ioc(uint32_t dir, char type, uint32_t nr, uint32_t size = 0) {
    return (dir << 30) | (size << 16) | (uint32_t(type) << 8) | nr;
}
constexpr uint32_t br(uint32_t nr, uint32_t size = 0) { return ioc(size ? 2 : 0, 'r', nr, size); }
struct WriteRead { uint64_t write_size, write_consumed, write_buffer, read_size, read_consumed, read_buffer; };
struct Transaction {
    uint64_t target, cookie;
    uint32_t code, flags;
    int32_t pid;
    uint32_t uid;
    uint64_t data_size, offsets_size, data, offsets;
};
struct Object { uint32_t type, flags; uint64_t value, cookie; };
struct PtrCookie { uint64_t ptr, cookie; };
static_assert(sizeof(Transaction) == 64 && sizeof(WriteRead) == 48 && sizeof(Object) == 24);
constexpr uint32_t kBinder = 0x73622a85, kHandle = 0x73682a85, kFd = 0x66642a85;

class BinderFile;
struct Node {
    std::weak_ptr<BinderFile> owner;
    uint64_t ptr = 0, cookie = 0;
};
struct Message {
    uint32_t command = 0;
    std::vector<uint8_t> payload;
    Transaction transaction{};
    std::vector<uint8_t> data;
    std::vector<uint64_t> offsets;
    std::weak_ptr<BinderFile> sender;
    int sender_tid = 0;
};
struct Domain {
    std::mutex lock;
    std::weak_ptr<BinderFile> manager;
    std::shared_ptr<Node> manager_node;
};
struct ThreadState {
    std::deque<Message> inbox;
    std::vector<std::pair<std::weak_ptr<BinderFile>, int>> callers;
    bool looper = false;
};

class BinderFile final : public OpenFile {
public:
    explicit BinderFile(std::shared_ptr<Domain> domain) : domain_(std::move(domain)) {}
    ~BinderFile() override {
        // Buffers belong to this open description, not a particular fd.
        if (base_) address_space().munmap(base_, size_);
    }
    bool has_device_mapping() const override { return true; }
    void on_close() override {
        std::lock_guard<std::mutex> held(domain_->lock);
        ++flush_epoch_;
        waiters_.wake_all();
    }
    int64_t map_device(uint64_t length, uint32_t prot, uint32_t flags, uint64_t offset) override {
        std::lock_guard<std::mutex> held(domain_->lock);
        if (base_) return -EBUSY_;
        if (offset || !length || length > (4 << 20) || (prot & PROT_WRITE_) || (flags & MAP_FIXED_)) return -EINVAL_;
        int64_t result = address_space().mmap(0, length, PROT_READ_, MAP_PRIVATE_ | MAP_ANONYMOUS_, nullptr, 0, "/dev/binder");
        if (result < 0) return result;
        base_ = result;
        size_ = page_up(length);
        pages_.resize(size_ / kPageSize);
        return result;
    }
    int64_t stat(kstat& st) override {
        st = {};
        st.st_mode = S_IFCHR_ | 0666;
        st.st_nlink = 1;
        st.st_blksize = 4096;
        return 0;
    }
    WaitQueue* wait_queue() override { return &waiters_; }
    uint32_t poll_mask() override {
        std::lock_guard<std::mutex> held(domain_->lock);
        auto* t = current_thread();
        return (!pending_.empty() || (t && !threads_[t->tid].inbox.empty())) ? POLLIN_ : 0;
    }
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override;

private:
    std::shared_ptr<BinderFile> self() { return std::static_pointer_cast<BinderFile>(shared_from_this()); }
    void send(int tid, Message message) {
        if (tid) threads_[tid].inbox.push_back(std::move(message));
        else pending_.push_back(std::move(message));
        waiters_.wake_all();
    }
    void code(int tid, uint32_t command) { Message m; m.command = command; send(tid, std::move(m)); }
    void reference(uint32_t command, const Node& node) {
        Message m;
        m.command = command;
        PtrCookie pc{node.ptr, node.cookie};
        m.payload.resize(sizeof pc);
        std::memcpy(m.payload.data(), &pc, sizeof pc);
        send(0, std::move(m));
    }
    std::shared_ptr<Node> node(uint32_t handle) {
        if (!handle) return domain_->manager_node;
        auto found = handles_.find(handle);
        return found == handles_.end() ? nullptr : found->second;
    }
    bool translate(Message& message, BinderFile& target);
    int64_t transact(int tid, const Transaction& transaction, bool reply);
    int64_t write_commands(int tid, WriteRead& wr);
    int64_t read_commands(int tid, WriteRead& wr);
    bool materialize(Message& message);
    void free_buffer(uint64_t address);

    std::shared_ptr<Domain> domain_;
    std::map<int, ThreadState> threads_;
    std::deque<Message> pending_;
    std::map<uint64_t, std::shared_ptr<Node>> nodes_;
    std::map<uint32_t, std::shared_ptr<Node>> handles_;
    uint32_t next_handle_ = 1, max_threads_ = 15;
    uint64_t flush_epoch_ = 0;
    uint64_t base_ = 0, size_ = 0;
    std::vector<bool> pages_;
    std::map<uint64_t, size_t> buffers_;
    WaitQueue waiters_;
};

bool BinderFile::translate(Message& message, BinderFile& target) {
    uint64_t previous_end = 0;
    for (uint64_t offset : message.offsets) {
        if (offset < previous_end || offset % 4 || offset > message.data.size() || message.data.size() - offset < sizeof(Object)) return false;
        previous_end = offset + sizeof(Object);
        Object object;
        std::memcpy(&object, message.data.data() + offset, sizeof object);
        std::shared_ptr<Node> exported;
        if (object.type == kBinder) {
            if (!object.value) continue;  // null Binder
            auto& n = nodes_[object.value];
            if (!n) {
                n = std::make_shared<Node>(Node{self(), object.value, object.cookie});
                reference(br(7, 16), *n);
                reference(br(8, 16), *n);
            }
            exported = n;
        } else if (object.type == kHandle) {
            exported = node(static_cast<uint32_t>(object.value));
            if (!exported || exported->owner.expired()) return false;
        } else if (object.type == kFd) {
            auto file = fds().get(static_cast<int>(object.value));
            if (!file) return false;
            // Endpoints currently share the Direct process's descriptor table.
            object.value = fds().install(file, true);
        } else {
            LOGW("binder", "unsupported object type %08x", object.type);
            return false;
        }
        if (exported) {
            if (exported->owner.lock().get() == &target) {
                object.type = kBinder;
                object.value = exported->ptr;
                object.cookie = exported->cookie;
            } else {
                uint32_t handle = 0;
                for (const auto& [id, n] : target.handles_) if (n == exported) { handle = id; break; }
                if (!handle) { handle = target.next_handle_++; target.handles_[handle] = exported; }
                object.type = kHandle;
                object.value = handle;
                object.cookie = 0;
            }
        }
        std::memcpy(message.data.data() + offset, &object, sizeof object);
    }
    return true;
}

int64_t BinderFile::transact(int tid, const Transaction& tr, bool reply) {
    if (tr.data_size > (1 << 20) || tr.offsets_size > (1 << 20) || tr.offsets_size % 8) return -EINVAL_;
    Message m;
    m.transaction = tr;
    m.transaction.pid = process().pid;
    m.transaction.uid = process().uid;
    m.data.resize(tr.data_size);
    m.offsets.resize(tr.offsets_size / 8);
    if (!guest_read(tr.data, m.data.data(), m.data.size()) ||
        !guest_read(tr.offsets, m.offsets.data(), tr.offsets_size)) return -EFAULT_;
    std::shared_ptr<BinderFile> target;
    int target_tid = 0;
    if (reply) {
        auto& stack = threads_[tid].callers;
        if (stack.empty()) { code(tid, br(17)); return 0; }
        target = stack.back().first.lock();
        target_tid = stack.back().second;
        stack.pop_back();
        m.command = br(3, sizeof(Transaction));
        m.transaction.target = m.transaction.cookie = 0;
    } else {
        auto n = node(static_cast<uint32_t>(tr.target));
        if (n) {
            target = n->owner.lock();
            m.transaction.target = n->ptr;
            m.transaction.cookie = n->cookie;
        }
        m.command = br(2, sizeof(Transaction));
    }
    if (!target) { code(tid, br(5)); return 0; }
    if (!translate(m, *target)) { code(tid, br(17)); return 0; }
    m.sender = self();
    m.sender_tid = tid;
    target->send(target_tid, std::move(m));
    code(tid, br(6));
    return 0;
}

bool BinderFile::materialize(Message& m) {
    uint64_t aligned_data = (m.data.size() + 7) & ~7ull;
    size_t count = static_cast<size_t>(page_up(std::max<uint64_t>(1, aligned_data + m.offsets.size() * 8)) / kPageSize);
    size_t start = 0, run = 0;
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i]) run = 0;
        else if (++run == count) { start = i + 1 - count; break; }
    }
    if (run != count) return false;
    uint64_t address = base_ + start * kPageSize;
    uint64_t length = count * kPageSize;
    if (address_space().mprotect(address, length, PROT_READ_ | PROT_WRITE_) < 0) return false;
    bool ok = guest_write(address, m.data.data(), m.data.size()) &&
              guest_write(address + aligned_data, m.offsets.data(), m.offsets.size() * 8);
    address_space().mprotect(address, length, PROT_READ_);
    if (!ok) return false;
    for (size_t i = start; i < start + count; ++i) pages_[i] = true;
    buffers_[address] = count;
    m.transaction.data = address;
    m.transaction.offsets = address + aligned_data;
    return true;
}

void BinderFile::free_buffer(uint64_t address) {
    auto found = buffers_.find(address);
    if (found == buffers_.end()) return;
    size_t start = static_cast<size_t>((address - base_) / kPageSize);
    for (size_t i = start; i < start + found->second; ++i) pages_[i] = false;
    address_space().madvise(address, found->second * kPageSize, MADV_DONTNEED_);
    buffers_.erase(found);
}

int64_t BinderFile::write_commands(int tid, WriteRead& wr) {
    while (wr.write_consumed < wr.write_size) {
        uint32_t command;
        uint64_t at = wr.write_buffer + wr.write_consumed;
        if (wr.write_size - wr.write_consumed < 4 || !guest_read(at, &command, 4)) return -EFAULT_;
        uint32_t length = (command >> 16) & 0x3fff;
        if (((command >> 8) & 0xff) != 'c' || length > 72 || wr.write_size - wr.write_consumed < 4 + length) return -EINVAL_;
        uint8_t payload[72]{};
        if (length && !guest_read(at + 4, payload, length)) return -EFAULT_;
        uint64_t value = 0;
        std::memcpy(&value, payload, std::min<size_t>(length, sizeof value));
        switch (command & 0xff) {
            case 0: case 1: case 17: case 18: {
                bool sg = (command & 0xff) >= 17;
                if (length != (sg ? 72 : 64)) return -EINVAL_;
                uint64_t extra = 0;
                if (sg) std::memcpy(&extra, payload + 64, 8);
                if (extra) { code(tid, br(17)); break; }
                Transaction tr;
                std::memcpy(&tr, payload, sizeof tr);
                int64_t error = transact(tid, tr, (command & 0xff) == 1 || (command & 0xff) == 18);
                if (error) return error;
                break;
            }
            case 3:
                if (length != 8) return -EINVAL_;
                free_buffer(value);
                break;
            case 4: case 5: case 6: case 7:
                if (length != 4) return -EINVAL_;
                // Handle ownership is retained for this open description.
                break;
            case 8: case 9:
                if (length != 16) return -EINVAL_;
                break;
            case 11: case 12: threads_[tid].looper = true; break;
            case 13: threads_[tid].looper = false; break;
            default:
                LOGW("binder", "unsupported command %08x", command);
                return -EINVAL_;
        }
        wr.write_consumed += 4 + length;
    }
    return 0;
}

int64_t BinderFile::read_commands(int tid, WriteRead& wr) {
    auto& state = threads_[tid];
    while (wr.read_size - wr.read_consumed >= 4) {
        std::deque<Message>* queue = !state.inbox.empty() ? &state.inbox : state.looper && !pending_.empty() ? &pending_ : nullptr;
        if (!queue) break;
        Message& m = queue->front();
        bool transaction = m.command == br(2, 64) || m.command == br(3, 64);
        uint64_t size = 4 + (transaction ? sizeof(Transaction) : m.payload.size());
        if (size > wr.read_size - wr.read_consumed) break;
        if (transaction && !materialize(m)) {
            if (auto sender = m.sender.lock()) sender->code(m.sender_tid, br(17));
            queue->pop_front();
            continue;
        }
        uint64_t at = wr.read_buffer + wr.read_consumed;
        if (!guest_write(at, &m.command, 4) ||
            !guest_write(at + 4, transaction ? static_cast<void*>(&m.transaction) : m.payload.data(), size - 4)) {
            if (transaction) free_buffer(m.transaction.data);
            return -EFAULT_;
        }
        if (m.command == br(2, 64) && !(m.transaction.flags & 1)) state.callers.emplace_back(m.sender, m.sender_tid);
        wr.read_consumed += size;
        queue->pop_front();
    }
    return 0;
}

int64_t BinderFile::ioctl(GuestThread& t, uint64_t request, uint64_t arg) {
    if (request == ioc(3, 'b', 9, 4)) {
        int32_t version = 8;
        return guest_write(arg, &version, sizeof version) ? 0 : -EFAULT_;
    }
    if (request == ioc(1, 'b', 5, 4)) {
        uint32_t value;
        if (!guest_read(arg, &value, 4)) return -EFAULT_;
        std::lock_guard<std::mutex> held(domain_->lock);
        max_threads_ = value;
        return 0;
    }
    if (request == ioc(1, 'b', 16, 4)) return 0; // optional oneway diagnostics
    if (request == ioc(1, 'b', 7, 4) || request == ioc(1, 'b', 13, 24)) {
        Object object{};
        if (request == ioc(1, 'b', 13, 24) && !guest_read(arg, &object, sizeof object)) return -EFAULT_;
        std::lock_guard<std::mutex> held(domain_->lock);
        if (!domain_->manager.expired()) return -EBUSY_;
        domain_->manager = self();
        domain_->manager_node = std::make_shared<Node>(Node{self(), object.value, object.cookie});
        return 0;
    }
    if (request == ioc(1, 'b', 8, 4)) {
        std::lock_guard<std::mutex> held(domain_->lock);
        for (auto& caller : threads_[t.tid].callers) if (auto file = caller.first.lock()) file->code(caller.second, br(5));
        threads_.erase(t.tid);
        return 0;
    }
    if (request != ioc(3, 'b', 1, sizeof(WriteRead))) {
        LOGW("binder", "unsupported ioctl %llx", static_cast<unsigned long long>(request));
        return -ENOTTY_;
    }
    WriteRead wr;
    if (!guest_read(arg, &wr, sizeof wr)) return -EFAULT_;
    if (wr.write_consumed > wr.write_size || wr.read_consumed > wr.read_size || wr.write_size > (4 << 20) || wr.read_size > (4 << 20)) return -EINVAL_;
    uint64_t epoch;
    {
        std::lock_guard<std::mutex> held(domain_->lock);
        epoch = flush_epoch_;
        int64_t error = write_commands(t.tid, wr);
        if (error) { guest_write(arg, &wr, sizeof wr); return error; }
    }
    int64_t result = 0;
    if (wr.read_size) {
        auto ready = [&] {
            std::lock_guard<std::mutex> held(domain_->lock);
            auto& state = threads_[t.tid];
            return epoch != flush_epoch_ || !state.inbox.empty() || (state.looper && !pending_.empty());
        };
        waiters_.add(&t);
        if (nonblocking() && !ready()) result = -EAGAIN_;
        else result = block_until(t, ready, 0, "binder");
        waiters_.remove(&t);
        if (!result) {
            std::lock_guard<std::mutex> held(domain_->lock);
            result = read_commands(t.tid, wr);
        }
    }
    if (!guest_write(arg, &wr, sizeof wr)) return -EFAULT_;
    return result;
}
} // namespace

void init_binder() {
    for (const char* name : {"binder", "hwbinder", "vndbinder"}) {
        auto domain = std::make_shared<Domain>();
        register_device(name, [domain] { return std::make_shared<BinderFile>(domain); });
        vfs().add_device(std::string("/dev/") + name, name);
    }
}
} // namespace axrb
