/* Networking for the guest, on Winsock.

   A guest socket is a descriptor in the same number space as its files and
   pipes, standing for a host SOCKET. What differs between bionic and Windows
   is translated at the edge and nothing else:
     - AF_INET6 is 10 on Linux and 23 on Windows (AF_INET is 2 on both), in
       socket(), in every sockaddr going either way, and in addrinfo;
     - bionic's struct addrinfo keeps ai_canonname before ai_addr;
     - SOL_SOCKET and its option numbers differ, as do a few TCP/IP ones;
     - errors come back as Linux errno values (a non-blocking connect under
       way is EINPROGRESS there, WSAEWOULDBLOCK here);
     - O_NONBLOCK (fcntl) and FIONBIO (ioctl) become ioctlsocket.
   read/write/close on a socket descriptor, and poll over any mix of sockets
   and other descriptors, are answered here too. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "android.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

const int kLinuxInet6 = 10;

std::mutex g_net_lock;
std::unordered_map<int, SOCKET> g_sockets;
std::unordered_map<int, bool> g_nonblocking;

void start_winsock() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

SOCKET socket_of(int fd) {
    std::lock_guard<std::mutex> held(g_net_lock);
    auto found = g_sockets.find(fd);
    return found == g_sockets.end() ? INVALID_SOCKET : found->second;
}

int linux_errno(int wsa, bool connecting = false) {
    switch (wsa) {
        case WSAEWOULDBLOCK: return connecting ? 115 : 11; /* EINPROGRESS : EAGAIN */
        case WSAEINPROGRESS: return 115;
        case WSAEALREADY: return 114;
        case WSAEINTR: return 4;
        case WSAEBADF: return 9;
        case WSAEACCES: return 13;
        case WSAEFAULT: return 14;
        case WSAEINVAL: return 22;
        case WSAEMFILE: return 24;
        case WSAENOTSOCK: return 88;
        case WSAEDESTADDRREQ: return 89;
        case WSAEMSGSIZE: return 90;
        case WSAEPROTOTYPE: return 91;
        case WSAENOPROTOOPT: return 92;
        case WSAEPROTONOSUPPORT: return 93;
        case WSAEOPNOTSUPP: return 95;
        case WSAEAFNOSUPPORT: return 97;
        case WSAEADDRINUSE: return 98;
        case WSAEADDRNOTAVAIL: return 99;
        case WSAENETDOWN: return 100;
        case WSAENETUNREACH: return 101;
        case WSAENETRESET: return 102;
        case WSAECONNABORTED: return 103;
        case WSAECONNRESET: return 104;
        case WSAENOBUFS: return 105;
        case WSAEISCONN: return 106;
        case WSAENOTCONN: return 107;
        case WSAESHUTDOWN: return 108;
        case WSAETIMEDOUT: return 110;
        case WSAECONNREFUSED: return 111;
        case WSAEHOSTDOWN: return 112;
        case WSAEHOSTUNREACH: return 113;
        default: return 5; /* EIO */
    }
}

/* A guest sockaddr as a host one (only the family field differs). */
int to_host_address(const uint8_t* guest, int length, sockaddr_storage* host) {
    if (!guest || length < 2 || length > (int)sizeof(sockaddr_storage)) return 0;
    std::memcpy(host, guest, (size_t)length);
    uint16_t family = 0;
    std::memcpy(&family, guest, 2);
    host->ss_family = family == kLinuxInet6 ? AF_INET6 : family;
    return length;
}

void to_guest_address(const sockaddr* host, int length, uint8_t* guest, uint32_t* guest_length) {
    if (!guest || !guest_length) return;
    int room = (int)*guest_length;
    int copy = std::min(room, length);
    if (copy > 0) std::memcpy(guest, host, (size_t)copy);
    if (copy >= 2) {
        uint16_t family = host->sa_family == AF_INET6 ? kLinuxInet6 : host->sa_family;
        std::memcpy(guest, &family, 2);
    }
    *guest_length = (uint32_t)length;
}

