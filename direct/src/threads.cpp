#include "cpu.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "time_math.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <mutex>

namespace axrb {
using namespace linux_abi;

// ---------------------------------------------------------------------------
// Time

int64_t ticks_per_second() {
    static int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    return frequency;
}

int64_t ticks_now() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<int64_t>(now.QuadPart);
}

int64_t ticks_from_timespec_relative(const ktimespec& ts) {
    int64_t f = ticks_per_second();
    if (ts.tv_sec < 0) return 0;
    return add_time(scale_time(ts.tv_sec, f, 1), scale_time(ts.tv_nsec, f, 1000000000));
}

int64_t monotonic_ns() {
    int64_t t = ticks_now(), f = ticks_per_second();
    return (t / f) * 1000000000 + (t % f) * 1000000000 / f;
}

int64_t realtime_ns() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>((t - 116444736000000000ull) * 100);
}

// ---------------------------------------------------------------------------
// Threads and blocking

GuestThread::GuestThread() {
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

GuestThread::~GuestThread() {
    if (event) CloseHandle(event);
}

static thread_local HANDLE t_timer = nullptr;

static bool signal_ready(GuestThread& thread) {
    uint64_t pending = thread.pending.load(std::memory_order_acquire);
    return (pending & ~thread.sigmask) != 0;
}

int64_t block_until(GuestThread& thread, const std::function<bool()>& ready, int64_t deadline, const char* why) {
    for (;;) {
        if (ready()) return 0;
        if (signal_ready(thread)) return -EINTR_;
        if (deadline == -1) return -ETIMEDOUT_;
        HANDLE handles[2] = {thread.event, nullptr};
        DWORD count = 1;
        DWORD wait = INFINITE;
        if (deadline > 0) {
            int64_t remaining = deadline - ticks_now();
            if (remaining <= 0) return ready() ? 0 : -ETIMEDOUT_;
            int64_t hundred_ns = scale_time(remaining, 10000000, ticks_per_second());
            if (!t_timer)
                t_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (t_timer) {
                LARGE_INTEGER due;
                due.QuadPart = -std::max<int64_t>(1, hundred_ns);
                SetWaitableTimer(t_timer, &due, 0, nullptr, nullptr, FALSE);
                handles[count++] = t_timer;
            } else {
                // INFINITE is reserved by Win32; a finite long wait must not
                // truncate to zero or accidentally become that sentinel.
                wait = static_cast<DWORD>(std::clamp<int64_t>(hundred_ns / 10000, 1, MAXDWORD - 1));
            }
        }
        thread.blocked_in.store(why, std::memory_order_relaxed);
        WaitForMultipleObjects(count, handles, FALSE, wait);
        thread.blocked_in.store(nullptr, std::memory_order_relaxed);
        if (count == 2) CancelWaitableTimer(t_timer);
    }
}

void WaitQueue::add(GuestThread* thread) {
    std::lock_guard<std::mutex> held(lock_);
    waiters_.push_back(thread);
}

void WaitQueue::remove(GuestThread* thread) {
    std::lock_guard<std::mutex> held(lock_);
    auto it = std::find(waiters_.begin(), waiters_.end(), thread);
    if (it != waiters_.end()) waiters_.erase(it);
}

void WaitQueue::wake_all() {
    std::lock_guard<std::mutex> held(lock_);
    for (GuestThread* thread : waiters_) SetEvent(thread->event);
}

// ---------------------------------------------------------------------------
// Futexes

namespace {

struct FutexWaiter {
    uint64_t address;
    uint32_t bitset;
    GuestThread* thread;
    std::atomic<bool> woken{false};
};

struct Bucket {
    std::mutex lock;
    std::list<FutexWaiter*> waiters;
};

Bucket g_buckets[1024];
Bucket& bucket_for(uint64_t address) { return g_buckets[(address >> 2) % 1024]; }

int wake_locked(Bucket& b, uint64_t address, int count, uint32_t bitset) {
    int woken = 0;
    for (auto it = b.waiters.begin(); it != b.waiters.end() && woken < count;) {
        FutexWaiter* w = *it;
        if (w->address == address && (w->bitset & bitset)) {
            it = b.waiters.erase(it);
            w->woken.store(true, std::memory_order_release);
            SetEvent(w->thread->event);
            ++woken;
        } else {
            ++it;
        }
    }
    return woken;
}

int64_t futex_wait(GuestThread& t, uint64_t address, uint32_t expected, int64_t deadline, uint32_t bitset) {
    if (!bitset) return -EINVAL_;
    if (address & 3) return -EINVAL_;
    FutexWaiter waiter{address, bitset, &t};
    Bucket& b = bucket_for(address);
    {
        std::lock_guard<std::mutex> held(b.lock);
        uint32_t current = 0;
        if (!guest_read(address, &current, 4)) return -EFAULT_;
        if (current != expected) return -EAGAIN_;
        b.waiters.push_back(&waiter);
    }
    int64_t r = block_until(t, [&] { return waiter.woken.load(std::memory_order_acquire); }, deadline, "futex");
    std::lock_guard<std::mutex> held(b.lock);
    if (waiter.woken.load(std::memory_order_acquire)) return 0;
    b.waiters.remove(&waiter);
    return r == 0 ? 0 : r;
}

int64_t futex_wake(uint64_t address, int count, uint32_t bitset) {
    if (!bitset) return -EINVAL_;
    Bucket& b = bucket_for(address);
    std::lock_guard<std::mutex> held(b.lock);
    return wake_locked(b, address, count, bitset);
}

int64_t futex_requeue(uint64_t from, uint64_t to, int wake, int requeue, bool compare, uint32_t expected) {
    Bucket& a = bucket_for(from);
    Bucket& b = bucket_for(to);
    std::unique_lock<std::mutex> first(&a < &b ? a.lock : b.lock, std::defer_lock);
    std::unique_lock<std::mutex> second(&a < &b ? b.lock : a.lock, std::defer_lock);
    first.lock();
    if (&a != &b) second.lock();
    if (compare) {
        uint32_t current = 0;
        if (!guest_read(from, &current, 4)) return -EFAULT_;
        if (current != expected) return -EAGAIN_;
    }
    int woken = wake_locked(a, from, wake, ~0u);
    int moved = 0;
    for (auto it = a.waiters.begin(); it != a.waiters.end() && moved < requeue;) {
        FutexWaiter* w = *it;
        if (w->address == from) {
            it = a.waiters.erase(it);
            w->address = to;
            b.waiters.push_back(w);
            ++moved;
        } else {
            ++it;
        }
    }
    return woken + moved;
}

bool futex_atomic_op(uint64_t address, uint32_t encoded_op, uint32_t oparg, uint32_t& old) {
    auto* word = reinterpret_cast<volatile long*>(address);
    __try {
        long prev = *word, next;
        for (;;) {
            uint32_t v = static_cast<uint32_t>(prev);
            switch (encoded_op) {
                case 0: next = static_cast<long>(oparg); break;
                case 1: next = static_cast<long>(v + oparg); break;
                case 2: next = static_cast<long>(v | oparg); break;
                case 3: next = static_cast<long>(v & ~oparg); break;
                default: next = static_cast<long>(v ^ oparg); break;
            }
            long seen = _InterlockedCompareExchange(word, next, prev);
            if (seen == prev) break;
            prev = seen;
        }
        old = static_cast<uint32_t>(prev);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int64_t futex_wake_op(uint64_t uaddr, uint64_t uaddr2, int count, int count2, uint32_t op) {
    Bucket& a = bucket_for(uaddr);
    Bucket& b = bucket_for(uaddr2);
    std::unique_lock<std::mutex> first(&a < &b ? a.lock : b.lock, std::defer_lock);
    std::unique_lock<std::mutex> second(&a < &b ? b.lock : a.lock, std::defer_lock);
    first.lock();
    if (&a != &b) second.lock();
    uint32_t encoded_op = (op >> 28) & 7, cmp = (op >> 24) & 15;
    uint32_t oparg = (op >> 12) & 0xfff, cmparg = op & 0xfff;
    if ((op >> 28) & 8) oparg = 1u << (oparg & 31);
    uint32_t old = 0;
    if (!futex_atomic_op(uaddr2, encoded_op, oparg, old)) return -EFAULT_;
    int woken = wake_locked(a, uaddr, count, ~0u);
    bool hit;
    switch (cmp) {
        case 0: hit = old == cmparg; break;
        case 1: hit = old != cmparg; break;
        case 2: hit = static_cast<int32_t>(old) < static_cast<int32_t>(cmparg); break;
        case 3: hit = static_cast<int32_t>(old) <= static_cast<int32_t>(cmparg); break;
        case 4: hit = static_cast<int32_t>(old) > static_cast<int32_t>(cmparg); break;
        default: hit = static_cast<int32_t>(old) >= static_cast<int32_t>(cmparg); break;
    }
    if (hit) woken += wake_locked(b, uaddr2, count2, ~0u);
    return woken;
}

// Priority inheritance is not modelled; ownership and hand-off follow the
// kernel's protocol on the futex word.
int64_t futex_lock_pi(GuestThread& t, uint64_t address, int64_t deadline, bool try_only) {
    auto* word = reinterpret_cast<volatile long*>(address);
    for (;;) {
        long value;
        if (!guest_read(address, &value, 4)) return -EFAULT_;
        uint32_t v = static_cast<uint32_t>(value);
        if ((v & FUTEX_TID_MASK_) == static_cast<uint32_t>(t.tid)) return -EDEADLK_;
        if ((v & FUTEX_TID_MASK_) == 0) {
            long desired = static_cast<long>(static_cast<uint32_t>(t.tid) | (v & FUTEX_WAITERS_));
            if (_InterlockedCompareExchange(word, desired, value) == value) return 0;
            continue;
        }
        if (try_only) return -EAGAIN_;
        if (!(v & FUTEX_WAITERS_)) {
            if (_InterlockedCompareExchange(word, static_cast<long>(v | FUTEX_WAITERS_), value) != value) continue;
            v |= FUTEX_WAITERS_;
        }
        int64_t r = futex_wait(t, address, v, deadline, ~0u);
        if (r == -ETIMEDOUT_ || r == -EINTR_) return r;
    }
}

int64_t futex_unlock_pi(GuestThread& t, uint64_t address) {
    auto* word = reinterpret_cast<volatile long*>(address);
    long value;
    if (!guest_read(address, &value, 4)) return -EFAULT_;
    if ((static_cast<uint32_t>(value) & FUTEX_TID_MASK_) != static_cast<uint32_t>(t.tid)) return -EPERM_;
    _InterlockedExchange(word, 0);
    futex_wake(address, 1, ~0u);
    return 0;
}

int64_t timeout_deadline(uint64_t timeout_addr, bool absolute, bool realtime, int64_t& deadline) {
    deadline = 0;
    if (!timeout_addr) return 0;
    ktimespec ts;
    if (!guest_read(timeout_addr, &ts, sizeof ts)) return -EFAULT_;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return -EINVAL_;
    if (absolute) {
        int64_t target = add_time(scale_time(ts.tv_sec, 1000000000, 1), ts.tv_nsec);
        int64_t now = realtime ? realtime_ns() : monotonic_ns();
        int64_t left = target - now;
        deadline = left <= 0 ? -1 : add_time(ticks_now(), scale_time(left, ticks_per_second(), 1000000000));
    } else {
        int64_t ticks = ticks_from_timespec_relative(ts);
        deadline = ticks == 0 ? -1 : add_time(ticks_now(), ticks);
    }
    return 0;
}

}  // namespace

int64_t do_futex(GuestThread& t, uint64_t uaddr, int op, uint32_t val, uint64_t timeout, uint64_t uaddr2, uint32_t val3) {
    int cmd = op & 127;
    bool realtime = (op & FUTEX_CLOCK_REALTIME_) != 0;
    switch (cmd) {
        case FUTEX_WAIT_: {
            int64_t deadline;
            if (int64_t e = timeout_deadline(timeout, false, false, deadline)) return e;
            return futex_wait(t, uaddr, val, deadline, ~0u);
        }
        case FUTEX_WAIT_BITSET_: {
            int64_t deadline;
            if (int64_t e = timeout_deadline(timeout, true, realtime, deadline)) return e;
            const int64_t result = futex_wait(t, uaddr, val, deadline, val3);
            if (trace_enabled("wait")) {
                static thread_local unsigned reports = 0;
                if (reports++ < 16) {
                    ktimespec ts{};
                    if (timeout) guest_read(timeout, &ts, sizeof ts);
                    LOGI("wait", "tid=%d name=%s op=%x timeout=%lld.%09lld realtime=%d now=%lld deadline=%lld result=%lld",
                         t.tid, t.name.c_str(), op, (long long)ts.tv_sec, (long long)ts.tv_nsec,
                         realtime, (long long)(realtime ? realtime_ns() : monotonic_ns()),
                         (long long)deadline, (long long)result);
                }
            }
            return result;
        }
        case FUTEX_WAKE_: return futex_wake(uaddr, static_cast<int>(val), ~0u);
        case FUTEX_WAKE_BITSET_: return futex_wake(uaddr, static_cast<int>(val), val3);
        case FUTEX_REQUEUE_:
            return futex_requeue(uaddr, uaddr2, static_cast<int>(val), static_cast<int>(timeout), false, 0);
        case FUTEX_CMP_REQUEUE_:
            return futex_requeue(uaddr, uaddr2, static_cast<int>(val), static_cast<int>(timeout), true, val3);
        case FUTEX_WAKE_OP_:
            return futex_wake_op(uaddr, uaddr2, static_cast<int>(val), static_cast<int>(timeout), val3);
        case FUTEX_LOCK_PI_:
        case FUTEX_LOCK_PI2_: {
            int64_t deadline;
            if (int64_t e = timeout_deadline(timeout, true, cmd == FUTEX_LOCK_PI_ || realtime, deadline)) return e;
            return futex_lock_pi(t, uaddr, deadline, false);
        }
        case FUTEX_TRYLOCK_PI_: return futex_lock_pi(t, uaddr, 0, true);
        case FUTEX_UNLOCK_PI_: return futex_unlock_pi(t, uaddr);
        default:
            LOGW("futex", "unsupported futex op %d", op);
            return -ENOSYS_;
    }
}

// ---------------------------------------------------------------------------
// Thread lifecycle

static void register_thread(GuestThread* thread) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    process().threads[thread->tid] = thread;
}

static void unregister_thread(GuestThread* thread) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    process().threads.erase(thread->tid);
}

int64_t wake_futex(uint64_t address, int count) { return futex_wake(address, count, ~0u); }
int64_t wake_futex_bitset(uint64_t address, int count, uint32_t bitset) { return futex_wake(address, count, bitset); }

GuestThread* find_thread(int tid) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    auto it = process().threads.find(tid);
    return it == process().threads.end() ? nullptr : it->second;
}

