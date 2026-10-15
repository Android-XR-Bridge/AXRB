#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include "net.h"

#include "cpu.h"
#include "log.h"
#include "memory.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace axrb {
using namespace linux_abi;

namespace {

constexpr uint16_t kLinuxAfInet6 = 10;

int64_t wsa_errno(int error) {
    switch (error) {
        case WSAEWOULDBLOCK: return -EAGAIN_;
        case WSAEINPROGRESS: return -EINPROGRESS_;
        case WSAEALREADY: return -EALREADY_;
        case WSAENOTSOCK: return -ENOTSOCK_;
        case WSAEDESTADDRREQ: return -EDESTADDRREQ_;
        case WSAEMSGSIZE: return -EMSGSIZE_;
        case WSAEPROTOTYPE: return -EPROTOTYPE_;
        case WSAENOPROTOOPT: return -ENOPROTOOPT_;
        case WSAEPROTONOSUPPORT: return -EPROTONOSUPPORT_;
        case WSAEOPNOTSUPP: return -EOPNOTSUPP_;
        case WSAEAFNOSUPPORT: return -EAFNOSUPPORT_;
        case WSAEADDRINUSE: return -EADDRINUSE_;
        case WSAEADDRNOTAVAIL: return -EADDRNOTAVAIL_;
        case WSAENETDOWN: return -ENETDOWN_;
        case WSAENETUNREACH: return -ENETUNREACH_;
        case WSAECONNABORTED: return -ECONNABORTED_;
        case WSAECONNRESET: return -ECONNRESET_;
        case WSAENOBUFS: return -ENOBUFS_;
        case WSAEISCONN: return -EISCONN_;
        case WSAENOTCONN: return -ENOTCONN_;
        case WSAESHUTDOWN: return -EPIPE_;
        case WSAETIMEDOUT: return -ETIMEDOUT_;
        case WSAECONNREFUSED: return -ECONNREFUSED_;
        case WSAEHOSTDOWN:
        case WSAEHOSTUNREACH: return -EHOSTUNREACH_;
        case WSAEINVAL: return -EINVAL_;
        case WSAEFAULT: return -EFAULT_;
        case WSAEINTR: return -EINTR_;
        case WSAEACCES: return -EACCES_;
        case WSAEMFILE: return -EMFILE_;
        default: return -EIO_;
    }
}

// Linux sockaddr (guest bytes) -> Windows sockaddr. Families differ only in
// the AF_INET6 value; the structures are laid out alike.
int64_t to_host_address(const uint8_t* bytes, size_t len, sockaddr_storage& out, int& out_len) {
    if (len < 2) return -EINVAL_;
    uint16_t family;
    std::memcpy(&family, bytes, 2);
    std::memset(&out, 0, sizeof out);
    if (family == AF_INET_) {
        if (len < sizeof(sockaddr_in)) return -EINVAL_;
        auto* in = reinterpret_cast<sockaddr_in*>(&out);
        std::memcpy(in, bytes, sizeof(sockaddr_in));
        in->sin_family = AF_INET;
        // The emulator's alias for the host machine.
        uint8_t* a = reinterpret_cast<uint8_t*>(&in->sin_addr);
        if (a[0] == 10 && a[1] == 0 && a[2] == 2 && (a[3] == 2 || a[3] == 15)) {
            a[0] = 127;
            a[1] = 0;
            a[2] = 0;
            a[3] = 1;
        }
        out_len = sizeof(sockaddr_in);
        return 0;
    }
    if (family == kLinuxAfInet6) {
        if (len < 24) return -EINVAL_;
        auto* in6 = reinterpret_cast<sockaddr_in6*>(&out);
        std::memcpy(in6, bytes, std::min(len, sizeof(sockaddr_in6)));
        in6->sin6_family = AF_INET6;
        out_len = sizeof(sockaddr_in6);
        return 0;
    }
    return -EAFNOSUPPORT_;
}

std::string to_guest_address(const sockaddr* address, int len) {
    std::string out;
    if (!address || len <= 0) return out;
    out.assign(reinterpret_cast<const char*>(address), static_cast<size_t>(len));
    uint16_t family = address->sa_family == AF_INET6 ? kLinuxAfInet6 : static_cast<uint16_t>(address->sa_family);
    std::memcpy(out.data(), &family, 2);
    return out;
}

// Writes a sockaddr to (addr, addrlen*) the way the kernel does: truncated to
// the buffer, the full length reported.
int64_t put_address(uint64_t addr, uint64_t len_addr, const std::string& bytes) {
    if (!addr || !len_addr) return 0;
    uint32_t room = 0;
    if (!guest_read(len_addr, &room, 4)) return -EFAULT_;
    if (!guest_write(addr, bytes.data(), std::min<size_t>(room, bytes.size()))) return -EFAULT_;
    uint32_t actual = static_cast<uint32_t>(bytes.size());
    return guest_write(len_addr, &actual, 4) ? 0 : -EFAULT_;
}

int host_msg_flags(int flags) {
    int out = 0;
    if (flags & MSG_OOB_) out |= MSG_OOB;
    if (flags & MSG_PEEK_) out |= MSG_PEEK;
    if (flags & MSG_DONTROUTE_) out |= MSG_DONTROUTE;
    return out;
}

// Network interfaces as SIOCGIF* reports them: loopback plus a "wlan0"
// carrying the host's primary IPv4 address. Online SDKs consult this before
// they try to connect at all.
struct NetInterface {
    const char* name;
    int index;
    uint32_t address;  // network byte order
    uint32_t netmask;
    uint16_t flags;
    uint16_t hw_type;
    uint8_t mac[6];
};

uint32_t primary_ipv4() {
    static const uint32_t address = [] {
        uint32_t found = htonl(0x0A00020F);  // 10.0.2.15, as in the emulator
        SOCKET probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe == INVALID_SOCKET) return found;
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(53);
        target.sin_addr.s_addr = htonl(0x08080808);
        sockaddr_in local{};
        int len = sizeof local;
        // A connected datagram socket sends nothing; it only selects a route.
        if (::connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof target) == 0 &&
            getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) == 0 && local.sin_addr.s_addr)
            found = local.sin_addr.s_addr;
        closesocket(probe);
        return found;
    }();
    return address;
}

const std::vector<NetInterface>& net_interfaces() {
    constexpr uint16_t kUp = 0x1, kBroadcast = 0x2, kLoopback = 0x8, kRunning = 0x40, kMulticast = 0x1000;
    static const std::vector<NetInterface> list = {
        {"lo", 1, htonl(0x7F000001), htonl(0xFF000000), kUp | kLoopback | kRunning, 772, {}},
        {"wlan0", 2, primary_ipv4(), htonl(0xFFFFFF00), kUp | kBroadcast | kRunning | kMulticast, 1,
         {0x02, 0x15, 0xB2, 0x00, 0x00, 0x00}},
    };
    return list;
}

// struct ifreq on arm64: 16-byte name, then a 24-byte union.
struct LinuxIfreq {
    char name[16];
    uint8_t data[24];
};
static_assert(sizeof(LinuxIfreq) == 40);

void put_ifreq_ipv4(LinuxIfreq& req, uint32_t address) {
    std::memset(req.data, 0, sizeof req.data);
    uint16_t family = AF_INET_;
    std::memcpy(req.data, &family, 2);
    std::memcpy(req.data + 4, &address, 4);
}