/* Socket options: (level, name) on Linux to the same on Windows; false
   when there is nothing equivalent, which is answered as success. */
bool host_option(int level, int name, int* host_level, int* host_name) {
    if (level == 1) { /* SOL_SOCKET */
        *host_level = SOL_SOCKET;
        switch (name) {
            case 2: *host_name = SO_REUSEADDR; return true;
            case 4: *host_name = SO_ERROR; return true;
            case 6: *host_name = SO_BROADCAST; return true;
            case 7: *host_name = SO_SNDBUF; return true;
            case 8: *host_name = SO_RCVBUF; return true;
            case 9: *host_name = SO_KEEPALIVE; return true;
            case 13: *host_name = SO_LINGER; return true;
            case 20: *host_name = SO_RCVTIMEO; return true;
            case 21: *host_name = SO_SNDTIMEO; return true;
            case 3: *host_name = SO_TYPE; return true;
            default: return false;
        }
    }
    if (level == 6) { /* IPPROTO_TCP */
        *host_level = IPPROTO_TCP;
        switch (name) {
            case 1: *host_name = TCP_NODELAY; return true;
            case 4: *host_name = TCP_KEEPIDLE; return true;
            case 5: *host_name = TCP_KEEPINTVL; return true;
            case 6: *host_name = TCP_KEEPCNT; return true;
            default: return false;
        }
    }
    if (level == 0) { /* IPPROTO_IP */
        *host_level = IPPROTO_IP;
        switch (name) {
            case 1: *host_name = IP_TOS; return true;
            case 2: *host_name = IP_TTL; return true;
            default: return false;
        }
    }
    if (level == 41) { /* IPPROTO_IPV6 */
        *host_level = IPPROTO_IPV6;
        switch (name) {
            case 26: *host_name = IPV6_V6ONLY; return true;
            default: return false;
        }
    }
    return false;
}

/* bionic's struct addrinfo: flags, family, socktype, protocol, addrlen,
   then canonname, addr, next, 48 bytes. */
struct GuestAddrinfo {
    int32_t flags, family, socktype, protocol;
    uint32_t addrlen;
    uint32_t pad;
    uint64_t canonname, addr, next;
};
static_assert(sizeof(GuestAddrinfo) == 48, "bionic addrinfo");

}  // namespace

