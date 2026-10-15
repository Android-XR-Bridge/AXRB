#include "kernel.h"

#include "cpu.h"
#include "files.h"
#include "log.h"
#include "memory.h"

#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace axrb {
using namespace linux_abi;

Process& process() {
    static Process instance;
    return instance;
}

// ---------------------------------------------------------------------------
// First-touch commits.

static LONG CALLBACK fault_handler(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        void* frames[32];
        USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
        LOGE("host-fault", "integer division by zero at %p", record->ExceptionAddress);
        for (USHORT i = 0; i < count; ++i) {
            HMODULE module = nullptr;
            char path[MAX_PATH]{};
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(frames[i]), &module);
            if (module) GetModuleFileNameA(module, path, MAX_PATH);
            LOGE("host-fault", "%s + 0x%llx", path,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)));
        }
    }
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR kind = record->ExceptionInformation[0];  // 0 read, 1 write, 8 execute
    uint64_t address = record->ExceptionInformation[1];
    if (address_space().handle_fault(address, kind == 1, kind == 8)) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

void install_fault_handler() {
    // First in line: Dynarmic's fastmem handler (an SEH function-table
    // handler) must only see accesses that are genuinely bad.
    AddVectoredExceptionHandler(1, fault_handler);
}

// ---------------------------------------------------------------------------
// Memory system calls.