int64_t interface_ioctl(uint64_t request, uint64_t arg) {
    constexpr uint64_t kGifName = 0x8910, kGifConf = 0x8912, kGifFlags = 0x8913, kGifAddr = 0x8915,
                       kGifBrdAddr = 0x8919, kGifNetmask = 0x891b, kGifMtu = 0x8921, kGifHwAddr = 0x8927,
                       kGifIndex = 0x8933;
    const auto& list = net_interfaces();
    if (request == kGifConf) {
        struct { int32_t len; int32_t pad; uint64_t buf; } conf{};
        if (!guest_read(arg, &conf, sizeof conf)) return -EFAULT_;
        const int32_t needed = static_cast<int32_t>(list.size() * sizeof(LinuxIfreq));
        if (conf.buf == 0) {
            conf.len = needed;
        } else {
            int32_t written = 0;
            for (const auto& item : list) {
                if (written + static_cast<int32_t>(sizeof(LinuxIfreq)) > conf.len) break;
                LinuxIfreq req{};
                std::strncpy(req.name, item.name, sizeof req.name - 1);
                put_ifreq_ipv4(req, item.address);
                if (!guest_write(conf.buf + written, &req, sizeof req)) return -EFAULT_;
                written += sizeof(LinuxIfreq);
            }
            conf.len = written;
        }
        return guest_write(arg, &conf, sizeof conf) ? 0 : -EFAULT_;
    }
    if (request != kGifName && request != kGifFlags && request != kGifAddr && request != kGifBrdAddr &&
        request != kGifNetmask && request != kGifMtu && request != kGifHwAddr && request != kGifIndex)
        return -ENOTTY_;
    LinuxIfreq req{};
    if (!guest_read(arg, &req, sizeof req)) return -EFAULT_;
    const NetInterface* found = nullptr;
    if (request == kGifName) {
        int32_t index;
        std::memcpy(&index, req.data, 4);
        for (const auto& item : list)
            if (item.index == index) found = &item;
        if (!found) return -ENODEV_;
        std::memset(req.name, 0, sizeof req.name);
        std::strncpy(req.name, found->name, sizeof req.name - 1);
        return guest_write(arg, &req, sizeof req) ? 0 : -EFAULT_;
    }
    req.name[sizeof req.name - 1] = 0;
    for (const auto& item : list)
        if (std::strcmp(item.name, req.name) == 0) found = &item;
    if (!found) return -ENODEV_;
    std::memset(req.data, 0, sizeof req.data);
    switch (request) {
        case kGifFlags: std::memcpy(req.data, &found->flags, 2); break;
        case kGifAddr: put_ifreq_ipv4(req, found->address); break;
        case kGifNetmask: put_ifreq_ipv4(req, found->netmask); break;
        case kGifBrdAddr: put_ifreq_ipv4(req, found->address | ~found->netmask); break;
        case kGifMtu: {
            int32_t mtu = found->hw_type == 772 ? 65536 : 1500;
            std::memcpy(req.data, &mtu, 4);
            break;
        }
        case kGifHwAddr:
            std::memcpy(req.data, &found->hw_type, 2);
            std::memcpy(req.data + 2, found->mac, 6);
            break;
        case kGifIndex: std::memcpy(req.data, &found->index, 4); break;
    }
    return guest_write(arg, &req, sizeof req) ? 0 : -EFAULT_;
}

void raise_sigpipe(GuestThread& t) {
    ksiginfo info{};
    info.si_signo = SIGPIPE_;
    info.si_code = SI_USER_;
    send_signal(t, SIGPIPE_, info);
}

}  // namespace

// ---------------------------------------------------------------------------
// Internet sockets on Winsock. The host socket is always non-blocking (event
// select makes it so); blocking calls wait the guest way, interruptibly.

class SocketFile : public OpenFile {
public:
    SocketFile(SOCKET socket, int family, int type, int protocol)
        : socket_(socket), family_(family), type_(type), protocol_(protocol) {
        event_ = WSACreateEvent();
        WSAEventSelect(socket_, event_, FD_READ | FD_WRITE | FD_ACCEPT | FD_CONNECT | FD_CLOSE | FD_OOB);
        RegisterWaitForSingleObject(&wait_, event_, &SocketFile::on_event, this, INFINITE, WT_EXECUTEINWAITTHREAD);
        assign_inode("socket:" + std::to_string(static_cast<uint64_t>(socket_)));
    }
    ~SocketFile() override {
        if (wait_) UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
        closesocket(socket_);
        if (event_) WSACloseEvent(event_);
    }

    SOCKET socket() const { return socket_; }
    int family() const { return family_; }
    int type() const { return type_; }
    int protocol() const { return protocol_; }

    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override {
        kiovec iov{buf, len};
        return receive(t, &iov, 1, 0, 0, 0);
    }
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override {
        kiovec iov{buf, len};
        return send(t, &iov, 1, 0, nullptr, 0);
    }
    int64_t writev(GuestThread& t, const std::vector<kiovec>& vecs) override {
        return send(t, vecs.data(), vecs.size(), 0, nullptr, 0);
    }
    int64_t stat(kstat& st) override {
        std::memset(&st, 0, sizeof st);
        st.st_mode = S_IFSOCK_ | 0777;
        st.st_nlink = 1;
        st.st_ino = inode();
        st.st_blksize = 4096;
        return 0;
    }
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override {
        if (request == FIONREAD_) {
            u_long available = 0;
            if (ioctlsocket(socket_, FIONREAD, &available) != 0) return wsa_errno(WSAGetLastError());
            int32_t value = static_cast<int32_t>(available);
            return guest_write(arg, &value, 4) ? 0 : -EFAULT_;
        }
        if ((request & 0xFF00) == 0x8900) {
            int64_t r = interface_ioctl(request, arg);
            TRACE("net", "ioctl(%llx) = %lld", static_cast<unsigned long long>(request), static_cast<long long>(r));
            return r;
        }
        return OpenFile::ioctl(t, request, arg);
    }
    uint32_t poll_mask() override {
        if (int error = connect_error_.load()) {
            (void)error;
            return POLLERR_ | POLLHUP_ | POLLOUT_ | POLLIN_;
        }
        if (type_ == SOCK_STREAM_ && state_ == State::Fresh) return POLLOUT_ | POLLHUP_;
        WSAPOLLFD p{socket_, POLLRDNORM | POLLWRNORM, 0};
        if (WSAPoll(&p, 1, 0) < 0) return POLLERR_;
        uint32_t mask = 0;
        if (p.revents & (POLLRDNORM | POLLRDBAND)) mask |= POLLIN_;
        if (p.revents & POLLWRNORM) {
            mask |= POLLOUT_;
            if (state_ == State::Connecting) state_ = State::Connected;
        }
        if (p.revents & POLLHUP) mask |= POLLHUP_ | POLLIN_ | POLLRDHUP_;
        if (p.revents & POLLERR) mask |= POLLERR_;
        return mask;
    }
    WaitQueue* wait_queue() override { return &waiters_; }

