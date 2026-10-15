#pragma once

#include "linux.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dynarmic {
class ExclusiveMonitor;
namespace A64 {
class Jit;
}
}  // namespace Dynarmic

namespace axrb {

class GuestCpu;  // cpu.cpp: Dynarmic callbacks + Jit for one thread
class OpenFile;

using Vec128 = std::array<uint64_t, 2>;

// A complete arm64 user register state, as saved around host calls, signal
// frames and thread creation.
struct CpuContext {
    uint64_t x[31] = {};
    uint64_t sp = 0;
    uint64_t pc = 0;
    uint32_t pstate = 0;
    uint32_t fpcr = 0;
    uint32_t fpsr = 0;
    Vec128 v[32] = {};
};

struct GuestCpuDeleter {
    void operator()(GuestCpu* cpu) const;  // cpu.cpp
};

// One guest thread = one host thread = one Dynarmic Jit.
struct GuestThread {
    int tid = 0;
    std::string name = "main";
    std::unique_ptr<GuestCpu, GuestCpuDeleter> cpu;
    uint64_t tpidr = 0;
    uint64_t clear_child_tid = 0;
    uint64_t robust_list = 0;
    uint64_t robust_list_len = 0;

    // Signals.
    std::mutex signal_lock;
    uint64_t sigmask = 0;
    std::atomic<uint64_t> pending{0};
    std::deque<linux_abi::ksiginfo> pending_info;
    linux_abi::kstack_t altstack{0, linux_abi::SS_DISABLE_, 0, 0};
    std::atomic<bool> in_handler{false};
    // A system call interrupted by a signal: restarted after the handler if
    // the handler has SA_RESTART (pc of the svc and the original x0).
    uint64_t restart_pc = 0;
    uint64_t restart_x0 = 0;
    // sigsuspend/ppoll/epoll_pwait run with a temporary mask; the handler's
    // frame must record (and sigreturn restore) the original.
    bool restore_sigmask = false;
    uint64_t saved_sigmask = 0;

    // Blocking: every interruptible wait sleeps on `event`; anything that can
    // end the wait (a futex wake, data on a pipe, a signal) sets it.
    HANDLE event = nullptr;
    std::atomic<bool> interrupt{false};
    // Set while the thread is blocked in a host call, for diagnostics.
    std::atomic<const char*> blocked_in{nullptr};

    // Host side.
    HANDLE host_thread = nullptr;
    DWORD host_tid = 0;
    int processor_id = 0;
    int call_depth = 0;  // nested host -> guest calls in progress
    bool exited = false;
    int exit_code = 0;

    // Thread-local scratch for host code acting for this thread.
    void* jni_env = nullptr;   // guest JNIEnv*, when attached to the VM
    void* java_thread = nullptr;

    GuestThread();
    ~GuestThread();
};

GuestThread* current_thread();
void set_current_thread(GuestThread* thread);

// Thrown (as a C++ exception) out of the run loop when a thread calls exit:
// unwinds any nested host frames back to the thread's entry.
struct ThreadExit {
    int code;
};

// Process-wide state.
struct Process {
    int pid = 0;
    uint32_t uid = 10123, gid = 10123;
    std::string cwd = "/";
    uint32_t umask = 022;
    std::string cmdline;  // argv joined with NULs
    std::string exe_path;
    uint64_t start_stack = 0;  // Initial SP, Linux /proc/<pid>/stat field 28.
    std::vector<uint64_t> auxv;
    std::string package;

    // Signal dispositions (index = signal number).
    std::mutex sigaction_lock;
    std::array<linux_abi::ksigaction, 65> actions{};

    std::mutex threads_lock;
    std::map<int, GuestThread*> threads;
    std::atomic<int> next_tid{0};

    std::array<linux_abi::krlimit, 16> rlimits{};
    Dynarmic::ExclusiveMonitor* monitor = nullptr;
    std::atomic<bool> exiting{false};
};

Process& process();

// Interruptible blocking. Returns 0 if `ready` became true, -EINTR when a
// signal must be handled, -ETIMEDOUT on timeout (deadline in QPC ticks, 0 =
// forever, -1 = poll once).
int64_t block_until(GuestThread& thread, const std::function<bool()>& ready, int64_t deadline_ticks,
                    const char* why);
int64_t ticks_now();
int64_t ticks_per_second();
int64_t ticks_from_timespec_relative(const linux_abi::ktimespec& ts);
// A list of threads to wake when some object changes.
class WaitQueue {
public:
    void add(GuestThread* thread);
    void remove(GuestThread* thread);
    void wake_all();

private:
    std::mutex lock_;
    std::vector<GuestThread*> waiters_;
};

// Syscall entry from the run loop (x8 = number, x0..x5 = arguments).
int64_t do_syscall(GuestThread& thread, uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5);
const char* syscall_name(uint64_t nr);

// Subsystems (each in its own file).
int64_t sys_files(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);
int64_t sys_threads(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);
int64_t sys_signals(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);
int64_t sys_net(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);

int64_t sys_memory(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);
int64_t sys_misc(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled);

GuestThread* find_thread(int tid);
// Wakes up to `count` guest threads waiting on the futex word at `address`.
int64_t wake_futex(uint64_t address, int count);
int64_t wake_futex_bitset(uint64_t address, int count, uint32_t bitset);
// Completes a non-blocking syscall (clock reads, futex wakes) for x[8] with
// arguments in x[0..5] directly from translated code. Returns false, without
// side effects, for anything that needs the regular syscall path.
bool fast_syscall(uint64_t* x);
void register_main_thread(GuestThread* thread);
void finish_thread(GuestThread& thread);
int guest_cpu_count();
// Commits pages of guest mappings on first touch (vectored exception handler).
void install_fault_handler();

// Host time sources.
int64_t realtime_ns();
int64_t monotonic_ns();
// Calls per syscall number, for the profiler.
extern std::atomic<uint64_t> g_syscall_counts[512];
// Sampled guest stacks of syscalls ((nr, frames) -> samples), while enabled.
extern std::atomic<bool> g_syscall_stacks_enabled;
extern std::mutex g_syscall_stacks_lock;
extern std::map<std::pair<uint64_t, std::vector<uint64_t>>, uint64_t> g_syscall_stacks;
const char* syscall_name(uint64_t nr);

// Delivers pending, unblocked signals to `thread` (called between guest
// instructions, i.e. from the run loop). Returns true if a frame was pushed.
bool deliver_signals(GuestThread& thread);
// Queues a signal for one thread and wakes it.
int64_t send_signal(GuestThread& target, int signal, const linux_abi::ksiginfo& info);
// A synchronous fault in guest code (bad memory, undefined instruction).
void raise_fault(GuestThread& thread, int signal, int code, uint64_t address);

}  // namespace axrb
