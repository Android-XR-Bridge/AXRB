#pragma once

// The Linux arm64 user ABI, as the guest's bionic sees it. Values come from
// the kernel's uapi headers (asm-generic layout, which arm64 uses).

#include <cstdint>

namespace axrb::linux_abi {

// errno
enum : int64_t {
    EPERM_ = 1, ENOENT_ = 2, ESRCH_ = 3, EINTR_ = 4, EIO_ = 5, ENXIO_ = 6, E2BIG_ = 7, ENOEXEC_ = 8, EBADF_ = 9,
    ECHILD_ = 10, EAGAIN_ = 11, ENOMEM_ = 12, EACCES_ = 13, EFAULT_ = 14, EBUSY_ = 16, EEXIST_ = 17, EXDEV_ = 18,
    ENODEV_ = 19, ENOTDIR_ = 20, EISDIR_ = 21, EINVAL_ = 22, ENFILE_ = 23, EMFILE_ = 24, ENOTTY_ = 25, EFBIG_ = 27,
    ENOSPC_ = 28, ESPIPE_ = 29, EROFS_ = 30, EMLINK_ = 31, EPIPE_ = 32, EDOM_ = 33, ERANGE_ = 34, EDEADLK_ = 35,
    ENAMETOOLONG_ = 36, ENOLCK_ = 37, ENOSYS_ = 38, ENOTEMPTY_ = 39, ELOOP_ = 40, ENOMSG_ = 42, ENODATA_ = 61,
    ETIME_ = 62, EOVERFLOW_ = 75, EBADFD_ = 77, EILSEQ_ = 84, ENOTSOCK_ = 88, EDESTADDRREQ_ = 89, EMSGSIZE_ = 90,
    EPROTOTYPE_ = 91, ENOPROTOOPT_ = 92, EPROTONOSUPPORT_ = 93, EOPNOTSUPP_ = 95, EAFNOSUPPORT_ = 97,
    EADDRINUSE_ = 98, EADDRNOTAVAIL_ = 99, ENETDOWN_ = 100, ENETUNREACH_ = 101, ECONNABORTED_ = 103,
    ECONNRESET_ = 104, ENOBUFS_ = 105, EISCONN_ = 106, ENOTCONN_ = 107, ETIMEDOUT_ = 110, ECONNREFUSED_ = 111,
    EHOSTUNREACH_ = 113, EALREADY_ = 114, EINPROGRESS_ = 115, ECANCELED_ = 125, EOWNERDEAD_ = 130,
    ERESTARTSYS_ = 512,
};

// open flags (arm64 uses the asm-generic values except O_DIRECTORY/O_NOFOLLOW/O_DIRECT/O_LARGEFILE)
enum : uint32_t {
    O_ACCMODE_ = 3, O_RDONLY_ = 0, O_WRONLY_ = 1, O_RDWR_ = 2,
    O_CREAT_ = 0100, O_EXCL_ = 0200, O_NOCTTY_ = 0400, O_TRUNC_ = 01000, O_APPEND_ = 02000, O_NONBLOCK_ = 04000,
    O_DSYNC_ = 010000, O_DIRECTORY_ = 040000, O_NOFOLLOW_ = 0100000, O_DIRECT_ = 0200000, O_LARGEFILE_ = 0400000,
    O_CLOEXEC_ = 02000000, O_PATH_ = 010000000, O_TMPFILE_ = 020040000,
};
constexpr int AT_FDCWD_ = -100;
constexpr uint32_t AT_SYMLINK_NOFOLLOW_ = 0x100, AT_REMOVEDIR_ = 0x200, AT_SYMLINK_FOLLOW_ = 0x400,
                   AT_EMPTY_PATH_ = 0x1000, AT_EACCESS_ = 0x200;

// mode bits
constexpr uint32_t S_IFMT_ = 0170000, S_IFSOCK_ = 0140000, S_IFLNK_ = 0120000, S_IFREG_ = 0100000,
                   S_IFBLK_ = 0060000, S_IFDIR_ = 0040000, S_IFCHR_ = 0020000, S_IFIFO_ = 0010000;

// dirent d_type
enum : uint8_t { DT_UNKNOWN_ = 0, DT_FIFO_ = 1, DT_CHR_ = 2, DT_DIR_ = 4, DT_BLK_ = 6, DT_REG_ = 8, DT_LNK_ = 10, DT_SOCK_ = 12 };

// mmap
enum : uint32_t {
    PROT_NONE_ = 0, PROT_READ_ = 1, PROT_WRITE_ = 2, PROT_EXEC_ = 4,
    MAP_SHARED_ = 0x01, MAP_PRIVATE_ = 0x02, MAP_SHARED_VALIDATE_ = 0x03, MAP_FIXED_ = 0x10, MAP_ANONYMOUS_ = 0x20,
    MAP_GROWSDOWN_ = 0x0100, MAP_DENYWRITE_ = 0x0800, MAP_EXECUTABLE_ = 0x1000, MAP_LOCKED_ = 0x2000,
    MAP_NORESERVE_ = 0x4000, MAP_POPULATE_ = 0x8000, MAP_NONBLOCK_ = 0x10000, MAP_STACK_ = 0x20000,
    MAP_HUGETLB_ = 0x40000, MAP_FIXED_NOREPLACE_ = 0x100000,
    MREMAP_MAYMOVE_ = 1, MREMAP_FIXED_ = 2, MREMAP_DONTUNMAP_ = 4,
};
enum : int { MADV_NORMAL_ = 0, MADV_RANDOM_ = 1, MADV_SEQUENTIAL_ = 2, MADV_WILLNEED_ = 3, MADV_DONTNEED_ = 4,
             MADV_FREE_ = 8, MADV_REMOVE_ = 9, MADV_DONTFORK_ = 10, MADV_MERGEABLE_ = 12, MADV_HUGEPAGE_ = 14,
             MADV_NOHUGEPAGE_ = 15, MADV_DONTDUMP_ = 16, MADV_COLD_ = 20, MADV_PAGEOUT_ = 21 };

// clone
enum : uint64_t {
    CSIGNAL_ = 0x000000ff, CLONE_VM_ = 0x00000100, CLONE_FS_ = 0x00000200, CLONE_FILES_ = 0x00000400,
    CLONE_SIGHAND_ = 0x00000800, CLONE_PIDFD_ = 0x00001000, CLONE_PTRACE_ = 0x00002000, CLONE_VFORK_ = 0x00004000,
    CLONE_PARENT_ = 0x00008000, CLONE_THREAD_ = 0x00010000, CLONE_NEWNS_ = 0x00020000, CLONE_SYSVSEM_ = 0x00040000,
    CLONE_SETTLS_ = 0x00080000, CLONE_PARENT_SETTID_ = 0x00100000, CLONE_CHILD_CLEARTID_ = 0x00200000,
    CLONE_DETACHED_ = 0x00400000, CLONE_UNTRACED_ = 0x00800000, CLONE_CHILD_SETTID_ = 0x01000000,
};

// futex
enum : int {
    FUTEX_WAIT_ = 0, FUTEX_WAKE_ = 1, FUTEX_FD_ = 2, FUTEX_REQUEUE_ = 3, FUTEX_CMP_REQUEUE_ = 4, FUTEX_WAKE_OP_ = 5,
    FUTEX_LOCK_PI_ = 6, FUTEX_UNLOCK_PI_ = 7, FUTEX_TRYLOCK_PI_ = 8, FUTEX_WAIT_BITSET_ = 9, FUTEX_WAKE_BITSET_ = 10,
    FUTEX_WAIT_REQUEUE_PI_ = 11, FUTEX_CMP_REQUEUE_PI_ = 12, FUTEX_LOCK_PI2_ = 13,
    FUTEX_PRIVATE_FLAG_ = 128, FUTEX_CLOCK_REALTIME_ = 256,
};
constexpr uint32_t FUTEX_WAITERS_ = 0x80000000u, FUTEX_OWNER_DIED_ = 0x40000000u, FUTEX_TID_MASK_ = 0x3fffffffu;

// clocks
enum : int { CLOCK_REALTIME_ = 0, CLOCK_MONOTONIC_ = 1, CLOCK_PROCESS_CPUTIME_ID_ = 2, CLOCK_THREAD_CPUTIME_ID_ = 3,
             CLOCK_MONOTONIC_RAW_ = 4, CLOCK_REALTIME_COARSE_ = 5, CLOCK_MONOTONIC_COARSE_ = 6, CLOCK_BOOTTIME_ = 7,
             CLOCK_REALTIME_ALARM_ = 8, CLOCK_BOOTTIME_ALARM_ = 9 };
constexpr int TIMER_ABSTIME_ = 1;

// signals
enum : int {
    SIGHUP_ = 1, SIGINT_ = 2, SIGQUIT_ = 3, SIGILL_ = 4, SIGTRAP_ = 5, SIGABRT_ = 6, SIGBUS_ = 7, SIGFPE_ = 8,
    SIGKILL_ = 9, SIGUSR1_ = 10, SIGSEGV_ = 11, SIGUSR2_ = 12, SIGPIPE_ = 13, SIGALRM_ = 14, SIGTERM_ = 15,
    SIGSTKFLT_ = 16, SIGCHLD_ = 17, SIGCONT_ = 18, SIGSTOP_ = 19, SIGTSTP_ = 20, SIGTTIN_ = 21, SIGTTOU_ = 22,
    SIGURG_ = 23, SIGXCPU_ = 24, SIGXFSZ_ = 25, SIGVTALRM_ = 26, SIGPROF_ = 27, SIGWINCH_ = 28, SIGIO_ = 29,
    SIGPWR_ = 30, SIGSYS_ = 31, SIGRTMIN_ = 32, NSIG_ = 64,
};
enum : uint64_t {
    SA_NOCLDSTOP_ = 1, SA_NOCLDWAIT_ = 2, SA_SIGINFO_ = 4, SA_ONSTACK_ = 0x08000000, SA_RESTART_ = 0x10000000,
    SA_NODEFER_ = 0x40000000, SA_RESETHAND_ = 0x80000000, SA_RESTORER_ = 0x04000000,
};
constexpr uint64_t SIG_DFL_ = 0, SIG_IGN_ = 1;
enum : int { SIG_BLOCK_ = 0, SIG_UNBLOCK_ = 1, SIG_SETMASK_ = 2 };
enum : int { SI_USER_ = 0, SI_KERNEL_ = 0x80, SI_QUEUE_ = -1, SI_TIMER_ = -2, SI_TKILL_ = -6,
             SEGV_MAPERR_ = 1, SEGV_ACCERR_ = 2, ILL_ILLOPC_ = 1, BUS_ADRALN_ = 1, TRAP_BRKPT_ = 1 };
constexpr int SS_ONSTACK_ = 1, SS_DISABLE_ = 2;

// fcntl
enum : int { F_DUPFD_ = 0, F_GETFD_ = 1, F_SETFD_ = 2, F_GETFL_ = 3, F_SETFL_ = 4, F_GETLK_ = 5, F_SETLK_ = 6,
             F_SETLKW_ = 7, F_SETOWN_ = 8, F_GETOWN_ = 9, F_SETSIG_ = 10, F_GETSIG_ = 11, F_OFD_GETLK_ = 36,
             F_OFD_SETLK_ = 37, F_OFD_SETLKW_ = 38, F_SETLEASE_ = 1024, F_GETLEASE_ = 1025, F_DUPFD_CLOEXEC_ = 1030,
             F_SETPIPE_SZ_ = 1031, F_GETPIPE_SZ_ = 1032, F_ADD_SEALS_ = 1033, F_GET_SEALS_ = 1034 };
constexpr int FD_CLOEXEC_ = 1;

// poll
enum : int16_t { POLLIN_ = 0x001, POLLPRI_ = 0x002, POLLOUT_ = 0x004, POLLERR_ = 0x008, POLLHUP_ = 0x010,
                 POLLNVAL_ = 0x020, POLLRDNORM_ = 0x040, POLLRDBAND_ = 0x080, POLLWRNORM_ = 0x100,
                 POLLWRBAND_ = 0x200, POLLRDHUP_ = 0x2000 };
enum : uint32_t { EPOLLIN_ = 0x001, EPOLLPRI_ = 0x002, EPOLLOUT_ = 0x004, EPOLLERR_ = 0x008, EPOLLHUP_ = 0x010,
                  EPOLLRDNORM_ = 0x040, EPOLLWRNORM_ = 0x100, EPOLLRDHUP_ = 0x2000, EPOLLEXCLUSIVE_ = 1u << 28,
                  EPOLLWAKEUP_ = 1u << 29, EPOLLONESHOT_ = 1u << 30, EPOLLET_ = 1u << 31 };
enum : int { EPOLL_CTL_ADD_ = 1, EPOLL_CTL_DEL_ = 2, EPOLL_CTL_MOD_ = 3 };

// sockets (Linux numbering)
enum : int { AF_UNSPEC_ = 0, AF_UNIX_ = 1, AF_INET_ = 2, AF_INET6_ = 10, AF_NETLINK_ = 16 };
enum : int { SOCK_STREAM_ = 1, SOCK_DGRAM_ = 2, SOCK_RAW_ = 3, SOCK_SEQPACKET_ = 5, SOCK_NONBLOCK_ = 04000,
             SOCK_CLOEXEC_ = 02000000 };
enum : int { SOL_SOCKET_ = 1, IPPROTO_TCP_ = 6, IPPROTO_UDP_ = 17, IPPROTO_IP_ = 0, IPPROTO_IPV6_ = 41 };
enum : int { SO_DEBUG_ = 1, SO_REUSEADDR_ = 2, SO_TYPE_ = 3, SO_ERROR_ = 4, SO_DONTROUTE_ = 5, SO_BROADCAST_ = 6,
             SO_SNDBUF_ = 7, SO_RCVBUF_ = 8, SO_KEEPALIVE_ = 9, SO_OOBINLINE_ = 10, SO_LINGER_ = 13,
             SO_REUSEPORT_ = 15, SO_PASSCRED_ = 16, SO_PEERCRED_ = 17, SO_RCVLOWAT_ = 18, SO_SNDLOWAT_ = 19,
             SO_RCVTIMEO_ = 20, SO_SNDTIMEO_ = 21, SO_ACCEPTCONN_ = 30, SO_DOMAIN_ = 39, SO_PROTOCOL_ = 38 };
enum : int { TCP_NODELAY_ = 1, TCP_KEEPIDLE_ = 4, TCP_KEEPINTVL_ = 5, TCP_KEEPCNT_ = 6 };
enum : int { MSG_OOB_ = 1, MSG_PEEK_ = 2, MSG_DONTROUTE_ = 4, MSG_TRUNC_ = 0x20, MSG_DONTWAIT_ = 0x40,
             MSG_WAITALL_ = 0x100, MSG_NOSIGNAL_ = 0x4000, MSG_CMSG_CLOEXEC_ = 0x40000000 };
enum : int { SHUT_RD_ = 0, SHUT_WR_ = 1, SHUT_RDWR_ = 2 };

// ioctl
enum : uint32_t { TCGETS_ = 0x5401, TIOCGWINSZ_ = 0x5413, FIONREAD_ = 0x541b, FIONBIO_ = 0x5421, FIOCLEX_ = 0x5451,
                  FIONCLEX_ = 0x5452, FIOASYNC_ = 0x5452 + 0 };

// prctl
enum : int { PR_SET_PDEATHSIG_ = 1, PR_GET_PDEATHSIG_ = 2, PR_GET_DUMPABLE_ = 3, PR_SET_DUMPABLE_ = 4,
             PR_SET_NAME_ = 15, PR_GET_NAME_ = 16, PR_SET_SECCOMP_ = 22, PR_CAPBSET_READ_ = 23,
             PR_SET_TIMERSLACK_ = 29, PR_SET_NO_NEW_PRIVS_ = 38, PR_GET_NO_NEW_PRIVS_ = 39,
             PR_SET_THP_DISABLE_ = 41, PR_SET_VMA_ = 0x53564d41, PR_SET_TAGGED_ADDR_CTRL_ = 55,
             PR_GET_TAGGED_ADDR_CTRL_ = 56, PR_PAC_RESET_KEYS_ = 54, PR_SET_MM_ = 35, PR_MTE_TCF_SHIFT_ = 1 };

// auxv
enum : uint64_t {
    AT_NULL_ = 0, AT_IGNORE_ = 1, AT_EXECFD_ = 2, AT_PHDR_ = 3, AT_PHENT_ = 4, AT_PHNUM_ = 5, AT_PAGESZ_ = 6,
    AT_BASE_ = 7, AT_FLAGS_ = 8, AT_ENTRY_ = 9, AT_NOTELF_ = 10, AT_UID_ = 11, AT_EUID_ = 12, AT_GID_ = 13,
    AT_EGID_ = 14, AT_PLATFORM_ = 15, AT_HWCAP_ = 16, AT_CLKTCK_ = 17, AT_SECURE_ = 23, AT_BASE_PLATFORM_ = 24,
    AT_RANDOM_ = 25, AT_HWCAP2_ = 26, AT_EXECFN_ = 31, AT_SYSINFO_EHDR_ = 33, AT_MINSIGSTKSZ_ = 51,
};

// arm64 HWCAP bits
enum : uint64_t {
    HWCAP_FP_ = 1 << 0, HWCAP_ASIMD_ = 1 << 1, HWCAP_EVTSTRM_ = 1 << 2, HWCAP_AES_ = 1 << 3, HWCAP_PMULL_ = 1 << 4,
    HWCAP_SHA1_ = 1 << 5, HWCAP_SHA2_ = 1 << 6, HWCAP_CRC32_ = 1 << 7, HWCAP_ATOMICS_ = 1 << 8, HWCAP_FPHP_ = 1 << 9,
    HWCAP_ASIMDHP_ = 1 << 10, HWCAP_CPUID_ = 1 << 11, HWCAP_ASIMDRDM_ = 1 << 12, HWCAP_JSCVT_ = 1 << 13,
    HWCAP_FCMA_ = 1 << 14, HWCAP_LRCPC_ = 1 << 15, HWCAP_DCPOP_ = 1 << 16, HWCAP_SHA3_ = 1 << 17,
    HWCAP_ASIMDDP_ = 1 << 20, HWCAP_SHA512_ = 1 << 21,
};

// Kernel-layout structures (arm64).
#pragma pack(push, 1)
struct kstat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t pad1;
    int64_t st_size;
    int32_t st_blksize;
    int32_t pad2;
    int64_t st_blocks;
    int64_t st_atime_sec;
    uint64_t st_atime_nsec;
    int64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    int64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
    uint32_t unused4;
    uint32_t unused5;
};
static_assert(sizeof(kstat) == 128);

