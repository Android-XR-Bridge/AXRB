#include "cpu.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"

#include <windows.h>

#include <cstring>

namespace axrb {
using namespace linux_abi;

GuestThread* find_thread(int tid);

namespace {

constexpr uint64_t bit(int signal) { return 1ull << (signal - 1); }
constexpr uint64_t kUnblockable = bit(SIGKILL_) | bit(SIGSTOP_);

// arm64 signal frame (arch/arm64/kernel/signal.c, uapi/asm/sigcontext.h).
constexpr uint64_t kUcontextOffset = 128;            // after siginfo
constexpr uint64_t kUcStack = 16;                    // within ucontext
constexpr uint64_t kUcSigmask = 40;
constexpr uint64_t kUcMcontext = 176;                // sigcontext, 16-aligned
constexpr uint64_t kScFaultAddress = 0;
constexpr uint64_t kScRegs = 8;
constexpr uint64_t kScSp = 256;
constexpr uint64_t kScPc = 264;
constexpr uint64_t kScPstate = 272;
constexpr uint64_t kScReserved = 288;
constexpr uint32_t kFpsimdMagic = 0x46508001;
constexpr uint32_t kFpsimdSize = 528;
constexpr uint64_t kFrameSize = kUcontextOffset + kUcMcontext + kScReserved + 4096;  // 4688
constexpr uint64_t kFrameRecord = (kFrameSize + 15) & ~15ull;
constexpr uint64_t kFrameTotal = kFrameRecord + 16;

bool default_ignored(int signal) {
    return signal == SIGCHLD_ || signal == SIGURG_ || signal == SIGWINCH_ || signal == SIGCONT_;
}

uint64_t sigreturn_trampoline() {
    static uint64_t address = [] {
        // mov x8, #139 ; svc #0  -- for handlers installed without SA_RESTORER
        int64_t at = address_space().mmap(0, kPageSize, PROT_READ_ | PROT_WRITE_, MAP_PRIVATE_ | MAP_ANONYMOUS_, nullptr, 0,
                                          "[sigreturn]");
        address_space().populate(static_cast<uint64_t>(at), kPageSize);
        uint32_t code[2] = {0xd2801168u, 0xd4000001u};
        std::memcpy(reinterpret_cast<void*>(at), code, sizeof code);
        address_space().mprotect(static_cast<uint64_t>(at), kPageSize, PROT_READ_ | PROT_EXEC_);
        return static_cast<uint64_t>(at);
    }();
    return address;
}

[[noreturn]] void die_of(GuestThread& thread, int signal, const ksiginfo& info) {
    static const char* names[] = {"?", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS",
                                  "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM"};
    char why[160];
    std::snprintf(why, sizeof why, "fatal signal %d (%s), code %d, fault addr %llx", signal,
                  signal < 16 ? names[signal] : "?", info.si_code, (unsigned long long)info.fields.fault.addr);
    dump_thread_state(thread, why);
    log_flush();
    process().exiting = true;
    TerminateProcess(GetCurrentProcess(), 128 + signal);
    for (;;) {
    }
}

// Pushes a frame for `signal` onto the thread's stack and points it at the
// handler. The cpu context is the state to resume when the handler returns.
void push_frame(GuestThread& thread, int signal, const ksiginfo& info, const ksigaction& action) {
    CpuContext c;
    save_context(thread, c);
    if (thread.restart_pc) {
        if (action.flags & SA_RESTART_) {
            c.pc = thread.restart_pc;
            c.x[0] = thread.restart_x0;
        }
        thread.restart_pc = 0;
    }
    uint64_t sp = c.sp;
    bool on_altstack = thread.altstack.ss_flags != SS_DISABLE_ && sp > thread.altstack.ss_sp &&
                       sp <= thread.altstack.ss_sp + thread.altstack.ss_size;
    if ((action.flags & SA_ONSTACK_) && thread.altstack.ss_flags != SS_DISABLE_ && !on_altstack)
        sp = thread.altstack.ss_sp + thread.altstack.ss_size;
    uint64_t frame = (sp - kFrameTotal) & ~15ull;
    address_space().populate(frame, kFrameTotal);
    std::vector<uint8_t> bytes(kFrameTotal, 0);
    std::memcpy(bytes.data(), &info, sizeof info);
    uint8_t* uc = bytes.data() + kUcontextOffset;
    kstack_t stack = thread.altstack;
    if (on_altstack) stack.ss_flags |= SS_ONSTACK_;
    std::memcpy(uc + kUcStack, &stack, sizeof stack);
    uint64_t saved_mask = thread.restore_sigmask ? thread.saved_sigmask : thread.sigmask;
    thread.restore_sigmask = false;
    std::memcpy(uc + kUcSigmask, &saved_mask, 8);
    uint8_t* mc = uc + kUcMcontext;
    uint64_t fault = (signal == SIGSEGV_ || signal == SIGBUS_ || signal == SIGILL_ || signal == SIGTRAP_) ? info.fields.fault.addr : 0;
    std::memcpy(mc + kScFaultAddress, &fault, 8);
    std::memcpy(mc + kScRegs, c.x, sizeof c.x);
    std::memcpy(mc + kScSp, &c.sp, 8);
    std::memcpy(mc + kScPc, &c.pc, 8);
    uint64_t pstate = c.pstate;
    std::memcpy(mc + kScPstate, &pstate, 8);
    uint8_t* fp = mc + kScReserved;
    std::memcpy(fp, &kFpsimdMagic, 4);
    std::memcpy(fp + 4, &kFpsimdSize, 4);
    std::memcpy(fp + 8, &c.fpsr, 4);
    std::memcpy(fp + 12, &c.fpcr, 4);
    std::memcpy(fp + 16, c.v, 512);
    // A terminating null record follows (already zero).
    uint64_t record[2] = {c.x[29], c.pc};
    std::memcpy(bytes.data() + kFrameRecord, record, 16);
    if (!guest_write(frame, bytes.data(), bytes.size())) {
        LOGE("signal", "cannot write a signal frame at %llx (sp %llx)", (unsigned long long)frame, (unsigned long long)sp);
        die_of(thread, SIGSEGV_, info);
    }
    set_reg(thread, 0, static_cast<uint64_t>(signal));
    set_reg(thread, 1, frame);
    set_reg(thread, 2, frame + kUcontextOffset);
    set_reg(thread, 29, frame + kFrameRecord);
    set_reg(thread, 30, (action.flags & SA_RESTORER_) && action.restorer ? action.restorer : sigreturn_trampoline());
    set_sp(thread, frame);
    set_pc(thread, action.handler);
    uint64_t mask = thread.sigmask | action.mask;
    if (!(action.flags & SA_NODEFER_)) mask |= bit(signal);
    thread.sigmask = mask & ~kUnblockable;
    TRACE("signal", "thread %d: signal %d -> handler %s", thread.tid, signal, describe_address(action.handler).c_str());
}

ksigaction action_for(int signal) {
    std::lock_guard<std::mutex> held(process().sigaction_lock);
    ksigaction action = process().actions[signal];
    if ((action.flags & SA_RESETHAND_) && action.handler > SIG_IGN_) {
        process().actions[signal].handler = SIG_DFL_;
        process().actions[signal].flags &= ~SA_SIGINFO_;
    }
    return action;
}

}  // namespace

bool deliver_signals(GuestThread& thread) {
    for (;;) {
        int signal = 0;
        ksiginfo info{};
        {
            std::lock_guard<std::mutex> held(thread.signal_lock);
            uint64_t ready = thread.pending.load() & ~thread.sigmask;
            if (!ready) return false;
            unsigned long index;
            _BitScanForward64(&index, ready);
            signal = static_cast<int>(index) + 1;
            bool more = false;
            bool found = false;
            for (auto it = thread.pending_info.begin(); it != thread.pending_info.end();) {
                if (it->si_signo == signal) {
                    if (!found) {
                        info = *it;
                        it = thread.pending_info.erase(it);
                        found = true;
                        continue;
                    }
                    more = true;
                }
                ++it;
            }
            if (!found) {
                info.si_signo = signal;
                info.si_code = SI_KERNEL_;
            }
            if (!more) thread.pending.fetch_and(~bit(signal));
        }
        ksigaction action = action_for(signal);
        if (action.handler == SIG_IGN_) continue;
        if (action.handler == SIG_DFL_) {
            if (default_ignored(signal)) continue;
            die_of(thread, signal, info);
        }
        push_frame(thread, signal, info, action);
        return true;
    }
}

int64_t send_signal(GuestThread& target, int signal, const ksiginfo& info) {
    if (signal <= 0 || signal > NSIG_) return -EINVAL_;
    if (signal == SIGKILL_) {
        LOGI("signal", "SIGKILL: terminating");
        log_flush();
        TerminateProcess(GetCurrentProcess(), 128 + SIGKILL_);
    }
    {
        std::lock_guard<std::mutex> held(target.signal_lock);
        bool realtime = signal >= SIGRTMIN_;
        if (realtime || !(target.pending.load() & bit(signal))) target.pending_info.push_back(info);
        target.pending.fetch_or(bit(signal));
    }
    kick_thread(target);
    return 0;
}

void raise_fault(GuestThread& thread, int signal, int code, uint64_t address) {
    ksiginfo info{};
    info.si_signo = signal;
    info.si_code = code;
    info.fields.fault.addr = address;
    ksigaction action = action_for(signal);
    bool blocked = (thread.sigmask & bit(signal)) != 0;
    if (action.handler <= SIG_IGN_ || blocked) {
        LOGE("signal", "unhandled synchronous signal %d at %s", signal, describe_address(get_pc(thread)).c_str());
        die_of(thread, signal, info);
    }
    LOGW("signal", "thread %d: signal %d (code %d, addr %llx) at %s -> guest handler", thread.tid, signal, code,
         (unsigned long long)address, describe_address(get_pc(thread)).c_str());
#ifdef AXRB_ARM_SERVICE_EMBEDDED
    LOGW("signal", "ARM bridge fault caller %s",describe_address(get_reg(thread,30)).c_str());
    MEMORY_BASIC_INFORMATION mbi{};VirtualQuery(reinterpret_cast<void*>(address),&mbi,sizeof mbi);
    LOGW("signal", "ARM bridge fault memory base=%p size=%llx state=%lx protection=%lx sp=%llx",mbi.BaseAddress,(unsigned long long)mbi.RegionSize,mbi.State,mbi.Protect,(unsigned long long)get_sp(thread));
#endif
    push_frame(thread, signal, info, action);
}

static int64_t sigreturn(GuestThread& thread) {
    uint64_t frame = get_sp(thread);
    std::vector<uint8_t> bytes(kFrameSize);
    if (!guest_read(frame, bytes.data(), bytes.size())) {
        ksiginfo info{};
        info.si_signo = SIGSEGV_;
        die_of(thread, SIGSEGV_, info);
    }
    const uint8_t* uc = bytes.data() + kUcontextOffset;
    const uint8_t* mc = uc + kUcMcontext;
    CpuContext c;
    std::memcpy(c.x, mc + kScRegs, sizeof c.x);
    std::memcpy(&c.sp, mc + kScSp, 8);
    std::memcpy(&c.pc, mc + kScPc, 8);
    uint64_t pstate;
    std::memcpy(&pstate, mc + kScPstate, 8);
    c.pstate = static_cast<uint32_t>(pstate) & 0xf0000000u;
    const uint8_t* fp = mc + kScReserved;
    uint32_t magic;
    std::memcpy(&magic, fp, 4);
    if (magic == kFpsimdMagic) {
        std::memcpy(&c.fpsr, fp + 8, 4);
        std::memcpy(&c.fpcr, fp + 12, 4);
        std::memcpy(c.v, fp + 16, 512);
    } else {
        CpuContext now;
        save_context(thread, now);
        c.fpsr = now.fpsr;
        c.fpcr = now.fpcr;
        std::memcpy(c.v, now.v, sizeof c.v);
    }
    uint64_t mask;
    std::memcpy(&mask, uc + kUcSigmask, 8);
    thread.sigmask = mask & ~kUnblockable;
    kstack_t stack;
    std::memcpy(&stack, uc + kUcStack, sizeof stack);
    load_context(thread, c);
    return 0;
}

int64_t sys_signals(GuestThread& t, uint64_t nr, const uint64_t* a, bool& handled) {
    handled = true;
    switch (nr) {
        case 134: {  // rt_sigaction(sig, act, oact, size)
            int signal = static_cast<int>(a[0]);
            if (signal <= 0 || signal > NSIG_) return -EINVAL_;
            ksigaction old{}, next{};
            if (a[1] && !guest_read(a[1], &next, sizeof next)) return -EFAULT_;
            {
                std::lock_guard<std::mutex> held(process().sigaction_lock);
                old = process().actions[signal];
                if (a[1]) {
                    if (signal == SIGKILL_ || signal == SIGSTOP_) return -EINVAL_;
                    next.mask &= ~kUnblockable;
                    process().actions[signal] = next;
                }
            }
            if (a[1]) TRACE("signal", "sigaction(%d) handler %s flags %llx", signal, describe_address(next.handler).c_str(),
                            (unsigned long long)next.flags);
            if (a[2] && !guest_write(a[2], &old, sizeof old)) return -EFAULT_;
            return 0;
        }
        case 135: {  // rt_sigprocmask(how, set, oset, size)
            uint64_t old = t.sigmask;
            if (a[1]) {
                uint64_t set;
                if (!guest_read(a[1], &set, 8)) return -EFAULT_;
                switch (static_cast<int>(a[0])) {
                    case SIG_BLOCK_: t.sigmask |= set; break;
                    case SIG_UNBLOCK_: t.sigmask &= ~set; break;
                    case SIG_SETMASK_: t.sigmask = set; break;
                    default: return -EINVAL_;
                }
                t.sigmask &= ~kUnblockable;
            }
            if (a[2] && !guest_write(a[2], &old, 8)) return -EFAULT_;
            return 0;
        }
        case 136: {  // rt_sigpending
            uint64_t pending = t.pending.load() & t.sigmask;
            return guest_write(a[0], &pending, 8) ? 0 : -EFAULT_;
        }
        case 133: {  // rt_sigsuspend(mask)
            uint64_t mask;
            if (!guest_read(a[0], &mask, 8)) return -EFAULT_;
            t.saved_sigmask = t.sigmask;
            t.restore_sigmask = true;
            t.sigmask = mask & ~kUnblockable;
            block_until(t, [] { return false; }, 0, "sigsuspend");
            return -EINTR_;
        }
        case 137: {  // rt_sigtimedwait(set, info, timeout, size)
            uint64_t set;
            if (!guest_read(a[0], &set, 8)) return -EFAULT_;
            int64_t deadline = 0;
            if (a[2]) {
                ktimespec ts;
                if (!guest_read(a[2], &ts, sizeof ts)) return -EFAULT_;
                deadline = (ts.tv_sec == 0 && ts.tv_nsec == 0) ? -1 : ticks_now() + ticks_from_timespec_relative(ts);
            }
            int got = 0;
            ksiginfo info{};
            auto take = [&] {
                std::lock_guard<std::mutex> held(t.signal_lock);
                uint64_t ready = t.pending.load() & set;
                if (!ready) return false;
                unsigned long index;
                _BitScanForward64(&index, ready);
                got = static_cast<int>(index) + 1;
                bool more = false, found = false;
                for (auto it = t.pending_info.begin(); it != t.pending_info.end();) {
                    if (it->si_signo == got) {
                        if (!found) {
                            info = *it;
                            it = t.pending_info.erase(it);
                            found = true;
                            continue;
                        }
                        more = true;
                    }
                    ++it;
                }
                if (!found) info.si_signo = got;
                if (!more) t.pending.fetch_and(~bit(got));
                return true;
            };
            // Signals in `set` are blocked by the caller; only others interrupt.
            uint64_t saved = t.sigmask;
            t.sigmask |= set;
            int64_t r = block_until(t, take, deadline, "sigtimedwait");
            t.sigmask = saved;
            if (r == -ETIMEDOUT_) return -EAGAIN_;
            if (r) return r;
            if (a[1]) guest_write(a[1], &info, sizeof info);
            return got;
        }
        case 139:  // rt_sigreturn
            return sigreturn(t);
        case 132: {  // sigaltstack(ss, oss)
            kstack_t old = t.altstack;
            uint64_t sp = get_sp(t);
            if (old.ss_flags != SS_DISABLE_ && sp > old.ss_sp && sp <= old.ss_sp + old.ss_size) old.ss_flags |= SS_ONSTACK_;
            if (a[0]) {
                kstack_t next;
                if (!guest_read(a[0], &next, sizeof next)) return -EFAULT_;
                if (old.ss_flags & SS_ONSTACK_) return -EPERM_;
                if (next.ss_flags & SS_DISABLE_) t.altstack = {0, SS_DISABLE_, 0, 0};
                else {
                    if (next.ss_size < 2048) return -ENOMEM_;
                    t.altstack = next;
                    t.altstack.ss_flags = 0;
                }
            }
            if (a[1] && !guest_write(a[1], &old, sizeof old)) return -EFAULT_;
            return 0;
        }
        case 129: {  // kill(pid, sig)
            int pid = static_cast<int>(a[0]), signal = static_cast<int>(a[1]);
            if (pid != process().pid && pid != 0 && pid != -1) return -ESRCH_;
            if (!signal) return 0;
            ksiginfo info{};
            info.si_signo = signal;
            info.si_code = SI_USER_;
            info.fields.kill.pid = process().pid;
            info.fields.kill.uid = process().uid;
            // Process-directed: the first thread not blocking it.
            GuestThread* target = nullptr;
            {
                std::lock_guard<std::mutex> held(process().threads_lock);
                for (auto& [tid, thread] : process().threads)
                    if (!(thread->sigmask & bit(signal))) {
                        target = thread;
                        break;
                    }
                if (!target && !process().threads.empty()) target = process().threads.begin()->second;
            }
            if (!target) return -ESRCH_;
            return send_signal(*target, signal, info);
        }
        case 130:    // tkill(tid, sig)
        case 131: {  // tgkill(tgid, tid, sig)
            int tid = static_cast<int>(nr == 130 ? a[0] : a[1]);
            int signal = static_cast<int>(nr == 130 ? a[1] : a[2]);
            if (nr == 131 && static_cast<int>(a[0]) != process().pid) return -ESRCH_;
            GuestThread* target = tid == t.tid ? &t : find_thread(tid);
            if (!target) return -ESRCH_;
            if (!signal) return 0;
            ksiginfo info{};
            info.si_signo = signal;
            info.si_code = SI_TKILL_;
            info.fields.kill.pid = process().pid;
            info.fields.kill.uid = process().uid;
            return send_signal(*target, signal, info);
        }
        case 138:    // rt_sigqueueinfo(pid, sig, info)
        case 240: {  // rt_tgsigqueueinfo(tgid, tid, sig, info)
            int signal = static_cast<int>(nr == 138 ? a[1] : a[2]);
            ksiginfo info{};
            if (!guest_read(nr == 138 ? a[2] : a[3], &info, sizeof info)) return -EFAULT_;
            info.si_signo = signal;
            GuestThread* target = nr == 240 ? find_thread(static_cast<int>(a[1])) : &t;
            if (!target) return -ESRCH_;
            return send_signal(*target, signal, info);
        }
        case 102: case 103: return -ENOSYS_;  // getitimer, setitimer
        default:
            handled = false;
            return 0;
    }
}

}  // namespace axrb