void register_main_thread(GuestThread* thread) { register_thread(thread); }

// The end of a thread, as the kernel does it: CLONE_CHILD_CLEARTID's word is
// zeroed and woken (pthread_join waits on it), robust futexes are released.
void finish_thread(GuestThread& thread) {
    if (thread.robust_list) {
        // struct robust_list_head { next; futex_offset; list_op_pending; }
        uint64_t head[3] = {};
        if (guest_read(thread.robust_list, head, sizeof head)) {
            auto release = [&](uint64_t entry) {
                uint64_t word = entry + head[1];
                uint32_t value = 0;
                if (!guest_read(word, &value, 4)) return;
                if ((value & FUTEX_TID_MASK_) != static_cast<uint32_t>(thread.tid)) return;
                uint32_t updated = (value & FUTEX_WAITERS_) | FUTEX_OWNER_DIED_;
                guest_write(word, &updated, 4);
                if (value & FUTEX_WAITERS_) futex_wake(word, 1, ~0u);
            };
            uint64_t entry = head[0];
            for (int guard = 0; entry && entry != thread.robust_list && guard < 2048; ++guard) {
                release(entry);
                if (!guest_read(entry, &entry, 8)) break;
            }
            if (head[2]) release(head[2]);
        }
    }
    if (thread.clear_child_tid) {
        uint32_t zero = 0;
        guest_write(thread.clear_child_tid, &zero, 4);
        futex_wake(thread.clear_child_tid, 1 << 30, ~0u);
    }
    unregister_thread(&thread);
}