int64_t sys_memory(GuestThread&, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    auto& space = address_space();
    switch (nr) {
        case 222: {  // mmap(addr, length, prot, flags, fd, offset)
            uint32_t prot = static_cast<uint32_t>(a[2]), flags = static_cast<uint32_t>(a[3]);
            std::shared_ptr<OpenFile> file;
            if (!(flags & MAP_ANONYMOUS_)) {
                file = fds().get(static_cast<int>(a[4]));
                if (!file) return -EBADF_;
                if (file->has_device_mapping()) return file->map_device(a[1], prot, flags, a[5]);
                if (uint64_t at = file->fixed_mapping()) return static_cast<int64_t>(at + a[5]);
                if (!file->mappable()) return -ENODEV_;
                if ((flags & MAP_SHARED_VALIDATE_) == MAP_SHARED_ && (prot & PROT_WRITE_) &&
                    (file->status_flags & O_ACCMODE_) == O_RDONLY_)
                    return -EACCES_;
            }
            bool fixed = (flags & MAP_FIXED_) != 0;
            if (fixed && space.any_exec(a[0], a[1])) invalidate_code(a[0], page_up(a[1]));
            int64_t r = space.mmap(a[0], a[1], prot, flags, std::move(file), a[5]);
            TRACE("mmap", "mmap(%llx, %llx, %x, %x, %d, %llx) = %llx", (unsigned long long)a[0],
                  (unsigned long long)a[1], prot, flags, static_cast<int>(a[4]), (unsigned long long)a[5],
                  (unsigned long long)r);
            return r;
        }
        case 215: {  // munmap
            bool exec = space.any_exec(a[0], a[1]);
            int64_t r = space.munmap(a[0], a[1]);
            if (exec && r == 0) invalidate_code(a[0], page_up(a[1]));
            TRACE("mmap", "munmap(%llx, %llx) = %lld", (unsigned long long)a[0], (unsigned long long)a[1], (long long)r);
            return r;
        }
        case 226: {  // mprotect
            bool exec_before = space.any_exec(a[0], a[1]);
            int64_t r = space.mprotect(a[0], a[1], static_cast<uint32_t>(a[2]));
            // Code that was writable may have been rewritten (JITs, relocations).
            if (r == 0 && (exec_before || (a[2] & PROT_EXEC_))) invalidate_code(a[0], page_up(a[1]));
            TRACE("mmap", "mprotect(%llx, %llx, %llx) = %lld", (unsigned long long)a[0], (unsigned long long)a[1],
                  (unsigned long long)a[2], (long long)r);
            return r;
        }
        case 216: {  // mremap
            bool exec = space.any_exec(a[0], a[1]);
            int64_t r = space.mremap(a[0], a[1], a[2], static_cast<uint32_t>(a[3]), a[4]);
            if (exec && r >= 0) invalidate_code(a[0], page_up(a[1]));
            return r;
        }
        case 233:  // madvise
            return space.madvise(a[0], a[1], static_cast<int>(a[2]));
        case 214:  // brk
            return space.brk(a[0]);
        case 227:  // msync
            return space.msync(a[0], a[1], static_cast<int>(a[2]));
        case 232: {  // mincore
            uint64_t pages = page_up(a[1]) / kPageSize;
            std::vector<uint8_t> out(static_cast<size_t>(pages));
            if (!space.check(a[0], a[1], 0)) return -ENOMEM_;
            space.mincore(a[0], a[1], out.data());
            return guest_write(a[2], out.data(), out.size()) ? 0 : -EFAULT_;
        }
        case 228: case 229: case 230: case 231: case 284:  // mlock, munlock, mlockall, munlockall, mlock2
            return 0;
        case 285: case 286: case 287:  // pkey_mprotect (as mprotect), pkey_alloc, pkey_free
            if (nr == 285) return space.mprotect(a[0], a[1], static_cast<uint32_t>(a[2]));
            return -ENOSYS_;
        case 283:  // membarrier
            if (a[0] == 0) return 1 | 8 | 16;  // QUERY: GLOBAL, PRIVATE_EXPEDITED, REGISTER_PRIVATE_EXPEDITED
            FlushProcessWriteBuffers();
            return 0;
        case 235: case 236: case 237: case 238: case 239:  // mbind, get/set_mempolicy, migrate/move_pages
            return 0;
        default:
            handled = false;
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Identity, time, limits and the rest.

static void filetime_to_timeval(const FILETIME& ft, ktimeval& tv) {
    uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;  // 100ns units
    tv.tv_sec = static_cast<int64_t>(t / 10000000);
    tv.tv_usec = static_cast<int64_t>((t % 10000000) / 10);
}

static int64_t filetime_ns(const FILETIME& ft) {
    uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>(t * 100);
}

static int64_t clock_ns(int clock, int64_t& out) {
    switch (clock) {
        case CLOCK_REALTIME_:
        case CLOCK_REALTIME_COARSE_:
        case CLOCK_REALTIME_ALARM_:
            out = realtime_ns();
            return 0;
        case CLOCK_MONOTONIC_:
        case CLOCK_MONOTONIC_RAW_:
        case CLOCK_MONOTONIC_COARSE_:
        case CLOCK_BOOTTIME_:
        case CLOCK_BOOTTIME_ALARM_:
            out = monotonic_ns();
            return 0;
        case CLOCK_PROCESS_CPUTIME_ID_: {
            FILETIME c, e, k, u;
            GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
            out = filetime_ns(k) + filetime_ns(u);
            return 0;
        }
        default:
            // CLOCK_THREAD_CPUTIME_ID and the per-thread cpu clocks bionic
            // builds from a tid (negative ids) both mean "this thread".
            if (clock == CLOCK_THREAD_CPUTIME_ID_ || clock < 0) {
                FILETIME c, e, k, u;
                if (GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) {
                    out = filetime_ns(k) + filetime_ns(u);
                    return 0;
                }
                out = monotonic_ns();
                return 0;
            }
            return -EINVAL_;
    }
}

bool fast_syscall(uint64_t* x) {
    const uint64_t nr = x[8];
    // Tagged pointers and every error case take the regular path.
    auto plain = [](uint64_t address) { return (address >> 56) == 0; };
    int64_t result = 0;
    switch (nr) {
        case 113: {  // clock_gettime
            int64_t ns;
            if (!plain(x[1]) || clock_ns(static_cast<int>(x[0]), ns)) return false;
            ktimespec ts{ns / 1000000000, ns % 1000000000};
            if (!guest_write(x[1], &ts, sizeof ts)) return false;
            break;
        }
        case 169: {  // gettimeofday without a timezone
            if (x[1] || !plain(x[0])) return false;
            if (x[0]) {
                int64_t ns = realtime_ns();
                ktimeval tv{ns / 1000000000, (ns % 1000000000) / 1000};
                if (!guest_write(x[0], &tv, sizeof tv)) return false;
            }
            break;
        }
        case 98: {  // futex wakes never block
            const int cmd = static_cast<int>(x[1]) & 127;
            if (!plain(x[0])) return false;
            if (cmd == FUTEX_WAKE_) result = wake_futex_bitset(x[0], static_cast<int>(x[2]), ~0u);
            else if (cmd == FUTEX_WAKE_BITSET_ && static_cast<uint32_t>(x[5])) result = wake_futex_bitset(x[0], static_cast<int>(x[2]), static_cast<uint32_t>(x[5]));
            else return false;
            if (result < 0) return false;
            break;
        }
        default:
            return false;
    }
    g_syscall_counts[nr].fetch_add(1, std::memory_order_relaxed);
    x[0] = static_cast<uint64_t>(result);
    return true;
}

static uint64_t total_memory() {
    MEMORYSTATUSEX status{sizeof status};
    GlobalMemoryStatusEx(&status);
    // A Quest 3 has 8 GiB; reporting far more than that changes engine
    // quality tiers, far less makes them refuse to start.
    return std::min<uint64_t>(status.ullTotalPhys, 8ull << 30);
}

static uint64_t free_memory() {
    MEMORYSTATUSEX status{sizeof status};
    GlobalMemoryStatusEx(&status);
    return std::min<uint64_t>(status.ullAvailPhys, total_memory() - std::min(total_memory(), address_space().mapped_bytes() / 8));
}

static void init_rlimits() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& r = process().rlimits;
    for (auto& limit : r) limit = {~0ull, ~0ull};
    r[3] = {8ull << 20, ~0ull};  // RLIMIT_STACK
    r[4] = {0, ~0ull};           // RLIMIT_CORE
    r[7] = {32768, 32768};       // RLIMIT_NOFILE
    r[6] = {16384, 16384};       // RLIMIT_NPROC
    r[8] = {64 << 10, 64 << 10}; // RLIMIT_MEMLOCK
}

int64_t sys_misc(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    switch (nr) {
        case 160: {  // uname
            kutsname u{};
            std::strcpy(u.sysname, "Linux");
            std::strcpy(u.nodename, "localhost");
            std::strcpy(u.release, "5.10.226-android12-9-axrb");
            std::strcpy(u.version, "#1 SMP PREEMPT");
            std::strcpy(u.machine, "aarch64");
            std::strcpy(u.domainname, "(none)");
            return guest_write(a[0], &u, sizeof u) ? 0 : -EFAULT_;
        }
        case 179: {  // sysinfo
            ksysinfo s{};
            s.uptime = monotonic_ns() / 1000000000;
            s.totalram = total_memory();
            s.freeram = free_memory();
            s.procs = static_cast<uint16_t>(process().threads.size());
            s.mem_unit = 1;
            return guest_write(a[0], &s, sizeof s) ? 0 : -EFAULT_;
        }
        case 163:    // getrlimit
        case 164:    // setrlimit
        case 261: {  // prlimit64(pid, resource, new, old)
            init_rlimits();
            uint64_t resource = nr == 261 ? a[1] : a[0];
            uint64_t new_limit = nr == 261 ? a[2] : (nr == 164 ? a[1] : 0);
            uint64_t old_limit = nr == 261 ? a[3] : (nr == 163 ? a[1] : 0);
            if (resource >= process().rlimits.size()) return -EINVAL_;
            if (old_limit && !guest_write(old_limit, &process().rlimits[resource], sizeof(krlimit))) return -EFAULT_;
            if (new_limit) {
                krlimit value;
                if (!guest_read(new_limit, &value, sizeof value)) return -EFAULT_;
                process().rlimits[resource] = value;
            }
            return 0;
        }
        case 166: {  // umask
            uint32_t old = process().umask;
            process().umask = static_cast<uint32_t>(a[0]) & 0777;
            return old;
        }
        case 167: {  // prctl
            int option = static_cast<int>(a[0]);
            switch (option) {
                case PR_SET_NAME_: {
                    std::string name;
                    if (!guest_string(a[1], name, 15)) return -EFAULT_;
                    t.name = name;
                    SetThreadDescription(GetCurrentThread(), widen(name).c_str());
                    TRACE("thread", "tid %d named %s", t.tid, name.c_str());
                    return 0;
                }
                case PR_GET_NAME_: {
                    char name[16] = {};
                    std::strncpy(name, t.name.c_str(), 15);
                    return guest_write(a[1], name, 16) ? 0 : -EFAULT_;
                }
                case PR_SET_VMA_: {
                    // PR_SET_VMA_ANON_NAME: (option, 0, addr, len, name)
                    std::string name;
                    if (a[4] && guest_string(a[4], name, 80)) address_space().name_range(a[2], a[3], "[anon:" + name + "]");
                    return 0;
                }
                case PR_GET_DUMPABLE_: return 1;
                case PR_GET_NO_NEW_PRIVS_: return 0;
                case PR_CAPBSET_READ_: return 0;
                case PR_GET_TAGGED_ADDR_CTRL_: return 0;
                case PR_SET_TAGGED_ADDR_CTRL_: return -EINVAL_;  // no MTE / TBI tagging
                case PR_GET_PDEATHSIG_: {
                    int32_t zero = 0;
                    return guest_write(a[1], &zero, 4) ? 0 : -EFAULT_;
                }
                case PR_PAC_RESET_KEYS_: return -EINVAL_;
                case 0x41555856:  // PR_SET_SYSCALL_USER_DISPATCH and friends
                    return -EINVAL_;
                default:
                    return 0;
            }
        }
        case 174: case 175: return process().uid;  // getuid, geteuid
        case 176: case 177: return process().gid;  // getgid, getegid
        case 148: case 150: {                     // getresuid, getresgid
            uint32_t id = nr == 148 ? process().uid : process().gid;
            for (int i = 0; i < 3; ++i)
                if (!guest_write(a[i], &id, 4)) return -EFAULT_;
            return 0;
        }
        case 158: {  // getgroups
            if (a[0] == 0) return 1;
            uint32_t gid = process().gid;
            return guest_write(a[1], &gid, 4) ? 1 : -EFAULT_;
        }
        case 143: case 144: case 145: case 146: case 147: case 149: case 151: case 152: case 159:
            return 0;  // set*id, setfsuid/gid, setgroups: the app is already who it is
        case 154: return 0;                     // setpgid
        case 155: case 156: return process().pid;  // getpgid, getsid
        case 157: return process().pid;         // setsid
        case 92: return 0;                      // personality
        case 90: {                              // capget
            if (a[1]) {
                uint32_t zero[6] = {};
                guest_write(a[1], zero, sizeof zero);
            }
            return 0;
        }
        case 91: return -EPERM_;                // capset
        case 278: {                             // getrandom
            uint64_t len = std::min<uint64_t>(a[1], 1 << 25);
            std::vector<uint8_t> bytes(static_cast<size_t>(len));
            if (len) BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            address_space().populate(a[0], len);
            return guest_write(a[0], bytes.data(), bytes.size()) ? static_cast<int64_t>(len) : -EFAULT_;
        }
        case 113: {  // clock_gettime
            int64_t ns;
            if (int64_t e = clock_ns(static_cast<int>(a[0]), ns)) return e;
            ktimespec ts{ns / 1000000000, ns % 1000000000};
            return guest_write(a[1], &ts, sizeof ts) ? 0 : -EFAULT_;
        }
        case 114: {  // clock_getres
            if (a[1]) {
                bool coarse = a[0] == CLOCK_REALTIME_COARSE_ || a[0] == CLOCK_MONOTONIC_COARSE_;
                ktimespec ts{0, coarse ? 4000000 : 1};
                if (!guest_write(a[1], &ts, sizeof ts)) return -EFAULT_;
            }
            return 0;
        }
        case 112: return -EPERM_;  // clock_settime
        case 169: {                // gettimeofday
            int64_t ns = realtime_ns();
            if (a[0]) {
                ktimeval tv{ns / 1000000000, (ns % 1000000000) / 1000};
                if (!guest_write(a[0], &tv, sizeof tv)) return -EFAULT_;
            }
            if (a[1]) {
                int32_t tz[2] = {0, 0};
                guest_write(a[1], tz, sizeof tz);
            }
            return 0;
        }
        case 153: {  // times
            FILETIME c, e, k, u;
            GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
            if (a[0]) {
                int64_t tms[4] = {filetime_ns(u) / 10000000, filetime_ns(k) / 10000000, 0, 0};
                guest_write(a[0], tms, sizeof tms);
            }
            return monotonic_ns() / 10000000;
        }
        case 165: {  // getrusage(who, usage)
            krusage r{};
            FILETIME c, e, k, u;
            if (static_cast<int>(a[0]) == 1)  // RUSAGE_THREAD
                GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
            else
                GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
            filetime_to_timeval(u, r.ru_utime);
            filetime_to_timeval(k, r.ru_stime);
            PROCESS_MEMORY_COUNTERS counters{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters))
                r.ru_maxrss = static_cast<int64_t>(counters.PeakWorkingSetSize / 1024);
            return guest_write(a[1], &r, sizeof r) ? 0 : -EFAULT_;
        }
        case 260: case 95: return -ECHILD_;  // wait4, waitid: never any children
        case 221: {                          // execve
            std::string path;
            guest_string(a[0], path);
            LOGW("process", "execve(%s) refused: one process image only", path.c_str());
            return -EACCES_;
        }
        case 117: return -EPERM_;   // ptrace
        case 116: return -EPERM_;   // syslog
        case 277: return -EINVAL_;  // seccomp
        case 293: return -ENOSYS_;  // rseq: bionic falls back
        case 270: case 271: return -ENOSYS_;  // process_vm_readv/writev
        case 272: return -ENOSYS_;  // kcmp
        case 282: return -ENOSYS_;  // userfaultfd
        case 424: case 434: case 438: return -ENOSYS_;  // pidfd_*
        case 107: case 108: case 109: case 110: case 111: return -ENOSYS_;  // timer_* (POSIX timers)
        case 0: case 1: case 2: case 3: case 4:  // io_setup ...
            return -ENOSYS_;
        case 425: case 426: case 427: return -ENOSYS_;  // io_uring
        case 89: return -EPERM_;    // acct
        case 280: return -ENOSYS_;  // bpf
        case 241: return -ENOSYS_;  // perf_event_open
        case 97: return -EPERM_;    // unshare
        case 268: return -EPERM_;   // setns
        case 40: case 39: return -EPERM_;  // mount, umount2
        case 41: return -EPERM_;    // pivot_root
        case 51: return -EPERM_;    // chroot
        case 142: return -EPERM_;   // reboot
        case 30: case 31: return 0; // ioprio_set, ioprio_get
        case 102: case 103: return -ENOSYS_;  // getitimer/setitimer (when signals does not)
        case 104: return -ENOSYS_;  // kexec_load
        case 105: case 106: case 273: case 313: return -EPERM_;  // init_module, delete_module, finit_module
        case 171: return 0;         // adjtimex: report TIME_OK
        case 266: return -EPERM_;   // clock_adjtime
        case 161: case 162: return -EPERM_;  // sethostname, setdomainname
        default:
            handled = false;
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Dispatch.

std::atomic<uint64_t> g_syscall_counts[512];
std::atomic<bool> g_syscall_stacks_enabled{false};
std::mutex g_syscall_stacks_lock;
std::map<std::pair<uint64_t, std::vector<uint64_t>>, uint64_t> g_syscall_stacks;

// Guest call stack (frame-pointer chain) of a sampled syscall, for the profiler.
static void sample_syscall_stack(GuestThread& thread, uint64_t nr) {
    std::vector<uint64_t> frames{get_pc(thread), get_reg(thread, 30)};
    uint64_t fp = get_reg(thread, 29);
    for (int depth = 0; depth < 10 && fp && (fp & 7) == 0; ++depth) {
        uint64_t record[2];
        if (!guest_read(fp, record, sizeof record)) break;
        frames.push_back(record[1]);
        if (record[0] <= fp) break;
        fp = record[0];
    }
    std::lock_guard<std::mutex> held(g_syscall_stacks_lock);
    ++g_syscall_stacks[{nr, frames}];
}

int64_t do_syscall(GuestThread& thread, uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5) {
    if (nr < 512) {
        const uint64_t n = g_syscall_counts[nr].fetch_add(1, std::memory_order_relaxed);
        if ((n & 4095) == 0 && g_syscall_stacks_enabled.load(std::memory_order_relaxed)) sample_syscall_stack(thread, nr);
    }
    uint64_t args[6] = {a0, a1, a2, a3, a4, a5};
    // Tagged pointers (TBI) are accepted by the kernel's tagged address ABI;
    // strip tags from anything that is a guest address under its tag.
    for (uint64_t& a : args) {
        uint64_t top = a >> 56;
        if (top && top != 0xff && address_space().overlaps_guest(untag(a), 1)) a = untag(a);
    }
    bool handled = false;
    int64_t result = sys_files(thread, nr, args, handled);
    if (!handled) result = sys_memory(thread, nr, args, handled);
    if (!handled) result = sys_threads(thread, nr, args, handled);
    if (!handled) result = sys_signals(thread, nr, args, handled);
    if (!handled) result = sys_net(thread, nr, args, handled);
    if (!handled) result = sys_misc(thread, nr, args, handled);
    if (!handled) {
        static std::atomic<int> reported[512];
        if (nr < 512 && reported[nr].fetch_add(1) < 3)
            LOGW("syscall", "unimplemented syscall %llu (%s) from %s", (unsigned long long)nr, syscall_name(nr),
                 describe_address(get_pc(thread)).c_str());
        result = -ENOSYS_;
    }
    TRACE("syscall", "[%d] %s(%llx, %llx, %llx, %llx) = %lld", thread.tid, syscall_name(nr), (unsigned long long)a0,
          (unsigned long long)a1, (unsigned long long)a2, (unsigned long long)a3, (long long)result);
    return result;
}

const char* syscall_name(uint64_t nr) {
    switch (nr) {
#define N(n, name) \
    case n: return #name;
        N(500, [lazy-commit faults]) N(501, [populate]) N(502, [fault already committed]) N(503, [code invalidations]) N(504, [invalidated pages]) N(505, [translator fetches])
        N(17, getcwd) N(19, eventfd2) N(20, epoll_create1) N(21, epoll_ctl) N(22, epoll_pwait) N(23, dup) N(24, dup3)
        N(25, fcntl) N(26, inotify_init1) N(27, inotify_add_watch) N(28, inotify_rm_watch) N(29, ioctl) N(32, flock)
        N(33, mknodat) N(34, mkdirat) N(35, unlinkat) N(36, symlinkat) N(37, linkat) N(38, renameat) N(43, statfs)
        N(44, fstatfs) N(45, truncate) N(46, ftruncate) N(47, fallocate) N(48, faccessat) N(49, chdir) N(50, fchdir)
        N(52, fchmod) N(53, fchmodat) N(54, fchownat) N(55, fchown) N(56, openat) N(57, close) N(59, pipe2)
        N(61, getdents64) N(62, lseek) N(63, read) N(64, write) N(65, readv) N(66, writev) N(67, pread64)
        N(68, pwrite64) N(69, preadv) N(70, pwritev) N(71, sendfile) N(72, pselect6) N(73, ppoll) N(74, signalfd4)
        N(78, readlinkat) N(79, newfstatat) N(80, fstat) N(81, sync) N(82, fsync) N(83, fdatasync) N(84, sync_file_range)
        N(85, timerfd_create) N(86, timerfd_settime) N(87, timerfd_gettime) N(88, utimensat) N(90, capget)
        N(93, exit) N(94, exit_group) N(96, set_tid_address) N(98, futex) N(99, set_robust_list) N(101, nanosleep)
        N(113, clock_gettime) N(114, clock_getres) N(115, clock_nanosleep) N(117, ptrace) N(122, sched_setaffinity)
        N(123, sched_getaffinity) N(124, sched_yield) N(129, kill) N(130, tkill) N(131, tgkill) N(132, sigaltstack)
        N(133, rt_sigsuspend) N(134, rt_sigaction) N(135, rt_sigprocmask) N(136, rt_sigpending)
        N(137, rt_sigtimedwait) N(139, rt_sigreturn) N(140, setpriority) N(141, getpriority) N(153, times)
        N(160, uname) N(163, getrlimit) N(165, getrusage) N(166, umask) N(167, prctl) N(168, getcpu)
        N(169, gettimeofday) N(172, getpid) N(173, getppid) N(174, getuid) N(175, geteuid) N(176, getgid)
        N(177, getegid) N(178, gettid) N(179, sysinfo) N(198, socket) N(199, socketpair) N(200, bind) N(201, listen)
        N(202, accept) N(203, connect) N(204, getsockname) N(205, getpeername) N(206, sendto) N(207, recvfrom)
        N(208, setsockopt) N(209, getsockopt) N(210, shutdown) N(211, sendmsg) N(212, recvmsg) N(214, brk)
        N(215, munmap) N(216, mremap) N(220, clone) N(221, execve) N(222, mmap) N(226, mprotect) N(227, msync)
        N(232, mincore) N(233, madvise) N(240, rt_tgsigqueueinfo) N(242, accept4) N(260, wait4) N(261, prlimit64)
        N(276, renameat2) N(278, getrandom) N(279, memfd_create) N(283, membarrier) N(291, statx) N(293, rseq)
        N(435, clone3) N(436, close_range) N(439, faccessat2)
#undef N
        default: return "?";
    }
}

}  // namespace axrb