    int64_t receive(GuestThread& t, const kiovec* iov, size_t count, int flags, uint64_t from, uint64_t from_len,
                    int* out_flags = nullptr) {
        std::vector<WSABUF> buffers;
        for (size_t i = 0; i < count; ++i) {
            if (!iov[i].len) continue;
            if (!address_space().populate(iov[i].base, iov[i].len)) return -EFAULT_;
            buffers.push_back({static_cast<ULONG>(std::min<uint64_t>(iov[i].len, 0x7fffffff)),
                               reinterpret_cast<char*>(iov[i].base)});
        }
        if (buffers.empty()) buffers.push_back({0, nullptr});
        sockaddr_storage address{};
        int64_t r = block(t, (flags & MSG_DONTWAIT_) != 0, rcvtimeo_ns_, POLLIN_, [&]() -> int64_t {
            DWORD got = 0, host_flags = static_cast<DWORD>(host_msg_flags(flags));
            int address_len = sizeof address;
            int rc = WSARecvFrom(socket_, buffers.data(), static_cast<DWORD>(buffers.size()), &got, &host_flags,
                                 from ? reinterpret_cast<sockaddr*>(&address) : nullptr, from ? &address_len : nullptr,
                                 nullptr, nullptr);
            if (rc == 0) {
                if (from) {
                    if (int64_t e = put_address(from, from_len, to_guest_address(reinterpret_cast<sockaddr*>(&address), address_len)))
                        return e;
                }
                return static_cast<int64_t>(got);
            }
            int error = WSAGetLastError();
            if (error == WSAEMSGSIZE) {
                if (out_flags) *out_flags |= MSG_TRUNC_;
                return static_cast<int64_t>(got);
            }
            if (error == WSAESHUTDOWN || error == WSAECONNABORTED) return 0;
            return wsa_errno(error);
        });
        return r;
    }

    int64_t send(GuestThread& t, const kiovec* iov, size_t count, int flags, const sockaddr* to, int to_len) {
        std::vector<WSABUF> buffers;
        for (size_t i = 0; i < count; ++i) {
            if (!iov[i].len) continue;
            if (!address_space().populate(iov[i].base, iov[i].len)) return -EFAULT_;
            buffers.push_back({static_cast<ULONG>(std::min<uint64_t>(iov[i].len, 0x7fffffff)),
                               reinterpret_cast<char*>(iov[i].base)});
        }
        if (buffers.empty()) buffers.push_back({0, nullptr});
        int64_t r = block(t, (flags & MSG_DONTWAIT_) != 0, sndtimeo_ns_, POLLOUT_, [&]() -> int64_t {
            DWORD sent = 0;
            int rc = WSASendTo(socket_, buffers.data(), static_cast<DWORD>(buffers.size()), &sent,
                               static_cast<DWORD>(host_msg_flags(flags)), to, to_len, nullptr, nullptr);
            if (rc == 0) return static_cast<int64_t>(sent);
            return wsa_errno(WSAGetLastError());
        });
        if (r == -EPIPE_ && type_ == SOCK_STREAM_ && !(flags & MSG_NOSIGNAL_)) raise_sigpipe(t);
        return r;
    }

    int64_t connect(GuestThread& t, const sockaddr* address, int len) {
        if (address->sa_family == AF_INET &&
            reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            // Fail fast on a closed local port, as Linux does, instead of
            // Windows' two seconds of SYN retries.
            TCP_INITIAL_RTO_PARAMETERS rto{TCP_INITIAL_RTO_UNSPECIFIED_RTT, TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS};
            DWORD unused = 0;
            WSAIoctl(socket_, SIO_TCP_INITIAL_RTO, &rto, sizeof rto, nullptr, 0, &unused, nullptr, nullptr);
        }
        connect_error_ = 0;
        if (::connect(socket_, address, len) == 0) {
            state_ = State::Connected;
            return 0;
        }
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            if (error == WSAEALREADY) return -EALREADY_;
            return wsa_errno(error);
        }
        state_ = State::Connecting;
        if (nonblocking() || type_ != SOCK_STREAM_) return type_ == SOCK_STREAM_ ? -EINPROGRESS_ : 0;
        waiters_.add(&t);
        int64_t w = block_until(t, [&] { return (poll_mask() & (POLLOUT_ | POLLERR_)) != 0; }, 0, "connect");
        waiters_.remove(&t);
        if (w == -EINTR_) return -EINTR_;
        return take_error();
    }

    // SO_ERROR: the pending asynchronous error, cleared by reading it.
    int64_t take_error() {
        int error = connect_error_.exchange(0);
        if (error) {
            state_ = State::Fresh;
            return wsa_errno(error);
        }
        int value = 0, len = sizeof value;
        getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &len);
        if (value) return wsa_errno(value);
        if (state_ == State::Connecting) state_ = State::Connected;
        return 0;
    }

    void mark_connected() { state_ = State::Connected; }
    void mark_listening() { state_ = State::Listening; }
    bool listening() const { return state_ == State::Listening; }
    void mark_bound() {
        if (state_ == State::Fresh && type_ != SOCK_STREAM_) state_ = State::Connected;
    }

    int64_t rcvtimeo_ns_ = 0;
    int64_t sndtimeo_ns_ = 0;

private:
    enum class State { Fresh, Connecting, Connected, Listening };

    template <class F>
    int64_t block(GuestThread& t, bool dont_wait, int64_t timeout_ns, uint32_t want, F&& attempt) {
        int64_t deadline = timeout_ns ? ticks_now() + timeout_ns * ticks_per_second() / 1000000000 : 0;
        for (;;) {
            int64_t r = attempt();
            if (r != -EAGAIN_) return r;
            if (nonblocking() || dont_wait) return -EAGAIN_;
            waiters_.add(&t);
            int64_t w = block_until(t, [&] { return (poll_mask() & (want | POLLERR_ | POLLHUP_)) != 0; }, deadline, "socket");
            waiters_.remove(&t);
            if (w == -EINTR_) return -EINTR_;
            if (w == -ETIMEDOUT_) return -EAGAIN_;
        }
    }

    static void CALLBACK on_event(void* param, BOOLEAN) {
        auto* self = static_cast<SocketFile*>(param);
        WSANETWORKEVENTS events{};
        if (WSAEnumNetworkEvents(self->socket_, self->event_, &events) == 0) {
            if ((events.lNetworkEvents & FD_CONNECT) && events.iErrorCode[FD_CONNECT_BIT])
                self->connect_error_ = events.iErrorCode[FD_CONNECT_BIT];
        }
        self->waiters_.wake_all();
    }

    SOCKET socket_;
    int family_, type_, protocol_;
    HANDLE event_ = nullptr;
    HANDLE wait_ = nullptr;
    WaitQueue waiters_;
    std::atomic<int> connect_error_{0};
    std::atomic<State> state_{State::Fresh};
};

// ---------------------------------------------------------------------------
// Host services behind AF_UNIX paths.

int64_t ServiceSocket::read(GuestThread& t, uint64_t buf, uint64_t len) {
    auto ready = [&] {
        std::lock_guard<std::mutex> held(lock_);
        return !pending_.empty() || finished_;
    };
    if (!ready()) {
        if (nonblocking()) return -EAGAIN_;
        waiters_.add(&t);
        int64_t w = block_until(t, ready, 0, "service");
        waiters_.remove(&t);
        if (w) return w;
    }
    std::lock_guard<std::mutex> held(lock_);
    size_t n = std::min<size_t>(pending_.size(), static_cast<size_t>(len));
    if (n && !guest_write(buf, pending_.data(), n)) return -EFAULT_;
    pending_.erase(0, n);
    return static_cast<int64_t>(n);
}