static DWORD WINAPI thread_entry(void* param) {
    auto* thread = static_cast<GuestThread*>(param);
    set_current_thread(thread);
    attach_cpu(*thread);
    CpuContext* start = static_cast<CpuContext*>(thread->java_thread);  // the child's first register state
    thread->java_thread = nullptr;
    load_context(*thread, *start);
    delete start;
    try {
        run_guest(*thread, false);
    } catch (const ThreadExit& exit) {
        thread->exit_code = exit.code;
    }
    finish_thread(*thread);
    detach_cpu(*thread);
    if (thread->host_thread) CloseHandle(thread->host_thread);
    delete thread;
    return 0;
}

// Host-side stacks are large because the Java interpreter and nested guest
// calls recurse on them; only touched pages are committed.
constexpr SIZE_T kHostStack = 64ull << 20;

static int64_t do_clone(GuestThread& parent, uint64_t flags, uint64_t newsp, uint64_t ptid, uint64_t tls, uint64_t ctid) {
    if (!(flags & CLONE_VM_) || (flags & CLONE_VFORK_)) {
        // fork/vfork: a second address space is not something one process can
        // provide. Callers (crash handlers, Runtime.exec) see the failure.
        LOGW("thread", "fork-style clone (flags %llx) refused", (unsigned long long)flags);
        return -ENOSYS_;
    }
    auto* child = new GuestThread();
    child->tid = process().next_tid.fetch_add(1);
    child->tpidr = (flags & CLONE_SETTLS_) ? tls : parent.tpidr;
    child->sigmask = parent.sigmask;
    child->name = parent.name;
    if (flags & CLONE_CHILD_CLEARTID_) child->clear_child_tid = ctid;
    auto* start = new CpuContext();
    save_context(parent, *start);
    start->x[0] = 0;
    if (newsp) start->sp = newsp;
    child->java_thread = start;
    if (flags & CLONE_PARENT_SETTID_) guest_write(ptid, &child->tid, 4);
    if (flags & CLONE_CHILD_SETTID_) guest_write(ctid, &child->tid, 4);
    register_thread(child);
    HANDLE h = CreateThread(nullptr, kHostStack, thread_entry, child, CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION,
                            &child->host_tid);
    if (!h) {
        unregister_thread(child);
        delete start;
        delete child;
        return -EAGAIN_;
    }
    child->host_thread = h;
    int tid = child->tid;
    TRACE("thread", "clone flags %llx -> tid %d", (unsigned long long)flags, tid);
    ResumeThread(h);
    return tid;
}