struct kstatx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t reserved;
};
struct kstatx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t spare0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    kstatx_timestamp stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32_t stx_rdev_major, stx_rdev_minor, stx_dev_major, stx_dev_minor;
    uint64_t spare2[14];
};
static_assert(sizeof(kstatx) == 256);

struct kstatfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    int32_t f_fsid[2];
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};
static_assert(sizeof(kstatfs) == 120);

struct ktimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
struct ktimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
struct kitimerspec {
    ktimespec it_interval;
    ktimespec it_value;
};

struct kutsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

struct ksysinfo {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t pad2;
    uint64_t totalhigh, freehigh;
    uint32_t mem_unit;
    char reserved[4];
};
static_assert(sizeof(ksysinfo) == 112);

struct krlimit {
    uint64_t cur, max;
};

struct krusage {
    ktimeval ru_utime, ru_stime;
    int64_t ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock,
        ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};

struct kiovec {
    uint64_t base;
    uint64_t len;
};

struct kpollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

struct kepoll_event {  // not packed on arm64: data is at offset 8
    uint32_t events;
    uint32_t pad;
    uint64_t data;
};
static_assert(sizeof(kepoll_event) == 16);

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
};

struct kstack_t {
    uint64_t ss_sp;
    int32_t ss_flags;
    int32_t pad;
    uint64_t ss_size;
};

struct ksiginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t pad0;
    union {
        struct {
            int32_t pid;
            uint32_t uid;
        } kill;
        struct {
            uint64_t addr;
        } fault;
        struct {
            int32_t tid;
            int32_t overrun;
            uint64_t value;
        } timer;
        struct {
            int32_t pid;
            uint32_t uid;
            uint64_t value;
        } rt;
        uint8_t raw[112];
    } fields;
};
static_assert(sizeof(ksiginfo) == 128);

struct kmsghdr {
    uint64_t msg_name;
    uint32_t msg_namelen;
    uint32_t pad0;
    uint64_t msg_iov;
    uint64_t msg_iovlen;
    uint64_t msg_control;
    uint64_t msg_controllen;
    int32_t msg_flags;
    int32_t pad1;
};
static_assert(sizeof(kmsghdr) == 56);

struct kcmsghdr {
    uint64_t cmsg_len;
    int32_t cmsg_level;
    int32_t cmsg_type;
};

struct klinger {
    int32_t l_onoff;
    int32_t l_linger;
};

struct kflock {
    int16_t l_type;
    int16_t l_whence;
    int32_t pad;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int32_t pad2;
};
#pragma pack(pop)

}  // namespace axrb::linux_abi