int64_t ServiceSocket::write(GuestThread&, uint64_t buf, uint64_t len) {
    std::string message(static_cast<size_t>(len), '\0');
    if (len && !guest_read(buf, message.data(), message.size())) return -EFAULT_;
    return send_message(message);
}

int64_t ServiceSocket::writev(GuestThread&, const std::vector<kiovec>& vecs) {
    std::string message;
    for (auto& v : vecs) {
        size_t at = message.size();
        message.resize(at + static_cast<size_t>(v.len));
        if (v.len && !guest_read(v.base, message.data() + at, static_cast<size_t>(v.len))) return -EFAULT_;
    }
    return send_message(message);
}

int64_t ServiceSocket::send_message(std::string_view message) {
    {
        std::lock_guard<std::mutex> held(lock_);
        if (finished_) return -EPIPE_;
    }
    if (handler_) handler_(*this, message);
    return static_cast<int64_t>(message.size());
}

void ServiceSocket::reply(const void* data, size_t len) {
    {
        std::lock_guard<std::mutex> held(lock_);
        pending_.append(static_cast<const char*>(data), len);
    }
    waiters_.wake_all();
}

void ServiceSocket::finish() {
    {
        std::lock_guard<std::mutex> held(lock_);
        finished_ = true;
    }
    waiters_.wake_all();
}

int64_t ServiceSocket::stat(kstat& st) {
    std::memset(&st, 0, sizeof st);
    st.st_mode = S_IFSOCK_ | 0777;
    st.st_nlink = 1;
    st.st_blksize = 4096;
    return 0;
}

uint32_t ServiceSocket::poll_mask() {
    std::lock_guard<std::mutex> held(lock_);
    uint32_t mask = finished_ ? 0 : POLLOUT_;
    if (!pending_.empty()) mask |= POLLIN_;
    if (finished_) mask |= POLLIN_ | POLLHUP_;
    return mask;
}

// ---------------------------------------------------------------------------
// AF_UNIX sockets between guest threads (and to host services).

class UnixSocketFile;

namespace {
std::mutex g_unix_lock;
std::map<std::string, std::weak_ptr<UnixSocketFile>> g_listeners;
std::map<std::string, std::function<ServiceSocket::Handler()>> g_services;
}  // namespace

class UnixSocketFile : public OpenFile {
public:
    explicit UnixSocketFile(int type) : type_(type) { assign_inode("unix:" + std::to_string(reinterpret_cast<uint64_t>(this))); }
    ~UnixSocketFile() override {
        if (!bound_.empty()) {
            std::lock_guard<std::mutex> held(g_unix_lock);
            auto it = g_listeners.find(bound_);
            if (it != g_listeners.end() && it->second.expired()) g_listeners.erase(it);
        }
    }

    int type() const { return type_; }
    std::shared_ptr<OpenFile> peer() {
        std::lock_guard<std::mutex> held(lock_);
        return peer_;
    }
    void set_peer(std::shared_ptr<OpenFile> peer, std::string peer_path) {
        std::lock_guard<std::mutex> held(lock_);
        peer_ = std::move(peer);
        peer_path_ = std::move(peer_path);
    }
    const std::string& bound() const { return bound_; }
    std::string peer_path() {
        std::lock_guard<std::mutex> held(lock_);
        return peer_path_;
    }

    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override {
        auto p = peer();
        if (!p) return -ENOTCONN_;
        p->status_flags = (p->status_flags & ~O_NONBLOCK_) | (status_flags & O_NONBLOCK_);
        return p->read(t, buf, len);
    }
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override {
        auto p = peer();
        if (!p) return type_ == SOCK_STREAM_ ? -ENOTCONN_ : -EDESTADDRREQ_;
        p->status_flags = (p->status_flags & ~O_NONBLOCK_) | (status_flags & O_NONBLOCK_);
        return p->write(t, buf, len);
    }
    int64_t writev(GuestThread& t, const std::vector<kiovec>& vecs) override {
        auto p = peer();
        if (!p) return type_ == SOCK_STREAM_ ? -ENOTCONN_ : -EDESTADDRREQ_;
        return p->writev(t, vecs);
    }
    int64_t stat(kstat& st) override {
        std::memset(&st, 0, sizeof st);
        st.st_mode = S_IFSOCK_ | 0777;
        st.st_nlink = 1;
        st.st_ino = inode();
        st.st_blksize = 4096;
        return 0;
    }
    int64_t ioctl(GuestThread& t, uint64_t request, uint64_t arg) override {
        if (auto p = peer()) return p->ioctl(t, request, arg);
        return OpenFile::ioctl(t, request, arg);
    }
    uint32_t poll_mask() override {
        if (auto p = peer()) return p->poll_mask();
        std::lock_guard<std::mutex> held(lock_);
        if (listening_) return backlog_.empty() ? 0 : POLLIN_;
        return POLLOUT_ | POLLHUP_;
    }
    WaitQueue* wait_queue() override {
        if (auto p = peer()) return p->wait_queue();
        return &waiters_;
    }

    int64_t bind(const std::string& path, const std::shared_ptr<UnixSocketFile>& self) {
        std::lock_guard<std::mutex> held(g_unix_lock);
        auto it = g_listeners.find(path);
        if (it != g_listeners.end() && !it->second.expired()) return -EADDRINUSE_;
        g_listeners[path] = self;
        bound_ = path;
        return 0;
    }
    int64_t listen() {
        if (bound_.empty()) return -EINVAL_;
        std::lock_guard<std::mutex> held(lock_);
        listening_ = true;
        return 0;
    }
    void enqueue(std::shared_ptr<UnixSocketFile> connection) {
        {
            std::lock_guard<std::mutex> held(lock_);
            backlog_.push_back(std::move(connection));
        }
        waiters_.wake_all();
    }
    bool is_listening() {
        std::lock_guard<std::mutex> held(lock_);
        return listening_;
    }
    int64_t accept(GuestThread& t, std::shared_ptr<UnixSocketFile>& out) {
        auto take = [&] {
            std::lock_guard<std::mutex> held(lock_);
            if (backlog_.empty()) return false;
            out = backlog_.front();
            backlog_.pop_front();
            return true;
        };
        if (!is_listening()) return -EINVAL_;
        if (take()) return 0;
        if (nonblocking()) return -EAGAIN_;
        waiters_.add(&t);
        int64_t w = block_until(t, take, 0, "accept");
        waiters_.remove(&t);
        return w;
    }

private:
    int type_;
    std::mutex lock_;
    std::shared_ptr<OpenFile> peer_;
    std::string peer_path_;
    std::string bound_;
    bool listening_ = false;
    std::deque<std::shared_ptr<UnixSocketFile>> backlog_;
    WaitQueue waiters_;
};

void register_unix_service(const std::string& path, std::function<ServiceSocket::Handler()> make) {
    std::lock_guard<std::mutex> held(g_unix_lock);
    g_services[path] = std::move(make);
}

