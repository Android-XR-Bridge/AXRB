#pragma once

#include "kernel.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace axrb {

// Supervisor-call immediates used by guest code. 0 is the Linux system call.
constexpr uint32_t kSvcSyscall = 0;
constexpr uint32_t kSvcHostCall = 1;     // x16 = host function id
constexpr uint32_t kSvcReturnTrap = 2;   // end of a host -> guest call
constexpr uint32_t kSvcNamedCall = 3;    // x17 = NUL-terminated host function name

// Arguments of a host function called from guest code, in AAPCS64 terms.
class HostCall {
public:
    HostCall(GuestThread& thread, GuestCpu& cpu, uint32_t id) : thread_(thread), cpu_(cpu), id_(id) {}
    GuestThread& thread() { return thread_; }
    uint32_t id() const { return id_; }
    uint64_t x(int i) const;      // general-purpose argument registers x0-x7 (and beyond: any register)
    float s(int i) const;         // float arguments in v0-v7
    double d(int i) const;
    Vec128 v(int i) const;
    uint64_t sp() const;
    uint64_t lr() const;
    uint64_t stack(int slot) const;  // the slot-th 8-byte stack argument
    void ret(uint64_t value);
    void ret_float(float value);
    void ret_double(double value);
    void set_x(int i, uint64_t value);
    void set_v(int i, Vec128 value);

private:
    GuestThread& thread_;
    GuestCpu& cpu_;
    uint32_t id_;
};

using HostFunction = std::function<void(HostCall&)>;

// Host functions reachable from guest code through a 16-byte trampoline.
uint32_t register_host_function(const std::string& name, HostFunction fn);
uint64_t host_trampoline(uint32_t id);
uint64_t host_trampoline(const std::string& name, HostFunction fn);
const std::string& host_function_name(uint32_t id);
// Host functions guest libraries reach by name (svc #3 stubs, see
// direct/guest/axrb_guest.h). Registering a name again replaces it.
void register_named_function(const std::string& name, HostFunction fn);
uint64_t return_trap_address();

// Creates the Jit for a thread (on the host thread that will run it).
void attach_cpu(GuestThread& thread);
void detach_cpu(GuestThread& thread);

// Register access for the current state of a thread's cpu.
void load_context(GuestThread& thread, const CpuContext& context);
void save_context(GuestThread& thread, CpuContext& context);
uint64_t get_reg(GuestThread& thread, int index);
void set_reg(GuestThread& thread, int index, uint64_t value);
uint64_t get_pc(GuestThread& thread);
// Guest block address of translated host code (~0 if none); thread must be stopped.
uint64_t guest_pc_for_host_code(GuestThread& thread, uint64_t host_ip);
// The same for a translator whose lookup needs no stopped thread but may allocate
// (so the thread must be running again); ~0 otherwise.
uint64_t guest_pc_for_host_code_resumed(GuestThread& thread, uint64_t host_ip, bool* optimized = nullptr,
                                        uint64_t* region_start = nullptr, uint32_t* region_bytes = nullptr);
void set_pc(GuestThread& thread, uint64_t pc);
uint64_t get_sp(GuestThread& thread);
void set_sp(GuestThread& thread, uint64_t sp);
Vec128 get_vec(GuestThread& thread, int index);
void set_vec(GuestThread& thread, int index, Vec128 value);

// Runs the thread's guest code from its current state until it exits (throws
// ThreadExit) or, for a nested call, until the return trap is reached.
void run_guest(GuestThread& thread, bool nested);

// Calls a guest function on the current thread, preserving its state.
struct GuestArgs {
    std::vector<uint64_t> ints;   // x0.. (beyond 8 they spill to `stack` — the caller lays it out)
    std::vector<Vec128> fps;      // v0..v7: a float in the low 32 bits, a double in the low 64
    std::vector<uint64_t> stack;  // stack argument words, lowest address first
};
uint64_t call_guest(GuestThread& thread, uint64_t function, std::initializer_list<uint64_t> args);
uint64_t call_guest_args(GuestThread& thread, uint64_t function, const GuestArgs& args, Vec128* fp_result = nullptr);

// Interrupts the thread's guest execution so pending work (signals) is seen.
void kick_thread(GuestThread& thread);
// Discards translations of [addr, addr+len) in every thread.
void invalidate_code(uint64_t addr, uint64_t len);

// The fallback interpreter for instructions Dynarmic does not implement (LSE
// atomics, RCPC loads, ID register reads). Returns false for an undefined one.
bool interpret_instruction(GuestThread& thread, uint64_t pc, uint32_t instruction);
// Stops the thread's current Run with a synchronous signal at `pc`.
void cpu_raise(GuestThread& thread, int signal, int code, uint64_t address, uint64_t pc);

std::string describe_address(uint64_t address);
void set_address_describer(std::function<std::string(uint64_t)> describer);
void dump_thread_state(GuestThread& thread, const char* why);

uint64_t cntfrq();
uint64_t cntpct();

}  // namespace axrb
