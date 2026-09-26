/* Runs the instruction tests through the interpreter and reports them.

   No headset, no graphics, no host: this loads neontest.so, executes it, and
   prints one line per failing case. Seconds per run instead of a minute, which
   is what makes the rest of the arm64 work tractable. */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "qb_env.h"
#include "android.h"
#include "loader.h"

#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <atomic>
#include <exception>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <thread>
#include <mutex>
#include <map>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdlib>

namespace {

GuestImage g_image;
GuestLibc g_libc;
int g_passed = 0;
int g_failed = 0;

const char* guest_string(uint64_t va) {
    const char* p = reinterpret_cast<const char*>(guest_ptr(g_image.mem, va, 1));
    return p ? p : "(unnamed)";
}

std::vector<std::string> g_called;
/* The host call each thread is inside, for the invalid-parameter handler. */
thread_local const char* t_current_call = nullptr;

/* A word from another thread's stack, which may not be mapped yet (a thread
   being created) or any more: the samplers read with this. */
bool safe_read(uint64_t address, uint64_t* out) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void thunk(GuestCpu& cpu, GuestMem&, int index, void*) {
    /* The hottest calls skip the name lookup (fastcalls.cpp). Each import is
       classified once; the diagnostic switches below want every call, so
       they turn this off. */
    static const bool diagnosing =
        QB_ENV("QB_TRACE") || QB_ENV("QB_STACKCHECK") || QB_ENV("QB_CALL_STATS") ||
        QB_ENV("QB_TRACE_CALLS");
    static std::atomic<int16_t> fast_ids[1 << 16];
    if (!diagnosing && index >= 0 && index < (1 << 16)) {
        int16_t kind = fast_ids[index].load(std::memory_order_relaxed);
        if (kind == 0) {
            kind = (int16_t)(GuestLibc::fast_id(g_image.imports()[index]) + 2);
            fast_ids[index].store(kind, std::memory_order_relaxed);
        }
        if (kind >= 2 && g_libc.fast_call(kind - 2, cpu, index, g_image.imports()[index])) return;
    }
    const std::string& name = g_image.imports()[index];
    t_current_call = name.c_str();
    static const bool trace_calls = QB_ENV("QB_TRACE") != nullptr;
    if (trace_calls) {
        bool seen = false;
        for (const std::string& already : g_called) seen = seen || already == name;
        if (!seen) {
            g_called.push_back(name);
            std::printf("call: %s\n", name.c_str());
            std::fflush(stdout);
        }
    }
    if (name != "qb_check") {
        /* QB_STACKCHECK: a host call must leave the caller's stack pointer
           and the words just above it (where compilers keep the saved link
           register) exactly as it found them. */
        static const bool check = QB_ENV("QB_STACKCHECK") != nullptr;
        uint64_t sp_before = cpu.sp;
        uint64_t above[4] = {};
        if (check) std::memcpy(above, reinterpret_cast<void*>(cpu.sp), sizeof(above));
        uint64_t entry_args[5] = {cpu.x[0], cpu.x[1], cpu.x[2], cpu.x[3], cpu.x[4]};
        /* QB_CALL_STATS: host calls by name, with the time spent in each,
           every ten seconds. */
        /* QB_CALL_STATS=<thread pointer hex> limits it to one thread. */
        static const uint64_t call_stats_tp = [] {
            const char* text = QB_ENV("QB_CALL_STATS");
            return text ? std::strtoull(text, nullptr, 16) : 0ull;
        }();
        static const bool call_stats_on = QB_ENV("QB_CALL_STATS") != nullptr;
        const bool call_stats = call_stats_on && (!call_stats_tp || cpu.tpidr == call_stats_tp);
        LARGE_INTEGER call_start{};
        if (call_stats) QueryPerformanceCounter(&call_start);
        bool answered = g_libc.call(name, cpu);
        if (call_stats) {
            LARGE_INTEGER call_end{}, frequency{};
            QueryPerformanceCounter(&call_end);
            QueryPerformanceFrequency(&frequency);
            struct Cost { uint64_t calls = 0; double us = 0; };
            static std::mutex cost_lock;
            static std::unordered_map<std::string, Cost> costs;
            static auto last = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> held(cost_lock);
            Cost& cost = costs[name];
            ++cost.calls;
            cost.us += (call_end.QuadPart - call_start.QuadPart) * 1e6 / (double)frequency.QuadPart;
            if (std::chrono::steady_clock::now() - last > std::chrono::seconds(10)) {
                last = std::chrono::steady_clock::now();
                std::vector<std::pair<double, std::string>> order;
                uint64_t all = 0;
                for (auto& entry : costs) {
                    order.push_back({entry.second.us, entry.first});
                    all += entry.second.calls;
                }
                std::sort(order.rbegin(), order.rend());
                std::printf("calls: %llu host calls in 10 s\n", (unsigned long long)all);
                for (size_t i = 0; i < order.size() && i < 12; ++i)
                    std::printf("calls: %10.0f us %9llu x %6.2f us  %s\n", order[i].first,
                                (unsigned long long)costs[order[i].second].calls,
                                order[i].first / costs[order[i].second].calls, order[i].second.c_str());
                std::fflush(stdout);
                costs.clear();
            }
        }
        if (check) {
            uint64_t after[4] = {};
            std::memcpy(after, reinterpret_cast<void*>(sp_before), sizeof(after));
            if (cpu.sp != sp_before || std::memcmp(above, after, sizeof(above)) != 0)
                std::fprintf(stderr,
                             "stackcheck: %s changed the caller's stack: sp %llx -> %llx, [sp+16] %llx -> %llx\n",
                             name.c_str(), (unsigned long long)sp_before, (unsigned long long)cpu.sp,
                             (unsigned long long)above[2], (unsigned long long)after[2]);
            if (check && after[2] != above[2])
                std::fprintf(stderr, "stackcheck:   args %llx %llx %llx %llx %llx, returned %llx\n",
                             (unsigned long long)entry_args[0], (unsigned long long)entry_args[1],
                             (unsigned long long)entry_args[2], (unsigned long long)entry_args[3],
                             (unsigned long long)entry_args[4], (unsigned long long)cpu.x[0]);
        }
        /* QB_TRACE_CALLS=read,lseek64,...: those calls, with their first
           six arguments and what they returned. */
        static const std::string traced = [] {
            const char* list = QB_ENV("QB_TRACE_CALLS");
            return list ? "," + std::string(list) + "," : std::string();
        }();
        if (!traced.empty() && traced.find("," + name + ",") != std::string::npos) {
            std::printf("calls: %s(%llx, %llx, %llx, %llx, %llx) -> %llx\n", name.c_str(),
                        (unsigned long long)entry_args[0], (unsigned long long)entry_args[1],
                        (unsigned long long)entry_args[2], (unsigned long long)entry_args[3],
                        (unsigned long long)entry_args[4], (unsigned long long)cpu.x[0]);
            std::fflush(stdout);
        }
        if (answered) return;
        std::fprintf(stderr, "test: the guest called %s, which this runner does not answer\n", name.c_str());
        return;
    }
    const char* label = guest_string(cpu.x[0]);
    uint64_t got_lo = cpu.x[1];
    uint64_t got_hi = cpu.x[2];
    uint64_t want_lo = cpu.x[3];
    uint64_t want_hi = cpu.x[4];
    if (got_lo == want_lo && got_hi == want_hi) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("  FAIL  %-28s got %016llx %016llx, wanted %016llx %016llx\n", label,
                (unsigned long long)got_hi, (unsigned long long)got_lo, (unsigned long long)want_hi,
                (unsigned long long)want_lo);
}

}  // namespace