namespace {

// sockaddr_un -> a registry key: a path, or '@' + an abstract name.
int64_t unix_path(uint64_t addr, uint32_t len, std::string& out) {
    if (len < 2 || len > 110) return -EINVAL_;
    char bytes[110] = {};
    if (!guest_read(addr, bytes, len)) return -EFAULT_;
    uint16_t family;
    std::memcpy(&family, bytes, 2);
    if (family != AF_UNIX_) return -EAFNOSUPPORT_;
    if (len == 2) return -EINVAL_;
    if (bytes[2] == 0) {
        out = "@" + std::string(bytes + 3, len - 3);
        while (!out.empty() && out.back() == 0) out.pop_back();
        return 0;
    }
    out.assign(bytes + 2, strnlen(bytes + 2, len - 2));
    out = Vfs::normalize(process().cwd, out);
    return 0;
}

std::string unix_address(const std::string& path) {
    std::string out(2, '\0');
    uint16_t family = AF_UNIX_;
    std::memcpy(out.data(), &family, 2);
    if (path.empty()) return out;
    if (path[0] == '@') {
        out.push_back('\0');
        out += path.substr(1);
    } else {
        out += path;
        out.push_back('\0');
    }
    return out;
}

int64_t unix_connect(GuestThread&, const std::shared_ptr<UnixSocketFile>& self, const std::string& path) {
    if (self->peer()) return -EISCONN_;
    std::function<ServiceSocket::Handler()> service;
    std::shared_ptr<UnixSocketFile> listener;
    {
        std::lock_guard<std::mutex> held(g_unix_lock);
        if (auto it = g_services.find(path); it != g_services.end()) service = it->second;
        else if (auto at = g_listeners.find(path); at != g_listeners.end()) listener = at->second.lock();
    }
    if (service) {
        self->set_peer(std::make_shared<ServiceSocket>(service()), path);
        TRACE("net", "unix connect %s -> host service", path.c_str());
        return 0;
    }
    if (!listener || !listener->is_listening()) {
        TRACE("net", "unix connect %s refused", path.c_str());
        return path[0] == '@' ? -ECONNREFUSED_ : -ENOENT_;
    }
    if (listener->type() != self->type()) return -EPROTOTYPE_;
    auto a = std::make_shared<PipeBuffer>();
    auto b = std::make_shared<PipeBuffer>();
    auto client_end = std::make_shared<PipeFile>(b, a, true);
    auto server_end = std::make_shared<PipeFile>(a, b, true);
    auto accepted = std::make_shared<UnixSocketFile>(self->type());
    accepted->set_peer(server_end, self->bound());
    self->set_peer(client_end, path);
    listener->enqueue(accepted);
    TRACE("net", "unix connect %s", path.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// Built-in services.

// /dev/socket/logdw: liblog's datagrams become log lines.
ServiceSocket::Handler logd_writer() {
    return [](ServiceSocket&, std::string_view message) {
        // android_log_header_t: id u8, tid u16, realtime {sec u32, nsec u32}
        if (message.size() < 11) return;
        uint8_t id = static_cast<uint8_t>(message[0]);
        uint16_t tid;
        std::memcpy(&tid, message.data() + 1, 2);
        std::string_view body = message.substr(11);
        if (id == 2 || id == 5 || id == 6 || body.size() < 2) return;  // binary event/stats/security buffers
        static const char kLevels[] = "??VDIWEFS";
        uint8_t priority = static_cast<uint8_t>(body[0]);
        std::string_view rest = body.substr(1);
        size_t tag_end = rest.find('\0');
        if (tag_end == std::string_view::npos) return;
        std::string_view tag = rest.substr(0, tag_end);
        std::string_view text = rest.substr(tag_end + 1);
        while (!text.empty() && (text.back() == '\0' || text.back() == '\n')) text.remove_suffix(1);
        char prefix[96];
        int n = std::snprintf(prefix, sizeof prefix, "%c/%.*s(%5u): ", priority < 9 ? kLevels[priority] : '?',
                              static_cast<int>(std::min<size_t>(tag.size(), 48)), tag.data(), tid);
        // One log line per text line, like logcat.
        size_t at = 0;
        do {
            size_t nl = text.find('\n', at);
            std::string_view line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
            std::string out(prefix, static_cast<size_t>(n));
            out.append(line);
            out.push_back('\n');
            log_raw(out.data(), out.size());
            at = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        } while (at <= text.size());
    };
}

void put_be32(std::string& out, uint32_t value) {
    uint32_t be = htonl(value);
    out.append(reinterpret_cast<const char*>(&be), 4);
}

// /dev/socket/dnsproxyd: netd's resolver protocol, answered by the host
// resolver. Only getaddrinfo is served; other requests fail cleanly.
ServiceSocket::Handler dns_proxy() {
    auto buffer = std::make_shared<std::string>();
    return [buffer](ServiceSocket& socket, std::string_view message) {
        buffer->append(message);
        size_t end = buffer->find('\0');
        if (end == std::string::npos) return;
        std::string command = buffer->substr(0, end);
        buffer->erase(0, end + 1);
        char host[1025] = {}, service[256] = {};
        int flags = 0, family = 0, socktype = 0, protocol = 0;
        unsigned netid = 0;
        std::string response;
        if (std::sscanf(command.c_str(), "getaddrinfo %1024s %255s %d %d %d %d %u", host, service, &flags, &family,
                        &socktype, &protocol, &netid) >= 6) {
            addrinfo hints{};
            hints.ai_flags = flags == -1 ? 0 : (flags & (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST));
            hints.ai_family = family == -1 ? AF_UNSPEC : (family == kLinuxAfInet6 ? AF_INET6 : family);
            hints.ai_socktype = socktype == -1 ? 0 : socktype;
            hints.ai_protocol = protocol == -1 ? 0 : protocol;
            addrinfo* result = nullptr;
            const char* node = std::strcmp(host, "^") == 0 ? nullptr : host;
            const char* serv = std::strcmp(service, "^") == 0 ? nullptr : service;
            int rc = getaddrinfo(node, serv, &hints, &result);
            LOGD("net", "dns %s -> %d", node ? node : "(null)", rc);
            if (rc != 0) {
                response.assign("401", 4);
                put_be32(response, 4);
                put_be32(response, 7);  // EAI_NODATA
            } else {
                response.assign("222", 4);
                for (addrinfo* ai = result; ai; ai = ai->ai_next) {
                    std::string address = to_guest_address(ai->ai_addr, static_cast<int>(ai->ai_addrlen));
                    put_be32(response, 1);
                    put_be32(response, static_cast<uint32_t>(ai->ai_flags));
                    put_be32(response, ai->ai_family == AF_INET6 ? kLinuxAfInet6 : static_cast<uint32_t>(ai->ai_family));
                    put_be32(response, static_cast<uint32_t>(ai->ai_socktype));
                    put_be32(response, static_cast<uint32_t>(ai->ai_protocol));
                    put_be32(response, static_cast<uint32_t>(address.size()));
                    response += address;
                    if (ai->ai_canonname) {
                        put_be32(response, static_cast<uint32_t>(std::strlen(ai->ai_canonname) + 1));
                        response.append(ai->ai_canonname, std::strlen(ai->ai_canonname) + 1);
                    } else {
                        put_be32(response, 0);
                    }
                }
                put_be32(response, 0);
                freeaddrinfo(result);
            }
        } else {
            LOGD("net", "dnsproxyd: unsupported request '%s'", command.c_str());
            response.assign("500", 4);
            put_be32(response, 4);
            put_be32(response, 7);
        }
        socket.reply(response.data(), response.size());
        socket.finish();
    };
}

// ---------------------------------------------------------------------------
// Socket options.

int64_t set_option(SocketFile& s, int level, int name, uint64_t value_addr, uint32_t len) {
    int32_t value = 0;
    if (len >= 4) guest_read(value_addr, &value, 4);
    auto host_set = [&](int host_level, int host_name, const void* data, int size) -> int64_t {
        if (setsockopt(s.socket(), host_level, host_name, static_cast<const char*>(data), size) != 0)
            return wsa_errno(WSAGetLastError());
        return 0;
    };
    if (level == SOL_SOCKET_) {
        switch (name) {
            case SO_KEEPALIVE_: return host_set(SOL_SOCKET, SO_KEEPALIVE, &value, 4);
            case SO_BROADCAST_: return host_set(SOL_SOCKET, SO_BROADCAST, &value, 4);
            case SO_SNDBUF_: return host_set(SOL_SOCKET, SO_SNDBUF, &value, 4);
            case SO_RCVBUF_: return host_set(SOL_SOCKET, SO_RCVBUF, &value, 4);
            case SO_OOBINLINE_: return host_set(SOL_SOCKET, SO_OOBINLINE, &value, 4);
            case SO_LINGER_: {
                klinger l{};
                if (!guest_read(value_addr, &l, sizeof l)) return -EFAULT_;
                linger host{static_cast<u_short>(l.l_onoff), static_cast<u_short>(l.l_linger)};
                return host_set(SOL_SOCKET, SO_LINGER, &host, sizeof host);
            }
            case SO_RCVTIMEO_:
            case SO_SNDTIMEO_: {
                ktimeval tv{};
                if (!guest_read(value_addr, &tv, sizeof tv)) return -EFAULT_;
                int64_t ns = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
                (name == SO_RCVTIMEO_ ? s.rcvtimeo_ns_ : s.sndtimeo_ns_) = ns;
                return 0;
            }
            default:
                // SO_REUSEADDR (Windows' version would allow port stealing),
                // SO_REUSEPORT, SO_PASSCRED, priorities: accepted.
                return 0;
        }
    }
    if (level == IPPROTO_TCP_) {
        switch (name) {
            case TCP_NODELAY_: return host_set(IPPROTO_TCP, TCP_NODELAY, &value, 4);
            case TCP_KEEPIDLE_: return host_set(IPPROTO_TCP, TCP_KEEPIDLE, &value, 4);
            case TCP_KEEPINTVL_: return host_set(IPPROTO_TCP, TCP_KEEPINTVL, &value, 4);
            case TCP_KEEPCNT_: return host_set(IPPROTO_TCP, TCP_KEEPCNT, &value, 4);
            default: return 0;
        }
    }
    if (level == IPPROTO_IPV6_ && name == 26) return host_set(IPPROTO_IPV6, IPV6_V6ONLY, &value, 4);
    return 0;
}

int64_t get_option(SocketFile& s, int level, int name, uint64_t value_addr, uint64_t len_addr) {
    uint32_t room = 0;
    if (!guest_read(len_addr, &room, 4)) return -EFAULT_;
    auto put = [&](const void* data, uint32_t size) -> int64_t {
        if (!guest_write(value_addr, data, std::min(room, size))) return -EFAULT_;
        uint32_t actual = std::min(room, size);
        return guest_write(len_addr, &actual, 4) ? 0 : -EFAULT_;
    };
    auto host_int = [&](int host_level, int host_name) -> int64_t {
        int value = 0, size = sizeof value;
        if (getsockopt(s.socket(), host_level, host_name, reinterpret_cast<char*>(&value), &size) != 0)
            return wsa_errno(WSAGetLastError());
        int32_t v = value;
        return put(&v, 4);
    };
    if (level == SOL_SOCKET_) {
        switch (name) {
            case SO_ERROR_: {
                int32_t error = static_cast<int32_t>(-s.take_error());
                return put(&error, 4);
            }
            case SO_TYPE_: {
                int32_t v = s.type();
                return put(&v, 4);
            }
            case SO_DOMAIN_: {
                int32_t v = s.family();
                return put(&v, 4);
            }
            case SO_PROTOCOL_: {
                int32_t v = s.protocol() ? s.protocol() : (s.type() == SOCK_STREAM_ ? IPPROTO_TCP_ : IPPROTO_UDP_);
                return put(&v, 4);
            }
            case SO_ACCEPTCONN_: {
                int32_t v = s.listening() ? 1 : 0;
                return put(&v, 4);
            }
            case SO_SNDBUF_: return host_int(SOL_SOCKET, SO_SNDBUF);
            case SO_RCVBUF_: return host_int(SOL_SOCKET, SO_RCVBUF);
            case SO_KEEPALIVE_: return host_int(SOL_SOCKET, SO_KEEPALIVE);
            case SO_RCVTIMEO_:
            case SO_SNDTIMEO_: {
                int64_t ns = name == SO_RCVTIMEO_ ? s.rcvtimeo_ns_ : s.sndtimeo_ns_;
                ktimeval tv{ns / 1000000000, (ns % 1000000000) / 1000};
                return put(&tv, sizeof tv);
            }
            default: {
                int32_t zero = 0;
                return put(&zero, 4);
            }
        }
    }
    if (level == IPPROTO_TCP_ && name == TCP_NODELAY_) return host_int(IPPROTO_TCP, TCP_NODELAY);
    int32_t zero = 0;
    return put(&zero, 4);
}

int64_t read_iovecs(uint64_t addr, uint64_t count, std::vector<kiovec>& out) {
    if (count > 1024) return -EINVAL_;
    out.resize(static_cast<size_t>(count));
    if (count && !guest_read(addr, out.data(), out.size() * sizeof(kiovec))) return -EFAULT_;
    return 0;
}

int64_t send_message(GuestThread& t, int fd, const kmsghdr& msg, const std::vector<kiovec>& iov, int flags) {
    auto file = fds().get(fd);
    if (!file) return -EBADF_;
    if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
        sockaddr_storage to{};
        int to_len = 0;
        if (msg.msg_name && msg.msg_namelen) {
            std::vector<uint8_t> bytes(msg.msg_namelen);
            if (!guest_read(msg.msg_name, bytes.data(), bytes.size())) return -EFAULT_;
            if (int64_t e = to_host_address(bytes.data(), bytes.size(), to, to_len)) return e;
        }
        return s->send(t, iov.data(), iov.size(), flags, to_len ? reinterpret_cast<sockaddr*>(&to) : nullptr, to_len);
    }
    std::shared_ptr<OpenFile> target = file;
    if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) {
        target = u->peer();
        if (!target && msg.msg_name && msg.msg_namelen) {
            // An unconnected datagram socket sending to a path.
            std::string path;
            if (int64_t e = unix_path(msg.msg_name, msg.msg_namelen, path)) return e;
            auto temp = std::make_shared<UnixSocketFile>(u->type());
            if (int64_t e = unix_connect(t, temp, path)) return e;
            target = temp->peer();
        }
        if (!target) return -ENOTCONN_;
    }
    return target->writev(t, iov);
}

// Receives into `iov`; the source address goes to (name, name_len_addr) when
// both are given (zero length for AF_UNIX peers).
int64_t receive_message(GuestThread& t, int fd, std::vector<kiovec>& iov, int flags, uint64_t name,
                        uint64_t name_len_addr, int& out_flags) {
    auto file = fds().get(fd);
    if (!file) return -EBADF_;
    int64_t r;
    if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
        r = s->receive(t, iov.data(), iov.size(), flags, name, name ? name_len_addr : 0, &out_flags);
    } else {
        std::shared_ptr<OpenFile> source = file;
        if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) {
            source = u->peer();
            if (!source) return -ENOTCONN_;
        }
        uint32_t saved = source->status_flags;
        if (flags & MSG_DONTWAIT_) source->status_flags |= O_NONBLOCK_;
        r = 0;
        for (size_t i = 0; i < iov.size(); ++i) {
            if (!iov[i].len) continue;
            if (r > 0 && !(source->poll_mask() & POLLIN_)) break;
            int64_t got = source->read(t, iov[i].base, iov[i].len);
            if (got < 0) {
                if (!r) r = got;
                break;
            }
            r += got;
            if (static_cast<uint64_t>(got) < iov[i].len) break;
        }
        source->status_flags = saved;
        uint32_t zero = 0;
        if (name && name_len_addr) guest_write(name_len_addr, &zero, 4);
    }
    return r;
}