bool GuestLibc::net_call(const std::string& name, GuestCpu& cpu) {
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    auto fail = [&](int error) {
        set_errno(cpu, error);
        ret((uint64_t)-1);
        return true;
    };
    auto fail_wsa = [&](bool connecting = false) { return fail(linux_errno(WSAGetLastError(), connecting)); };
    auto bytes = [&](uint64_t va) { return reinterpret_cast<uint8_t*>(va); };

    /* epoll over the same descriptors poll understands: pipes and eventfds
       (readable when they hold something), sockets (WSAPoll), and anything
       else, always ready. Level-triggered; EPOLLET and EPOLLONESHOT are
       honoured only as far as a oneshot entry being disarmed once reported.
       struct epoll_event on arm64 is {u32 events; (pad) u64 data}, 16 bytes. */
    if (name == "epoll_create1" || name == "epoll_create") {
        static std::mutex epoll_lock;
        (void)epoll_lock;
        int fd;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            fd = next_fd++;
            epolls[fd];
        }
        ret((uint64_t)fd);
        return true;
    }
    if (name == "epoll_ctl") {
        int epfd = (int)arg(0), op = (int)arg(1), fd = (int)arg(2);
        std::lock_guard<std::recursive_mutex> held(lock);
        auto set = epolls.find(epfd);
        if (set == epolls.end()) return fail(9);
        if (op == 2) { /* EPOLL_CTL_DEL */
            set->second.erase(fd);
            ret(0);
            return true;
        }
        const uint8_t* event = bytes(arg(3));
        if (!event) return fail(14);
        EpollEntry entry;
        std::memcpy(&entry.events, event, 4);
        std::memcpy(&entry.data, event + 8, 8);
        if (op == 1 && set->second.count(fd)) return fail(17); /* EEXIST */
        if (op == 3 && !set->second.count(fd)) return fail(2);  /* ENOENT */
        set->second[fd] = entry;
        ret(0);
        return true;
    }
    if (name == "epoll_wait" || name == "epoll_pwait" || name == "epoll_pwait2") {
        int epfd = (int)arg(0);
        uint8_t* out = bytes(arg(1));
        int capacity = (int)arg(2);
        int timeout = (int)(int32_t)arg(3);
        if (name == "epoll_pwait2") {
            timeout = -1;
            if (arg(3)) {
                int64_t seconds = 0, nanos = 0;
                std::memcpy(&seconds, bytes(arg(3)), 8);
                std::memcpy(&nanos, bytes(arg(3)) + 8, 8);
                timeout = (int)(seconds * 1000 + nanos / 1000000);
            }
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout < 0 ? 0 : timeout);
        for (;;) {
            int ready = 0;
            {
                std::lock_guard<std::recursive_mutex> held(lock);
                auto set = epolls.find(epfd);
                if (set == epolls.end()) return fail(9);
                for (auto& [fd, entry] : set->second) {
                    if (ready >= capacity) break;
                    if (entry.disarmed) continue;
                    uint32_t revents = 0;
                    SOCKET s = socket_of(fd);
                    if (s != INVALID_SOCKET) {
                        WSAPOLLFD one{s, 0, 0};
                        if (entry.events & 1) one.events |= POLLRDNORM;
                        if (entry.events & 4) one.events |= POLLWRNORM;
                        if (WSAPoll(&one, 1, 0) > 0) {
                            if (one.revents & (POLLRDNORM | POLLRDBAND)) revents |= 1;
                            if (one.revents & POLLWRNORM) revents |= 4;
                            if (one.revents & POLLERR) revents |= 8;
                            if (one.revents & POLLHUP) revents |= 0x10;
                        }
                    } else {
                        auto pipe = fds.find(fd);
                        bool readable = true;
                        if (pipe != fds.end()) {
                            std::lock_guard<std::mutex> inner(pipe->second->lock);
                            readable = pipe->second->event ? pipe->second->counter != 0 : !pipe->second->bytes.empty();
                        }
                        if ((entry.events & 1) && readable) revents |= 1;
                        if (entry.events & 4) revents |= 4;
                    }
                    if (!revents) continue;
                    std::memcpy(out + 16 * ready, &revents, 4);
                    std::memcpy(out + 16 * ready + 8, &entry.data, 8);
                    if (entry.events & (1u << 30)) entry.disarmed = true; /* EPOLLONESHOT */
                    ++ready;
                }
            }
            if (ready || timeout == 0) {
                ret((uint64_t)ready);
                return true;
            }
            if (timeout > 0 && std::chrono::steady_clock::now() >= deadline) {
                ret(0);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (poll_signals(cpu)) return fail(4);
        }
    }
    if (name == "socket") {
        start_winsock();
        int family = (int)arg(0), type = (int)arg(1), protocol = (int)arg(2);
        bool nonblocking = (type & 0x800) != 0; /* SOCK_NONBLOCK; SOCK_CLOEXEC (0x80000) means nothing here */
        type &= 0xf;
        int host_family = family == kLinuxInet6 ? AF_INET6 : family;
        if (host_family != AF_INET && host_family != AF_INET6) return fail(97); /* EAFNOSUPPORT: no AF_UNIX etc. */
        SOCKET s = ::socket(host_family, type, protocol);
        if (s == INVALID_SOCKET) return fail_wsa();
        if (host_family == AF_INET6) {
            /* Linux's IPv6 sockets reach IPv4 too (as ::ffff:a.b.c.d) unless
               told otherwise; Windows' are IPv6-only by default. Photon's
               UDP socket depends on the Linux default. */
            DWORD v6only = 0;
            ::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
        }
        if (nonblocking) {
            u_long on = 1;
            ioctlsocket(s, FIONBIO, &on);
        }
        int fd;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            fd = next_fd++;
        }
        {
            std::lock_guard<std::mutex> held(g_net_lock);
            g_sockets[fd] = s;
            g_nonblocking[fd] = nonblocking;
        }
        if (QB_ENV("QB_TRACE_NET")) std::printf("net: socket(%d, %d, %d) -> fd %d\n", family, type, protocol, fd);
        ret((uint64_t)fd);
        return true;
    }

    if (name == "getaddrinfo") {
        start_winsock();
        const char* node = reinterpret_cast<const char*>(arg(0));
        const char* service = reinterpret_cast<const char*>(arg(1));
        const GuestAddrinfo* guest_hints = reinterpret_cast<const GuestAddrinfo*>(arg(2));
        addrinfo hints{}, *found = nullptr;
        if (guest_hints) {
            hints.ai_flags = guest_hints->flags & (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST); /* same low bits */
            hints.ai_family = guest_hints->family == kLinuxInet6 ? AF_INET6 : guest_hints->family;
            hints.ai_socktype = guest_hints->socktype;
            hints.ai_protocol = guest_hints->protocol;
        }
        int error = ::getaddrinfo(node, service, guest_hints ? &hints : nullptr, &found);
        if (QB_ENV("QB_TRACE_NET")) std::printf("net: getaddrinfo(%s) -> %d\n", node ? node : "(null)", error);
        if (error != 0) {
            /* bionic's EAI_* numbers. */
            int code = error == WSAHOST_NOT_FOUND ? 8 : error == WSATRY_AGAIN ? 2 : error == WSAEAFNOSUPPORT ? 5 : 4;
            ret((uint64_t)code);
            return true;
        }
        uint64_t first = 0, previous = 0;
        for (addrinfo* at = found; at; at = at->ai_next) {
            if (at->ai_family != AF_INET && at->ai_family != AF_INET6) continue;
            uint64_t entry = guest_alloc(sizeof(GuestAddrinfo));
            uint64_t address = guest_alloc(sizeof(sockaddr_in6));
            GuestAddrinfo* g = reinterpret_cast<GuestAddrinfo*>(entry);
            std::memset(g, 0, sizeof(*g));
            g->flags = at->ai_flags;
            g->family = at->ai_family == AF_INET6 ? kLinuxInet6 : at->ai_family;
            g->socktype = at->ai_socktype;
            g->protocol = at->ai_protocol;
            uint32_t length = sizeof(sockaddr_in6);
            to_guest_address(at->ai_addr, (int)at->ai_addrlen, bytes(address), &length);
            g->addrlen = (uint32_t)at->ai_addrlen;
            g->addr = address;
            if (at->ai_canonname) g->canonname = guest_string(at->ai_canonname);
            if (previous) reinterpret_cast<GuestAddrinfo*>(previous)->next = entry;
            else first = entry;
            previous = entry;
        }
        ::freeaddrinfo(found);
        if (uint8_t* out = bytes(arg(3))) std::memcpy(out, &first, 8);
        ret(first ? 0 : 8);
        return true;
    }
    if (name == "freeaddrinfo") {
        return true; /* guest heap blocks; small and rare */
    }
    if (name == "gai_strerror") {
        static uint64_t text = 0;
        if (!text) text = guest_string("name resolution failed");
        ret(text);
        return true;
    }
    if (name == "inet_ntop") {
        int family = (int)arg(0) == kLinuxInet6 ? AF_INET6 : (int)arg(0);
        char* out = reinterpret_cast<char*>(arg(2));
        ret(out && ::inet_ntop(family, reinterpret_cast<void*>(arg(1)), out, (size_t)arg(3)) ? arg(2) : 0);
        return true;
    }
    if (name == "inet_pton") {
        int family = (int)arg(0) == kLinuxInet6 ? AF_INET6 : (int)arg(0);
        const char* text = reinterpret_cast<const char*>(arg(1));
        ret(text ? (uint64_t)(int64_t)::inet_pton(family, text, reinterpret_cast<void*>(arg(2))) : 0);
        return true;
    }

    /* Everything below takes a descriptor; only sockets are answered. */
    bool per_socket = name == "connect" || name == "bind" || name == "listen" || name == "accept" ||
                      name == "accept4" || name == "send" || name == "sendto" || name == "recv" ||
                      name == "recvfrom" || name == "shutdown" || name == "getsockname" || name == "getpeername" ||
                      name == "setsockopt" || name == "getsockopt" || name == "read" || name == "write" ||
                      name == "close" || name == "fcntl" || name == "ioctl" || name == "__recvfrom_chk" ||
                      name == "__sendto_chk" || name == "sendmsg" || name == "recvmsg";
    if (per_socket) {
        int fd = (int)arg(0);
        SOCKET s = socket_of(fd);
        if (s == INVALID_SOCKET) {
            if (name == "fcntl") {
                /* Files and pipes: F_GETFL says read/write (and non-blocking
                   for a pipe set so), F_SETFL sets a pipe's O_NONBLOCK, which
                   its reads honour; the rest succeed. */
                int command = (int)arg(1);
                std::shared_ptr<GuestPipe> pipe;
                {
                    std::lock_guard<std::recursive_mutex> held(lock);
                    auto found = fds.find(fd);
                    if (found != fds.end()) pipe = found->second;
                }
                if (command == 3) {
                    bool nonblocking = false;
                    if (pipe) {
                        std::lock_guard<std::mutex> held(pipe->lock);
                        nonblocking = pipe->nonblocking;
                    }
                    ret(2 | (nonblocking ? 0x800 : 0));
                    return true;
                }
                if (command == 4 && pipe) {
                    std::lock_guard<std::mutex> held(pipe->lock);
                    pipe->nonblocking = (arg(2) & 0x800) != 0;
                }
                ret(0);
                return true;
            }
            if (name == "read" || name == "write" || name == "close" || name == "ioctl") return false;
            return fail(88); /* ENOTSOCK */
        }
        const bool trace = QB_ENV("QB_TRACE_NET") != nullptr;
        if (name == "connect") {
            sockaddr_storage address{};
            int length = to_host_address(bytes(arg(1)), (int)arg(2), &address);
            if (!length) return fail(22);
            int result = ::connect(s, reinterpret_cast<sockaddr*>(&address), length);
            int error = result ? WSAGetLastError() : 0;
            if (trace) std::printf("net: connect(fd %d) -> %d (%d)\n", fd, result, error);
            if (error == WSAEISCONN) {
                ret(0);
                return true;
            }
            bool blocking;
            {
                std::lock_guard<std::mutex> held(g_net_lock);
                blocking = !g_nonblocking[fd];
            }
            /* A blocking connect waits for the connection on Linux, even when
               an earlier non-blocking one started it (il2cpp does exactly
               that); Winsock answers "already in progress" at once instead. */
            if (blocking && (error == WSAEWOULDBLOCK || error == WSAEALREADY || error == WSAEINVAL)) {
                fd_set writable, failed;
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(s, &writable);
                FD_SET(s, &failed);
                timeval limit{75, 0}; /* Linux's default connect timeout is of this order */
                int ready = ::select(0, nullptr, &writable, &failed, &limit);
                if (ready > 0 && FD_ISSET(s, &writable)) {
                    ret(0);
                    return true;
                }
                int so_error = 0, so_length = sizeof(so_error);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &so_length);
                return fail(ready == 0 ? 110 : linux_errno(so_error ? so_error : WSAECONNREFUSED));
            }
            if (result != 0) {
                /* A second non-blocking connect while one is pending. */
                if (error == WSAEALREADY || error == WSAEINVAL) return fail(114);
                return fail(linux_errno(error, true));
            }
            ret(0);
            return true;
        }
        if (name == "bind") {
            sockaddr_storage address{};
            int length = to_host_address(bytes(arg(1)), (int)arg(2), &address);
            if (!length) return fail(22);
            if (::bind(s, reinterpret_cast<sockaddr*>(&address), length) != 0) return fail_wsa();
            ret(0);
            return true;
        }
        if (name == "listen") {
            if (::listen(s, (int)arg(1)) != 0) return fail_wsa();
            ret(0);
            return true;
        }
        if (name == "accept" || name == "accept4") {
            sockaddr_storage address{};
            int length = sizeof(address);
            SOCKET got = ::accept(s, reinterpret_cast<sockaddr*>(&address), &length);
            if (got == INVALID_SOCKET) return fail_wsa();
            to_guest_address(reinterpret_cast<sockaddr*>(&address), length, bytes(arg(1)),
                             reinterpret_cast<uint32_t*>(arg(2)));
            int accepted;
            {
                std::lock_guard<std::recursive_mutex> held(lock);
                accepted = next_fd++;
            }
            std::lock_guard<std::mutex> held(g_net_lock);
            g_sockets[accepted] = got;
            ret((uint64_t)accepted);
            return true;
        }
        if (name == "send" || name == "write" || name == "sendto" || name == "__sendto_chk") {
            const char* data = reinterpret_cast<const char*>(arg(1));
            int length = (int)std::min<uint64_t>(arg(2), 0x7fffffff);
            int result;
            if ((name == "sendto" || name == "__sendto_chk") && arg(4)) {
                sockaddr_storage address{};
                int address_length = to_host_address(bytes(arg(4)), (int)arg(5), &address);
                result = ::sendto(s, data, length, 0, reinterpret_cast<sockaddr*>(&address), address_length);
            } else {
                result = ::send(s, data, length, 0);
            }
            if (result == SOCKET_ERROR) {
                static int said = 0;
                if (trace && said++ < 20)
                    std::printf("net: %s(fd %d, %d bytes) failed: %d\n", name.c_str(), fd, length, WSAGetLastError());
                return fail_wsa();
            }
            ret((uint64_t)result);
            return true;
        }
        if (name == "recv" || name == "read" || name == "recvfrom" || name == "__recvfrom_chk") {
            char* data = reinterpret_cast<char*>(arg(1));
            int length = (int)std::min<uint64_t>(arg(2), 0x7fffffff);
            int flags = ((int)arg(3) & 2) ? MSG_PEEK : 0; /* MSG_PEEK is 2 on both */
            if (name == "read") flags = 0;
            int result;
            bool from = name == "recvfrom" || name == "__recvfrom_chk";
            /* __recvfrom_chk puts the buffer size fourth. */
            uint64_t address_va = from ? (name == "recvfrom" ? arg(4) : arg(5)) : 0;
            uint64_t length_va = from ? (name == "recvfrom" ? arg(5) : arg(6)) : 0;
            if (from && address_va) {
                sockaddr_storage address{};
                int address_length = sizeof(address);
                result = ::recvfrom(s, data, length, flags, reinterpret_cast<sockaddr*>(&address), &address_length);
                if (result != SOCKET_ERROR)
                    to_guest_address(reinterpret_cast<sockaddr*>(&address), address_length, bytes(address_va),
                                     reinterpret_cast<uint32_t*>(length_va));
            } else {
                result = ::recv(s, data, length, flags);
            }
            if (result == SOCKET_ERROR) {
                /* A datagram bigger than the buffer: Linux truncates quietly. */
                if (WSAGetLastError() == WSAEMSGSIZE) {
                    ret((uint64_t)length);
                    return true;
                }
                return fail_wsa();
            }
            ret((uint64_t)result);
            return true;
        }
        if (name == "sendmsg" || name == "recvmsg") {
            return fail(95); /* EOPNOTSUPP: not used by anything seen so far */
        }
        if (name == "shutdown") {
            ::shutdown(s, (int)arg(1)); /* SHUT_RD/WR/RDWR are 0/1/2 on both */
            ret(0);
            return true;
        }
        if (name == "getsockname" || name == "getpeername") {
            sockaddr_storage address{};
            int length = sizeof(address);
            int result = name == "getsockname" ? ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length)
                                               : ::getpeername(s, reinterpret_cast<sockaddr*>(&address), &length);
            if (result != 0) return fail_wsa();
            to_guest_address(reinterpret_cast<sockaddr*>(&address), length, bytes(arg(1)),
                             reinterpret_cast<uint32_t*>(arg(2)));
            ret(0);
            return true;
        }
        if (name == "setsockopt" || name == "getsockopt") {
            int level = 0, option = 0;
            if (!host_option((int)arg(1), (int)arg(2), &level, &option)) {
                if (name == "getsockopt" && arg(3) && arg(4)) std::memset(bytes(arg(3)), 0, 4);
                ret(0);
                return true;
            }
            if (name == "setsockopt") {
                char value[16] = {};
                int length = (int)std::min<uint64_t>(arg(4), sizeof(value));
                std::memcpy(value, bytes(arg(3)), (size_t)length);
                if (option == SO_RCVTIMEO || option == SO_SNDTIMEO) {
                    /* struct timeval there, milliseconds as a DWORD here. */
                    int64_t seconds = 0, micros = 0;
                    std::memcpy(&seconds, value, 8);
                    std::memcpy(&micros, value + 8, 8);
                    DWORD ms = (DWORD)(seconds * 1000 + micros / 1000);
                    ::setsockopt(s, level, option, reinterpret_cast<const char*>(&ms), sizeof(ms));
                } else {
                    ::setsockopt(s, level, option, value, length);
                }
                ret(0);
                return true;
            }
            int value = 0, length = sizeof(value);
            if (::getsockopt(s, level, option, reinterpret_cast<char*>(&value), &length) != 0) return fail_wsa();
            if (option == SO_ERROR && value) value = linux_errno(value, false);
            if (option == SO_TYPE) value &= 0xf;
            if (arg(3)) std::memcpy(bytes(arg(3)), &value, 4);
            if (arg(4)) {
                uint32_t four = 4;
                std::memcpy(bytes(arg(4)), &four, 4);
            }
            ret(0);
            return true;
        }
        if (name == "fcntl") {
            int command = (int)arg(1);
            if (command == 3) { /* F_GETFL */
                std::lock_guard<std::mutex> held(g_net_lock);
                ret(2 | (g_nonblocking[fd] ? 0x800 : 0));
                return true;
            }
            if (command == 4) { /* F_SETFL */
                bool on = (arg(2) & 0x800) != 0;
                u_long mode = on ? 1 : 0;
                ioctlsocket(s, FIONBIO, &mode);
                std::lock_guard<std::mutex> held(g_net_lock);
                g_nonblocking[fd] = on;
                ret(0);
                return true;
            }
            ret(0); /* F_GETFD/F_SETFD and the rest */
            return true;
        }
        if (name == "ioctl") {
            unsigned long request = (unsigned long)arg(1);
            if (request == 0x5421) { /* FIONBIO */
                int on = 0;
                std::memcpy(&on, bytes(arg(2)), 4);
                u_long mode = on ? 1 : 0;
                ioctlsocket(s, FIONBIO, &mode);
                std::lock_guard<std::mutex> held(g_net_lock);
                g_nonblocking[fd] = on != 0;
                ret(0);
                return true;
            }
            if (request == 0x541B) { /* FIONREAD */
                u_long available = 0;
                ioctlsocket(s, FIONREAD, &available);
                int value = (int)available;
                std::memcpy(bytes(arg(2)), &value, 4);
                ret(0);
                return true;
            }
            return fail(22);
        }
        if (name == "close") {
            ::closesocket(s);
            std::lock_guard<std::mutex> held(g_net_lock);
            g_sockets.erase(fd);
            g_nonblocking.erase(fd);
            ret(0);
            return true;
        }
    }

    if (name == "poll" || name == "ppoll") {
        /* Only when a socket is among the descriptors; the rest stays with
           the pipe-and-file poll. */
        struct pollfd_guest {
            int32_t fd;
            int16_t events, revents;
        };
        pollfd_guest* list = reinterpret_cast<pollfd_guest*>(arg(0));
        uint64_t count = arg(1);
        bool any_socket = false;
        for (uint64_t i = 0; list && i < count; ++i)
            if (socket_of(list[i].fd) != INVALID_SOCKET) any_socket = true;
        if (!any_socket) return false;
        int timeout = (int)(int32_t)arg(2);
        if (name == "ppoll") {
            timeout = -1;
            if (arg(2)) {
                int64_t seconds = 0, nanos = 0;
                std::memcpy(&seconds, bytes(arg(2)), 8);
                std::memcpy(&nanos, bytes(arg(2)) + 8, 8);
                timeout = (int)(seconds * 1000 + nanos / 1000000);
            }
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout < 0 ? 0 : timeout);
        for (;;) {
            int ready = 0;
            for (uint64_t i = 0; i < count; ++i) {
                pollfd_guest& entry = list[i];
                entry.revents = 0;
                if (entry.fd < 0) continue;
                SOCKET s = socket_of(entry.fd);
                if (s == INVALID_SOCKET) {
                    /* Files are always ready; pipes when they hold something. */
                    std::shared_ptr<GuestPipe> pipe;
                    {
                        std::lock_guard<std::recursive_mutex> held(lock);
                        auto found = fds.find(entry.fd);
                        if (found != fds.end()) pipe = found->second;
                    }
                    bool readable = true;
                    if (pipe) {
                        std::lock_guard<std::mutex> held(pipe->lock);
                        readable = pipe->event ? pipe->counter != 0 : !pipe->bytes.empty();
                    }
                    if ((entry.events & 1) && readable) entry.revents |= 1;
                    if (entry.events & 4) entry.revents |= 4;
                } else {
                    WSAPOLLFD one{s, 0, 0};
                    if (entry.events & 1) one.events |= POLLRDNORM;
                    if (entry.events & 4) one.events |= POLLWRNORM;
                    int polled = WSAPoll(&one, 1, 0);
                    static const bool trace_poll = QB_ENV("QB_TRACE_NET") != nullptr;
                    static thread_local auto last_said = std::chrono::steady_clock::now() - std::chrono::seconds(2);
                    if (trace_poll && std::chrono::steady_clock::now() - last_said > std::chrono::seconds(1)) {
                        last_said = std::chrono::steady_clock::now();
                        std::printf("net: poll fd %d events %x -> %d revents %x (error %d)\n", entry.fd, entry.events,
                                    polled, one.revents, polled < 0 ? WSAGetLastError() : 0);
                    }
                    if (polled > 0) {
                        if (one.revents & (POLLRDNORM | POLLRDBAND)) entry.revents |= 1;  /* POLLIN */
                        if (one.revents & POLLWRNORM) entry.revents |= 4;                 /* POLLOUT */
                        if (one.revents & POLLERR) entry.revents |= 8;                    /* POLLERR */
                        if (one.revents & POLLHUP) entry.revents |= 0x10;                 /* POLLHUP */
                        if (one.revents & POLLNVAL) entry.revents |= 0x20;                /* POLLNVAL */
                    }
                }
                if (entry.revents) ++ready;
            }
            if (ready || timeout == 0) {
                ret((uint64_t)ready);
                return true;
            }
            if (timeout > 0 && std::chrono::steady_clock::now() >= deadline) {
                ret(0);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (poll_signals(cpu)) return fail(4);
        }
    }
    return false;
}
