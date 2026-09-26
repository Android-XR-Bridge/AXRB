/* Threads, locks and per-thread keys for the guest.

   A guest thread is a real host thread running the interpreter over its own
   registers. Memory is shared, which is what the guest expects; the stack and
   the thread local block are carved from a span mapped once at the start, so
   no region is added to the map while another thread is walking it. */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "qb_env.h"
#include <windows.h>

#include "android.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace {

const uint64_t kDefaultStack = 1u << 20;
const uint64_t kAlign = 16;

}  // namespace

GuestLibc::~GuestLibc() {
    for (auto& entry : threads)
        if (entry.second->runner.joinable()) entry.second->runner.detach();
}

/* QB_LOCK_STATS: time spent waiting for locks, by where the holder took
   the lock and where the waiter asked, every ten seconds. */
static void note_lock_wait(GuestCpu& cpu, uint64_t va, uint64_t holder_site, uint64_t holder_tp, double ms) {
    struct Wait { double ms = 0; uint64_t count = 0; uint64_t va = 0; };
    static std::mutex stats_lock;
    static std::unordered_map<std::string, Wait> waits;
    static auto last_print = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> held(stats_lock);
    uint64_t waiter_site = cpu.x[30], outer = 0;
    if (cpu.x[29] >= 0x10000 && !(cpu.x[29] & 7)) std::memcpy(&outer, reinterpret_cast<void*>(cpu.x[29] + 8), 8);
    char tps[64];
    std::snprintf(tps, sizeof(tps), " by tp %llx, holder tp %llx", (unsigned long long)cpu.tpidr,
                  (unsigned long long)holder_tp);
    std::string key = "held from " + guest_describe(holder_site) + ", waited at " + guest_describe(outer ? outer : waiter_site) + tps;
    Wait& one = waits[key];
    one.ms += ms;
    ++one.count;
    one.va = va;
    if (std::chrono::steady_clock::now() - last_print > std::chrono::seconds(10)) {
        last_print = std::chrono::steady_clock::now();
        std::vector<std::pair<double, std::string>> order;
        for (auto& entry : waits) order.push_back({entry.second.ms, entry.first});
        std::sort(order.rbegin(), order.rend());
        for (size_t i = 0; i < order.size() && i < 8; ++i)
            std::printf("lockstats: %8.0f ms %6llu waits on %llx, %s\n", order[i].first,
                        (unsigned long long)waits[order[i].second].count,
                        (unsigned long long)waits[order[i].second].va, order[i].second.c_str());
        std::fflush(stdout);
        waits.clear();
    }
}
static bool wait_slice(GuestLibc& libc, GuestCpu& cpu, volatile uint32_t* word, uint32_t value) {
    WaitOnAddress(word, &value, 4, 5);
    /* A thread that owns a looper keeps servicing it while it waits: on the
       device the UI thread and the engine's thread are different threads, so
       the engine may wait for work it posted to the UI looper; here they can
       be the same thread, which would otherwise wait for itself. */
    libc.pump_looper(cpu);
    return libc.poll_signals(cpu);
}
/* 0, EBUSY (16) for a try that finds it held, ETIMEDOUT (110). */
int guest_mutex_lock(GuestLibc& libc, GuestCpu& cpu, uint64_t va, bool try_only, int64_t deadline_ns) {
    uint32_t* words = reinterpret_cast<uint32_t*>(guest_ptr(libc.image->mem, va, 12));
    if (!words) return 22; /* EINVAL */
    std::atomic_ref<uint32_t> state(words[0]), owner(words[1]), depth(words[2]);
    uint32_t me = (uint32_t)GuestLibc::guest_tid();
    uint32_t high = state.load(std::memory_order_relaxed) & ~3u;
    bool recursive = ((high >> 14) & 3) == 1;
    if (recursive && owner.load(std::memory_order_relaxed) == me) {
        depth.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    uint32_t seen = high;
    static const bool stats = QB_ENV("QB_LOCK_STATS") != nullptr;
    if (!state.compare_exchange_strong(seen, high | 1, std::memory_order_acquire)) {
        if (try_only) return 16;
        int64_t started = stats ? guest_clock_ns() : 0;
        /* The holder's lock site, kept in the mutex's unused fourth and
           fifth words, is read before waiting: it names who we wait on. */
        uint64_t holder_site = 0, holder_tp = 0;
        if (stats) std::memcpy(&holder_site, &words[3], 8);
        if ((seen & 3) != 2) seen = state.exchange(high | 2, std::memory_order_acquire);
        while ((seen & 3) != 0) {
            wait_slice(libc, cpu, reinterpret_cast<volatile uint32_t*>(&words[0]), high | 2);
            if (deadline_ns >= 0 && guest_clock_ns() >= deadline_ns) return 110;
            seen = state.exchange(high | 2, std::memory_order_acquire);
        }
        if (stats) note_lock_wait(cpu, va, holder_site, holder_tp, (guest_clock_ns() - started) / 1e6);
    }
    owner.store(me, std::memory_order_relaxed);
    depth.store(1, std::memory_order_relaxed);
    if (stats) {
        /* The site is the lock call's caller's caller when there is a
           frame record: lock helpers are one small function everyone calls. */
        uint64_t site = cpu.x[30], outer = 0;
        if (cpu.x[29] >= 0x10000 && !(cpu.x[29] & 7)) std::memcpy(&outer, reinterpret_cast<void*>(cpu.x[29] + 8), 8);
        if (outer) site = outer;
        std::memcpy(&words[3], &site, 8);
    }
    return 0;
}
/* Releases one level, or every level when full (for a condition wait);
   returns the depth that was released. */
uint32_t guest_mutex_unlock(GuestLibc& libc, GuestCpu& cpu, uint64_t va, bool full) {
    uint32_t* words = reinterpret_cast<uint32_t*>(guest_ptr(libc.image->mem, va, 12));
    if (!words) return 0;
    std::atomic_ref<uint32_t> state(words[0]), owner(words[1]), depth(words[2]);
    uint32_t high = state.load(std::memory_order_relaxed) & ~3u;
    uint32_t held = depth.load(std::memory_order_relaxed);
    if (((high >> 14) & 3) == 1 && !full && held > 1) {
        depth.store(held - 1, std::memory_order_relaxed);
        return 1;
    }
    owner.store(0, std::memory_order_relaxed);
    depth.store(0, std::memory_order_relaxed);
    if ((state.exchange(high, std::memory_order_release) & 3) == 2) WakeByAddressSingle(&words[0]);
    return held ? held : 1;
}

/* pthread_getspecific and setspecific's storage: per host thread (a guest
   thread is one), by thread pointer as well in case a host thread ever runs
   as more than one, and a flat array by key since keys are small numbers. */
namespace {
struct Specifics {
    uint64_t tp = 0;
    std::vector<uint64_t>* values = nullptr;
};
std::vector<uint64_t>& specifics_for(uint64_t tp) {
    thread_local std::unordered_map<uint64_t, std::vector<uint64_t>> all;
    thread_local Specifics last;
    if (last.values && last.tp == tp) return *last.values;
    last.tp = tp;
    last.values = &all[tp];
    return *last.values;
}
}  // namespace

uint64_t guest_specific_get(uint64_t tp, uint64_t key) {
    std::vector<uint64_t>& values = specifics_for(tp);
    return key < values.size() ? values[(size_t)key] : 0;
}

void guest_specific_set(uint64_t tp, uint64_t key, uint64_t value) {
    std::vector<uint64_t>& values = specifics_for(tp);
    if (key >= values.size()) {
        if (key > (1u << 20)) return; /* not a key pthread_key_create gave */
        values.resize((size_t)key + 1, 0);
    }
    values[(size_t)key] = value;
}

bool GuestLibc::thread_call(const std::string& name, GuestCpu& cpu) {
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    auto write64 = [&](uint64_t va, uint64_t value) {
        uint8_t* p = guest_ptr(mem, va, 8);
        if (p) std::memcpy(p, &value, 8);
    };
    auto read64 = [&](uint64_t va) {
        uint64_t value = 0;
        uint8_t* p = guest_ptr(mem, va, 8);
        if (p) std::memcpy(&value, p, 8);
        return value;
    };

    if (QB_ENV("QB_SYNC") && (name.find("cond") != std::string::npos || name == "pthread_create" ||
                                   name == "pthread_join"))
        std::fprintf(stderr, "sync: t%llu %s %llx %llx lr %llx\n", (unsigned long long)guest_tid(), name.c_str(),
                     (unsigned long long)arg(0), (unsigned long long)arg(1), (unsigned long long)cpu.x[30]);
    /* Semaphores. The count lives in the first word of the guest's sem_t,
       so it is shared memory like everything else, and waiting is done on
       that word the same way the futex call waits. */
    if (name.compare(0, 4, "sem_") == 0) {
        volatile LONG* count = reinterpret_cast<volatile LONG*>(guest_ptr(mem, arg(0), 4));
        if (!count) {
            ret((uint64_t)-1);
            return true;
        }
        if (name == "sem_init") {
            *count = (LONG)arg(2);
            ret(0);
            return true;
        }
        if (name == "sem_destroy") {
            ret(0);
            return true;
        }
        if (name == "sem_getvalue") {
            uint8_t* out = guest_ptr(mem, arg(1), 4);
            LONG value = *count;
            if (out) std::memcpy(out, &value, 4);
            ret(0);
            return true;
        }
        if (name == "sem_post") {
            InterlockedIncrement(count);
            WakeByAddressSingle((PVOID)count);
            ret(0);
            return true;
        }
        if (name == "sem_wait" || name == "sem_trywait" || name == "sem_timedwait" ||
            name == "sem_timedwait_monotonic_np" || name == "sem_clockwait") {
            bool try_only = name == "sem_trywait";
            bool timed = !try_only && name != "sem_wait";
            /* Timed waits give an absolute time; polling in short steps up to
               a generous bound is simpler than converting clocks and never
               wakes later than a real one would by more than the step. */
            auto start = std::chrono::steady_clock::now();
            for (;;) {
                LONG seen = *count;
                if (seen > 0) {
                    if (InterlockedCompareExchange(count, seen - 1, seen) == seen) {
                        ret(0);
                        return true;
                    }
                    continue;
                }
                if (try_only) {
                    set_errno(cpu, 11); /* EAGAIN */
                    ret((uint64_t)-1);
                    return true;
                }
                if (timed && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(100)) {
                    set_errno(cpu, 110); /* ETIMEDOUT, which the caller rechecks */
                    ret((uint64_t)-1);
                    return true;
                }
                LONG zero = 0;
                WaitOnAddress((volatile VOID*)count, &zero, sizeof(zero), 5);
                if (poll_signals(cpu)) {
                    set_errno(cpu, 4); /* EINTR; callers retry */
                    ret((uint64_t)-1);
                    return true;
                }
            }
        }
    }
    if (name == "pthread_kill") {
        uint64_t thread_pointer = arg(0);
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            auto found = threads.find(arg(0));
            if (found != threads.end()) thread_pointer = found->second->tls_va;
        }
        int signal = (int)arg(1);
        if (signal == 0) {
            ret(guest_thread_running(thread_pointer) ? 0 : 3);
            return true;
        }
        ret(guest_raise(thread_pointer, signal) ? 0 : 3 /* ESRCH */);
        if (thread_pointer == cpu.tpidr) poll_signals(cpu);
        return true;
    }
    if (name == "pthread_create") {
        uint64_t handle_out = arg(0);
        uint64_t start = arg(2);
        uint64_t argument = arg(3);

        std::unique_lock<std::recursive_mutex> held(lock);
        /* A stack, then the thread local block, both out of the shared span. */
        uint64_t tls_bytes = (image->linker.tls_used + kAlign - 1) & ~(kAlign - 1);
        /* The size the attributes asked for, else bionic's default of about
           a megabyte, rounded to pages and never less than 256 kilobytes. */
        uint64_t stack_bytes = kDefaultStack;
        if (arg(1)) {
            uint64_t asked = read64(arg(1) + 16);
            if (asked) stack_bytes = asked;
        }
        stack_bytes = std::max<uint64_t>((stack_bytes + 0xffff) & ~0xffffull, 256u << 10);
        if (thread_area_used + stack_bytes + tls_bytes > thread_area.size()) {
            std::fprintf(stderr, "guest: out of thread stack space at thread %llu\n",
                         (unsigned long long)threads.size());
            ret(11); /* EAGAIN */
            return true;
        }
        uint64_t stack_va = thread_area_va + thread_area_used;
        thread_area_used += stack_bytes;
        uint64_t tls_va = thread_area_va + thread_area_used;
        thread_area_used += tls_bytes;

        /* Every thread gets its own copy of the template, so one thread's
           writes are invisible to another. */
        std::memcpy(thread_area.data() + (tls_va - thread_area_va), image->linker.tls_initial.data(),
                    (size_t)std::min<uint64_t>(tls_bytes, image->linker.tls_initial.size()));

        auto thread = std::make_unique<GuestThread>();
        thread->stack_va = stack_va;
        thread->tls_va = tls_va;
        thread->stack_bytes = stack_bytes;
        thread->cpu.sp = stack_va + stack_bytes - 64;
        thread->cpu.tpidr = tls_va;
        thread->cpu.pc = start;
        thread->cpu.x[0] = argument;
        thread->cpu.x[30] = 1; /* the sentinel guest_run stops on */
        uint64_t handle = next_thread++;
        GuestThread* raw = thread.get();
        threads[handle] = std::move(thread);
        held.unlock();

        if (QB_ENV("QB_THREADS"))
            std::fprintf(stderr, "thread: create tp %llx start %s arg %llx stack %llu KB, from %s\n",
                         (unsigned long long)tls_va, guest_describe(start).c_str(), (unsigned long long)argument,
                         (unsigned long long)(stack_bytes >> 10), guest_describe(cpu.x[30]).c_str());
        raw->runner = std::thread([this, raw]() {
            bool ran = guest_run(raw->cpu, image->mem, image->callback, image->callback_user, 0);
            raw->result = raw->cpu.x[0];
            if (QB_ENV("QB_THREADS"))
                std::fprintf(stderr, "thread: tp %llx %s, x0 %llx\n", (unsigned long long)raw->cpu.tpidr,
                             ran ? "returned" : "stopped", (unsigned long long)raw->cpu.x[0]);
            raw->finished = true;
        });
        if (handle_out) write64(handle_out, handle);
        ret(0);
        return true;
    }
    if (name == "pthread_barrier_init" || name == "pthread_barrier_destroy" || name == "pthread_barrier_wait") {
        /* Kept in the guest's barrier: [0] how many, [1] arrived, [2] generation. */
        uint32_t* words = reinterpret_cast<uint32_t*>(guest_ptr(mem, arg(0), 12));
        if (!words) {
            ret(22);
            return true;
        }
        std::atomic_ref<uint32_t> count(words[0]), arrived(words[1]), generation(words[2]);
        if (name == "pthread_barrier_init") {
            count.store((uint32_t)arg(2));
            arrived.store(0);
            generation.store(0);
            ret(arg(2) ? 0 : 22);
            return true;
        }
        if (name == "pthread_barrier_destroy") {
            ret(0);
            return true;
        }
        uint32_t mine = generation.load();
        if (arrived.fetch_add(1) + 1 == count.load()) {
            arrived.store(0);
            generation.fetch_add(1);
            WakeByAddressAll(&words[2]);
            ret((uint64_t)(int64_t)-1); /* PTHREAD_BARRIER_SERIAL_THREAD */
            return true;
        }
        while (generation.load() == mine) {
            WaitOnAddress(&words[2], &mine, 4, 5);
            poll_signals(cpu);
        }
        ret(0);
        return true;
    }
    if (name == "pthread_exit") {
        /* The thread's value becomes x0 and the call "returns" to the sentinel
           guest_run stops on, which ends the thread as returning from its
           start routine would. */
        cpu.x[0] = arg(0);
        cpu.x[30] = 1;
        return true;
    }
    if (name == "pthread_join") {
        uint64_t handle = arg(0);
        GuestThread* thread = nullptr;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            auto found = threads.find(handle);
            if (found != threads.end()) thread = found->second.get();
        }
        if (!thread || thread->joined) {
            ret(3); /* ESRCH */
            return true;
        }
        while (!thread->finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            poll_signals(cpu);
        }
        if (thread->runner.joinable()) thread->runner.join();
        thread->joined = true;
        if (arg(1)) write64(arg(1), thread->result);
        ret(0);
        return true;
    }
    /* Thread attributes, stored in the guest's own pthread_attr_t the way
       bionic lays it out: flags, stack base, stack size, guard size. The
       garbage collector finds the bottom of each stack this way. */
    if (name == "pthread_getattr_np") {
        uint64_t base = kStackBase, size = kStackSize;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            for (const auto& entry : threads)
                if (entry.first == arg(0) || entry.second->tls_va == arg(0)) {
                    base = entry.second->stack_va;
                    size = entry.second->stack_bytes;
                }
        }
        uint8_t* attr = guest_ptr(mem, arg(1), 40);
        if (attr) {
            std::memset(attr, 0, 40);
            std::memcpy(attr + 8, &base, 8);
            std::memcpy(attr + 16, &size, 8);
        }
        ret(0);
        return true;
    }
    if (name == "pthread_attr_getstack") {
        uint8_t* attr = guest_ptr(mem, arg(0), 40);
        uint64_t base = 0, size = 0;
        if (attr) {
            std::memcpy(&base, attr + 8, 8);
            std::memcpy(&size, attr + 16, 8);
        }
        write64(arg(1), base);
        write64(arg(2), size);
        ret(0);
        return true;
    }
    if (name == "pthread_attr_getstacksize" || name == "pthread_attr_getguardsize") {
        uint8_t* attr = guest_ptr(mem, arg(0), 40);
        uint64_t value = 0;
        if (attr) std::memcpy(&value, attr + (name == "pthread_attr_getstacksize" ? 16 : 24), 8);
        write64(arg(1), value);
        ret(0);
        return true;
    }
    if (name == "pthread_attr_setstack") {
        write64(arg(0) + 8, arg(1));
        write64(arg(0) + 16, arg(2));
        ret(0);
        return true;
    }
    if (name == "pthread_attr_setstacksize" || name == "pthread_attr_setguardsize") {
        write64(arg(0) + (name == "pthread_attr_setstacksize" ? 16 : 24), arg(1));
        ret(0);
        return true;
    }
    if (name == "pthread_attr_init") {
        uint8_t* attr = guest_ptr(mem, arg(0), 40);
        if (attr) std::memset(attr, 0, 40);
        ret(0);
        return true;
    }
    if (name == "pthread_condattr_setclock" || name == "pthread_condattr_getclock" ||
        name == "pthread_setname_np" || name == "pthread_getname_np" ||
        name == "pthread_mutexattr_setpshared" || name == "pthread_condattr_setpshared" ||
        name == "pthread_attr_setschedparam" || name == "pthread_setschedparam" ||
        name == "pthread_rwlockattr_init" || name == "pthread_rwlockattr_destroy" || name == "pthread_sigmask") {
        ret(0);
        return true;
    }
    if (name == "pthread_detach" || name == "pthread_attr_destroy" || name == "pthread_attr_setdetachstate" ||
        name == "pthread_attr_getdetachstate" || name == "pthread_attr_setschedpolicy" ||
        name == "pthread_attr_getschedparam" || name == "pthread_attr_getschedpolicy") {
        ret(0);
        return true;
    }
    if (name == "pthread_self") {
        /* The handle pthread_create gave this thread, so the two agree; the
           main thread, which nobody created, is named by its thread pointer.
           Remembered per host thread, since it is asked for constantly. */
        thread_local uint64_t cached_tp = 0, cached_self = 0;
        if (cached_tp == cpu.tpidr && cached_self) {
            ret(cached_self);
            return true;
        }
        std::lock_guard<std::recursive_mutex> held(lock);
        uint64_t self = cpu.tpidr;
        for (const auto& entry : threads)
            if (entry.second->tls_va == cpu.tpidr) self = entry.first;
        cached_tp = cpu.tpidr;
        cached_self = self;
        ret(self);
        return true;
    }
    if (name == "pthread_equal") {
        ret(arg(0) == arg(1) ? 1 : 0);
        return true;
    }
    if (name == "sched_yield") {
        std::this_thread::yield();
        ret(0);
        return true;
    }

    /* Locks, done the way bionic does them: on the guest's own
       pthread_mutex_t and pthread_cond_t, with atomics and futex-style waits,
       and no state of ours. Keeping a host mutex per guest mutex in a table
       meant every lock and unlock in the engine first went through one global
       lock; with thirty-odd threads that is where frame time went.

       In the 40-byte mutex, word 0 is bionic's state: bits 14-15 are the type
       (0 normal, 1 recursive, 2 error checking; a static initialiser sets
       them, pthread_mutex_init writes them from the attributes) and the low
       two bits are the lock: 0 free, 1 held, 2 held with waiters, the classic
       three-state futex mutex. Word 1 is the holder's thread id, word 2 the
       recursion depth. A condition variable's word 0 is a sequence number,
       stepped by 4 so bionic's two flag bits below it are left alone.

       Waits are in short slices so a signal (a collector stopping the world)
       still gets through, as from a thread asleep in the kernel. */
    if (name == "pthread_mutexattr_init" || name == "pthread_mutexattr_settype") {
        uint8_t* attr = guest_ptr(mem, arg(0), 4);
        uint32_t type = name == "pthread_mutexattr_settype" ? (uint32_t)arg(1) : 0;
        if (attr) std::memcpy(attr, &type, 4);
        ret(0);
        return true;
    }
    if (name == "pthread_mutexattr_gettype") {
        uint8_t* attr = guest_ptr(mem, arg(0), 4);
        uint8_t* out = guest_ptr(mem, arg(1), 4);
        if (attr && out) std::memcpy(out, attr, 4);
        ret(0);
        return true;
    }
    if (name == "pthread_mutex_init" || name == "pthread_mutex_destroy" || name == "pthread_mutexattr_destroy") {
        if (name == "pthread_mutex_init") {
            uint32_t type = 0;
            uint8_t* attr = arg(1) ? guest_ptr(mem, arg(1), 4) : nullptr;
            if (attr) std::memcpy(&type, attr, 4);
            uint32_t words[3] = {type == 1 ? (1u << 14) : type == 2 ? (2u << 14) : 0u, 0, 0};
            uint8_t* state = guest_ptr(mem, arg(0), 12);
            if (state) std::memcpy(state, words, 12);
        }
        ret(0);
        return true;
    }

    auto wait_slice_here = [&](volatile uint32_t* word, uint32_t value) { return wait_slice(*this, cpu, word, value); };
    auto mutex_lock = [&](uint64_t va, bool try_only, int64_t deadline_ns) {
        return guest_mutex_lock(*this, cpu, va, try_only, deadline_ns);
    };
    auto mutex_unlock = [&](uint64_t va, bool full) { return guest_mutex_unlock(*this, cpu, va, full); };
    /* A timespec deadline on the guest's clock, or -1 for none. */
    auto deadline_of = [&](uint64_t spec_va) -> int64_t {
        uint8_t* spec = spec_va ? guest_ptr(mem, spec_va, 16) : nullptr;
        if (!spec) return -1;
        int64_t seconds = 0, nanos = 0;
        std::memcpy(&seconds, spec, 8);
        std::memcpy(&nanos, spec + 8, 8);
        return seconds * 1000000000 + nanos;
    };

    if (name == "pthread_mutex_lock" || name == "pthread_mutex_trylock" || name == "pthread_mutex_timedlock" ||
        name == "pthread_mutex_unlock") {
        int result = 0;
        if (name == "pthread_mutex_unlock") mutex_unlock(arg(0), false);
        else result = mutex_lock(arg(0), name == "pthread_mutex_trylock",
                                 name == "pthread_mutex_timedlock" ? deadline_of(arg(1)) : -1);
        ret((uint64_t)result);
        return true;
    }

    if (name == "pthread_cond_init" || name == "pthread_cond_destroy" || name == "pthread_condattr_init" ||
        name == "pthread_condattr_destroy") {
        if (name == "pthread_cond_init") {
            uint8_t* state = guest_ptr(mem, arg(0), 4);
            uint32_t zero = 0;
            if (state) std::memcpy(state, &zero, 4);
        }
        ret(0);
        return true;
    }
    if (name == "pthread_cond_signal" || name == "pthread_cond_broadcast") {
        uint32_t* word = reinterpret_cast<uint32_t*>(guest_ptr(mem, arg(0), 4));
        if (word) {
            std::atomic_ref<uint32_t>(*word).fetch_add(4, std::memory_order_release);
            if (name == "pthread_cond_signal") WakeByAddressSingle(word);
            else WakeByAddressAll(word);
        }
        ret(0);
        return true;
    }
    if (name == "pthread_cond_wait" || name == "pthread_cond_timedwait" || name == "pthread_cond_timedwait_monotonic_np" ||
        name == "pthread_cond_clockwait" || name == "pthread_cond_timedwait_monotonic") {
        uint32_t* word = reinterpret_cast<uint32_t*>(guest_ptr(mem, arg(0), 4));
        if (!word) {
            ret(22);
            return true;
        }
        /* pthread_cond_clockwait(cond, mutex, clock, abstime) has the time
           one argument later; every guest clock is the same clock here. */
        int64_t deadline = name == "pthread_cond_wait" ? -1
                           : deadline_of(name == "pthread_cond_clockwait" ? arg(3) : arg(2));
        std::atomic_ref<uint32_t> sequence(*word);
        uint32_t seen = sequence.load(std::memory_order_acquire);
        uint32_t depth = mutex_unlock(arg(1), true);
        int result = 0;
        for (;;) {
            bool interrupted = wait_slice_here(reinterpret_cast<volatile uint32_t*>(word), seen);
            if (sequence.load(std::memory_order_acquire) != seen) break;
            if (interrupted) break; /* a spurious wakeup, which callers allow for */
            if (deadline >= 0 && guest_clock_ns() >= deadline) {
                result = 110;
                break;
            }
        }
        mutex_lock(arg(1), false, -1);
        if (depth > 1) {
            uint32_t* words = reinterpret_cast<uint32_t*>(guest_ptr(mem, arg(1), 12));
            if (words) std::atomic_ref<uint32_t>(words[2]).store(depth, std::memory_order_relaxed);
        }
        ret((uint64_t)result);
        return true;
    }

    /* Read and write locks, held as plain exclusion on their first word,
       which is correct if slower than it needs to be for readers. */
    if (name.compare(0, 15, "pthread_rwlock_") == 0) {
        std::string what = name.substr(15);
        int result = 0;
        if (what == "rdlock" || what == "wrlock") result = mutex_lock(arg(0), false, -1);
        else if (what == "tryrdlock" || what == "trywrlock") result = mutex_lock(arg(0), true, -1);
        else if (what == "timedrdlock" || what == "timedwrlock") result = mutex_lock(arg(0), false, deadline_of(arg(1)));
        else if (what == "unlock") mutex_unlock(arg(0), true);
        else if (what == "init") {
            uint8_t* state = guest_ptr(mem, arg(0), 12);
            if (state) std::memset(state, 0, 12);
        }
        ret((uint64_t)result);
        return true;
    }

    /* Per-thread values kept by key, which is the older way of doing what
       thread local storage does. */
    if (name == "pthread_key_create") {
        std::lock_guard<std::recursive_mutex> held(lock);
        uint64_t key = next_key++;
        if (arg(0)) {
            uint8_t* p = guest_ptr(mem, arg(0), 4);
            uint32_t narrow = (uint32_t)key;
            if (p) std::memcpy(p, &narrow, 4);
        }
        ret(0);
        return true;
    }
    if (name == "pthread_key_delete") {
        ret(0);
        return true;
    }
    /* Values per guest thread. A guest thread is one host thread, so these
       live in the host thread's own table, keyed by thread pointer as well in
       case a host thread ever runs as more than one. No lock needed. */
    if (name == "pthread_setspecific" || name == "pthread_getspecific") {
        if (name == "pthread_setspecific") {
            guest_specific_set(cpu.tpidr, arg(0), arg(1));
            ret(0);
        } else {
            ret(guest_specific_get(cpu.tpidr, arg(0)));
        }
        return true;
    }
    if (name == "pthread_once") {
        /* The flag lives in guest memory, so the guest can see it too. It is
           bionic's pthread_once_t, a 32-bit int: reading eight bytes took in
           whatever sat next to it and skipped initialisers that never ran. */
        std::unique_lock<std::recursive_mutex> held(lock);
        uint8_t* flag = guest_ptr(mem, arg(0), 4);
        uint32_t state = 0;
        if (flag) std::memcpy(&state, flag, 4);
        if (state != 0) {
            ret(0);
            return true;
        }
        state = 1;
        if (flag) std::memcpy(flag, &state, 4);
        held.unlock();
        call_guest_on(cpu, arg(1), 0, 0);
        ret(0);
        return true;
    }

    return false;
}