int64_t do_sendmsg(GuestThread& t, int fd, uint64_t msg_addr, int flags) {
    kmsghdr msg{};
    if (!guest_read(msg_addr, &msg, sizeof msg)) return -EFAULT_;
    std::vector<kiovec> iov;
    if (int64_t e = read_iovecs(msg.msg_iov, msg.msg_iovlen, iov)) return e;
    return send_message(t, fd, msg, iov, flags);
}

int64_t do_recvmsg(GuestThread& t, int fd, uint64_t msg_addr, int flags) {
    kmsghdr msg{};
    if (!guest_read(msg_addr, &msg, sizeof msg)) return -EFAULT_;
    std::vector<kiovec> iov;
    if (int64_t e = read_iovecs(msg.msg_iov, msg.msg_iovlen, iov)) return e;
    int out_flags = 0;
    int64_t r = receive_message(t, fd, iov, flags, msg.msg_name, msg_addr + offsetof(kmsghdr, msg_namelen), out_flags);
    if (r < 0) return r;
    uint64_t no_control = 0;
    guest_write(msg_addr + offsetof(kmsghdr, msg_controllen), &no_control, 8);
    guest_write(msg_addr + offsetof(kmsghdr, msg_flags), &out_flags, 4);
    return r;
}

}  // namespace