int main(int argc, char** argv) {
    HANDLE stop_event = nullptr;
    if (const char* event_name = QB_ENV("QB_STOP_EVENT")) {
        stop_event = CreateEventA(nullptr, TRUE, FALSE, event_name);
        if (!stop_event) { std::fprintf(stderr, "bridge: cannot create stop event\n"); return 1; }
    }
    std::string program_path;
    if (argc > 1) {
        program_path = argv[1];
    } else {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = std::strrchr(path, '\\');
        if (slash) std::strcpy(slash + 1, "libneontest.so");
        program_path = path;
    }
    const char* path = program_path.c_str();
    /* An application built as a native activity has no qb_guest_main: it is
       entered through ANativeActivity_onCreate and then driven by lifecycle
       callbacks, which is what a real one on the headset does. */
    bool as_activity = std::strstr(path, "glue") != nullptr;
    /* A second argument names the entry point, so a library that starts
       somewhere else, such as a game's JNI_OnLoad, can be tried. */
    const char* entry = argc > 2 ? argv[2] : (as_activity ? "ANativeActivity_onCreate" : "qb_guest_main");
    /* Whatever the file is called, an activity entry point has to be handed a
       real activity: it writes its callbacks through the pointer it is given. */
    as_activity = std::strcmp(entry, "ANativeActivity_onCreate") == 0;
    /* A host-side exception or thread misuse ends the process with 127 and
       no word; say what it was and where the guest stood. */
    /* The C runtime ends the process silently (0xC0000409) on a bad argument
       to one of its functions; say which guest call handed it over. */
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        const GuestCpu* cpu = t_guest_cpu;
        std::fprintf(stderr, "crt: invalid parameter inside host call %s, guest pc %s lr %s\n",
                     t_current_call ? t_current_call : "-", cpu ? guest_describe(cpu->pc).c_str() : "-",
                     cpu ? guest_describe(cpu->x[30]).c_str() : "-");
        std::fflush(stderr);
        std::_Exit(126);
    });
    std::set_terminate([] {
        const char* what = "unknown";
        try {
            if (std::current_exception()) std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        const GuestCpu* cpu = t_guest_cpu;
        std::fprintf(stderr, "terminate: %s, guest pc %s lr %s\n", what,
                     cpu ? guest_describe(cpu->pc).c_str() : "-", cpu ? guest_describe(cpu->x[30]).c_str() : "-");
        std::fflush(stderr);
        std::_Exit(127);
    });
    /* A host crash that no guard catches (in a driver, say): name the module
       and what the guest thread on this host thread was doing. */
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG {
        uint64_t at = (uint64_t)info->ExceptionRecord->ExceptionAddress;
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(at), &module))
            GetModuleFileNameA(module, name, MAX_PATH);
        const GuestCpu* cpu = t_guest_cpu;
        std::fprintf(stderr, "crash: code %08lx at %llx (%s+%llx), reading %llx; guest pc %s lr %s\n",
                     info->ExceptionRecord->ExceptionCode, (unsigned long long)at, name,
                     (unsigned long long)(at - (uint64_t)module),
                     info->ExceptionRecord->NumberParameters > 1
                         ? (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]
                         : 0ull,
                     cpu ? guest_describe(cpu->pc).c_str() : "-", cpu ? guest_describe(cpu->x[30]).c_str() : "-");
        /* The host function and line, from the PDB next to the exe. */
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        if (SymInitialize(process, nullptr, TRUE)) {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymFromAddr(process, at, &displacement, symbol)) {
                bool has_line = SymGetLineFromAddr64(process, at, &line_displacement, &line) != FALSE;
                std::fprintf(stderr, "crash:   in %s+%llx%s%s:%lu\n", symbol->Name, (unsigned long long)displacement,
                             has_line ? " at " : "", has_line ? line.FileName : "", has_line ? line.LineNumber : 0ul);
            }
        }
        std::fflush(stderr);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    /* Windows rounds every sleep and timed wait up to its timer tick, 15.6 ms
       by default. The guest's threads hand work to each other through short
       sleeps and timed waits all frame long, so each handoff cost a tick.
       One millisecond, as games on Windows ask for. */
    timeBeginPeriod(1);
    if (!guest_load(g_image, path, entry)) return 1;
    /* QB_JITFUZZ=N: N random instructions from the families the JIT
       translates, each run once by the interpreter and once as a JIT block
       from the same registers and memory, and every difference reported.
       Needs no device: the interpreter is the reference, and it is itself
       checked against real hardware by tools/fuzz. */
    if (const char* fuzz = QB_ENV("QB_JITFUZZ")) {
        struct Family {
            uint32_t mask, value;
            const char* name;
        };
        static const Family families[] = {
            {0x1f000000u, 0x11000000u, "add/sub imm"},   {0x1f200000u, 0x0b000000u, "add/sub reg"},
            {0x1f000000u, 0x0a000000u, "logical reg"},   {0x1f800000u, 0x12000000u, "logical imm"},
            {0x1f800000u, 0x12800000u, "movn/z/k"},      {0x1f000000u, 0x10000000u, "adr/adrp"},
            {0x1f800000u, 0x13000000u, "sbfm/ubfm"},     {0x7f800000u, 0x33000000u, "bfm"},
            {0x7fa00000u, 0x13800000u, "extr"},          {0x1fe00000u, 0x1a800000u, "csel"},
            {0x7fe00000u, 0x1b000000u, "madd/msub"},     {0xff600000u, 0x9b200000u, "maddl"},
            {0xff60fc00u, 0x9b407c00u, "mulh"},          {0x7fe0f000u, 0x1ac02000u, "shiftv"},
            {0x7fe0f800u, 0x1ac00800u, "div"},           {0x1fe00000u, 0x0b200000u, "add/sub ext"},
            {0x3b000000u, 0x39000000u, "ldr/str uimm"},  {0x3b200000u, 0x38000000u, "ldr/str imm9"},
            {0x3b200c00u, 0x38200800u, "ldr/str reg"},   {0x3a000000u, 0x28000000u, "ldp/stp"},
            {0x3f000000u, 0x3d000000u, "vec ldr/str"},   {0x3f200000u, 0x3c000000u, "vec imm9"},     {0x3f200c00u, 0x3c200800u, "vec ldr reg"},
            {0x3a000000u, 0x2c000000u, "vec ldp/stp"},   {0x7c000000u, 0x14000000u, "b/bl"},
            {0xff000010u, 0x54000000u, "b.cond"},        {0x7e000000u, 0x34000000u, "cbz"},
            {0x7e000000u, 0x36000000u, "tbz"},           {0x3ffffc00u, 0x08dffc00u, "ldar"},
            {0x3ffffc00u, 0x089ffc00u, "stlr"},
            {0xff200c00u, 0x1e200800u, "fp arith s"},     {0xff200c00u, 0x1e600800u, "fp arith d"},
            {0x9f20fc00u, 0x0e20d400u, "vec fadd/fsub"},  {0xbfa0fc00u, 0x2e20dc00u, "vec fmul"},
            {0xbfa0fc00u, 0x2e20fc00u, "vec fdiv"},       {0x9f20fc00u, 0x0e201c00u, "vec logical"},
            {0xbfe0fc00u, 0x0e000400u, "dup element"},    {0xbfe0fc00u, 0x0e000c00u, "dup general"},
            {0x9fbffc00u, 0x0ea0f800u, "vec fabs/fneg"},  {0xbfc0f400u, 0x0f809000u, "fmul element"},
            {0x7ffffc00u, 0x5ac01000u, "clz"},           {0xbffffc00u, 0x0e205800u, "cnt"}, {0xffffffffu, 0u, "flags+csel"}, {0xbfe0fc00u, 0x0e000000u, "tbl1"}, {0xffc0f400u, 0x5f809000u, "fmul elem s"}, {0xffbffc00u, 0x1e230000u, "ucvtf w"}, {0xffffffffu, 0u, "int+fp/div+int"}, {0x3fe00410u, 0x3a400000u, "ccmp/ccmn"}, {0x3f20cc00u, 0x38200000u, "ldadd/clr/eor/set"}, {0x9fa0fc00u, 0x0e20e400u, "fcmeq v"}, {0x9f20fc00u, 0x2e20e400u, "fcmge/gt v"},
            {0x9fa0fc00u, 0x2ea0d400u, "fabd v"}, {0x9f60fc00u, 0x0e20f400u, "fmax/min v"},
            {0xbf20fc00u, 0x0e20bc00u, "addp v"}, {0x9f80fc00u, 0x0f00a400u, "sshll/ushll"},
            {0xbf3ffc00u, 0x0e212800u, "xtn"}, {0xbf3ffc00u, 0x0e200800u, "rev64"},
            {0xbffffc00u, 0x2e21d800u, "ucvtf v"}, {0xdffffc00u, 0x5e21d800u, "s/ucvtf scalar"},
            {0xbfbfa000u, 0x0d000000u, "ld1/st1 b/h lane"}, {0xbffffc00u, 0x2e303800u, "uaddlv b"},
            {0xff3e7c00u, 0x1e204000u, "fp 1-source s"}, {0xff3e7c00u, 0x1e604000u, "fp 1-source d"},
            {0xff207c00u, 0x1e22c000u, "fcvt s>d"},      {0xff207c00u, 0x1e624000u, "fcvt d>s"},
            {0xff20fc07u, 0x1e202000u, "fcmp s"},        {0xff20fc07u, 0x1e602010u, "fcmpe d"},
            {0xffe0fc1fu, 0x1e202008u, "fcmp s zero"},   {0xff200c00u, 0x1e200c00u, "fcsel s"},
            {0xff200c00u, 0x1e600c00u, "fcsel d"},       {0xfffffc00u, 0x1e260000u, "fmov w,s"},
            {0xfffffc00u, 0x9e670000u, "fmov d,x"},      {0xfffffc00u, 0x1e220000u, "scvtf s,w"},
            {0xfffffc00u, 0x9e620000u, "scvtf d,x"},     {0xfffffc00u, 0x1e380000u, "fcvtzs w,s"},
            {0xfffffc00u, 0x9e780000u, "fcvtzs x,d"},    {0xffe08400u, 0x6e000400u, "ins element"},
            {0xffe0fc00u, 0x4e001c00u, "ins general"},   {0xffe0fc00u, 0x5e000400u, "dup scalar"},
            {0xbfe0fc00u, 0x0e003c00u, "umov"},          {0xbfe08400u, 0x2e000000u, "ext"},
            {0xbf208c00u, 0x0e000800u, "zip/uzp/trn"},   {0xbfbffc00u, 0x0e21d800u, "scvtf vec"},
            {0xbfbffc00u, 0x0ea1b800u, "fcvtzs vec"},    {0xfffffc00u, 0xdac00c00u, "rev x"},
            {0xfffffc00u, 0x5ac00800u, "rev w"},         {0x3fe08000u, 0x08400000u, "ldxr"},
            {0x3fe08000u, 0x08000000u, "stxr"},
            {0x9f20fc00u, 0x0e208400u, "vec add/sub"},   {0xbf20fc00u, 0x0e209c00u, "vec mul"},
            {0xbf20fc00u, 0x2e208c00u, "cmeq"},          {0xbf20fc00u, 0x0e203400u, "cmgt"},
            {0xbf20fc00u, 0x0e203c00u, "cmge"},          {0xbf20fc00u, 0x0e208c00u, "cmtst"},
            {0x9f20f400u, 0x0e206400u, "max/min"},       {0xbfa0fc00u, 0x0e20cc00u, "fmla vec"},
            {0xbfa0fc00u, 0x0ea0cc00u, "fmls vec"},      {0xbfa0fc00u, 0x2e20d400u, "faddp"},
            {0xbffffc00u, 0x2e205800u, "not"},           {0xbfc0b400u, 0x0f801000u, "fmla elem"},
            {0xbffff000u, 0x0d40c000u, "ld1r"},          {0xbfe0f000u, 0x0dc0c000u, "ld1r post"},
            {0xbfbff000u, 0x0c007000u, "ld1/st1 1reg"},  {0xbfa0f000u, 0x0c807000u, "ld1/st1 post"},
            {0xbfbfe000u, 0x0d008000u, "ld1/st1 lane"},  {0x7ffffc00u, 0x5ac00000u, "rbit"},
        };
        const uint64_t code_va = 0x5000000000ull, data_va = 0x5000100000ull;
        guest_reserve(code_va, 0x10000, true);
        guest_reserve(data_va, 0x10000, true);
        uint8_t* data = reinterpret_cast<uint8_t*>(data_va);
        uint64_t seed = 0x9e3779b97f4a7c15ull;
        auto next = [&]() {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        std::vector<uint8_t> memory_before(0x10000);
        int total = std::atoi(fuzz), compared = 0, differ = 0, skipped = 0;
        std::unordered_map<std::string, int> per_family_differ, per_family_compared;
        for (int n = 0; n < total; ++n) {
            const Family& family = families[next() % (sizeof(families) / sizeof(families[0]))];
            uint32_t insn = (uint32_t)(next() & ~family.mask) | family.value;
            std::memcpy(reinterpret_cast<void*>(code_va), &insn, 4);
            uint32_t stop = 0; /* udf: ends the JIT block and the interpreter step */
            std::memcpy(reinterpret_cast<void*>(code_va + 4), &stop, 4);
            /* "flags+csel": a flag-setting add, subtract or logical op, then a
               conditional select that reads its flags, run as a pair. */
            bool pair = std::strcmp(family.name, "flags+csel") == 0;
            /* "int+fp/div+int": registers written, a NaN-guarded FP op or a
               divide in between, then read: the register cache kept across. */
            bool triple = std::strcmp(family.name, "int+fp/div+int") == 0;
            if (triple) {
                auto from = [&](uint32_t mask, uint32_t value) { return (uint32_t)(next() & ~mask) | value; };
                uint32_t middle;
                switch (next() % 4) {
                    case 0: middle = from(0x9f20fc00u, 0x0e20d400u); break; /* vec fadd/fsub */
                    case 1: middle = from(0xff200c00u, 0x1e200800u); break; /* fp arith s */
                    case 2: middle = from(0x7fe0f800u, 0x1ac00800u); break; /* div */
                    default: middle = from(0xbfbffc00u, 0x0ea1b800u); break; /* fcvtzs vec */
                }
                uint32_t words[4] = {from(0x1f200000u, 0x0b000000u), middle, from(0x1f200000u, 0x0b000000u), 0};
                /* the third reads what the first wrote */
                words[2] = (words[2] & ~(31u << 5)) | ((words[0] & 31u) << 5);
                std::memcpy(reinterpret_cast<void*>(code_va), words, 16);
            }
            if (pair) {
                static const uint32_t setters[][2] = {{0x1f200000u, 0x0b200000u}, /* adds/subs ext: masked below */
                                                      {0x5f000000u, 0x31000000u}, /* adds/subs imm */
                                                      {0x5f200000u, 0x2b000000u}, /* adds/subs reg */
                                                      {0x7f200000u, 0x6a000000u}, /* ands/bics reg */
                                                      {0x7f800000u, 0x72000000u}}; /* ands imm */
                const auto& setter = setters[1 + next() % 4];
                uint32_t first = (uint32_t)(next() & ~setter[0]) | setter[1];
                if (setter[1] == 0x31000000u || setter[1] == 0x2b000000u) first |= 0x20000000u; /* S bit */
                uint32_t second = (uint32_t)(next() & ~0x5fe00800u) | 0x1a800000u;           /* CSEL family */
                std::memcpy(reinterpret_cast<void*>(code_va), &first, 4);
                std::memcpy(reinterpret_cast<void*>(code_va + 4), &second, 4);
                std::memcpy(reinterpret_cast<void*>(code_va + 8), &stop, 4);
            }
            guest_jit_invalidate();
            GuestCpu start{};
            bool pointers = (n & 1) == 0;
            for (int r = 0; r < 31; ++r)
                start.x[r] = pointers ? data_va + 0x8000 + (next() & 0x7f0) : next();
            start.sp = data_va + 0x8000 + (next() & 0x7f0);
            start.pc = code_va;
            start.n = next() & 1;
            start.z = next() & 1;
            start.c = next() & 1;
            start.v = next() & 1;
            start.tpidr = next();
            for (int r = 0; r < 32; ++r) start.q[r] = {next(), next()};
            /* A quarter of the time, float lanes from a few edge values:
               signed zeros, equal inputs, NaNs, infinities, small integers. */
            if ((n & 3) == 1) {
                static const uint32_t edge[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7fc00000u,
                                                0x7f800001u, 0x7f800000u, 0xff800000u, 0x40490fdbu, 0x00000001u};
                for (int r = 0; r < 32; ++r) {
                    uint64_t lanes[2];
                    for (int w = 0; w < 4; ++w) {
                        uint32_t v = edge[next() % (sizeof(edge) / sizeof(edge[0]))];
                        uint32_t* words = reinterpret_cast<uint32_t*>(lanes);
                        words[w] = v;
                    }
                    start.q[r] = {lanes[0], lanes[1]};
                }
            }
            for (size_t i = 0; i < memory_before.size(); ++i) memory_before[i] = (uint8_t)next();

            std::memcpy(data, memory_before.data(), memory_before.size());
            GuestCpu interpreted = start;
            bool ok_interpreter = guest_step_once(interpreted, g_image.mem) == 1;
            if ((pair || triple) && ok_interpreter) ok_interpreter = guest_step_once(interpreted, g_image.mem) == 1;
            if (triple && ok_interpreter) ok_interpreter = guest_step_once(interpreted, g_image.mem) == 1;
            std::vector<uint8_t> memory_interpreted(data, data + 0x10000);

            std::memcpy(data, memory_before.data(), memory_before.size());
            GuestCpu jitted = start;
            int ran = guest_jit_step(jitted, g_image.mem);
            if (pair && ran == 1) ran = 0; /* the pair must run as one block to test anything */
            if (triple && ran < 3) ran = 0;
            if (ran <= 0 || !ok_interpreter) {
                ++skipped; /* not translated after all, or it faulted */
                continue;
            }
            ++compared;
            ++per_family_compared[family.name];
            bool same = std::memcmp(interpreted.x, jitted.x, sizeof(jitted.x)) == 0 && interpreted.sp == jitted.sp &&
                        interpreted.pc == jitted.pc && interpreted.n == jitted.n && interpreted.z == jitted.z &&
                        interpreted.c == jitted.c && interpreted.v == jitted.v &&
                        std::memcmp(interpreted.q, jitted.q, sizeof(jitted.q)) == 0 &&
                        std::memcmp(memory_interpreted.data(), data, 0x10000) == 0;
            if (same) continue;
            ++differ;
            if (per_family_differ[family.name]++ < 3) {
                std::printf("jitfuzz: %s %08x differs:", family.name, insn);
                for (int r = 0; r < 31; ++r)
                    if (interpreted.x[r] != jitted.x[r])
                        std::printf(" x%d %llx/%llx", r, (unsigned long long)interpreted.x[r],
                                    (unsigned long long)jitted.x[r]);
                if (interpreted.sp != jitted.sp) std::printf(" sp");
                if (interpreted.pc != jitted.pc)
                    std::printf(" pc %llx/%llx", (unsigned long long)interpreted.pc, (unsigned long long)jitted.pc);
                if (interpreted.n != jitted.n || interpreted.z != jitted.z || interpreted.c != jitted.c ||
                    interpreted.v != jitted.v)
                    std::printf(" nzcv %d%d%d%d/%d%d%d%d", interpreted.n, interpreted.z, interpreted.c, interpreted.v,
                                jitted.n, jitted.z, jitted.c, jitted.v);
                if (std::memcmp(interpreted.q, jitted.q, sizeof(jitted.q)) != 0) std::printf(" q");
                if (std::memcmp(memory_interpreted.data(), data, 0x10000) != 0) std::printf(" memory");
                std::printf("\n");
            }
        }
        /* Pairs: a flag-setting instruction then a conditional branch, which
           the JIT may fuse into one x86 compare-and-jump. */
        {
            static const Family setters[] = {
                {0x7f000000u, 0x71000000u, "subs imm"}, {0x7f200000u, 0x6b000000u, "subs reg"},
                {0x7f000000u, 0x31000000u, "adds imm"}, {0x7f200000u, 0x2b000000u, "adds reg"},
                {0x7f800000u, 0x72000000u, "ands imm"}, {0x7f200000u, 0x6a000000u, "ands reg"},
            };
            int pair_compared = 0, pair_differ = 0;
            for (int n = 0; n < total / 4; ++n) {
                const Family& family = setters[next() % (sizeof(setters) / sizeof(setters[0]))];
                uint32_t first = (uint32_t)(next() & ~family.mask) | family.value;
                uint32_t branch = 0x54000000u | (uint32_t)(next() % 14) | (8u << 5); /* b.cc +32 */
                uint32_t stop = 0;
                std::memcpy(reinterpret_cast<void*>(code_va), &first, 4);
                std::memcpy(reinterpret_cast<void*>(code_va + 4), &branch, 4);
                std::memcpy(reinterpret_cast<void*>(code_va + 8), &stop, 4);
                guest_jit_invalidate();
                GuestCpu start{};
                for (int r = 0; r < 31; ++r) start.x[r] = (n & 3) ? next() : (next() & 0xff);
                start.sp = data_va + 0x8000;
                start.pc = code_va;
                GuestCpu interpreted = start, jitted = start;
                if (guest_step_once(interpreted, g_image.mem) != 1 || guest_step_once(interpreted, g_image.mem) != 1)
                    continue;
                int ran = guest_jit_step(jitted, g_image.mem);
                if (ran != 2) continue;
                ++pair_compared;
                bool same = std::memcmp(interpreted.x, jitted.x, sizeof(jitted.x)) == 0 &&
                            interpreted.pc == jitted.pc && interpreted.n == jitted.n && interpreted.z == jitted.z &&
                            interpreted.c == jitted.c && interpreted.v == jitted.v;
                if (!same && pair_differ++ < 5)
                    std::printf("jitfuzz: pair %s %08x + %08x differs: pc %llx/%llx\n", family.name, first, branch,
                                (unsigned long long)interpreted.pc, (unsigned long long)jitted.pc);
            }
            std::printf("jitfuzz: %d pairs compared, %d differ\n", pair_compared, pair_differ);
            differ += pair_differ;
        }
        std::printf("jitfuzz: %d compared, %d differ, %d skipped\n", compared, differ, skipped);
        for (auto& entry : per_family_differ) std::printf("jitfuzz:   %s: %d\n", entry.first.c_str(), entry.second);
        if (QB_ENV("QB_JITFUZZ_COVERAGE"))
            for (auto& entry : per_family_compared)
                std::printf("jitfuzz:   compared %6d %s\n", entry.second, entry.first.c_str());
        std::fflush(stdout);
        std::_Exit(differ ? 1 : 0);
    }
    /* QB_PROFILE=seconds: sample every running guest thread each millisecond
       for that long, then print where the time went, by 256-byte region of
       code (roughly one function each), and which host calls it sat in. */
    if (const char* profile = QB_ENV("QB_PROFILE")) {
        int seconds = std::max(1, std::atoi(profile));
        /* QB_PROFILE_DELAY=seconds: start sampling that long after launch. */
        int delay = QB_ENV("QB_PROFILE_DELAY") ? std::atoi(QB_ENV("QB_PROFILE_DELAY")) : 0;
        std::thread([seconds, delay]() {
            std::this_thread::sleep_for(std::chrono::seconds(delay));
            static std::unordered_map<std::string, uint64_t> hits;
            static uint64_t total = 0;
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            while (std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                guest_each_running([](const GuestCpu& cpu) {
                    const GuestMem& mem = g_image.mem;
                    std::string where;
                    if (cpu.pc >= mem.thunk_va && cpu.pc < mem.thunk_va + (uint64_t)mem.thunk_count * mem.thunk_stride)
                        where = "call " + g_image.imports()[(size_t)((cpu.pc - mem.thunk_va) / mem.thunk_stride)];
                    else
                        where = guest_describe(cpu.pc & ~0xffull);
                    char thread[32];
                    std::snprintf(thread, sizeof(thread), "%llx ", (unsigned long long)cpu.tpidr);
                    ++hits[thread + where];
                    ++total;
                });
            }
            std::vector<std::pair<uint64_t, std::string>> sorted;
            for (auto& entry : hits) sorted.push_back({entry.second, entry.first});
            std::sort(sorted.rbegin(), sorted.rend());
            std::fprintf(stderr, "profile: %llu samples\n", (unsigned long long)total);
            for (size_t i = 0; i < sorted.size() && i < 400; ++i)
                std::fprintf(stderr, "profile: %7llu %s\n", (unsigned long long)sorted[i].first,
                             sorted[i].second.c_str());
            /* Per thread: how much of its time is guest code, and which host
               calls take the rest. */
            {
                std::unordered_map<std::string, uint64_t> thread_total, thread_code;
                for (auto& entry : hits) {
                    std::string thread = entry.first.substr(0, entry.first.find(' '));
                    thread_total[thread] += entry.second;
                    if (entry.first.find(" call ") == std::string::npos) thread_code[thread] += entry.second;
                }
                for (auto& entry : thread_total)
                    if (thread_code[entry.first] * 20 > entry.second) /* threads more than 5% busy */
                        std::fprintf(stderr, "profile thread: %s %5.1f%% guest code of %llu samples\n",
                                     entry.first.c_str(), 100.0 * thread_code[entry.first] / entry.second,
                                     (unsigned long long)entry.second);
            }
            /* Per thread and library (or "call <name>"), over every sample,
               for threads busy enough to matter. */
            {
                std::map<std::string, uint64_t> by_lib;
                for (auto& entry : hits) {
                    std::string key = entry.first;
                    size_t plus = key.find('+');
                    if (plus != std::string::npos && key.find(" call ") == std::string::npos) key = key.substr(0, plus);
                    by_lib[key] += entry.second;
                }
                for (auto& entry : by_lib)
                    if (entry.second >= 200)
                        std::fprintf(stderr, "profile lib: %7llu %s\n", (unsigned long long)entry.second, entry.first.c_str());
            }
            /* Threads parked in host calls crowd out the top of that list, so
               the guest code actually running gets a list of its own. */
            int shown = 0;
            for (size_t i = 0; i < sorted.size() && shown < 80; ++i) {
                if (sorted[i].second.find(" call ") != std::string::npos) continue;
                std::fprintf(stderr, "profile code: %7llu %s\n", (unsigned long long)sorted[i].first,
                             sorted[i].second.c_str());
                ++shown;
            }
            std::fflush(stderr);
        }).detach();
    }
    /* QB_SAMPLE=N prints where every guest thread is, every N seconds. */
    if (const char* every = QB_ENV("QB_SAMPLE")) {
        int seconds = std::max(1, std::atoi(every));
        /* QB_PROFILE_DELAY holds the sampler back too, past start-up. */
        int delay = QB_ENV("QB_PROFILE_DELAY") ? std::atoi(QB_ENV("QB_PROFILE_DELAY")) : 0;
        std::thread([seconds, delay]() {
            std::this_thread::sleep_for(std::chrono::seconds(delay));
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                guest_each_running([](const GuestCpu& cpu) {
                    const GuestMem& mem = g_image.mem;
                    if (cpu.pc >= mem.thunk_va && cpu.pc < mem.thunk_va + (uint64_t)mem.thunk_count * mem.thunk_stride)
                        std::fprintf(stderr, "sample: in %s\n",
                                     g_image.imports()[(size_t)((cpu.pc - mem.thunk_va) / mem.thunk_stride)].c_str());
                    std::fprintf(stderr, "sample: tp %llx pc %s lr %s sp %llx\n", (unsigned long long)cpu.tpidr,
                                 guest_describe(cpu.pc).c_str(), guest_describe(cpu.x[30]).c_str(),
                                 (unsigned long long)cpu.sp);
                    /* QB_SAMPLE_SCAN: every word in the top of the stack that
                       points into a loaded library, a rough backtrace that
                       survives code built without frame pointers. */
                    if (QB_ENV("QB_SAMPLE_SCAN")) {
                        int shown = 0;
                        for (uint64_t at = cpu.sp; at < cpu.sp + 0x4000 && shown < 40 && at + 8 <= (cpu.tpidr == 0x60000000ull ? 0x7f000000ull : cpu.tpidr); at += 8) {
                            uint64_t word = 0;
                            if (!safe_read(at, &word)) break;
                            if (word < 0x2000000000ull || word > 0x3000000000ull) continue;
                            std::string where = guest_describe(word);
                            if (where.find('+') == std::string::npos) continue;
                            std::fprintf(stderr, "sample:   ~%llx %s\n", (unsigned long long)(at - cpu.sp),
                                         where.c_str());
                            ++shown;
                        }
                    }
                    /* The frame record chain: [fp] is the caller's fp, [fp+8]
                       its return address. */
                    uint64_t fp = cpu.x[29];
                    for (int depth = 0; depth < 14 && fp >= 0x10000 && fp < 0x800000000000ull && !(fp & 7); ++depth) {
                        uint64_t next = 0, lr = 0;
                        if (!safe_read(fp, &next) || !safe_read(fp + 8, &lr)) break;
                        if (!lr) break;
                        std::fprintf(stderr, "sample:   #%d %s\n", depth, guest_describe(lr).c_str());
                        if (next <= fp) break;
                        fp = next;
                    }
                });
                std::fflush(stderr);
            }
        }).detach();
    }
    g_image.callback = thunk;
    static auto started = std::chrono::steady_clock::now();
    g_libc.start(&g_image, 256u << 20);
    std::printf("test: %s, %zu imports, %zu initialisers\n", path, g_image.imports().size(),
                g_image.initialisers.size());
    std::fflush(stdout);
    g_libc.run_initialisers();
    bool ok = true;
    if (as_activity) {
        uint64_t activity = g_libc.make_activity();
        uint64_t entry = g_image.cpu.pc;
        /* System.loadLibrary runs JNI_OnLoad before the activity is created;
           an engine that has one (Unreal caches the VM there) expects that. */
        if (uint64_t on_load = g_image.linker.lookup("JNI_OnLoad")) {
            uint64_t version = g_libc.call_guest(on_load, g_libc.vm_va, 0);
            std::printf("test: JNI_OnLoad returned %llx\n", (unsigned long long)version);
            std::fflush(stdout);
        }
        /* onCreate starts the application's own thread and returns. */
        /* Unreal: its GameActivity's Java onCreate passes the engine its
           files, OBBs and device, then nativeResumeMainInit, which
           AndroidMain waits for. Called here in that order, as that Java
           would. Paths are guest paths, which live under QB_ROOT. */
        std::string unreal_activity;
        for (const char* prefix : {"Java_com_epicgames_ue4_GameActivity_", "Java_com_epicgames_unreal_GameActivity_"}) {
            if (g_image.linker.lookup(std::string(prefix) + "nativeResumeMainInit")) { unreal_activity = prefix; break; }
        }
        const bool unreal = !unreal_activity.empty();
        if (unreal) {
            const std::string package = guest_package();
            const std::string obb_dir = "/sdcard/Android/obb/" + package + "/";
            const std::string root = QB_ENV("QB_ROOT") ? QB_ENV("QB_ROOT") : ".";
            std::string main_obb, patch_obb;
            WIN32_FIND_DATAA found{};
            HANDLE search = FindFirstFileA((root + obb_dir + "*.obb").c_str(), &found);
            if (search != INVALID_HANDLE_VALUE) {
                do {
                    std::string name = found.cFileName;
                    if (name.compare(0, 5, "main.") == 0) main_obb = obb_dir + name;
                    else if (name.compare(0, 6, "patch.") == 0 && name.find("pakchunk") == std::string::npos)
                        patch_obb = obb_dir + name;
                } while (FindNextFileA(search, &found));
                FindClose(search);
            }
            for (const std::string& dir : {"/data/data/" + package + "/files", "/sdcard/Android/data/" + package + "/files"})
                std::filesystem::create_directories(root + dir);
            /* The project's name, from assets/UE4CommandLine.txt ("../../../Wrath2/Wrath2.uproject"). */
            std::string project;
            if (FILE* line = std::fopen((root + "/apk/assets/UE4CommandLine.txt").c_str(), "rb")) {
                char text[512] = {};
                std::fread(text, 1, sizeof(text) - 1, line);
                std::fclose(line);
                std::string all = text;
                size_t end = all.find(".uproject"), start = all.rfind('/', end);
                if (end != std::string::npos && start != std::string::npos) project = all.substr(start + 1, end - start - 1);
            }
            uint64_t env = g_libc.env_va;
            uint64_t thiz = g_libc.handle_for("object", "activity");
            auto str = [&](const std::string& value) { /* a Java String: the handle and its text */
                uint64_t handle = g_libc.handle_for("string", value);
                g_libc.strings_by_handle[handle] = value;
                return handle;
            };
            auto call = [&](const char* name, std::vector<uint64_t> args) {
                uint64_t fn = g_image.linker.lookup(unreal_activity + name);
                if (!fn) {
                    std::printf("test: no GameActivity.%s\n", name);
                    return;
                }
                args.insert(args.begin(), {env, thiz});
                g_libc.call_guest_args(fn, args);
                std::printf("test: GameActivity.%s\n", name);
                std::fflush(stdout);
            };
            call("nativeSetAndroidStartupState", {0});
            call("nativeSetGlobalActivity", {0, 0, str("/data/data/" + package + "/files"),
                                             str("/sdcard/Android/data/" + package + "/files"), 0,
                                             str("/data/app/" + package + "/base.apk")});
            call("nativeSetAndroidVersionInformation", {str("14"), 34, str("Oculus"), str("Quest 3"),
                                                        str("UP1A.231005.007.A1"), str("en")});
            if (!QB_ENV("QB_UE_OBB_SCAN")) call("nativeSetObbFilePaths", {str(main_obb), str(patch_obb), str(""), str("")});
            /* main.<version>.<package>.obb and patch.<version>.<package>.obb name the versions. */
            auto version_of = [](const std::string& path) {
                size_t slash = path.rfind('/'), dot = path.find('.', slash + 1);
                return dot == std::string::npos ? 0 : std::atoi(path.c_str() + dot + 1);
            };
            /* QB_UE_OBB_SCAN: a version with no main OBB of that name, so the
               engine mounts every .obb in the folder instead (its visitor). */
            call("nativeSetObbInfo", {str(project), str(package), QB_ENV("QB_UE_OBB_SCAN") ? 1ull : (uint64_t)version_of(main_obb),
                                      (uint64_t)version_of(patch_obb), str("")});
            call("nativeSetWindowInfo", {0, 0});
            call("nativeSetSurfaceViewInfo", {2064, 2208});
            call("nativeSetAffinityInfo", {0, 0, 0});
            std::printf("test: OBBs %s | %s\n", main_obb.c_str(), patch_obb.c_str());
            /* QB_UE_GLOBALS: the engine's own view of what was just set:
               FStrings (data, count, capacity) of UTF-16 or UTF-32. */
            if (QB_ENV("QB_UE_GLOBALS"))
                for (const char* global : {"GPackageName", "GFilePathBase", "GInternalFilePath", "GExternalFilePath",
                                           "GOBBMainFilePath", "GAPKFilename", "GAndroidPackageVersion"}) {
                    uint64_t at = g_image.linker.lookup(global);
                    if (!at) {
                        std::printf("ue: %s not exported\n", global);
                        continue;
                    }
                    uint64_t data = 0;
                    int32_t count = 0;
                    std::memcpy(&data, reinterpret_cast<void*>(at), 8);
                    std::memcpy(&count, reinterpret_cast<void*>(at + 8), 4);
                    std::string text;
                    if (data && count > 0 && count < 512) {
                        const uint16_t* w16 = reinterpret_cast<const uint16_t*>(data);
                        for (int i = 0; i < count - 1; ++i) text += (char)(w16[i] < 128 ? w16[i] : '?');
                    }
                    std::printf("ue: %s at %llx: data %llx count %d \"%s\" (as int %lld)\n", global,
                                (unsigned long long)at, (unsigned long long)data, count, text.c_str(),
                                (long long)data);
                }
        }
        g_libc.call_guest_three(entry, activity, 0, 0);
        std::printf("test: ANativeActivity_onCreate returned\n");
        std::fflush(stdout);
        /* ...then, once onCreate has started its thread, the Java side's last word. */
        if (unreal) {
            uint64_t fn = g_image.linker.lookup(unreal_activity + "nativeResumeMainInit");
            g_libc.call_guest_args(fn, {g_libc.env_va, g_libc.handle_for("object", "activity")});
            std::printf("test: GameActivity.nativeResumeMainInit\n");
            std::fflush(stdout);
        }
        /* Then the lifecycle, in the order Android sends it. */
        const int kStart = 0, kResume = 1, kWindowFocus = 6, kWindowCreated = 7;
        g_libc.lifecycle(kStart, 0);
        g_libc.lifecycle(kResume, 0);
        g_libc.lifecycle(kWindowCreated, 0x5730000);
        g_libc.lifecycle(kWindowFocus, 1);
        /* Give the application's thread time to work through them; a game
           keeps running for QB_RUN_SECONDS (its own thread does the work). */
        int seconds = QB_ENV("QB_RUN_SECONDS") ? std::atoi(QB_ENV("QB_RUN_SECONDS")) : 3;
        if (stop_event) {
            WaitForSingleObject(stop_event, seconds > 0 ? (DWORD)seconds * 1000 : INFINITE);
            g_libc.lifecycle(6, 0); // focus lost
            g_libc.lifecycle(3, 0); // pause: let the engine write saves
            g_libc.lifecycle(4, 0); // stop
            g_libc.lifecycle(5, 0); // destroy
            std::printf("bridge: native lifecycle shutdown completed; save durability is unverified\n");
        } else std::this_thread::sleep_for(std::chrono::seconds(seconds));
    } else if (argc > 2 && !as_activity) {
        /* JNI_OnLoad takes the virtual machine and a reserved argument. */
        std::printf("test: calling %s\n", entry);
        std::fflush(stdout);
        uint64_t result = g_libc.call_guest(g_image.cpu.pc, g_libc.vm_va, 0);
        std::printf("test: %s returned %llx\n", entry, (unsigned long long)result);
        /* Drive the engine the way UnityPlayer's Java side would: set up,
           hand it a surface, resume, take focus, then render frames. */
        if (QB_ENV("QB_DRIVE")) {
            auto native = [&](const char* name) -> uint64_t {
                /* Newer Unity (2023 on) keeps most of them on
                   UnityPlayerForActivityOrService, UnityPlayer's delegate. */
                for (const char* owner : {"com/unity3d/player/UnityPlayer.",
                                          "com/unity3d/player/UnityPlayerForActivityOrService."}) {
                    auto found = g_libc.natives.find(std::string(owner) + name);
                    if (found != g_libc.natives.end()) return found->second;
                }
                return 0;
            };
            uint64_t env = g_libc.env_va;
            uint64_t player = g_libc.handle_for("object", "UnityPlayer");
            uint64_t context = g_libc.handle_for("object", "activity");
            uint64_t surface = g_libc.handle_for("object", "Surface");
            struct Step {
                const char* name;
                std::vector<uint64_t> extra;
            } steps[] = {
                {"initJni", {context}},
                {"nativeRecreateGfxState", {0, surface}},
                {"nativeSendSurfaceChangedEvent", {}}, /* surfaceChanged follows surfaceCreated */
                {"nativeResume", {}},
                {"nativeFocusChanged", {1}},
            };
            for (const Step& step : steps) {
                uint64_t function = native(step.name);
                if (!function) {
                    std::printf("drive: %s was never registered\n", step.name);
                    continue;
                }
                std::vector<uint64_t> args = {env, player};
                for (uint64_t extra : step.extra) args.push_back(extra);
                std::printf("drive: %s\n", step.name);
                std::fflush(stdout);
                uint64_t got = g_libc.call_guest_args(function, args);
                std::printf("drive: %s gave %llx\n", step.name, (unsigned long long)got);
                std::fflush(stdout);
            }
            int frames = std::atoi(QB_ENV("QB_DRIVE"));
            uint64_t render = native("nativeRender");
            if (!render) { std::fprintf(stderr, "bridge: Unity nativeRender was not registered\n"); return 1; }
            double window_ms = 0;
            int window_frames = 0;
            auto report_at = std::chrono::steady_clock::now();
            for (int i = 0; i < frames && render; ++i) {
                if (stop_event && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) break;
                auto frame_start = std::chrono::steady_clock::now();
                uint64_t got = g_libc.call_guest_args(render, {env, player});
                if (!got) {
                    std::printf("bridge: Unity nativeRender requested application exit\n");
                    break;
                }
                window_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
                ++window_frames;
                if (QB_ENV("QB_TRACE")) std::printf("drive: frame %d rendered, returned %llx\n", i, (unsigned long long)got);
                /* Time-bounded reporting also keeps an idle engine from flooding diagnostics. */
                if (std::chrono::steady_clock::now() - report_at >= std::chrono::seconds(1)) {
                    std::printf("drive: nativeRender takes %.1f ms on average\n", window_ms / window_frames);
                    window_ms = 0;
                    window_frames = 0;
                    report_at = std::chrono::steady_clock::now();
                }
                std::fflush(stdout);
            }
            if (stop_event) {
                if (uint64_t focus = native("nativeFocusChanged")) g_libc.call_guest_args(focus, {env, player, 0});
                if (uint64_t pause = native("nativePause")) g_libc.call_guest_args(pause, {env, player});
                if (uint64_t done = native("nativeDone")) g_libc.call_guest_args(done, {env, player});
                std::printf("bridge: Unity lifecycle shutdown completed; save durability is unverified\n");
            }
        }
    } else {
        ok = guest_run(g_image.cpu, g_image.mem, thunk, nullptr, 0);
    }
    if (!ok) {
        std::printf("test: the guest stopped early, so the cases after that point did not run\n");
        return 1;
    }
    if (QB_ENV("QB_TRACE")) {
        std::printf("test: %d native methods registered\n", g_libc.registered_natives);
        for (const auto& entry : g_libc.handle_names) std::printf("test:   java %s\n", entry.second.c_str());
    }
    std::printf("test: %d resolver calls, %d live allocations\n", g_libc.tls_resolves, g_libc.allocations);
    std::printf("test: %d passed, %d failed\n", g_passed, g_failed);
    {
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        uint64_t steps = g_guest_steps.load();
        std::printf("test: %llu instructions in %.2fs, %.1f million a second\n", (unsigned long long)steps, seconds,
                    steps / seconds / 1e6);
        std::fflush(stdout);
        guest_jit_print_stats();
    }
    /* QB_PEEK=object: dumps 0x40 bytes of the Oculus plugin's OculusSystem
       singleton (its settings), for checking what C# configured. */
    if (QB_ENV("QB_PEEK")) {
        for (GuestObject* object : g_image.linker.objects) {
            if (object->name != "libOculusXRPlugin.so") continue;
            uint64_t system = 0;
            std::memcpy(&system, reinterpret_cast<void*>(object->base + 0x53518), 8);
            std::printf("peek: OculusSystem at %llx\n", (unsigned long long)system);
            for (int at = 0; system && at < 0x40; at += 16) {
                uint8_t bytes[16];
                std::memcpy(bytes, reinterpret_cast<void*>(system + at), 16);
                std::printf("peek: +%02x", at);
                for (int b = 0; b < 16; ++b) std::printf(" %02x", bytes[b]);
                std::printf("\n");
            }
        }
    }
    /* QB_FINDPTR=hex: every place in a loaded library's image holding that
       8-byte value, which is how a function kept in a data table (a vtable,
       a callback registration) is traced back to the table. */
    if (const char* wanted_text = QB_ENV("QB_FINDPTR")) {
        uint64_t wanted = std::strtoull(wanted_text, nullptr, 16);
        for (GuestObject* object : g_image.linker.objects) {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(object->base);
            for (size_t at = 0; at + 8 <= object->image.size(); at += 8) {
                uint64_t word = 0;
                std::memcpy(&word, data + at, 8);
                if (word != wanted) continue;
                std::printf("findptr: %s+%zx\n", object->name.c_str(), at);
                /* What sits around it, described, for recognising the table. */
                for (size_t near_at = at >= 0x40 ? at - 0x40 : 0; near_at < at + 0x48 && near_at + 8 <= object->image.size();
                     near_at += 8) {
                    uint64_t near_word = 0;
                    std::memcpy(&near_word, data + near_at, 8);
                    std::printf("findptr:   +%zx %016llx %s\n", near_at, (unsigned long long)near_word,
                                guest_describe(near_word).c_str());
                }
            }
        }
    }
    /* QB_BASES: where each library was loaded, for turning a library offset
       into the absolute pc that QB_BREAK and friends take. */
    if (QB_ENV("QB_BASES"))
        for (GuestObject* object : g_image.linker.objects)
            std::printf("base: %s %llx\n", object->name.c_str(), (unsigned long long)object->base);
    /* QB_PEEKPTR=hex: reads the pointer stored at that guest address and
       dumps 0x300 bytes of what it points at. */
    if (const char* at = QB_ENV("QB_PEEKPTR")) {
        uint64_t slot = std::strtoull(at, nullptr, 16), object = 0;
        std::memcpy(&object, reinterpret_cast<void*>(slot), 8);
        std::printf("peekptr: [%llx] = %llx\n", (unsigned long long)slot, (unsigned long long)object);
        for (int off = 0; object && off < 0x300; off += 16) {
            uint64_t words[2];
            std::memcpy(words, reinterpret_cast<void*>(object + off), 16);
            std::printf("peekptr: +%03x %016llx %016llx\n", off,
                        (unsigned long long)words[0], (unsigned long long)words[1]);
        }
    }
    /* Guest threads may still be running, on memory that static destructors
       would free underneath them, so leave without running any. */
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(g_failed == 0 ? 0 : 1);
}