static int nice_to_priority(int nice) {
    if (nice <= -16) return THREAD_PRIORITY_HIGHEST;
    if (nice <= -8) return THREAD_PRIORITY_ABOVE_NORMAL;
    if (nice < 5) return THREAD_PRIORITY_NORMAL;
    if (nice < 15) return THREAD_PRIORITY_BELOW_NORMAL;
    return THREAD_PRIORITY_LOWEST;
}

int guest_cpu_count() {
    static int count = [] {
        if (const char* text = std::getenv("AXRB_DIRECT_CPUS")) return std::clamp(std::atoi(text), 1, 64);
        // The processors this process may run on: fewer than the machine's
        // when the player (or a core-parking tool) restricts it.
        int processors = 0;
        DWORD_PTR allowed = 0, system = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system)) processors = static_cast<int>(__popcnt64(allowed));
        if (!processors) {
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            processors = static_cast<int>(info.dwNumberOfProcessors);
        }
        return std::clamp(processors, 2, 8);
    }();
    return count;
}

int64_t sys_threads(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    switch (nr) {
        case 98:  // futex
            return do_futex(t, a[0], static_cast<int>(a[1]), static_cast<uint32_t>(a[2]), a[3], a[4], static_cast<uint32_t>(a[5]));
        case 220:  // clone(flags, newsp, ptid, tls, ctid)
            return do_clone(t, a[0], a[1], a[2], a[3], a[4]);
        case 435: {  // clone3
            uint64_t args[11] = {};
            if (!guest_read(a[0], args, std::min<uint64_t>(a[1], sizeof args))) return -EFAULT_;
            // flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls
            uint64_t sp = args[5] ? args[5] + args[6] : 0;
            return do_clone(t, args[0] | (args[4] & 0xff), sp, args[3], args[7], args[2]);
        }
        case 93:  // exit
            throw ThreadExit{static_cast<int>(a[0])};
        case 94: {  // exit_group
            LOGI("process", "exit_group(%d)", static_cast<int>(a[0]));
            if (trace_enabled("exit")) {
                dump_thread_state(t, "exit_group caller (diagnostic trace)");
                CpuContext context;
                save_context(t, context);
                auto describe_mapping = [](uint64_t address) {
                    address_space().for_each([&](const Vma& vma) {
                        if (address >= vma.start && address < vma.end)
                            LOGI("process", "exit address %llx: %s file-offset=%llx mapping=%llx-%llx",
                                 (unsigned long long)address, vma.name.c_str(),
                                 (unsigned long long)(vma.offset + address - vma.start),
                                 (unsigned long long)vma.start, (unsigned long long)vma.end);
                    });
                };
                describe_mapping(context.pc);
                describe_mapping(context.x[30]);
                uint64_t fp = context.x[29];
                for (int depth = 0; depth < 16 && fp; ++depth) {
                    uint64_t frame[2];
                    if ((fp & 7) || !guest_read(fp, frame, sizeof frame)) break;
                    describe_mapping(frame[1]);
                    if (frame[0] <= fp) break;
                    fp = frame[0];
                }
            }
            process().exiting = true;
            log_flush();
            TerminateProcess(GetCurrentProcess(), static_cast<UINT>(a[0] & 0xff));
            return 0;
        }
        case 96:  // set_tid_address
            t.clear_child_tid = a[0];
            return t.tid;
        case 99:  // set_robust_list
            t.robust_list = a[0];
            t.robust_list_len = a[1];
            return 0;
        case 100: {  // get_robust_list
            GuestThread* target = a[0] ? find_thread(static_cast<int>(a[0])) : &t;
            if (!target) return -ESRCH_;
            guest_write(a[1], &target->robust_list, 8);
            guest_write(a[2], &target->robust_list_len, 8);
            return 0;
        }
        case 178: return t.tid;                       // gettid
        case 172: return process().pid;               // getpid
        case 173: return 1;                           // getppid
        case 124: SwitchToThread(); return 0;         // sched_yield
        case 122: return 0;                           // sched_setaffinity
        case 123: {                                   // sched_getaffinity
            uint64_t size = a[1];
            if (size < 8) return -EINVAL_;
            uint64_t mask = (guest_cpu_count() >= 64) ? ~0ull : ((1ull << guest_cpu_count()) - 1);
            std::vector<uint8_t> bytes(static_cast<size_t>(std::min<uint64_t>(size, 128)), 0);
            std::memcpy(bytes.data(), &mask, 8);
            if (!guest_write(a[2], bytes.data(), bytes.size())) return -EFAULT_;
            return static_cast<int64_t>(bytes.size());
        }
        case 118: case 119: return 0;                 // sched_setparam, sched_setscheduler
        case 120: return 0;                           // sched_getscheduler: SCHED_OTHER
        case 121: {                                   // sched_getparam
            int32_t zero = 0;
            return guest_write(a[1], &zero, 4) ? 0 : -EFAULT_;
        }
        case 125: return (a[0] == 1 || a[0] == 2) ? 99 : 0;  // sched_get_priority_max
        case 126: return (a[0] == 1 || a[0] == 2) ? 1 : 0;   // sched_get_priority_min
        case 274: return 0;                           // sched_setattr
        case 275: return -ENOSYS_;                    // sched_getattr
        case 140: {                                   // setpriority(which, who, nice)
            if (a[0] == 0) {  // PRIO_PROCESS: a thread id on Linux
                GuestThread* target = a[1] ? find_thread(static_cast<int>(a[1])) : &t;
                if (target && target->host_thread) SetThreadPriority(target->host_thread, nice_to_priority(static_cast<int>(a[2])));
                else if (target == &t) SetThreadPriority(GetCurrentThread(), nice_to_priority(static_cast<int>(a[2])));
            }
            return 0;
        }
        case 141: return 20;                          // getpriority: nice 0
        case 168: {                                   // getcpu
            uint32_t cpu = GetCurrentProcessorNumber() % static_cast<uint32_t>(guest_cpu_count()), node = 0;
            if (a[0]) guest_write(a[0], &cpu, 4);
            if (a[1]) guest_write(a[1], &node, 4);
            return 0;
        }
        case 101:    // nanosleep
        case 115: {  // clock_nanosleep
            uint64_t req = nr == 101 ? a[0] : a[2], rem = nr == 101 ? a[1] : a[3];
            bool absolute = nr == 115 && (a[1] & TIMER_ABSTIME_);
            int64_t deadline;
            if (int64_t e = timeout_deadline(req, absolute, nr == 115 && a[0] == CLOCK_REALTIME_, deadline)) return e;
            if (deadline == -1) return 0;
            int64_t r = block_until(t, [] { return false; }, deadline, "sleep");
            if (r == -EINTR_) {
                if (rem && !absolute) {
                    int64_t left = scale_time(std::max<int64_t>(0, deadline - ticks_now()), 1000000000, ticks_per_second());
                    ktimespec out{left / 1000000000, left % 1000000000};
                    guest_write(rem, &out, sizeof out);
                }
                return -EINTR_;
            }
            return 0;
        }
        default:
            handled = false;
            return 0;
    }
}

}  // namespace axrb