void net_init() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) LOGE("net", "WSAStartup failed");
    register_unix_service("/dev/socket/logdw", logd_writer);
    register_unix_service("/dev/socket/dnsproxyd", dns_proxy);
}

int64_t sys_net(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    int fd = static_cast<int>(static_cast<int32_t>(a[0]));
    switch (nr) {
        case 198: {  // socket(domain, type, protocol)
            int domain = static_cast<int>(a[0]);
            int type = static_cast<int>(a[1]) & 0xf;
            bool nonblock = (a[1] & SOCK_NONBLOCK_) != 0, cloexec = (a[1] & SOCK_CLOEXEC_) != 0;
            int protocol = static_cast<int>(a[2]);
            std::shared_ptr<OpenFile> file;
            if (domain == AF_UNIX_) {
                if (type != SOCK_STREAM_ && type != SOCK_DGRAM_ && type != SOCK_SEQPACKET_) return -EINVAL_;
                file = std::make_shared<UnixSocketFile>(type);
            } else if (domain == AF_INET_ || domain == AF_INET6_) {
                if (type != SOCK_STREAM_ && type != SOCK_DGRAM_) return -EPROTONOSUPPORT_;
                SOCKET s = ::socket(domain == AF_INET6_ ? AF_INET6 : AF_INET, type == SOCK_STREAM_ ? SOCK_STREAM : SOCK_DGRAM,
                                    protocol);
                if (s == INVALID_SOCKET) return wsa_errno(WSAGetLastError());
                file = std::make_shared<SocketFile>(s, domain, type, protocol);
            } else {
                TRACE("net", "socket(%d, %d) unsupported", domain, type);
                return -EAFNOSUPPORT_;
            }
            file->status_flags = O_RDWR_ | (nonblock ? O_NONBLOCK_ : 0);
            int r = fds().install(file, cloexec);
            TRACE("net", "socket(%d, %d, %d) = %d", domain, type, protocol, r);
            return r;
        }
        case 199: {  // socketpair
            if (a[0] != AF_UNIX_) return -EAFNOSUPPORT_;
            auto x = std::make_shared<PipeBuffer>();
            auto y = std::make_shared<PipeBuffer>();
            auto one = std::make_shared<PipeFile>(x, y, true);
            auto two = std::make_shared<PipeFile>(y, x, true);
            uint32_t flags = (a[1] & SOCK_NONBLOCK_) ? O_NONBLOCK_ : 0;
            one->status_flags = two->status_flags = O_RDWR_ | flags;
            bool cloexec = (a[1] & SOCK_CLOEXEC_) != 0;
            int32_t pair[2] = {fds().install(one, cloexec), fds().install(two, cloexec)};
            if (!guest_write(a[3], pair, sizeof pair)) return -EFAULT_;
            return 0;
        }
        case 200:    // bind
        case 203: {  // connect
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                uint32_t len = static_cast<uint32_t>(std::min<uint64_t>(a[2], 128));
                std::vector<uint8_t> bytes(len);
                if (!guest_read(a[1], bytes.data(), len)) return -EFAULT_;
                sockaddr_storage address{};
                int address_len = 0;
                if (int64_t e = to_host_address(bytes.data(), len, address, address_len)) return e;
                if (nr == 200) {
                    if (::bind(s->socket(), reinterpret_cast<sockaddr*>(&address), address_len) != 0)
                        return wsa_errno(WSAGetLastError());
                    s->mark_bound();
                    return 0;
                }
                int64_t r = s->connect(t, reinterpret_cast<sockaddr*>(&address), address_len);
                TRACE("net", "connect(%d) = %lld", fd, (long long)r);
                return r;
            }
            auto unix_file = std::dynamic_pointer_cast<UnixSocketFile>(file);
            if (!unix_file) return -ENOTSOCK_;
            std::string path;
            if (int64_t e = unix_path(a[1], static_cast<uint32_t>(a[2]), path)) return e;
            if (nr == 200) return unix_file->bind(path, unix_file);
            return unix_connect(t, unix_file, path);
        }
        case 201: {  // listen
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                if (::listen(s->socket(), static_cast<int>(a[1]) > 0 ? static_cast<int>(a[1]) : SOMAXCONN) != 0)
                    return wsa_errno(WSAGetLastError());
                s->mark_listening();
                return 0;
            }
            if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) return u->listen();
            return -ENOTSOCK_;
        }
        case 202:    // accept
        case 242: {  // accept4
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            int flags = nr == 242 ? static_cast<int>(a[3]) : 0;
            std::shared_ptr<OpenFile> accepted;
            std::string peer_address;
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                if (!s->listening()) return -EINVAL_;
                for (;;) {
                    sockaddr_storage address{};
                    int len = sizeof address;
                    SOCKET c = ::accept(s->socket(), reinterpret_cast<sockaddr*>(&address), &len);
                    if (c != INVALID_SOCKET) {
                        auto connection = std::make_shared<SocketFile>(c, s->family(), s->type(), s->protocol());
                        connection->mark_connected();
                        accepted = connection;
                        peer_address = to_guest_address(reinterpret_cast<sockaddr*>(&address), len);
                        break;
                    }
                    int error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK) return wsa_errno(error);
                    if (s->nonblocking()) return -EAGAIN_;
                    s->wait_queue()->add(&t);
                    int64_t w = block_until(t, [&] { return (s->poll_mask() & POLLIN_) != 0; }, 0, "accept");
                    s->wait_queue()->remove(&t);
                    if (w) return w;
                }
            } else if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) {
                std::shared_ptr<UnixSocketFile> connection;
                if (int64_t e = u->accept(t, connection)) return e;
                peer_address = unix_address(connection->peer_path());
                accepted = connection;
            } else {
                return -ENOTSOCK_;
            }
            accepted->status_flags = O_RDWR_ | ((flags & SOCK_NONBLOCK_) ? O_NONBLOCK_ : 0);
            if (a[1]) put_address(a[1], a[2], peer_address);
            return fds().install(accepted, (flags & SOCK_CLOEXEC_) != 0);
        }
        case 204:    // getsockname
        case 205: {  // getpeername
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                sockaddr_storage address{};
                int len = sizeof address;
                int rc = nr == 204 ? getsockname(s->socket(), reinterpret_cast<sockaddr*>(&address), &len)
                                   : getpeername(s->socket(), reinterpret_cast<sockaddr*>(&address), &len);
                if (rc != 0) return wsa_errno(WSAGetLastError());
                return put_address(a[1], a[2], to_guest_address(reinterpret_cast<sockaddr*>(&address), len));
            }
            if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) {
                if (nr == 205 && !u->peer()) return -ENOTCONN_;
                return put_address(a[1], a[2], unix_address(nr == 204 ? u->bound() : u->peer_path()));
            }
            if (auto* p = dynamic_cast<PipeFile*>(file.get()); p && p->is_socket()) return put_address(a[1], a[2], unix_address(""));
            return -ENOTSOCK_;
        }
        case 206: {  // sendto(fd, buf, len, flags, addr, addrlen)
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            kiovec iov{a[1], a[2]};
            int flags = static_cast<int>(a[3]);
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                sockaddr_storage to{};
                int to_len = 0;
                if (a[4] && a[5]) {
                    std::vector<uint8_t> bytes(static_cast<size_t>(std::min<uint64_t>(a[5], 128)));
                    if (!guest_read(a[4], bytes.data(), bytes.size())) return -EFAULT_;
                    if (int64_t e = to_host_address(bytes.data(), bytes.size(), to, to_len)) return e;
                }
                return s->send(t, &iov, 1, flags, to_len ? reinterpret_cast<sockaddr*>(&to) : nullptr, to_len);
            }
            // Everything else through the sendmsg path.
            kmsghdr msg{};
            msg.msg_name = a[4];
            msg.msg_namelen = static_cast<uint32_t>(a[5]);
            return send_message(t, fd, msg, {iov}, flags);
        }
        case 207: {  // recvfrom(fd, buf, len, flags, addr, addrlen)
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            kiovec iov{a[1], a[2]};
            int flags = static_cast<int>(a[3]);
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) return s->receive(t, &iov, 1, flags, a[4], a[5]);
            std::vector<kiovec> vec{iov};
            int out_flags = 0;
            return receive_message(t, fd, vec, flags, a[4], a[5], out_flags);
        }
        case 211: return do_sendmsg(t, fd, a[1], static_cast<int>(a[2]));  // sendmsg
        case 212: return do_recvmsg(t, fd, a[1], static_cast<int>(a[2]));  // recvmsg
        case 269: {  // sendmmsg(fd, msgvec, vlen, flags): struct mmsghdr { msghdr; u32 len; pad }
            uint64_t count = std::min<uint64_t>(a[2], 1024);
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t entry = a[1] + i * 64;
                int64_t r = do_sendmsg(t, fd, entry, static_cast<int>(a[3]));
                if (r < 0) return i ? static_cast<int64_t>(i) : r;
                uint32_t len = static_cast<uint32_t>(r);
                guest_write(entry + 56, &len, 4);
            }
            return static_cast<int64_t>(count);
        }
        case 243: {  // recvmmsg
            uint64_t count = std::min<uint64_t>(a[2], 1024);
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t entry = a[1] + i * 64;
                int64_t r = do_recvmsg(t, fd, entry, static_cast<int>(a[3]) | (i ? MSG_DONTWAIT_ : 0));
                if (r < 0) return i ? static_cast<int64_t>(i) : r;
                uint32_t len = static_cast<uint32_t>(r);
                guest_write(entry + 56, &len, 4);
            }
            return static_cast<int64_t>(count);
        }
        case 208: {  // setsockopt(fd, level, name, value, len)
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            if (auto* s = dynamic_cast<SocketFile*>(file.get()))
                return set_option(*s, static_cast<int>(a[1]), static_cast<int>(a[2]), a[3], static_cast<uint32_t>(a[4]));
            return 0;
        }
        case 209: {  // getsockopt(fd, level, name, value, len*)
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            if (auto* s = dynamic_cast<SocketFile*>(file.get()))
                return get_option(*s, static_cast<int>(a[1]), static_cast<int>(a[2]), a[3], a[4]);
            uint32_t room = 0;
            if (!guest_read(a[4], &room, 4)) return -EFAULT_;
            if (a[1] == SOL_SOCKET_ && a[2] == SO_PEERCRED_) {
                int32_t cred[3] = {process().pid, static_cast<int32_t>(process().uid), static_cast<int32_t>(process().gid)};
                uint32_t n = std::min<uint32_t>(room, sizeof cred);
                guest_write(a[3], cred, n);
                return guest_write(a[4], &n, 4) ? 0 : -EFAULT_;
            }
            int32_t value = 0;
            if (a[1] == SOL_SOCKET_ && a[2] == SO_TYPE_) {
                if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) value = u->type();
                else value = SOCK_STREAM_;
            } else if (a[1] == SOL_SOCKET_ && (a[2] == SO_SNDBUF_ || a[2] == SO_RCVBUF_)) {
                value = 65536;
            } else if (a[1] == SOL_SOCKET_ && a[2] == SO_DOMAIN_) {
                value = AF_UNIX_;
            }
            uint32_t n = std::min<uint32_t>(room, 4);
            guest_write(a[3], &value, n);
            return guest_write(a[4], &n, 4) ? 0 : -EFAULT_;
        }
        case 210: {  // shutdown
            auto file = fds().get(fd);
            if (!file) return -EBADF_;
            int how = static_cast<int>(a[1]);
            if (auto* s = dynamic_cast<SocketFile*>(file.get())) {
                int host_how = how == SHUT_RD_ ? SD_RECEIVE : how == SHUT_WR_ ? SD_SEND : SD_BOTH;
                if (::shutdown(s->socket(), host_how) != 0) return wsa_errno(WSAGetLastError());
                return 0;
            }
            std::shared_ptr<OpenFile> target = file;
            if (auto* u = dynamic_cast<UnixSocketFile*>(file.get())) target = u->peer();
            if (auto* p = dynamic_cast<PipeFile*>(target.get())) {
                p->shutdown(how);
                return 0;
            }
            if (auto* service = dynamic_cast<ServiceSocket*>(target.get())) {
                (void)service;
                return 0;
            }
            return -ENOTCONN_;
        }
        default:
            handled = false;
            return 0;
    }
}

}  // namespace axrb
