/* Standing in for the parts of bionic a guest calls.

   The heap lives in guest address space so a pointer the guest is handed can
   be dereferenced by guest code. Everything else works on translated pointers
   and is implemented here rather than interpreted. */

#include "qb_env.h"
#include "android.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#pragma comment(lib, "Synchronization.lib")

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace {

/* Every block carries its size and whether it is in use, so free can find its
   neighbour and join up with it. */
struct Block {
    uint64_t size; /* payload bytes, not counting this header */
    uint64_t used;
};

const uint64_t kHeapBase = 0x50000000ull;
const uint64_t kAlign = 16;

uint64_t round_up(uint64_t value) { return (value + kAlign - 1) & ~(kAlign - 1); }

}  // namespace

/* Variables bionic exports, which code reads rather than calls: stdin,
   stdout and stderr are FILE pointers. Guest memory is the host's, so these
   live here; each FILE is a block whose first word is its descriptor, as the
   guest's own fopen'd streams are, and start() ties the three to the host's. */
namespace {
struct LibcData {
    alignas(16) uint64_t file_blocks[3][32] = {{0}, {1}, {2}};
    uint64_t stdin_cell = 0, stdout_cell = 0, stderr_cell = 0;
    /* <time.h>'s globals, for UTC: tzname[2], timezone, daylight. */
    const char* tzname[2] = {"UTC", "UTC"};
    int64_t timezone_seconds = 0;
    int32_t daylight = 0;
    uint64_t stack_chk_guard = 0x5a17c0de5a17c0deull;
    LibcData() {
        stdin_cell = (uint64_t)(uintptr_t)file_blocks[0];
        stdout_cell = (uint64_t)(uintptr_t)file_blocks[1];
        stderr_cell = (uint64_t)(uintptr_t)file_blocks[2];
    }
};
LibcData g_libc_data;
}  // namespace

/* A copy that reports an unreadable or unwritable page instead of dying, for
   process_vm_readv, which code uses exactly as a memcpy that may fail. */
static bool guarded_copy(void* to, const void* from, size_t bytes) {
    __try {
        std::memcpy(to, from, bytes);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                    : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool is_libc_name(const std::string& name);

/* One wake counter per hashed futex address (see the futex syscall): a wake
   can land while a waiter is between WaitOnAddress slices, and Linux still
   counts that waiter as woken. Sharing a counter only causes spurious
   wakeups, which every futex caller already allows for. */
static std::atomic<uint64_t>& futex_generation(volatile uint32_t* word) {
    static std::atomic<uint64_t> table[4096];
    return table[(reinterpret_cast<uintptr_t>(word) >> 2) & 4095];
}

uint64_t guest_libc_data(const std::string& name) {
    if (name == "stdin") return (uint64_t)(uintptr_t)&g_libc_data.stdin_cell;
    if (name == "stdout") return (uint64_t)(uintptr_t)&g_libc_data.stdout_cell;
    if (name == "stderr") return (uint64_t)(uintptr_t)&g_libc_data.stderr_cell;
    if (name == "__stack_chk_guard") return (uint64_t)(uintptr_t)&g_libc_data.stack_chk_guard;
    if (name == "tzname") return (uint64_t)(uintptr_t)&g_libc_data.tzname[0];
    if (name.compare(0, 7, "SL_IID_") == 0) {
        uint64_t guest_opensl_data(const std::string& name);
        return guest_opensl_data(name);
    }
    if (name == "timezone") return (uint64_t)(uintptr_t)&g_libc_data.timezone_seconds;
    if (name == "daylight") return (uint64_t)(uintptr_t)&g_libc_data.daylight;
    return 0;
}

void GuestLibc::start(GuestImage* loaded, uint64_t heap_bytes) {
    image = loaded;
    install_signal_hook();
    image->mem.map(reinterpret_cast<uint8_t*>(&g_libc_data), (uint64_t)(uintptr_t)&g_libc_data, sizeof(g_libc_data));
    for (int fd = 0; fd < 3; ++fd) {
        FILE* host = fd == 0 ? stdin : fd == 1 ? stdout : stderr;
        if (!files.count(fd)) files[fd] = host;
        shared_files.insert(fd);
        streams[(uint64_t)(uintptr_t)g_libc_data.file_blocks[fd]] = fd;
    }
    host_heap = HeapCreate(0, 0, 0);
    /* Space for the stacks and thread local blocks of any threads the guest
       starts. It is mapped once, here, so the region list never changes while
       a thread is reading it. */
    thread_area.at = 0x70000000ull;
    thread_area.assign((size_t)(kStackBase - 0x70000000ull), 0); /* up to the main stack */
    thread_area_va = 0x70000000ull;
    image->mem.map(thread_area.data(), thread_area_va, thread_area.size());
    map_va = guest_reserve_near(map_va, map_size, false, 0x1000000000ull);
    map_area = reinterpret_cast<uint8_t*>(map_va);
    if (map_area) image->mem.map(map_area, map_va, map_size);
    /* Only now that the heap can hand out memory, because the tables live in
       guest address space. */
    start_java();
}

uint64_t GuestLibc::call_guest(uint64_t function, uint64_t a0, uint64_t a1) {
    /* Called from inside guest code (a dlopen running initialisers and
       JNI_OnLoad, say), the callee has to run below the frames already on
       this thread's stack. Starting from image->cpu, whose stack pointer is
       the top of the main stack, overwrote the caller's saved registers. */
    if (t_guest_cpu) {
        GuestCpu current = *t_guest_cpu;
        return call_guest_on(current, function, a0, a1);
    }
    /* The interpreter is re-entered here, so the registers of the call that
       is already in progress have to be put back afterwards. */
    GuestCpu saved = image->cpu;
    image->cpu.x[0] = a0;
    image->cpu.x[1] = a1;
    image->cpu.x[30] = 1; /* the sentinel guest_run stops on */
    image->cpu.pc = function;
    uint64_t result = 0;
    if (guest_run(image->cpu, image->mem, image->callback, image->callback_user, 0)) result = image->cpu.x[0];
    image->cpu = saved;
    return result;
}

uint64_t GuestLibc::call_guest_on(GuestCpu& from, uint64_t function, uint64_t a0, uint64_t a1, uint64_t a2) {
    GuestCpu local = from;
    local.x[0] = a0;
    local.x[1] = a1;
    local.x[2] = a2;
    local.x[30] = 1; /* the sentinel guest_run stops on */
    local.pc = function;
    if (!guest_run(local, image->mem, image->callback, image->callback_user, 0)) return 0;
    return local.x[0];
}

uint64_t GuestLibc::call_guest_args(uint64_t function, const std::vector<uint64_t>& args) {
    GuestCpu local = t_guest_cpu ? *t_guest_cpu : image->cpu; /* below any frames already live */
    for (size_t i = 0; i < args.size() && i < 8; ++i) local.x[i] = args[i];
    local.x[30] = 1;
    local.pc = function;
    if (!guest_run(local, image->mem, image->callback, image->callback_user, 0)) {
        std::printf("drive: the call to %llx stopped\n", (unsigned long long)function);
        return 0;
    }
    return local.x[0];
}

uint64_t GuestLibc::call_guest_three(uint64_t function, uint64_t a0, uint64_t a1, uint64_t a2) {
    GuestCpu saved = image->cpu;
    image->cpu.x[0] = a0;
    image->cpu.x[1] = a1;
    image->cpu.x[2] = a2;
    image->cpu.x[30] = 1;
    image->cpu.pc = function;
    uint64_t result = 0;
    if (guest_run(image->cpu, image->mem, image->callback, image->callback_user, 0)) result = image->cpu.x[0];
    image->cpu = saved;
    return result;
}

void GuestLibc::run_initialisers() {
    for (uint64_t entry : image->initialisers) call_guest(entry, 0, 0);
}

/* Printf formatting where the arguments are the guest's registers: integers
   in x, floating point in v, and a string is a pointer into guest memory. The
   host's own vsnprintf cannot be handed these, so the format is walked. */
std::string GuestLibc::format_from_guest(const char* format, GuestCpu& cpu, int next_int, int next_float) {
    std::string result;
    if (!format) return result;
    /* Only the first eight of each kind travel in registers. Everything after
       that was pushed, so it is read from the stack instead. A long log line
       reads nothing but rubbish without this. */
    uint64_t stacked = 0;
    auto next_integer = [&]() -> uint64_t {
        if (next_int < 8) return cpu.x[next_int++];
        uint64_t value = 0;
        uint8_t* p = guest_ptr(image->mem, cpu.sp + stacked * 8, 8);
        if (p) std::memcpy(&value, p, 8);
        ++stacked;
        ++next_int;
        return value;
    };
    auto next_double = [&]() -> double {
        double value = 0;
        if (next_float < 8) {
            std::memcpy(&value, &cpu.q[next_float++].lo, 8);
            return value;
        }
        uint8_t* p = guest_ptr(image->mem, cpu.sp + stacked * 8, 8);
        if (p) std::memcpy(&value, p, 8);
        ++stacked;
        ++next_float;
        return value;
    };
    for (const char* at = format; *at; ++at) {
        if (*at != '%') {
            result += *at;
            continue;
        }
        std::string spec = "%";
        ++at;
        while (*at && !std::strchr("diouxXeEfgGcspn%", *at)) spec += *at++;
        if (!*at) break;
        char kind = *at;
        spec += kind;
        char piece[512];
        if (kind == '%') {
            result += '%';
        } else if (kind == 'f' || kind == 'e' || kind == 'E' || kind == 'g' || kind == 'G') {
            std::snprintf(piece, sizeof(piece), spec.c_str(), next_double());
            result += piece;
        } else if (kind == 's') {
            const char* s = reinterpret_cast<const char*>(guest_ptr(image->mem, next_integer(), 1));
            std::snprintf(piece, sizeof(piece), spec.c_str(), s ? s : "(null)");
            result += piece;
        } else if (kind == 'c') {
            std::snprintf(piece, sizeof(piece), spec.c_str(), (int)next_integer());
            result += piece;
        } else if (kind == 'p') {
            std::snprintf(piece, sizeof(piece), "0x%llx", (unsigned long long)next_integer());
            result += piece;
        } else {
            /* Every integer is printed through MSVC's %ll, so bionic's length
               modifiers (l, ll, z, j, t, q; h and hh narrow) all come out
               first: MSVC takes some of them as an invalid parameter and ends
               the process. %n is refused by MSVC outright; it is written
               here instead. */
            bool wide = spec.find_first_of("ljztq") != std::string::npos;
            bool half = spec.find('h') != std::string::npos;
            bool byte = spec.find("hh") != std::string::npos;
            uint64_t value = next_integer();
            if (kind == 'n') {
                int32_t so_far = (int32_t)result.size();
                if (uint8_t* where = guest_ptr(image->mem, value, 4)) std::memcpy(where, &so_far, 4);
                continue;
            }
            std::string fixed;
            for (char c : spec)
                if (!std::strchr("hljztqL", c)) fixed += c;
            fixed.insert(fixed.size() - 1, "ll");
            long long number = wide ? (long long)value : (long long)(int32_t)value;
            bool is_signed = kind == 'd' || kind == 'i';
            if (byte) number = is_signed ? (long long)(int8_t)value : (long long)(uint8_t)value;
            else if (half) number = is_signed ? (long long)(int16_t)value : (long long)(uint16_t)value;
            else if (!wide && !is_signed) number = (long long)(uint32_t)value;
            std::snprintf(piece, sizeof(piece), fixed.c_str(), number);
            result += piece;
        }
    }
    return result;
}

/* A va_list on this architecture is a small structure describing where the
   arguments went: a pointer past the saved integer registers, another past
   the saved vector ones, the offsets reached so far, and the stack for
   anything that did not fit. Walking it is how the v-forms of printf read
   their arguments. */
std::string GuestLibc::format_from_valist(const char* format, uint64_t list_va) {
    std::string result;
    if (!format) return result;
    uint8_t* list = guest_ptr(image->mem, list_va, 32);
    if (!list) return result;
    uint64_t stack = 0, gr_top = 0, vr_top = 0;
    int32_t gr_offs = 0, vr_offs = 0;
    std::memcpy(&stack, list, 8);
    std::memcpy(&gr_top, list + 8, 8);
    std::memcpy(&vr_top, list + 16, 8);
    std::memcpy(&gr_offs, list + 24, 4);
    std::memcpy(&vr_offs, list + 28, 4);

    auto next_integer = [&]() -> uint64_t {
        uint64_t value = 0;
        uint8_t* from = nullptr;
        if (gr_offs < 0) {
            from = guest_ptr(image->mem, gr_top + (int64_t)gr_offs, 8);
            gr_offs += 8;
        } else {
            from = guest_ptr(image->mem, stack, 8);
            stack += 8;
        }
        if (from) std::memcpy(&value, from, 8);
        return value;
    };
    auto next_double = [&]() -> double {
        double value = 0;
        uint8_t* from = nullptr;
        if (vr_offs < 0) {
            from = guest_ptr(image->mem, vr_top + (int64_t)vr_offs, 8);
            vr_offs += 16;
        } else {
            from = guest_ptr(image->mem, stack, 8);
            stack += 8;
        }
        if (from) std::memcpy(&value, from, 8);
        return value;
    };

    for (const char* at = format; *at; ++at) {
        if (*at != '%') {
            result += *at;
            continue;
        }
        std::string spec = "%";
        ++at;
        while (*at && !std::strchr("diouxXeEfgGcspn%", *at)) spec += *at++;
        if (!*at) break;
        char kind = *at;
        spec += kind;
        char piece[512];
        if (kind == '%') {
            result += '%';
        } else if (kind == 'f' || kind == 'e' || kind == 'E' || kind == 'g' || kind == 'G') {
            std::snprintf(piece, sizeof(piece), spec.c_str(), next_double());
            result += piece;
        } else if (kind == 's') {
            const char* text = reinterpret_cast<const char*>(guest_ptr(image->mem, next_integer(), 1));
            std::snprintf(piece, sizeof(piece), spec.c_str(), text ? text : "(null)");
            result += piece;
        } else if (kind == 'c') {
            std::snprintf(piece, sizeof(piece), spec.c_str(), (int)next_integer());
            result += piece;
        } else if (kind == 'p') {
            std::snprintf(piece, sizeof(piece), "0x%llx", (unsigned long long)next_integer());
            result += piece;
        } else {
            /* Every integer is printed through MSVC's %ll, so bionic's length
               modifiers (l, ll, z, j, t, q; h and hh narrow) all come out
               first: MSVC takes some of them as an invalid parameter and ends
               the process. %n is refused by MSVC outright; it is written
               here instead. */
            bool wide = spec.find_first_of("ljztq") != std::string::npos;
            bool half = spec.find('h') != std::string::npos;
            bool byte = spec.find("hh") != std::string::npos;
            uint64_t value = next_integer();
            if (kind == 'n') {
                int32_t so_far = (int32_t)result.size();
                if (uint8_t* where = guest_ptr(image->mem, value, 4)) std::memcpy(where, &so_far, 4);
                continue;
            }
            std::string fixed;
            for (char c : spec)
                if (!std::strchr("hljztqL", c)) fixed += c;
            fixed.insert(fixed.size() - 1, "ll");
            long long number = wide ? (long long)value : (long long)(int32_t)value;
            bool is_signed = kind == 'd' || kind == 'i';
            if (byte) number = is_signed ? (long long)(int8_t)value : (long long)(uint8_t)value;
            else if (half) number = is_signed ? (long long)(int16_t)value : (long long)(uint16_t)value;
            else if (!wide && !is_signed) number = (long long)(uint32_t)value;
            std::snprintf(piece, sizeof(piece), fixed.c_str(), number);
            result += piece;
        }
    }
    return result;
}

bool GuestLibc::call(const std::string& original, GuestCpu& cpu) {
    /* The fortified forms take an extra size argument and then do the same
       work, so they are answered by the plain ones. */
    static const struct {
        const char* fortified;
        const char* plain;
    } hardened[] = {
        {"__memcpy_chk", "memcpy"},   {"__memmove_chk", "memmove"}, {"__memset_chk", "memset"},
        {"__strcpy_chk", "strcpy"},   {"__strcat_chk", "strcat"},   {"__strlen_chk", "strlen"},
        {"__write_chk", "write"},     {"__read_chk", "read"},       {"__fread_chk", "fread"},
        {"__strncpy_chk", "strncpy"}, {"__strchr_chk", "strchr"},
    };
    std::string name = original;
    for (const auto& entry : hardened)
        if (name == entry.fortified) name = entry.plain;

    if ((name.compare(0, 7, "pthread") == 0 || name.compare(0, 4, "sem_") == 0 || name == "sched_yield") &&
        thread_call(name, cpu))
        return true;
    if (name.compare(0, 3, "egl") == 0) return egl_call(name, cpu);
    if (name.compare(0, 2, "gl") == 0 && egl_call(name, cpu)) return true;
    if (name.compare(0, 2, "vk") == 0) return vulkan_call(name, cpu);
    if (name.compare(0, 2, "xr") == 0) return openxr_call(name, cpu);
    if (name.compare(0, 4, "ovr_") == 0) return platform_call(name, cpu);
    if (name.compare(0, 4, "jni.") == 0 || name.compare(0, 4, "jvm.") == 0) return java_call(name, cpu);
    if (net_call(name, cpu)) return true;
    if (file_call(name, cpu)) return true;
    if (name.compare(0, 1, "A") == 0 || name == "pipe" || name == "pipe2" || name == "read" ||
        name == "write" || name == "close" || name.compare(0, 13, "__android_log") == 0) {
        if (activity_call(name, cpu)) return true;
    }
    /* How a test, and later the activity, gets hold of the virtual machine. */
    /* A signal handler delivered in place returned: the registers it
       interrupted come back from its frame's mcontext. The run loop then
       continues at x30, so x30 carries the pc; a handler that returns from
       SIGILL without moving the pc would only fault again anyway. */
    /* Android's libz.so (zlib.cpp). */
    static const char* const zlib_names[] = {"inflate", "inflateInit_", "inflateInit2_", "inflateEnd", "inflateReset",
                                             "inflateReset2", "deflate", "deflateInit_", "deflateInit2_", "deflateEnd",
                                             "deflateReset", "deflateBound", "compress", "compress2", "compressBound",
                                             "uncompress", "uncompress2", "crc32", "crc32_z", "adler32", "adler32_z",
                                             "zlibVersion"};
    for (const char* one : zlib_names)
        if (name == one) return zlib_call(name, cpu);
    /* Android's libOpenSLES.so (opensl.cpp). */
    if (name == "slCreateEngine" || name.compare(0, 6, "qb_sl_") == 0) return opensl_call(name, cpu);
    if (name == "qb_sigreturn") {
        const uint8_t* mc = guest_ptr(image->mem, cpu.sp + 128 + 176, 8 + 34 * 8);
        if (mc) {
            uint64_t pc = 0;
            std::memcpy(cpu.x, mc + 8, 31 * 8);
            std::memcpy(&cpu.sp, mc + 8 + 31 * 8, 8);
            std::memcpy(&pc, mc + 8 + 32 * 8, 8);
            cpu.x[30] = pc;
        }
        return true;
    }
    if (name == "qb_java_vm") {
        cpu.x[0] = vm_va;
        return true;
    }
    /* Allocation can now happen on more than one thread. */
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ptr = [&](uint64_t va, uint64_t bytes) { return guest_ptr(mem, va, bytes); };
    auto text = [&](uint64_t va) -> char* { return reinterpret_cast<char*>(guest_ptr(mem, va, 1)); };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };

    /* The heap. Guest memory is identity mapped, so any host address is a
       guest address, and the guest's heap can simply be a host heap: a private
       Windows heap, which is thread safe and fast, where the walk over a
       fixed arena it replaces was neither. Every block carries a small header
       just below what the guest sees: the size asked for, and where the host
       block really starts, which alignment may have moved it away from. */
    auto allocate = [&](uint64_t size, uint64_t align, bool zero) -> uint64_t {
        if (align < 16) align = 16;
        if (align & (align - 1)) return 0; /* not a power of two */
        size_t total = (size_t)size + (size_t)align + 16;
        uint8_t* raw = static_cast<uint8_t*>(HeapAlloc(host_heap, zero ? HEAP_ZERO_MEMORY : 0, total));
        if (!raw) return 0;
        uint64_t start = ((uint64_t)raw + 16 + align - 1) & ~(align - 1);
        reinterpret_cast<uint64_t*>(start)[-1] = (uint64_t)raw;
        reinterpret_cast<uint64_t*>(start)[-2] = size;
        ++allocations;
        return start;
    };
    auto release = [&](uint64_t at) {
        if (!at) return;
        HeapFree(host_heap, 0, reinterpret_cast<void*>(reinterpret_cast<uint64_t*>(at)[-1]));
        --allocations;
    };
    auto size_of = [&](uint64_t at) -> uint64_t { return at ? reinterpret_cast<uint64_t*>(at)[-2] : 0; };

    if (name == "malloc" || name == "calloc" || name == "memalign" || name == "aligned_alloc" ||
        name == "posix_memalign" || name == "valloc" || name == "pvalloc") {
        uint64_t size = 0, align = 16;
        if (name == "malloc") size = arg(0);
        else if (name == "calloc") size = arg(0) * arg(1);
        else if (name == "memalign" || name == "aligned_alloc") align = arg(0), size = arg(1);
        else if (name == "posix_memalign") align = arg(1), size = arg(2);
        else align = 4096, size = arg(0);
        uint64_t found = allocate(size ? size : 1, align, name == "calloc");
        if (QB_ENV("QB_TRACE"))
            std::printf("heap: %s(%llu) -> %llx\n", name.c_str(), (unsigned long long)size, (unsigned long long)found);
        if (name == "posix_memalign") {
            if (found) {
                uint8_t* out = ptr(arg(0), 8);
                if (out) std::memcpy(out, &found, 8);
            }
            ret(found ? 0 : (align & (align - 1)) ? 22 /* EINVAL */ : 12 /* ENOMEM */);
        } else {
            ret(found);
        }
        return true;
    }
    if (name == "free") {
        release(arg(0));
        return true;
    }
    if (name == "malloc_usable_size") {
        ret(size_of(arg(0)));
        return true;
    }
    if (name == "realloc" || name == "reallocarray") {
        uint64_t old = arg(0);
        uint64_t want = name == "reallocarray" ? arg(1) * arg(2) : arg(1);
        if (old && !want) {
            release(old);
            ret(0);
            return true;
        }
        uint64_t fresh = allocate(want, 16, false);
        if (fresh && old) {
            std::memcpy(reinterpret_cast<void*>(fresh), reinterpret_cast<void*>(old),
                        (size_t)std::min(size_of(old), want));
            release(old);
        }
        ret(fresh);
        return true;
    }

    /* Strings and memory. The compiler inlines most of these, but a call that
       survives has to land somewhere. */
    if (name == "strlen") {
        const char* s = text(arg(0));
        ret(s ? std::strlen(s) : 0);
        return true;
    }
    if (name == "strcmp" || name == "strncmp") {
        const char* a = text(arg(0));
        const char* b = text(arg(1));
        int result = !a || !b ? 0 : name == "strcmp" ? std::strcmp(a, b) : std::strncmp(a, b, (size_t)arg(2));
        ret((uint64_t)(int64_t)result);
        return true;
    }
    if (name == "strcpy" || name == "strcat") {
        char* to = text(arg(0));
        const char* from = text(arg(1));
        if (to && from) {
            if (name == "strcpy") std::strcpy(to, from);
            else std::strcat(to, from);
        }
        ret(arg(0));
        return true;
    }
    if (name == "strchr" || name == "strrchr") {
        char* s = text(arg(0));
        if (!s) {
            ret(0);
            return true;
        }
        char* hit = name == "strchr" ? std::strchr(s, (int)arg(1)) : std::strrchr(s, (int)arg(1));
        ret(hit ? arg(0) + (uint64_t)(hit - s) : 0);
        return true;
    }
    if (name == "memcpy" || name == "memmove") {
        uint8_t* to = ptr(arg(0), arg(2));
        uint8_t* from = ptr(arg(1), arg(2));
        if (to && from) std::memmove(to, from, (size_t)arg(2));
        ret(arg(0));
        return true;
    }
    if (name == "memset") {
        uint8_t* to = ptr(arg(0), arg(2));
        if (to) std::memset(to, (int)arg(1), (size_t)arg(2));
        ret(arg(0));
        return true;
    }
    if (name == "memcmp") {
        uint8_t* a = ptr(arg(0), arg(2));
        uint8_t* b = ptr(arg(1), arg(2));
        ret((uint64_t)(int64_t)(a && b ? std::memcmp(a, b, (size_t)arg(2)) : 0));
        return true;
    }

    /* Numbers out of text */
    if (name == "strtol" || name == "strtoll" || name == "atoi" || name == "atol") {
        const char* s = text(arg(0));
        char* end = nullptr;
        long long value = s ? std::strtoll(s, &end, name[0] == 'a' ? 10 : (int)arg(2)) : 0;
        if ((name == "strtol" || name == "strtoll") && arg(1) && s) {
            uint64_t at = arg(0) + (uint64_t)(end - s);
            uint8_t* out = ptr(arg(1), 8);
            if (out) std::memcpy(out, &at, 8);
        }
        ret((uint64_t)value);
        return true;
    }
    if (name == "strtoul" || name == "strtoull" || name == "strtoumax" || name == "strtoimax") {
        const char* s = text(arg(0));
        char* end = nullptr;
        uint64_t value = s ? (name == "strtoimax" ? (uint64_t)std::strtoll(s, &end, (int)arg(2))
                                                   : std::strtoull(s, &end, (int)arg(2)))
                           : 0;
        if (arg(1) && s) {
            uint64_t at = arg(0) + (uint64_t)(end - s);
            uint8_t* out = ptr(arg(1), 8);
            if (out) std::memcpy(out, &at, 8);
        }
        ret(value);
        return true;
    }
    /* Character classes, in the C locale bionic always has. */
    {
        static const struct {
            const char* name;
            int (*test)(int);
        } kClasses[] = {
            {"isalpha", isalpha}, {"isdigit", isdigit}, {"isxdigit", isxdigit}, {"isspace", isspace},
            {"isupper", isupper}, {"islower", islower}, {"isalnum", isalnum},   {"ispunct", ispunct},
            {"isprint", isprint}, {"iscntrl", iscntrl}, {"isgraph", isgraph},   {"isblank", isblank},
            {"toupper", toupper}, {"tolower", tolower},
        };
        for (const auto& entry : kClasses) {
            if (name != entry.name && name != std::string(entry.name) + "_l" && name != std::string("isw") + (entry.name + 2) &&
                name != std::string("tow") + (entry.name + 2))
                continue;
            int c = (int)(int32_t)arg(0);
            int got = (c >= -1 && c < 256) ? entry.test(c & 0xff) : (entry.name[0] == 't' ? c : 0);
            if (c == -1) got = entry.name[0] == 't' ? -1 : 0;
            ret((uint64_t)(int64_t)got);
            return true;
        }
    }
    if (name == "sysconf") {
        /* The handful of names bionic numbers these by. */
        int which = (int)arg(0);
        uint64_t value;
        switch (which) {
        case 0x27: value = 4096; break;                   /* _SC_PAGESIZE */
        case 0x28: value = 4096; break;                   /* _SC_PAGE_SIZE */
        case 0x60: case 0x61:                             /* _SC_NPROCESSORS_CONF, _ONLN */
            /* A Quest 3 has 6; QB_CPUS offers the game more, which sizes
               its job worker pool. */
            value = QB_ENV("QB_CPUS") ? (uint64_t)std::max(1, std::atoi(QB_ENV("QB_CPUS"))) : 6;
            break;
        case 0x62: value = 8ull << 30 >> 12; break;       /* _SC_PHYS_PAGES */
        case 0x63: value = 4ull << 30 >> 12; break;       /* _SC_AVPHYS_PAGES */
        case 0x06: value = 100; break;                    /* _SC_CLK_TCK */
        case 0x07: value = 1024; break;                   /* _SC_OPEN_MAX */
        default:
            if (QB_ENV("QB_TRACE")) std::printf("guest: sysconf %d\n", which);
            value = (uint64_t)-1;
        }
        ret(value);
        return true;
    }
    if (name == "clock") {
        ret((uint64_t)std::clock() * (1000000 / CLOCKS_PER_SEC));
        return true;
    }
    if (name == "setlocale") {
        static uint64_t c_locale = 0;
        if (!c_locale) c_locale = guest_string("C");
        ret(c_locale);
        return true;
    }
    if (name == "readlink" || name == "readlinkat") {
        bool at = name == "readlinkat";
        const char* path = text(arg(at ? 1 : 0));
        std::string wanted = path ? path : "";
        std::string target;
        if (wanted == "/proc/self/exe") target = "/system/bin/app_process64";
        if (QB_ENV("QB_TRACE")) std::printf("file: readlink %s\n", wanted.c_str());
        if (target.empty()) {
            ret((uint64_t)-1);
            return true;
        }
        uint64_t room = arg(at ? 3 : 2);
        uint64_t count = std::min<uint64_t>(room, target.size());
        uint8_t* out = ptr(arg(at ? 2 : 1), count);
        if (out) std::memcpy(out, target.data(), (size_t)count);
        ret(count);
        return true;
    }
    if (name == "strncpy" || name == "__strncpy_chk2" || name == "stpcpy" || name == "stpncpy") {
        /* strncpy pads with zeros up to n; stpcpy returns the end. */
        char* to = text(arg(0));
        const char* from = text(arg(1));
        if (!to || !from) {
            ret(arg(0));
            return true;
        }
        if (name == "stpcpy") {
            size_t length = std::strlen(from);
            std::memcpy(to, from, length + 1);
            ret(arg(0) + length);
            return true;
        }
        size_t limit = (size_t)arg(2);
        size_t length = strnlen(from, limit);
        std::memcpy(to, from, length);
        std::memset(to + length, 0, limit - length);
        ret(name == "stpncpy" ? arg(0) + length : arg(0));
        return true;
    }
    if (name == "strnlen") {
        const char* from = text(arg(0));
        ret(from ? strnlen(from, (size_t)arg(1)) : 0);
        return true;
    }
    if (name == "eventfd") {
        auto pipe = std::make_shared<GuestPipe>();
        pipe->event = true;
        pipe->counter = (uint32_t)arg(0);
        pipe->semaphore = (arg(1) & 1) != 0;       /* EFD_SEMAPHORE */
        pipe->nonblocking = (arg(1) & 04000) != 0; /* EFD_NONBLOCK */
        std::lock_guard<std::recursive_mutex> held(lock);
        int fd = next_fd++;
        fds[fd] = pipe;
        ret((uint64_t)fd);
        return true;
    }
    if (name == "poll" || name == "ppoll") {
        /* struct pollfd { int fd; short events; short revents; }. Pipes and
           eventfds are readable when they hold something; files and anything
           else are always ready. Waits in short slices until one is ready or
           the timeout passes, answering signals meanwhile. */
        uint64_t list = arg(0);
        uint64_t count = arg(1);
        int timeout = (int)(int32_t)arg(2);
        if (name == "ppoll") {
            timeout = -1;
            if (arg(2)) {
                uint8_t* spec = ptr(arg(2), 16);
                int64_t seconds = 0, nanos = 0;
                if (spec) {
                    std::memcpy(&seconds, spec, 8);
                    std::memcpy(&nanos, spec + 8, 8);
                }
                timeout = (int)(seconds * 1000 + nanos / 1000000);
            }
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout < 0 ? 0 : timeout);
        for (;;) {
            int ready = 0;
            for (uint64_t i = 0; i < count; ++i) {
                uint8_t* entry = ptr(list + i * 8, 8);
                if (!entry) continue;
                int32_t fd;
                int16_t events, revents = 0;
                std::memcpy(&fd, entry, 4);
                std::memcpy(&events, entry + 4, 2);
                if (fd >= 0) {
                    std::shared_ptr<GuestPipe> pipe;
                    {
                        std::lock_guard<std::recursive_mutex> held(lock);
                        auto found = fds.find(fd);
                        if (found != fds.end()) pipe = found->second;
                    }
                    bool readable = true;
                    if (pipe) {
                        std::lock_guard<std::mutex> held(pipe->lock);
                        readable = pipe->event ? pipe->counter != 0 : !pipe->bytes.empty();
                    }
                    if ((events & 1) && readable) revents |= 1; /* POLLIN */
                    if (events & 4) revents |= 4;               /* POLLOUT */
                }
                std::memcpy(entry + 6, &revents, 2);
                if (revents) ++ready;
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
            if (poll_signals(cpu)) {
                set_errno(cpu, 4);
                ret((uint64_t)-1);
                return true;
            }
        }
    }
    if (name == "strcasecmp" || name == "strncasecmp") {
        const char* a = text(arg(0));
        const char* b = text(arg(1));
        size_t limit = name == "strncasecmp" ? (size_t)arg(2) : (size_t)-1;
        int order = 0;
        for (size_t i = 0; a && b && i < limit; ++i) {
            int x = std::tolower((unsigned char)a[i]), y = std::tolower((unsigned char)b[i]);
            if (x != y || !x) {
                order = x - y;
                break;
            }
        }
        ret((uint64_t)(int64_t)order);
        return true;
    }
    if (name == "sincosf" || name == "sincos") {
        /* The argument is in s0 or d0; the results go through two pointers. */
        if (name == "sincosf") {
            float angle = [&] { float f; uint32_t b = (uint32_t)cpu.q[0].lo; std::memcpy(&f, &b, 4); return f; }();
            float sine = std::sin(angle), cosine = std::cos(angle);
            uint8_t* s_out = ptr(arg(0), 4);
            uint8_t* c_out = ptr(arg(1), 4);
            if (s_out) std::memcpy(s_out, &sine, 4);
            if (c_out) std::memcpy(c_out, &cosine, 4);
        } else {
            double angle;
            std::memcpy(&angle, &cpu.q[0].lo, 8);
            double sine = std::sin(angle), cosine = std::cos(angle);
            uint8_t* s_out = ptr(arg(0), 8);
            uint8_t* c_out = ptr(arg(1), 8);
            if (s_out) std::memcpy(s_out, &sine, 8);
            if (c_out) std::memcpy(c_out, &cosine, 8);
        }
        return true;
    }
    if (name == "bsearch") {
        /* The comparator is guest code, so each probe is a call into it. */
        uint64_t key = arg(0), base = arg(1), count = arg(2), size = arg(3), compare = arg(4);
        uint64_t low = 0, high = count;
        uint64_t found = 0;
        while (low < high) {
            uint64_t middle = low + (high - low) / 2;
            int32_t order = (int32_t)call_guest_on(cpu, compare, key, base + middle * size);
            if (order == 0) {
                found = base + middle * size;
                break;
            }
            if (order < 0) high = middle;
            else low = middle + 1;
        }
        ret(found);
        return true;
    }
    if (name == "strlcpy" || name == "strlcat") {
        const char* from = text(arg(1));
        uint64_t room = arg(2);
        size_t length = from ? std::strlen(from) : 0;
        char* to = reinterpret_cast<char*>(ptr(arg(0), room ? room : 1));
        size_t existing = 0;
        if (name == "strlcat" && to) {
            while (existing < room && to[existing]) ++existing;
        }
        if (to && room > existing) {
            size_t copy = std::min<size_t>(length, (size_t)(room - existing - 1));
            std::memcpy(to + existing, from, copy);
            to[existing + copy] = 0;
        }
        ret(existing + length);
        return true;
    }
    if (name == "setenv" || name == "unsetenv" || name == "putenv") {
        ret(0);
        return true;
    }
    if (name == "strspn" || name == "strcspn") {
        const char* s = text(arg(0));
        const char* set = text(arg(1));
        if (!s || !set) {
            ret(0);
            return true;
        }
        ret(name == "strspn" ? std::strspn(s, set) : std::strcspn(s, set));
        return true;
    }
    if (name == "strpbrk") {
        const char* s = text(arg(0));
        const char* set = text(arg(1));
        const char* found = s && set ? std::strpbrk(s, set) : nullptr;
        ret(found ? arg(0) + (uint64_t)(found - s) : 0);
        return true;
    }
    if (name == "strsep") {
        /* char* strsep(char** stringp, const char* delim) */
        uint64_t* where = reinterpret_cast<uint64_t*>(ptr(arg(0), 8));
        const char* delim = text(arg(1));
        if (!where || !*where || !delim) {
            ret(0);
            return true;
        }
        char* start = reinterpret_cast<char*>(*where);
        char* end = std::strpbrk(start, delim);
        if (end) {
            *end = 0;
            *where = reinterpret_cast<uint64_t>(end + 1);
        } else {
            *where = 0;
        }
        ret(reinterpret_cast<uint64_t>(start));
        return true;
    }
    if (name == "strtok_r") {
        /* char* strtok_r(char* s, const char* delim, char** save) */
        uint64_t* save = reinterpret_cast<uint64_t*>(ptr(arg(2), 8));
        const char* delim = text(arg(1));
        if (!save || !delim) {
            ret(0);
            return true;
        }
        char* s = arg(0) ? reinterpret_cast<char*>(arg(0)) : reinterpret_cast<char*>(*save);
        if (!s) {
            ret(0);
            return true;
        }
        s += std::strspn(s, delim);
        if (!*s) {
            *save = reinterpret_cast<uint64_t>(s);
            ret(0);
            return true;
        }
        char* end = s + std::strcspn(s, delim);
        if (*end) *end++ = 0;
        *save = reinterpret_cast<uint64_t>(end);
        ret(reinterpret_cast<uint64_t>(s));
        return true;
    }
    if (name == "logb" || name == "logbf" || name == "ilogb" || name == "ilogbf") {
        bool single = name.back() == 'f';
        double value;
        if (single) {
            float f;
            uint32_t b = (uint32_t)cpu.q[0].lo;
            std::memcpy(&f, &b, 4);
            value = f;
        } else {
            std::memcpy(&value, &cpu.q[0].lo, 8);
        }
        if (name[0] == 'i') {
            ret((uint64_t)(int64_t)std::ilogb(value));
            return true;
        }
        double result = std::logb(value);
        if (single) {
            float f = (float)result;
            uint32_t b;
            std::memcpy(&b, &f, 4);
            cpu.q[0].lo = b;
        } else {
            std::memcpy(&cpu.q[0].lo, &result, 8);
        }
        cpu.q[0].hi = 0;
        return true;
    }
    if (name == "strstr" || name == "strcasestr") {
        const char* hay = text(arg(0));
        const char* needle = text(arg(1));
        if (!hay || !needle) {
            ret(0);
            return true;
        }
        std::string h = hay, n = needle;
        if (name == "strcasestr") {
            for (char& c : h) c = (char)std::tolower((unsigned char)c);
            for (char& c : n) c = (char)std::tolower((unsigned char)c);
        }
        size_t at = h.find(n);
        ret(at == std::string::npos ? 0 : arg(0) + at);
        return true;
    }
    if (name == "ATrace_isEnabled" || name == "ATrace_beginSection" || name == "ATrace_endSection" ||
        name == "ATrace_setCounter" || name == "ATrace_beginAsyncSection" || name == "ATrace_endAsyncSection") {
        ret(0);
        return true;
    }
    if (name == "setvbuf" || name == "setbuf" || name == "setpriority" || name == "getpriority") {
        ret(0);
        return true;
    }
    if (name == "getenv") {
        /* The environment a freshly started application process has. */
        const char* key = text(arg(0));
        std::string wanted = key ? key : "";
        const char* value = nullptr;
        static const std::string home = std::string("/data/data/") + guest_package();
        static const std::string tmp = home + "/cache";
        if (wanted == "HOME") value = home.c_str();
        else if (wanted == "TMPDIR") value = tmp.c_str();
        else if (wanted == "PATH") value = "/system/bin";
        else if (wanted == "ANDROID_ROOT") value = "/system";
        else if (wanted == "ANDROID_DATA") value = "/data";
        if (QB_ENV("QB_TRACE")) std::printf("getenv: %s\n", wanted.c_str());
        ret(value ? guest_string(value) : 0);
        return true;
    }
    /* struct sysinfo, as bionic's arm64 layout has it: a Quest 3's 8 GB,
       most of it free. Unreal sizes its allocator from totalram. */
    if (name == "sysinfo") {
        uint8_t* out = ptr(arg(0), 112);
        if (out) {
            std::memset(out, 0, 112);
            int64_t uptime = guest_clock_ns() / 1000000000 + 600;
            uint64_t total = 8ull << 30, free_ = 5ull << 30;
            uint16_t procs = 400;
            uint32_t unit = 1;
            std::memcpy(out, &uptime, 8);
            std::memcpy(out + 32, &total, 8);
            std::memcpy(out + 40, &free_, 8);
            std::memcpy(out + 80, &procs, 2);
            std::memcpy(out + 104, &unit, 4);
        }
        ret(0);
        return true;
    }
    /* Resource limits: generous and unlimited where Android's are, and a
       set is accepted as a no-op. struct rlimit is two unsigned longs. */
    if (name == "getrlimit" || name == "getrlimit64" || name == "prlimit" || name == "prlimit64") {
        bool pr = name.compare(0, 7, "prlimit") == 0;
        int resource = (int)arg(pr ? 1 : 0);
        uint8_t* out = ptr(arg(pr ? 3 : 1), 16);
        if (out) {
            uint64_t infinity = ~0ull, soft = infinity, hard = infinity;
            if (resource == 7) soft = hard = 32768;            /* RLIMIT_NOFILE */
            else if (resource == 3) soft = 8ull << 20;         /* RLIMIT_STACK */
            std::memcpy(out, &soft, 8);
            std::memcpy(out + 8, &hard, 8);
        }
        ret(0);
        return true;
    }
    if (name == "setrlimit" || name == "setrlimit64") {
        ret(0);
        return true;
    }
    /* wchar_t is 32 bits on Android, 16 on Windows: done by hand. */
    if (name == "wcschr") {
        const uint32_t* text = reinterpret_cast<const uint32_t*>(ptr(arg(0), 4));
        uint32_t wanted = (uint32_t)arg(1);
        uint64_t found = 0;
        for (uint64_t i = 0; text; ++i) {
            if (text[i] == wanted) {
                found = arg(0) + 4 * i;
                break;
            }
            if (!text[i]) break;
        }
        ret(found);
        return true;
    }
    /* Every CPU the game was told about (sysconf / the CPU files). */
    if (name == "sched_getaffinity") {
        uint64_t bytes = std::min<uint64_t>(arg(1), 128);
        uint8_t* mask = ptr(arg(2), bytes ? bytes : 1);
        int cpus = QB_ENV("QB_CPUS") ? std::max(1, std::atoi(QB_ENV("QB_CPUS"))) : 6;
        if (mask) {
            std::memset(mask, 0, (size_t)bytes);
            for (int i = 0; i < cpus && i / 8 < (int)bytes; ++i) mask[i / 8] |= (uint8_t)(1 << (i % 8));
        }
        ret(0);
        return true;
    }
    if (name == "__sched_cpucount") {
        uint64_t bytes = std::min<uint64_t>(arg(0), 128);
        const uint8_t* mask = ptr(arg(1), bytes ? bytes : 1);
        int count = 0;
        for (uint64_t i = 0; mask && i < bytes; ++i)
            for (int b = 0; b < 8; ++b) count += (mask[i] >> b) & 1;
        ret((uint64_t)count);
        return true;
    }
    if (name == "tzset") return true; /* UTC, always: the globals above */

    /* An app's own user and group, as Android gives each app one. */
    if (name == "getuid" || name == "geteuid" || name == "getgid" || name == "getegid") {
        ret(10123);
        return true;
    }
    if (name == "strerror") {
        static std::mutex text_lock;
        static std::unordered_map<int, uint64_t> texts;
        int code = (int)(int32_t)arg(0);
        std::lock_guard<std::mutex> held(text_lock);
        uint64_t& text = texts[code];
        if (!text) {
            char buffer[128];
            strerror_s(buffer, sizeof(buffer), code);
            text = guest_string(buffer);
        }
        ret(text);
        return true;
    }
    /* socketpair: two connected ends, which here are a pipe each way, the
       same pipes pipe() makes (activity.cpp), so read and write work. */
    if (name == "socketpair") {
        int a = make_pipe(), b = make_pipe();
        uint8_t* out = ptr(arg(3), 8);
        if (!out) {
            ret((uint64_t)-1);
            return true;
        }
        /* a: read end a, write end a+1; each side reads one pipe and writes
           the other, which a single fd cannot do here, so each side gets the
           read end of one pipe; writes to it go to the other pipe. */
        link_socket_pair(a, b);
        int32_t fds[2] = {a, b};
        std::memcpy(out, fds, 8);
        ret(0);
        return true;
    }
    /* Scheduling parameters: every thread is SCHED_OTHER, priority 0. */
    if (name == "pthread_getschedparam") {
        uint8_t* policy = ptr(arg(1), 4);
        uint8_t* param = ptr(arg(2), 4);
        int32_t zero = 0;
        if (policy) std::memcpy(policy, &zero, 4);
        if (param) std::memcpy(param, &zero, 4);
        ret(0);
        return true;
    }
    if (name == "__strrchr_chk") { /* strrchr, with the buffer size unchecked */
        const char* text = reinterpret_cast<const char*>(ptr(arg(0), 1));
        const char* found = text ? std::strrchr(text, (int)(char)arg(1)) : nullptr;
        ret(found ? arg(0) + (uint64_t)(found - text) : 0);
        return true;
    }
    /* Google's markers around calls that may block: nothing to do. */
    if (name == "__google_potentially_blocking_region_begin" || name == "__google_potentially_blocking_region_end") {
        return true;
    }
    if (name == "gettimeofday") {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        uint8_t* out = ptr(arg(0), 16);
        if (out) {
            int64_t seconds = micros / 1000000, rest = micros % 1000000;
            std::memcpy(out, &seconds, 8);
            std::memcpy(out + 8, &rest, 8);
        }
        ret(0);
        return true;
    }
    if (name == "newlocale" || name == "duplocale" || name == "uselocale") {
        ret(0x4c4f43); /* any non-null handle: bionic only has the C locale */
        return true;
    }
    if (name == "freelocale") {
        ret(0);
        return true;
    }
    /* Time conversion. bionic's struct tm is nine ints, then a long
       tm_gmtoff and a tm_zone pointer: 56 bytes. gmtime and localtime hand
       back a per-thread block, as bionic's do. */
    if (name == "gmtime" || name == "localtime" || name == "gmtime_r" || name == "localtime_r" ||
        name == "mktime" || name == "timegm" || name == "difftime" || name == "strftime") {
        struct GuestTm {
            int32_t sec, min, hour, mday, mon, year, wday, yday, isdst;
            int32_t pad;
            int64_t gmtoff;
            uint64_t zone;
        };
        static_assert(sizeof(GuestTm) == 56, "bionic struct tm");
        auto to_guest = [&](const std::tm& host, GuestTm& out, bool local) {
            out = GuestTm{host.tm_sec, host.tm_min, host.tm_hour, host.tm_mday, host.tm_mon, host.tm_year,
                          host.tm_wday, host.tm_yday, host.tm_isdst, 0, 0, 0};
            if (local) {
                long bias = 0;
                _get_timezone(&bias);
                out.gmtoff = -(int64_t)bias + (host.tm_isdst > 0 ? 3600 : 0);
            }
            static uint64_t utc = 0;
            if (!utc) utc = guest_string("UTC");
            out.zone = utc;
        };
        auto from_guest = [&](const GuestTm& in) {
            std::tm host{};
            host.tm_sec = in.sec;
            host.tm_min = in.min;
            host.tm_hour = in.hour;
            host.tm_mday = in.mday;
            host.tm_mon = in.mon;
            host.tm_year = in.year;
            host.tm_wday = in.wday;
            host.tm_yday = in.yday;
            host.tm_isdst = in.isdst;
            return host;
        };
        if (name == "difftime") {
            double seconds = (double)(int64_t)arg(0) - (double)(int64_t)arg(1);
            std::memcpy(&cpu.q[0].lo, &seconds, 8);
            cpu.q[0].hi = 0;
            return true;
        }
        if (name == "mktime" || name == "timegm") {
            GuestTm* in = reinterpret_cast<GuestTm*>(ptr(arg(0), sizeof(GuestTm)));
            if (!in) {
                ret((uint64_t)-1);
                return true;
            }
            std::tm host = from_guest(*in);
            __time64_t made = name == "mktime" ? _mktime64(&host) : _mkgmtime64(&host);
            to_guest(host, *in, name == "mktime"); /* both normalise the fields they were given */
            ret((uint64_t)(int64_t)made);
            return true;
        }
        if (name == "strftime") {
            char* out = reinterpret_cast<char*>(ptr(arg(0), arg(1)));
            const char* format = text(arg(2));
            GuestTm* in = reinterpret_cast<GuestTm*>(ptr(arg(3), sizeof(GuestTm)));
            if (!out || !format || !in || !arg(1)) {
                ret(0);
                return true;
            }
            std::tm host = from_guest(*in);
            /* The MSVC runtime rejects conversions it lacks (%P, %s, %k...);
               those few are left out rather than ending the process. */
            std::string safe;
            for (const char* at = format; *at; ++at) {
                if (*at == '%' && at[1]) {
                    char c = at[1];
                    if (std::strchr("aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%", c)) {
                        safe += '%';
                        safe += c;
                    }
                    ++at;
                } else {
                    safe += *at;
                }
            }
            ret(std::strftime(out, (size_t)arg(1), safe.c_str(), &host));
            return true;
        }
        /* gmtime/localtime and their _r forms. */
        int64_t when = 0;
        if (uint8_t* t = ptr(arg(0), 8)) std::memcpy(&when, t, 8);
        __time64_t host_time = when;
        std::tm host{};
        bool local = name.compare(0, 9, "localtime") == 0;
        if ((local ? _localtime64_s(&host, &host_time) : _gmtime64_s(&host, &host_time)) != 0) {
            ret(0);
            return true;
        }
        uint64_t block = 0;
        if (name.back() == 'r') {
            block = arg(1);
        } else {
            static thread_local uint64_t mine = 0;
            if (!mine) mine = guest_alloc(sizeof(GuestTm));
            block = mine;
        }
        GuestTm* out = reinterpret_cast<GuestTm*>(ptr(block, sizeof(GuestTm)));
        if (!out) {
            ret(0);
            return true;
        }
        to_guest(host, *out, local);
        ret(block);
        return true;
    }
    if (name.compare(0, 6, "AMedia") == 0 && name.compare(0, 18, "AMEDIAFORMAT_KEY_") != 0) {
        /* No media codecs here: constructors give null, the rest
           AMEDIA_ERROR_UNKNOWN, and a video player reports it cannot open. */
        bool constructor = name.size() > 4 && (name.compare(name.size() - 4, 4, "_new") == 0 ||
                                               name.find("createDecoder") != std::string::npos ||
                                               name.find("createEncoder") != std::string::npos ||
                                               name.find("createCodec") != std::string::npos);
        bool destructor = name.find("_delete") != std::string::npos;
        ret(constructor || destructor ? 0 : (uint64_t)(int64_t)-10000);
        return true;
    }
    if (name == "sched_get_priority_min" || name == "sched_get_priority_max") {
        /* SCHED_OTHER has only priority 0; FIFO and RR run 1..99. */
        int policy = (int)arg(0);
        ret(policy == 1 || policy == 2 ? (name.back() == 'n' ? 1 : 99) : 0);
        return true;
    }
    if (name == "strerror_r" || name == "__gnu_strerror_r") {
        /* The XSI form bionic declares: fills the buffer, returns 0. */
        char* out = reinterpret_cast<char*>(ptr(arg(1), arg(2)));
        char message[128] = "";
        strerror_s(message, sizeof(message), (int)arg(0));
        if (out && arg(2)) {
            std::snprintf(out, (size_t)arg(2), "%s", message);
        }
        ret(name == "strerror_r" ? 0 : arg(1));
        return true;
    }
    if (name == "getifaddrs") {
        /* No interfaces to list; callers then fall back to their defaults. */
        if (uint8_t* out = ptr(arg(0), 8)) std::memset(out, 0, 8);
        set_errno(cpu, 38); /* ENOSYS */
        ret((uint64_t)-1);
        return true;
    }
    if (name == "freeifaddrs") {
        return true;
    }
    if (name == "getrandom" || name == "getentropy" || name == "arc4random_buf") {
        /* getrandom(buf, len, flags) -> len; getentropy(buf, len) -> 0;
           arc4random_buf(buf, len). All from the system's generator. */
        uint8_t* out = ptr(arg(0), arg(1));
        size_t length = (size_t)arg(1);
        if (out && length) BCryptGenRandom(nullptr, out, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (name == "getrandom") ret(out ? length : (uint64_t)-1);
        else ret(0);
        return true;
    }
    if (name == "__ctype_get_mb_cur_max") {
        ret(4); /* UTF-8 */
        return true;
    }
    if (name == "mbtowc" || name == "mbrtowc") {
        /* One UTF-8 character: its length, 0 at the terminator, -1 if bad. */
        const uint8_t* s = reinterpret_cast<const uint8_t*>(arg(1));
        if (!s) {
            ret(0);
            return true;
        }
        size_t available = (size_t)arg(2);
        uint32_t c = s[0];
        int length = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!length || (size_t)length > available) {
            ret((uint64_t)-1);
            return true;
        }
        if (length > 1) c &= 0x7f >> length;
        for (int i = 1; i < length; ++i) c = (c << 6) | (s[i] & 0x3f);
        if (arg(0)) std::memcpy(reinterpret_cast<void*>(arg(0)), &c, 4);
        ret(c == 0 ? 0 : (uint64_t)length);
        return true;
    }
    if (name == "wmemcpy" || name == "wmemmove" || name == "wmemset") {
        uint64_t n = arg(2);
        if (name == "wmemset") {
            uint32_t value = (uint32_t)arg(1);
            for (uint64_t i = 0; i < n; ++i) std::memcpy(reinterpret_cast<void*>(arg(0) + 4 * i), &value, 4);
        } else {
            std::memmove(reinterpret_cast<void*>(arg(0)), reinterpret_cast<void*>(arg(1)), (size_t)(4 * n));
        }
        ret(arg(0));
        return true;
    }
    if (name == "wcstombs" || name == "mbstowcs" || name == "wcsrtombs" || name == "mbsrtowcs") {
        /* 32-bit code points to UTF-8 and back (bionic's locale is UTF-8).
           The *rtombs/*rtowcs forms take a pointer to the source pointer. */
        bool restartable = name[3] == 'r' || name[4] == 'r';
        uint64_t source = arg(1);
        if (restartable) std::memcpy(&source, reinterpret_cast<void*>(arg(1)), 8);
        uint64_t limit = arg(2);
        uint64_t out = arg(0);
        uint64_t written = 0;
        if (name[0] == 'w') {
            const uint32_t* from = reinterpret_cast<const uint32_t*>(source);
            for (;; ++from) {
                uint32_t c = *from;
                char bytes[4];
                int count = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
                if (count == 1) bytes[0] = (char)c;
                else if (count == 2) { bytes[0] = (char)(0xc0 | (c >> 6)); bytes[1] = (char)(0x80 | (c & 0x3f)); }
                else if (count == 3) { bytes[0] = (char)(0xe0 | (c >> 12)); bytes[1] = (char)(0x80 | ((c >> 6) & 0x3f)); bytes[2] = (char)(0x80 | (c & 0x3f)); }
                else { bytes[0] = (char)(0xf0 | (c >> 18)); bytes[1] = (char)(0x80 | ((c >> 12) & 0x3f)); bytes[2] = (char)(0x80 | ((c >> 6) & 0x3f)); bytes[3] = (char)(0x80 | (c & 0x3f)); }
                if (c == 0) {
                    if (out && written < limit) reinterpret_cast<char*>(out)[written] = 0;
                    if (restartable && out) { uint64_t none = 0; std::memcpy(reinterpret_cast<void*>(arg(1)), &none, 8); }
                    break;
                }
                if (out && written + count > limit) break;
                if (out) std::memcpy(reinterpret_cast<char*>(out) + written, bytes, (size_t)count);
                written += count;
            }
        } else {
            const uint8_t* from = reinterpret_cast<const uint8_t*>(source);
            while (!out || written < limit) {
                uint32_t c = *from;
                int extra = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
                if (extra) c &= 0x3f >> extra;
                for (int i = 1; i <= extra && from[i]; ++i) c = (c << 6) | (from[i] & 0x3f);
                if (out) std::memcpy(reinterpret_cast<uint32_t*>(out) + written, &c, 4);
                if (c == 0) {
                    if (restartable && out) { uint64_t none = 0; std::memcpy(reinterpret_cast<void*>(arg(1)), &none, 8); }
                    break;
                }
                from += 1 + extra;
                ++written;
            }
        }
        ret(written);
        return true;
    }
    if (name == "wmemchr" || name == "wcslen" || name == "wmemcmp") {
        /* bionic's wchar_t is 32 bits; Windows' is 16, so never the host's. */
        if (name == "wcslen") {
            uint64_t n = 0;
            for (;; ++n) {
                const uint8_t* c = ptr(arg(0) + 4 * n, 4);
                uint32_t v = 0;
                if (c) std::memcpy(&v, c, 4);
                if (!c || !v) break;
            }
            ret(n);
            return true;
        }
        if (name == "wmemcmp") {
            int result = 0;
            for (uint64_t i = 0; i < arg(2) && !result; ++i) {
                int32_t a = 0, b = 0;
                std::memcpy(&a, reinterpret_cast<void*>(arg(0) + 4 * i), 4);
                std::memcpy(&b, reinterpret_cast<void*>(arg(1) + 4 * i), 4);
                result = a < b ? -1 : a > b ? 1 : 0;
            }
            ret((uint64_t)(int64_t)result);
            return true;
        }
        uint32_t wanted = (uint32_t)arg(1);
        uint64_t found = 0;
        for (uint64_t i = 0; i < arg(2); ++i) {
            uint32_t v = 0;
            std::memcpy(&v, reinterpret_cast<void*>(arg(0) + 4 * i), 4);
            if (v == wanted) {
                found = arg(0) + 4 * i;
                break;
            }
        }
        ret(found);
        return true;
    }
    if (name == "strtof" || name == "strtod" || name == "atof") {
        const char* s = text(arg(0));
        double value = s ? std::strtod(s, nullptr) : 0;
        if (name == "strtof") {
            float single = (float)value;
            uint32_t bits;
            std::memcpy(&bits, &single, 4);
            cpu.q[0].lo = bits;
        } else {
            uint64_t bits;
            std::memcpy(&bits, &value, 8);
            cpu.q[0].lo = bits;
        }
        cpu.q[0].hi = 0;
        return true;
    }

    /* Sorting, which hands us one of the guest's own functions to call. */
    if (name == "qsort") {
        uint64_t base = arg(0);
        uint64_t count = arg(1);
        uint64_t width = arg(2);
        uint64_t comparator = arg(3);
        std::vector<std::vector<uint8_t>> items;
        for (uint64_t i = 0; i < count; ++i) {
            uint8_t* item = ptr(base + i * width, width);
            if (!item) return true;
            items.emplace_back(item, item + width);
        }
        /* The comparison runs guest code, so this cannot be std::sort with a
           predicate that might be called after the interpreter is disturbed;
           an insertion sort keeps the order of calls simple and predictable. */
        uint64_t scratch_va = 0;
        {
            GuestCpu temp = cpu;
            temp.x[0] = width * 2;
            call("malloc", temp);
            scratch_va = temp.x[0];
        }
        if (!scratch_va) return true;
        uint8_t* scratch = ptr(scratch_va, width * 2);
        for (size_t i = 1; i < items.size(); ++i) {
            std::vector<uint8_t> key = items[i];
            size_t j = i;
            while (j > 0) {
                std::memcpy(scratch, items[j - 1].data(), (size_t)width);
                std::memcpy(scratch + width, key.data(), (size_t)width);
                int64_t order = (int64_t)(int32_t)call_guest_on(cpu, comparator, scratch_va, scratch_va + width);
                if (order <= 0) break;
                items[j] = items[j - 1];
                --j;
            }
            items[j] = key;
        }
        for (uint64_t i = 0; i < count; ++i) {
            uint8_t* item = ptr(base + i * width, width);
            if (item) std::memcpy(item, items[(size_t)i].data(), (size_t)width);
        }
        GuestCpu freeing = cpu;
        freeing.x[0] = scratch_va;
        call("free", freeing);
        return true;
    }

    /* Formatting. The guest passes its own pointers, so the arguments are
       walked by hand rather than handed to the host's vsnprintf. */
    if (name == "snprintf" || name == "sprintf") {
        bool sized = name == "snprintf";
        uint64_t out_va = arg(0);
        uint64_t limit = sized ? arg(1) : 1u << 20;
        const char* format = text(sized ? arg(2) : arg(1));
        std::string result = format_from_guest(format, cpu, sized ? 3 : 2, 0);
        uint64_t written = result.size();
        uint64_t room = limit ? std::min<uint64_t>(limit - 1, written) : 0;
        uint8_t* target = ptr(out_va, room + 1);
        if (target) {
            std::memcpy(target, result.data(), (size_t)room);
            target[room] = 0;
        }
        ret(written);
        return true;
    }

    /* Startup and shutdown bookkeeping, which the toolchain adds by itself. */
    if (name == "__cxa_atexit" || name == "__cxa_finalize" || name == "__register_atfork" ||
        name == "atexit" || name == "__stack_chk_fail" || name == "abort") {
        if (name == "__stack_chk_fail" || name == "abort") {
            /* Returning from either would run on into whatever follows the
               call, which the compiler assumed could never happen. */
            std::fprintf(stderr, "guest: the library called %s from %llx, so the process ends here\n", name.c_str(),
                         (unsigned long long)cpu.x[30]);
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(134);
        }
        ret(0);
        return true;
    }
    /* The descriptor resolver the linker points thread local accesses at.
       It is handed the descriptor, and gives back the offset stored beside
       the resolver, which the guest then adds to its thread pointer. */
    if (name == "__tlsdesc_resolve") {
        ++tls_resolves;
        uint8_t* descriptor = ptr(arg(0), 16);
        uint64_t offset = 0;
        if (descriptor) std::memcpy(&offset, descriptor + 8, 8);
        ret(offset);
        return true;
    }

    /* The loader, as the guest sees it. */
    if (name == "dlopen") {
        const char* want = text(arg(0));
        /* dlopen(NULL) is the program itself: a handle whose dlsym searches
           everything loaded, never a failure. */
        uint64_t handle = want ? open_library(want) : 0xd20000;
        if ((QB_ENV("QB_TRACE") || QB_ENV("QB_TRACE_DLSYM")))
            std::printf("dlopen: %s -> %llx\n", want ? want : "(the program)", (unsigned long long)handle);
        ret(handle);
        return true;
    }
    if (name == "dlsym") {
        /* A handle from dlopen names one object, and the symbol is looked for
           there first, as the dynamic linker does; RTLD_DEFAULT (0), RTLD_NEXT
           and our own libraries' handle search everything. */
        const char* want = text(arg(1));
        uint64_t handle = arg(0);
        uint64_t found = 0;
        if (want) {
            if (handle >= 1 && handle <= image->linker.objects.size()) {
                auto& symbols = image->linker.objects[(size_t)handle - 1]->symbols;
                auto hit = symbols.find(want);
                if (hit != symbols.end()) found = hit->second;
            } else {
                /* RTLD_DEFAULT, RTLD_NEXT and the program: libc's names are
                   libc's (so an interposer's RTLD_NEXT finds the real one). */
                found = is_libc_name(want) ? guest_libc_data(want) : image->linker.lookup(want);
                if (!found && is_libc_name(want)) found = image->linker.thunk_for(want);
            }
            /* The Vulkan and OpenXR loaders are ours: their functions are thunks
               to the host's. */
            bool loader_function = (want[0] == 'v' && want[1] == 'k') || (want[0] == 'x' && want[1] == 'r') ||
                                   std::strncmp(want, "ovr_", 4) == 0 || /* the platform stand-in */
                                   std::strncmp(want, "egl", 3) == 0 || std::strncmp(want, "gl", 2) == 0; /* egl.cpp */
            if (!found && loader_function && (handle == 0xd10000 || handle > 0xffff || !handle))
                found = image->linker.thunk_for(want);
        }
        ret(found);
        if ((QB_ENV("QB_TRACE") || QB_ENV("QB_TRACE_DLSYM")) && want) std::printf("dlsym:%s -> %llx\n", want, (unsigned long long)found);
        return true;
    }
    if (name == "dlclose") {
        ret(0);
        return true;
    }
    if (name == "dlerror") {
        ret(0);
        return true;
    }
    if (name == "dl_iterate_phdr") {
        /* Calls back once per loaded object with a dl_phdr_info: load
           address, name, program headers and their count, then the adds and
           subs counters, which say whether the list changed since last time. */
        uint64_t callback = arg(0), data = arg(1);
        std::vector<GuestObject*> objects = image->linker.objects;
        uint64_t result = 0;
        for (GuestObject* object : objects) {
            uint8_t info[64] = {};
            uint64_t base = object->base;
            uint64_t named = reinterpret_cast<uint64_t>(object->name.c_str());
            uint64_t headers = reinterpret_cast<uint64_t>(object->program_headers.data());
            uint16_t count = object->program_header_count;
            uint64_t adds = objects.size(), subs = 0;
            std::memcpy(info, &base, 8);
            std::memcpy(info + 8, &named, 8);
            std::memcpy(info + 16, &headers, 8);
            std::memcpy(info + 24, &count, 2);
            std::memcpy(info + 32, &adds, 8);
            std::memcpy(info + 40, &subs, 8);
            /* On the guest's own stack, below where it stands. */
            GuestCpu local = cpu;
            local.sp = (cpu.sp - 256) & ~0xfull;
            std::memcpy(reinterpret_cast<void*>(local.sp), info, sizeof(info));
            result = call_guest_on(local, callback, local.sp, sizeof(info), data);
            if (result) break;
        }
        ret(result);
        return true;
    }
    if (name == "strdup" || name == "strndup") {
        const char* from = text(arg(0));
        std::string copy = from ? from : "";
        if (name == "strndup" && copy.size() > arg(1)) copy.resize((size_t)arg(1));
        ret(guest_string(copy));
        return true;
    }
    if (name == "memchr" || name == "memrchr") {
        const uint8_t* from = ptr(arg(0), arg(2));
        uint64_t found = 0;
        if (from) {
            int wanted = (int)(uint8_t)arg(1);
            if (name == "memchr") {
                const void* hit = std::memchr(from, wanted, (size_t)arg(2));
                if (hit) found = arg(0) + (uint64_t)(static_cast<const uint8_t*>(hit) - from);
            } else {
                for (uint64_t i = arg(2); i-- > 0;)
                    if (from[i] == wanted) {
                        found = arg(0) + i;
                        break;
                    }
            }
        }
        ret(found);
        return true;
    }
    if (name == "dladdr") {
        /* Which loaded object an address falls in, named by the path it
           would have on the device. No symbol: callers use the base. */
        uint64_t address = arg(0);
        for (GuestObject* object : image->linker.objects) {
            if (address < object->base || address >= object->base + object->image.size()) continue;
            uint8_t* info = ptr(arg(1), 32);
            if (!info) break;
            std::string path = std::string("/data/app/") + guest_package() + "/lib/arm64/" + object->name;
            uint64_t& named = library_paths[object->name];
            if (!named) named = guest_string(path);
            uint64_t zero = 0;
            std::memcpy(info, &named, 8);
            std::memcpy(info + 8, &object->base, 8);
            std::memcpy(info + 16, &zero, 8);
            std::memcpy(info + 24, &zero, 8);
            ret(1);
            return true;
        }
        ret(0);
        return true;
    }
    if (name == "dlclose") {
        ret(0);
        return true;
    }
    if (name == "dlerror") {
        ret(0);
        return true;
    }

    /* What the loader and the C library ask the system about themselves. */
    /* The v-forms, which take a va_list rather than their own arguments. */
    if (name == "vsnprintf" || name == "vsprintf" || name == "vasprintf" || name == "vfprintf" ||
        name == "__vsnprintf_chk" || name == "__vsprintf_chk") {
        /* The fortified forms carry a flag and the buffer's real size ahead of
           the format: __vsnprintf_chk(s, maxlen, flag, slen, format, ap) and
           __vsprintf_chk(s, flag, slen, format, ap). */
        bool sized = name == "vsnprintf" || name == "__vsnprintf_chk" || name == "__vsprintf_chk";
        bool allocating = name == "vasprintf";
        bool to_stream = name == "vfprintf";
        int format_arg = name == "__vsnprintf_chk" ? 4 : name == "__vsprintf_chk" ? 3
                         : (to_stream || allocating) ? 1 : sized ? 2 : 1;
        const char* format = text(arg(format_arg));
        uint64_t list = arg(format_arg + 1);
        std::string result = format_from_valist(format, list);
        if (to_stream) {
            std::printf("guest: %s", result.c_str());
            std::fflush(stdout);
            ret(result.size());
            return true;
        }
        if (allocating) {
            uint64_t where = guest_string(result);
            uint8_t* out = ptr(arg(0), 8);
            if (out) std::memcpy(out, &where, 8);
            ret(result.size());
            return true;
        }
        uint64_t limit = name == "__vsprintf_chk" ? arg(2) : sized ? arg(1) : (1u << 20);
        uint64_t room = limit ? std::min<uint64_t>(limit - 1, result.size()) : 0;
        uint8_t* target = ptr(arg(0), room + 1);
        if (target) {
            std::memcpy(target, result.data(), (size_t)room);
            target[room] = 0;
        }
        ret(result.size());
        return true;
    }
    /* Anonymous memory. The engine asks for large spans and manages them
       itself, which is why a first-fit heap is not enough behind it. */
    if (name == "mmap" || name == "mmap64") {
        uint64_t length = (arg(1) + 0xfff) & ~0xfffull;
        int64_t fd = (int64_t)(int32_t)arg(4);
        std::lock_guard<std::recursive_mutex> held(lock);
        FILE* backing = nullptr;
        if (fd != -1) {
            auto found = files.find((int)fd);
            if (found != files.end()) backing = found->second;
        }
        if ((fd != -1 && !backing) || !map_area || map_used + length > map_size) {
            if (QB_ENV("QB_TRACE"))
                std::printf("guest: mmap of %llu bytes, fd %lld, refused\n", (unsigned long long)arg(1),
                            (long long)fd);
            ret((uint64_t)-1); /* MAP_FAILED */
            return true;
        }
        uint64_t offset = map_used;
        map_used += length + 0x10000; /* a guard gap after each mapping */
        if (!VirtualAlloc(map_area + offset, (SIZE_T)length, MEM_COMMIT, PAGE_READWRITE)) {
            ret((uint64_t)-1);
            return true;
        }
        if (backing) {
            /* A private copy of the file is what a read-only mapping looks
               like to anyone who only reads it. */
            int64_t here = _ftelli64(backing);
            _fseeki64(backing, (int64_t)arg(5), SEEK_SET);
            std::fread(map_area + offset, 1, (size_t)arg(1), backing);
            _fseeki64(backing, here, SEEK_SET);
        }
        ret(map_va + offset);
        return true;
    }
    if (name == "munmap") {
        uint64_t at = arg(0);
        if (at >= map_va && at + arg(1) <= map_va + map_size)
            VirtualFree(map_area + (at - map_va), (SIZE_T)((arg(1) + 0xfff) & ~0xfffull), MEM_DECOMMIT);
        ret(0);
        return true;
    }
    if (name == "mprotect" || name == "madvise" || name == "mlock" || name == "munlock" || name == "msync" ||
        name == "prctl") {
        /* PR_SET_NAME (15): which thread is which, for QB_THREADS. */
        if (name == "prctl" && arg(0) == 15 && QB_ENV("QB_THREADS") && text(arg(1)))
            std::fprintf(stderr, "thread: tp %llx is named %s\n", (unsigned long long)cpu.tpidr, text(arg(1)));
        ret(0);
        return true;
    }
    /* setjmp and longjmp. Both ends are ours, so the buffer layout is too:
       x19 to x29, the link register, the stack pointer, then d8 to d15, which
       is everything the calling convention says a callee must preserve. When
       the thunk returns, execution resumes at x30, so restoring x30 is what
       makes longjmp land back at the setjmp call. */
    if (name == "setjmp" || name == "_setjmp" || name == "sigsetjmp" || name == "__sigsetjmp") {
        uint8_t* buffer = ptr(arg(0), 21 * 8);
        if (buffer) {
            for (int i = 0; i <= 11; ++i) std::memcpy(buffer + i * 8, &cpu.x[19 + i], 8);
            std::memcpy(buffer + 12 * 8, &cpu.sp, 8);
            for (int i = 0; i < 8; ++i) std::memcpy(buffer + (13 + i) * 8, &cpu.q[8 + i].lo, 8);
        }
        ret(0);
        return true;
    }
    if (name == "longjmp" || name == "_longjmp" || name == "siglongjmp") {
        uint8_t* buffer = ptr(arg(0), 21 * 8);
        uint64_t value = arg(1) ? arg(1) : 1;
        if (buffer) {
            for (int i = 0; i <= 11; ++i) std::memcpy(&cpu.x[19 + i], buffer + i * 8, 8);
            std::memcpy(&cpu.sp, buffer + 12 * 8, 8);
            for (int i = 0; i < 8; ++i) {
                std::memcpy(&cpu.q[8 + i].lo, buffer + (13 + i) * 8, 8);
                cpu.q[8 + i].hi = 0;
            }
        }
        ret(value);
        return true;
    }
    if (name == "usleep" || name == "nanosleep" || name == "clock_nanosleep" || name == "sleep") {
        int64_t micros = 0;
        if (name == "usleep") micros = (int64_t)arg(0);
        else if (name == "sleep") micros = (int64_t)arg(0) * 1000000;
        else {
            uint8_t* spec = ptr(arg(name == "nanosleep" ? 0 : 2), 16);
            int64_t seconds = 0, nanos = 0;
            if (spec) {
                std::memcpy(&seconds, spec, 8);
                std::memcpy(&nanos, spec + 8, 8);
            }
            micros = seconds * 1000000 + nanos / 1000;
            /* clock_nanosleep with TIMER_ABSTIME: treat as a short nap. */
            if (name == "clock_nanosleep" && (arg(1) & 1)) micros = 1000;
        }
        /* Sleep in slices so a signal can cut it short. */
        auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
        while (std::chrono::steady_clock::now() < until) {
            auto left = std::chrono::duration_cast<std::chrono::microseconds>(until - std::chrono::steady_clock::now());
            std::this_thread::sleep_for(std::min<std::chrono::microseconds>(left, std::chrono::milliseconds(5)));
            if (poll_signals(cpu)) break;
        }
        ret(0);
        return true;
    }
    if (name == "sigsuspend") {
        /* Sleep until a signal arrives for this thread, run it, and return
           EINTR, which is the only way sigsuspend ever returns. */
        while (!poll_signals(cpu)) std::this_thread::sleep_for(std::chrono::microseconds(200));
        set_errno(cpu, 4);
        ret((uint64_t)-1);
        return true;
    }
    /* Signals. What sigaction installs is kept, and pthread_kill runs it. */
    if (name == "sigaction" || name == "sigemptyset" || name == "sigfillset" || name == "sigaddset" ||
        name == "sigdelset" || name == "sigprocmask" || name == "pthread_sigmask" || name == "sigaltstack" ||
        name == "signal" || name == "sigismember") {
        /* bionic's 64-bit sigaction puts the flags first: flags (padded to
           eight), handler, mask, restorer. */
        if (name == "sigaction") {
            int which = (int)arg(0) & 63;
            if (arg(2)) {
                uint8_t* old = ptr(arg(2), 32);
                if (old) {
                    std::memset(old, 0, 32);
                    uint32_t flags = (uint32_t)signal_flags[which];
                    std::memcpy(old, &flags, 4);
                    std::memcpy(old + 8, &signal_handlers[which], 8);
                }
            }
            if (arg(1)) {
                uint8_t* act = ptr(arg(1), 32);
                if (act) {
                    uint32_t flags = 0;
                    std::memcpy(&flags, act, 4);
                    std::memcpy(&signal_handlers[which], act + 8, 8);
                    signal_flags[which] = flags;
                }
            }
        }
        if (name == "signal") {
            int which = (int)arg(0) & 63;
            uint64_t previous = signal_handlers[which];
            signal_handlers[which] = arg(1);
            signal_flags[which] = 0;
            ret(previous);
            return true;
        }
        if ((name == "sigemptyset" || name == "sigfillset") && arg(0)) {
            uint8_t* set = ptr(arg(0), 8);
            if (set) std::memset(set, name == "sigfillset" ? 0xff : 0, 8);
        }
        ret(0);
        return true;
    }
    if (name == "getpagesize") {
        ret(4096);
        return true;
    }
    if (name == "syscall") {
        /* Only the few a runtime actually needs; anything else says so. */
        uint64_t which = arg(0);
        if (which == 178) ret(guest_tid()); /* gettid */
        else if (which == 124) ret(0);      /* sched_yield */
        else if (which == 98) {             /* futex */
            int op = (int)arg(2) & 0x7f;
            if (QB_ENV("QB_SYNC")) {
                uint8_t* w = ptr(arg(1), 4);
                uint32_t now = 0;
                if (w) std::memcpy(&now, w, 4);
                std::fprintf(stderr, "sync: t%llu futex op %d at %llx value %u arg %llu tp %llx lr %llx\n",
                             (unsigned long long)guest_tid(), op, (unsigned long long)arg(1), now,
                             (unsigned long long)arg(3), (unsigned long long)cpu.tpidr,
                             (unsigned long long)cpu.x[30]);
            }
            volatile uint32_t* word = reinterpret_cast<volatile uint32_t*>(ptr(arg(1), 4));
            if (!word) {
                ret((uint64_t)-14);
            } else if (op == 0 || op == 9) { /* WAIT, WAIT_BITSET */
                uint32_t expected = (uint32_t)arg(3);
                DWORD wait = INFINITE;
                int64_t deadline_ns = -1;
                if (arg(4)) {
                    uint8_t* spec = ptr(arg(4), 16);
                    int64_t seconds = 0, nanos = 0;
                    if (spec) {
                        std::memcpy(&seconds, spec, 8);
                        std::memcpy(&nanos, spec + 8, 8);
                    }
                    /* WAIT takes a relative time, WAIT_BITSET an absolute
                       one on the clock clock_gettime gives the guest (every
                       clock is guest_clock_ns here). Both become a deadline,
                       kept to the nanosecond: a runtime waiting a fraction
                       of a millisecond must not sleep a whole slice. */
                    int64_t span = seconds * 1000000000 + nanos;
                    deadline_ns = op == 9 ? span : guest_clock_ns() + span;
                    wait = 0; /* not INFINITE: the deadline decides */
                }
                std::atomic<uint64_t>& generation = futex_generation(word);
                uint64_t entered = generation.load(std::memory_order_acquire);
                if (*word != expected) {
                    ret((uint64_t)-11); /* EAGAIN */
                } else {
                    /* In slices, so a signal can interrupt the wait: EINTR. */
                    auto start = std::chrono::steady_clock::now();
                    uint64_t result = (uint64_t)-110; /* ETIMEDOUT */
                    /* First a short spin: an engine waiting on its job
                       workers is usually woken within microseconds, and a
                       sleep and a wake through the scheduler cost more than
                       that each time. QB_FUTEX_SPIN_US sets it (0: none). */
                    static const int64_t spin_us = QB_ENV("QB_FUTEX_SPIN_US") ? std::atoll(QB_ENV("QB_FUTEX_SPIN_US")) : 30;
                    if (spin_us > 0) {
                        LARGE_INTEGER frequency, now, until;
                        QueryPerformanceFrequency(&frequency);
                        QueryPerformanceCounter(&until);
                        until.QuadPart += frequency.QuadPart * spin_us / 1000000;
                        do {
                            for (int i = 0; i < 64; ++i) YieldProcessor();
                            if (*word != expected || generation.load(std::memory_order_acquire) != entered) {
                                result = 0;
                                break;
                            }
                            QueryPerformanceCounter(&now);
                        } while (now.QuadPart < until.QuadPart);
                    }
                    for (; result != 0;) {
                        DWORD slice = 10;
                        if (deadline_ns >= 0) {
                            int64_t remaining = deadline_ns - guest_clock_ns();
                            if (remaining <= 0) break; /* ETIMEDOUT */
                            if (remaining < 1000000) {
                                /* Under a millisecond: WaitOnAddress cannot
                                   time that, so a yielding spin does. */
                                YieldProcessor();
                                if (*word != expected || generation.load(std::memory_order_acquire) != entered) {
                                    result = 0;
                                    break;
                                }
                                continue;
                            }
                            slice = (DWORD)std::min<int64_t>(10, remaining / 1000000);
                        }
                        /* Sleep on the wake counter, not the word: a wake
                           that lands after `entered` was read has already
                           moved the counter, so no wake is ever lost, even
                           one that leaves the word itself unchanged. */
                        WaitOnAddress((volatile void*)&generation, &entered, 8, slice);
                        if (*word != expected || generation.load(std::memory_order_acquire) != entered) {
                            result = 0;
                            break;
                        }
                        if (poll_signals(cpu)) {
                            result = (uint64_t)-4;
                            break;
                        }

                    }
                    ret(result);
                    /* QB_FUTEX_STATS: where threads wait, how long in total, and
                       how the waits end; a table every ten seconds. */
                    static const bool stats = QB_ENV("QB_FUTEX_STATS") != nullptr;
                    if (stats) {
                        struct Site { double ms = 0; uint64_t waits = 0, timeouts = 0; };
                        static std::mutex stats_lock;
                        static std::unordered_map<uint64_t, Site> sites;
                        static auto last_print = std::chrono::steady_clock::now();
                        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                        std::lock_guard<std::mutex> held(stats_lock);
                        Site& site = sites[cpu.x[30]];
                        site.ms += ms;
                        ++site.waits;
                        if (result == (uint64_t)-110) ++site.timeouts;
                        if (std::chrono::steady_clock::now() - last_print > std::chrono::seconds(10)) {
                            last_print = std::chrono::steady_clock::now();
                            std::vector<std::pair<double, uint64_t>> order;
                            for (auto& entry : sites) order.push_back({entry.second.ms, entry.first});
                            std::sort(order.rbegin(), order.rend());
                            for (size_t i = 0; i < order.size() && i < 10; ++i) {
                                const Site& one = sites[order[i].second];
                                std::printf("futex: %9.0f ms %7llu waits %6llu timeouts, avg %.2f ms, from %s\n", one.ms,
                                            (unsigned long long)one.waits, (unsigned long long)one.timeouts,
                                            one.ms / (double)one.waits, guest_describe(order[i].second).c_str());
                            }
                            std::fflush(stdout);
                            sites.clear();
                        }
                    }
                }
            } else if (op == 1 || op == 10) { /* WAKE, WAKE_BITSET */
                /* The generation tells a waiter between two slices that it
                   was woken, even when the word itself never changed. */
                std::atomic<uint64_t>& generation = futex_generation(word);
                generation.fetch_add(1, std::memory_order_acq_rel);
                /* All: counters are shared between addresses, and the ones
                   whose word did not move just look again. */
                WakeByAddressAll((void*)&generation);
                ret(arg(3));
            } else {
                if (QB_ENV("QB_TRACE")) std::printf("guest: futex op %d\n", op);
                ret(0);
            }
        }
        else if (which == 278) {         /* getrandom */
            uint8_t* to = ptr(arg(1), arg(2));
            for (uint64_t i = 0; to && i < arg(2); ++i) to[i] = (uint8_t)(i * 37 + 11);
            ret(arg(2));
        } else {
            if (QB_ENV("QB_TRACE")) std::printf("guest: syscall %llu\n", (unsigned long long)which);
            ret(0);
        }
        return true;
    }
    if (name == "raise" || name == "kill") {
        std::printf("guest: the runtime raised signal %llu\n", (unsigned long long)arg(name == "raise" ? 0 : 1));
        std::fflush(stdout);
        ret(0);
        return true;
    }
    if (name == "getauxval") {
        /* AT_HWCAP says which optional instructions exist. Claim exactly what
           the interpreter does: FP, ASIMD and the LSE atomics. Claiming
           nothing made engines decide the device was too old to run them.
           AT_PAGESZ is real. */
        const uint64_t kFp = 1, kAsimd = 2, kAtomics = 1 << 8;
        ret(arg(0) == 6 ? 4096 : arg(0) == 16 ? (kFp | kAsimd | kAtomics) : 0);
        return true;
    }
    if (name == "__system_property_get") {
        /* The value is written into a buffer of PROP_VALUE_MAX (92) bytes. */
        const char* key = text(arg(0));
        const std::string& value = property(key ? key : "");
        uint8_t* out = ptr(arg(1), 92);
        size_t length = std::min<size_t>(value.size(), 91);
        if (out) {
            std::memcpy(out, value.data(), length);
            out[length] = 0;
        }
        if (QB_ENV("QB_TRACE")) std::printf("property: %s = %s\n", key ? key : "", value.c_str());
        ret(length);
        return true;
    }
    if (name == "__system_property_find") {
        /* A handle for a property that has a value: the name, kept as a
           guest string, which __system_property_read reads back. */
        const char* key = text(arg(0));
        std::string wanted = key ? key : "";
        uint64_t handle = 0;
        if (!property(wanted).empty()) {
            static std::mutex found_lock;
            static std::unordered_map<std::string, uint64_t> found;
            std::lock_guard<std::mutex> held(found_lock);
            uint64_t& slot = found[wanted];
            if (!slot) slot = guest_string(wanted);
            handle = slot;
        }
        ret(handle);
        return true;
    }
    if (name == "__system_property_read" || name == "__system_property_read_callback") {
        const char* key = text(arg(0));
        std::string property_name = key ? key : "";
        const std::string& value = property(property_name);
        if (name == "__system_property_read_callback") {
            /* callback(cookie, name, value, serial) */
            if (arg(1)) call_guest_args(arg(1), {arg(2), guest_string(property_name), guest_string(value), 0});
            ret(0);
            return true;
        }
        if (uint8_t* out = ptr(arg(1), 32)) {
            size_t length = std::min<size_t>(property_name.size(), 31);
            std::memcpy(out, property_name.data(), length);
            out[length] = 0;
        }
        size_t length = std::min<size_t>(value.size(), 91);
        if (uint8_t* out = ptr(arg(2), 92)) {
            std::memcpy(out, value.data(), length);
            out[length] = 0;
        }
        ret(length);
        return true;
    }
    if (name == "process_vm_readv" || name == "process_vm_writev") {
        /* Only ever this process here (guest memory is ours), so it is a
           copy between the two iovec lists, stopping at the first page that
           cannot be read, as the kernel does. */
        struct Iov {
            uint64_t base, length;
        };
        const Iov* local = reinterpret_cast<const Iov*>(ptr(arg(1), 16 * arg(2)));
        const Iov* remote = reinterpret_cast<const Iov*>(ptr(arg(3), 16 * arg(4)));
        bool reading = name == "process_vm_readv";
        uint64_t done = 0;
        size_t li = 0, ri = 0;
        uint64_t loff = 0, roff = 0;
        bool failed = false;
        while (local && remote && li < arg(2) && ri < arg(4)) {
            uint64_t chunk = std::min(local[li].length - loff, remote[ri].length - roff);
            if (chunk) {
                void* l = reinterpret_cast<void*>(local[li].base + loff);
                void* r = reinterpret_cast<void*>(remote[ri].base + roff);
                if (!(reading ? guarded_copy(l, r, (size_t)chunk) : guarded_copy(r, l, (size_t)chunk))) {
                    failed = true;
                    break;
                }
                done += chunk;
                loff += chunk;
                roff += chunk;
            }
            if (loff >= local[li].length) {
                ++li;
                loff = 0;
            }
            if (roff >= remote[ri].length) {
                ++ri;
                roff = 0;
            }
        }
        if (failed && done == 0) {
            set_errno(cpu, 14); /* EFAULT */
            ret((uint64_t)-1);
        } else {
            ret(done);
        }
        return true;
    }
    if (name == "clock_getres") {
        uint8_t* out = ptr(arg(1), 16);
        if (out) {
            int64_t seconds = 0, nanos = 1;
            std::memcpy(out, &seconds, 8);
            std::memcpy(out + 8, &nanos, 8);
        }
        ret(0);
        return true;
    }
    if (name == "time") {
        int64_t now = (int64_t)std::time(nullptr);
        if (arg(0)) {
            uint8_t* out = ptr(arg(0), 8);
            if (out) std::memcpy(out, &now, 8);
        }
        ret((uint64_t)now);
        return true;
    }
    if (name == "srand48" || name == "lrand48" || name == "mrand48" || name == "drand48" || name == "srand" ||
        name == "rand" || name == "random" || name == "srandom") {
        /* The 48-bit generator POSIX specifies, so sequences match bionic's. */
        static uint64_t state = 0x1234abcd330eull;
        const uint64_t a = 0x5deece66dull, c = 0xb, mask = (1ull << 48) - 1;
        if (name == "srand48" || name == "srand" || name == "srandom") {
            state = (((uint64_t)(uint32_t)arg(0)) << 16 | 0x330e) & mask;
            return true;
        }
        state = (a * state + c) & mask;
        if (name == "drand48") {
            double value = (double)state / (double)(1ull << 48);
            std::memcpy(&cpu.q[0].lo, &value, 8);
            cpu.q[0].hi = 0;
            return true;
        }
        if (name == "mrand48") ret((uint64_t)(int64_t)(int32_t)(uint32_t)(state >> 16));
        else ret((state >> 17) & 0x7fffffff);
        return true;
    }
    if (name == "uname") {
        /* Six fields of 65 bytes: system, node, release, version, machine,
           domain. */
        uint8_t* out = ptr(arg(0), 65 * 6);
        if (out) {
            std::memset(out, 0, 65 * 6);
            const char* fields[] = {"Linux", "localhost", "5.10.160-android12-9", "#1 SMP PREEMPT", "aarch64",
                                    "localdomain"};
            for (int i = 0; i < 6; ++i) std::memcpy(out + 65 * i, fields[i], std::strlen(fields[i]));
        }
        ret(0);
        return true;
    }
    if (name == "gethostname") {
        uint8_t* out = ptr(arg(0), arg(1));
        const char* host = "localhost";
        if (out && arg(1) > std::strlen(host)) std::memcpy(out, host, std::strlen(host) + 1);
        ret(0);
        return true;
    }
    if (name == "inet_pton") {
        /* Only the dotted IPv4 form is parsed; the rest say "not an address". */
        const char* textual = text(arg(1));
        unsigned parts[4];
        if ((int)arg(0) == 2 && textual &&
            std::sscanf(textual, "%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]) == 4) {
            uint8_t* out = ptr(arg(2), 4);
            for (int i = 0; out && i < 4; ++i) out[i] = (uint8_t)parts[i];
            ret(1);
        } else {
            ret(0);
        }
        return true;
    }
    if (name == "getpid") {
        ret(1);
        return true;
    }
    if (name == "gettid") {
        ret(guest_tid());
        return true;
    }
    if (name == "getcwd") {
        /* An Android app process runs with / as its working directory. */
        uint8_t* out = guest_ptr(mem, arg(0), 2);
        if (!out || arg(1) < 2) {
            set_errno(cpu, 34); /* ERANGE */
            ret(0);
            return true;
        }
        out[0] = '/';
        out[1] = 0;
        ret(arg(0));
        return true;
    }
    if (name == "clock_gettime") {
        /* Seconds and nanoseconds since this process started. */
        int64_t nanos = guest_clock_ns();
        uint8_t* out = ptr(arg(1), 16);
        if (out) {
            int64_t seconds = nanos / 1000000000;
            int64_t rest = nanos % 1000000000;
            std::memcpy(out, &seconds, 8);
            std::memcpy(out + 8, &rest, 8);
        }
        ret(0);
        return true;
    }
    if (name == "__errno") {
        /* One slot per thread, handed out from the heap the first time, and
           remembered per host thread since it is asked for constantly. */
        thread_local uint64_t cached_tp = 0, cached_slot = 0;
        if (cached_tp == cpu.tpidr && cached_slot) {
            ret(cached_slot);
            return true;
        }
        std::lock_guard<std::recursive_mutex> held(lock);
        uint64_t& slot = errno_slots[cpu.tpidr];
        if (!slot) {
            slot = guest_alloc(16);
            uint8_t* p = guest_ptr(mem, slot, 16);
            if (p) std::memset(p, 0, 16);
        }
        cached_tp = cpu.tpidr;
        cached_slot = slot;
        ret(slot);
        return true;
    }

    /* Maths that the compiler did not turn into an instruction. */
    static const struct {
        const char* name;
        int arity;
        double (*fn1)(double);
        double (*fn2)(double, double);
    } maths[] = {
        {"sin", 1, std::sin, nullptr},      {"cos", 1, std::cos, nullptr},    {"tan", 1, std::tan, nullptr},
        {"asin", 1, std::asin, nullptr},    {"acos", 1, std::acos, nullptr},  {"atan", 1, std::atan, nullptr},
        {"exp", 1, std::exp, nullptr},      {"log", 1, std::log, nullptr},    {"log2", 1, std::log2, nullptr},
        {"log10", 1, std::log10, nullptr},  {"sqrt", 1, std::sqrt, nullptr},  {"cbrt", 1, std::cbrt, nullptr},
        {"floor", 1, std::floor, nullptr},  {"ceil", 1, std::ceil, nullptr},  {"round", 1, std::round, nullptr},
        {"trunc", 1, std::trunc, nullptr},  {"fabs", 1, std::fabs, nullptr},  {"sinh", 1, std::sinh, nullptr},
        {"cosh", 1, std::cosh, nullptr},    {"tanh", 1, std::tanh, nullptr},
        {"pow", 2, nullptr, std::pow},      {"atan2", 2, nullptr, std::atan2}, {"fmod", 2, nullptr, std::fmod},
        {"hypot", 2, nullptr, std::hypot},  {"fmin", 2, nullptr, std::fmin},  {"fmax", 2, nullptr, std::fmax},
        {"exp2", 1, std::exp2, nullptr},    {"expm1", 1, std::expm1, nullptr}, {"log1p", 1, std::log1p, nullptr},
        {"asinh", 1, std::asinh, nullptr},  {"acosh", 1, std::acosh, nullptr}, {"atanh", 1, std::atanh, nullptr},
        {"rint", 1, std::rint, nullptr},    {"nearbyint", 1, std::nearbyint, nullptr},
        {"copysign", 2, nullptr, std::copysign}, {"remainder", 2, nullptr, std::remainder},
        {"fdim", 2, nullptr, std::fdim},
    };
    /* The ones with an integer or pointer in them, which the table above
       cannot carry. */
    {
        /* modf is the double one (modff the float); the trailing f is part of its name. */
        bool single = name.size() > 1 && name.back() == 'f' && name != "modf";
        std::string base = single ? name.substr(0, name.size() - 1) : name;
        auto read_value = [&]() {
            if (single) {
                float f;
                uint32_t b = (uint32_t)cpu.q[0].lo;
                std::memcpy(&f, &b, 4);
                return (double)f;
            }
            double d;
            std::memcpy(&d, &cpu.q[0].lo, 8);
            return d;
        };
        auto write_value = [&](double v) {
            if (single) {
                float f = (float)v;
                uint32_t b;
                std::memcpy(&b, &f, 4);
                cpu.q[0].lo = b;
            } else {
                std::memcpy(&cpu.q[0].lo, &v, 8);
            }
            cpu.q[0].hi = 0;
        };
        if (base == "ldexp" || base == "scalbn" || base == "scalbln") {
            write_value(std::ldexp(read_value(), (int)(int32_t)arg(0)));
            return true;
        }
        if (base == "frexp") {
            int exponent = 0;
            double fraction = std::frexp(read_value(), &exponent);
            uint8_t* out = ptr(arg(0), 4);
            if (out) std::memcpy(out, &exponent, 4);
            write_value(fraction);
            return true;
        }
        if (base == "modf") {
            double whole = 0;
            double fraction = std::modf(read_value(), &whole);
            if (single) {
                float w = (float)whole;
                uint8_t* out = ptr(arg(0), 4);
                if (out) std::memcpy(out, &w, 4);
            } else {
                uint8_t* out = ptr(arg(0), 8);
                if (out) std::memcpy(out, &whole, 8);
            }
            write_value(fraction);
            return true;
        }
        if (base == "lround" || base == "llround" || base == "lrint" || base == "llrint") {
            double v = read_value();
            ret((uint64_t)(int64_t)(base.find("round") != std::string::npos ? std::llround(v) : std::llrint(v)));
            return true;
        }
        if (base == "isnan" || base == "__isnan" || base == "isinf" || base == "__isinf" || base == "isfinite" ||
            base == "__isfinite" || base == "finite") {
            double v = read_value();
            bool r = base.find("nan") != std::string::npos   ? std::isnan(v)
                     : base.find("inf") != std::string::npos ? std::isinf(v)
                                                             : std::isfinite(v);
            ret(r ? 1 : 0);
            return true;
        }
    }
    {
        /* A trailing f means the single precision form, taking and returning
           its value in the low half of a vector register. */
        /* modf is the double one (modff the float); the trailing f is part of its name. */
        bool single = name.size() > 1 && name.back() == 'f' && name != "modf";
        std::string base = single ? name.substr(0, name.size() - 1) : name;
        for (const auto& entry : maths) {
            if (base != entry.name) continue;
            double a = 0;
            double b = 0;
            if (single) {
                float fa, fb;
                uint32_t ba = (uint32_t)cpu.q[0].lo, bb = (uint32_t)cpu.q[1].lo;
                std::memcpy(&fa, &ba, 4);
                std::memcpy(&fb, &bb, 4);
                a = fa;
                b = fb;
            } else {
                std::memcpy(&a, &cpu.q[0].lo, 8);
                std::memcpy(&b, &cpu.q[1].lo, 8);
            }
            double result = entry.arity == 1 ? entry.fn1(a) : entry.fn2(a, b);
            if (single) {
                float single_result = (float)result;
                uint32_t bits;
                std::memcpy(&bits, &single_result, 4);
                cpu.q[0].lo = bits;
            } else {
                uint64_t bits;
                std::memcpy(&bits, &result, 8);
                cpu.q[0].lo = bits;
            }
            cpu.q[0].hi = 0;
            return true;
        }
    }

    return false;
}

/* Files. A guest path is looked up under QB_ROOT (the unpacked application
   and its data), so /data/app/.../base.apk and friends resolve to real host
   files without the guest ever knowing. Descriptors share the numbering the
   pipes use, which keeps read and close routed by a single table lookup. */
/* A thread id for the calling host thread: the first thread asked is the
   process's main thread, and has the same id as the process. */
uint64_t GuestLibc::guest_tid() {
    static std::atomic<uint64_t> next{1};
    thread_local uint64_t mine = next++;
    return mine;
}

bool GuestLibc::file_call(const std::string& name, GuestCpu& cpu) {
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    auto fail = [&](int error) {
        std::lock_guard<std::recursive_mutex> held(lock);
        uint64_t slot = errno_slots[cpu.tpidr];
        if (slot) {
            uint8_t* p = guest_ptr(mem, slot, 4);
            if (p) std::memcpy(p, &error, 4);
        }
        ret((uint64_t)-1);
        return true;
    };
    auto host_path = [&](uint64_t va) {
        const char* guest = reinterpret_cast<const char*>(guest_ptr(mem, va, 1));
        std::string path = guest ? guest : "";
        const char* root = QB_ENV("QB_ROOT");
        if (QB_ENV("QB_TRACE") || QB_ENV("QB_TRACE_FILES")) {
            std::printf("file: %s\n", path.c_str());
            std::fflush(stdout);
        }
        /* An empty path names nothing (ENOENT on Android); joined to the
           root it would name the root folder, which exists. */
        if (path.empty()) return std::string();
        std::string host = std::string(root ? root : ".") + path;
        /* QB_APK_LOOSE=1: a path inside the APK (base.apk/assets/...) names
           the extracted copy in QB_ROOT/apk when there is one, so Unity reads
           it as an ordinary file instead of through its own zip reader. */
        static const bool apk_loose = QB_ENV("QB_APK_LOOSE") != nullptr;
        size_t inside = apk_loose ? path.find("/base.apk/") : std::string::npos;
        if (inside != std::string::npos) {
            std::string loose = std::string(root ? root : ".") + "/apk/" + path.substr(inside + 10);
            if (GetFileAttributesA(loose.c_str()) != INVALID_FILE_ATTRIBUTES) return loose;
        }
        /* /proc/self/maps is written fresh for each open, from what is
           really loaded, the way the kernel would: code that reads it to find
           its libraries (crash reporters, plugins) indexes into what it
           finds. One file per thread, so two readers never share one. */
        if (path == "/proc/self/maps" || path == "/proc/" + std::to_string(GetCurrentProcessId()) + "/maps") {
            host = std::string(root ? root : ".") + "/proc/self/maps." + std::to_string(GetCurrentThreadId());
            if (FILE* out = std::fopen(host.c_str(), "wb")) {
                const std::string lib_dir_text = std::string("/data/app/") + guest_package() + "/lib/arm64/";
                const char* lib_dir = lib_dir_text.c_str();
                for (GuestObject* object : image->linker.objects) {
                    for (uint16_t i = 0; i < object->program_header_count; ++i) {
                        const uint8_t* ph = object->program_headers.data() + 56 * (size_t)i;
                        uint32_t type = 0, flags = 0;
                        uint64_t offset = 0, vaddr = 0, memsz = 0;
                        std::memcpy(&type, ph, 4);
                        std::memcpy(&flags, ph + 4, 4);
                        std::memcpy(&offset, ph + 8, 8);
                        std::memcpy(&vaddr, ph + 16, 8);
                        std::memcpy(&memsz, ph + 40, 8);
                        if (type != 1 || !memsz) continue; /* PT_LOAD */
                        uint64_t start = (object->base + vaddr) & ~0xfffull;
                        uint64_t end = (object->base + vaddr + memsz + 0xfff) & ~0xfffull;
                        std::fprintf(out, "%llx-%llx %c%c%cp %08llx fd:04 %u %s%s\n", (unsigned long long)start,
                                     (unsigned long long)end, (flags & 4) ? 'r' : '-', (flags & 2) ? 'w' : '-',
                                     (flags & 1) ? 'x' : '-', (unsigned long long)(offset & ~0xfffull), 1000 + i,
                                     lib_dir, object->name.c_str());
                    }
                }
                std::fprintf(out, "7e000000-7f000000 rw-p 00000000 00:00 0 [stack]\n");
                std::fclose(out);
            }
        }
        /* QB_CPUS=N: the CPU files say N cores instead of the Quest's 6
           (extra cpuN directories are copies of cpu5), so the engine sizes
           its job workers to the host. Written once per process. */
        static const int cpus = QB_ENV("QB_CPUS") ? std::max(1, std::atoi(QB_ENV("QB_CPUS"))) : 0;
        if (cpus) {
            const std::string base = std::string(root ? root : ".");
            const std::string sys = "/sys/devices/system/cpu/";
            if (path == sys + "online" || path == sys + "possible" || path == sys + "present" ||
                path == "/proc/cpuinfo") {
                std::string name = path == "/proc/cpuinfo" ? "cpuinfo" : path.substr(sys.size());
                host = base + "/qb_cpus_" + std::to_string(cpus) + "_" + name;
                static std::mutex made_lock;
                std::lock_guard<std::mutex> held(made_lock);
                if (GetFileAttributesA(host.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    if (FILE* out = std::fopen(host.c_str(), "wb")) {
                        if (name == "cpuinfo") {
                            std::string one;
                            if (FILE* in = std::fopen((base + "/proc/cpuinfo").c_str(), "rb")) {
                                char buffer[4096];
                                size_t got = std::fread(buffer, 1, sizeof(buffer), in);
                                std::fclose(in);
                                std::string all(buffer, got);
                                size_t end = all.find("\n\n");
                                one = all.substr(0, end == std::string::npos ? all.size() : end);
                                one = one.substr(one.find('\n') + 1); /* without "processor : 0" */
                            }
                            for (int i = 0; i < cpus; ++i) std::fprintf(out, "processor\t: %d\n%s\n\n", i, one.c_str());
                        } else {
                            std::fprintf(out, "0-%d\n", cpus - 1);
                        }
                        std::fclose(out);
                    }
                }
            } else if (path.compare(0, sys.size() + 3, sys + "cpu") == 0) {
                size_t digits = sys.size() + 3, stop = digits;
                while (stop < path.size() && std::isdigit((unsigned char)path[stop])) ++stop;
                if (stop > digits) {
                    int n = std::atoi(path.substr(digits, stop - digits).c_str());
                    if (n >= 6 && n < cpus) host = base + sys + "cpu5" + path.substr(stop);
                }
            }
        }
        return host;
    };
    auto stat_into = [&](uint64_t va, int64_t size, bool directory) {
        uint8_t* p = guest_ptr(mem, va, 128);
        if (!p) return;
        std::memset(p, 0, 128);
        uint32_t mode = directory ? 0040755 : 0100644;
        int32_t block = 4096;
        int64_t blocks = (size + 511) / 512;
        std::memcpy(p + 16, &mode, 4);
        std::memcpy(p + 48, &size, 8);
        std::memcpy(p + 56, &block, 4);
        std::memcpy(p + 64, &blocks, 8);
    };
    auto host_file = [&](int fd) -> FILE* {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = files.find(fd);
        return found == files.end() ? nullptr : found->second;
    };

    if (name == "open" || name == "open64" || name == "openat" || name == "__open_2" || name == "__openat_2") {
        bool at = name.find("at") != std::string::npos;
        const char* guest_path = reinterpret_cast<const char*>(guest_ptr(mem, arg(at ? 1 : 0), 1));
        if (guest_path && (std::strcmp(guest_path, "/dev/urandom") == 0 || std::strcmp(guest_path, "/dev/random") == 0)) {
            /* The kernel's random devices: reads give fresh random bytes. */
            std::lock_guard<std::recursive_mutex> held(lock);
            int fd = next_fd++;
            random_fds.insert(fd);
            ret((uint64_t)fd);
            return true;
        }
        std::string path = host_path(arg(at ? 1 : 0));
        int flags = (int)arg(at ? 2 : 1);
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return fail(21);
        const char* how = (flags & 3) == 0 ? "rb" : ((flags & 0x40) ? ((flags & 0x200) ? "w+b" : "a+b") : "r+b");
        FILE* file = std::fopen(path.c_str(), how);
        if (!file) return fail(2);
        std::lock_guard<std::recursive_mutex> held(lock);
        int fd = next_fd++;
        files[fd] = file;
        ret((uint64_t)fd);
        return true;
    }
    if (name == "stat" || name == "lstat" || name == "stat64" || name == "fstatat" || name == "newfstatat" ||
        name == "access" || name == "faccessat") {
        bool at = name.find("at") != std::string::npos && name != "stat" && name != "stat64";
        std::string path = host_path(arg(at ? 1 : 0));
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return fail(2);
        if (name.find("access") == std::string::npos) {
            int64_t size = ((int64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            stat_into(arg(at ? 2 : 1), size, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        }
        ret(0);
        return true;
    }
    if (name == "fstat" || name == "fstat64") {
        FILE* file = host_file((int)arg(0));
        if (!file) {
            /* A pipe, or one of the standard streams. */
            stat_into(arg(1), 0, false);
            ret(0);
            return true;
        }
        int64_t here = _ftelli64(file);
        _fseeki64(file, 0, SEEK_END);
        int64_t size = _ftelli64(file);
        _fseeki64(file, here, SEEK_SET);
        stat_into(arg(1), size, false);
        ret(0);
        return true;
    }
    if (name == "dup" || name == "dup2" || name == "dup3") {
        FILE* file = host_file((int)arg(0));
        if (!file) return fail(9);
        std::lock_guard<std::recursive_mutex> held(lock);
        int fd = name == "dup" ? next_fd++ : (int)arg(1);
        files[fd] = file;
        shared_files.insert(fd);
        ret((uint64_t)fd);
        return true;
    }
    if (name == "ftruncate" || name == "ftruncate64") {
        FILE* file = host_file((int)arg(0));
        if (!file) return fail(9);
        std::fflush(file);
        if (_chsize_s(_fileno(file), (int64_t)arg(1)) != 0) return fail(22);
        ret(0);
        return true;
    }
    if (name == "lseek" || name == "lseek64") {
        FILE* file = host_file((int)arg(0));
        if (!file) return fail(29);
        if (_fseeki64(file, (int64_t)arg(1), (int)arg(2)) != 0) return fail(22);
        ret((uint64_t)_ftelli64(file));
        return true;
    }
    if (name == "read" || name == "pread" || name == "pread64" || name == "write" || name == "pwrite" ||
        name == "pwrite64") {
        bool random = false;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            random = random_fds.count((int)arg(0)) != 0;
        }
        if (random) {
            uint8_t* buffer = guest_ptr(mem, arg(1), arg(2));
            if (name.find("read") != std::string::npos && buffer && arg(2))
                BCryptGenRandom(nullptr, buffer, (ULONG)arg(2), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            ret(arg(2));
            return true;
        }
        FILE* file = host_file((int)arg(0));
        if (!file) {
            if (name == "write" && (arg(0) == 1 || arg(0) == 2)) {
                uint8_t* from = guest_ptr(mem, arg(1), arg(2));
                if (from) std::fwrite(from, 1, (size_t)arg(2), stdout);
                ret(arg(2));
                return true;
            }
            return false; /* a pipe: the activity layer has it */
        }
        bool positioned = name[0] == 'p';
        uint8_t* buffer = guest_ptr(mem, arg(1), arg(2));
        if (!buffer && arg(2)) return fail(14);
        int64_t here = _ftelli64(file);
        if (positioned) _fseeki64(file, (int64_t)arg(3), SEEK_SET);
        else _fseeki64(file, here, SEEK_SET); /* switching between reading and writing */
        size_t done = name.find("read") != std::string::npos ? std::fread(buffer, 1, (size_t)arg(2), file)
                                                              : std::fwrite(buffer, 1, (size_t)arg(2), file);
        if (positioned) _fseeki64(file, here, SEEK_SET);
        ret(done);
        return true;
    }
    if (name == "close") {
        std::lock_guard<std::recursive_mutex> held(lock);
        if (random_fds.erase((int)arg(0))) {
            ret(0);
            return true;
        }
        auto found = files.find((int)arg(0));
        if (found == files.end()) return false;
        FILE* file = found->second;
        files.erase(found);
        shared_files.erase((int)arg(0));
        bool still_used = false;
        for (const auto& other : files) still_used = still_used || other.second == file;
        if (!still_used) std::fclose(file);
        ret(0);
        return true;
    }
    /* Standard streams over the same descriptors. The guest's FILE is a
       small block of its own memory whose first word is the descriptor. */
    auto stream = [&](uint64_t va) -> FILE* {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = streams.find(va);
        if (found == streams.end()) return nullptr;
        auto file = files.find(found->second);
        return file == files.end() ? nullptr : file->second;
    };
    if (name == "fopen" || name == "fopen64" || name == "fdopen") {
        FILE* file = nullptr;
        int fd = -1;
        if (name == "fdopen") {
            fd = (int)arg(0);
            file = host_file(fd);
            if (!file) return fail(9);
        } else {
            std::string path = host_path(arg(0));
            const char* mode = reinterpret_cast<const char*>(guest_ptr(mem, arg(1), 1));
            std::string how = mode ? mode : "r";
            /* bionic's 'e' (close on exec) means nothing here, and the MSVC
               runtime treats any letter it does not know as an invalid
               parameter, which ends the process. */
            how.erase(std::remove(how.begin(), how.end(), 'e'), how.end());
            if (how.find('b') == std::string::npos) how += "b";
            DWORD attributes = GetFileAttributesA(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                ret(0);
                fail(21);
                ret(0);
                return true;
            }
            file = std::fopen(path.c_str(), how.c_str());
            if (!file) {
                fail(2);
                ret(0);
                return true;
            }
        }
        std::lock_guard<std::recursive_mutex> held(lock);
        if (fd < 0) {
            fd = next_fd++;
            files[fd] = file;
        }
        uint64_t va = guest_alloc(64);
        uint8_t* p = guest_ptr(mem, va, 64);
        if (p) {
            std::memset(p, 0, 64);
            std::memcpy(p, &fd, 4);
        }
        streams[va] = fd;
        ret(va);
        return true;
    }
    if (name == "fclose") {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = streams.find(arg(0));
        if (found == streams.end()) return false;
        auto file = files.find(found->second);
        if (file != files.end()) {
            std::fclose(file->second);
            files.erase(file);
        }
        streams.erase(found);
        ret(0);
        return true;
    }
    if (name == "fputs" || name == "fputc" || name == "putc") {
        FILE* file = stream(arg(1));
        if (!file) {
            ret((uint64_t)-1);
            return true;
        }
        if (name == "fputs") {
            const char* s = reinterpret_cast<const char*>(guest_ptr(mem, arg(0), 1));
            if (s) std::fputs(s, file);
        } else {
            std::fputc((int)arg(0), file);
        }
        if (file == stdout || file == stderr) std::fflush(file);
        ret(name == "fputs" ? 0 : (arg(0) & 0xff));
        return true;
    }
    if (name == "fprintf" || name == "__fprintf_chk") {
        /* fprintf(stream, format, ...), or __fprintf_chk(stream, flag, format, ...). */
        bool checked = name == "__fprintf_chk";
        const char* format = reinterpret_cast<const char*>(guest_ptr(mem, arg(checked ? 2 : 1), 1));
        std::string result = format ? format_from_guest(format, cpu, checked ? 3 : 2, 0) : std::string();
        FILE* file = stream(arg(0));
        if (!file) {
            ret((uint64_t)-1);
            return true;
        }
        std::fwrite(result.data(), 1, result.size(), file);
        if (file == stdout || file == stderr) std::fflush(file);
        ret(result.size());
        return true;
    }
    if (name == "fscanf" || name == "sscanf") {
        /* One conversion at a time through the host's own scanf, each with a
           trailing %n to learn whether it matched. Guest pointers are host
           pointers, so results land where the guest wants them; only the
           sizes differ: bionic's long is 64 bits and Windows' is 32, so l, z,
           j and t become ll. Arguments after the eighth are on the stack. */
        FILE* file = nullptr;
        const char* text = nullptr;
        if (name == "fscanf") {
            file = stream(arg(0));
            if (!file) {
                ret((uint64_t)-1);
                return true;
            }
        } else {
            text = reinterpret_cast<const char*>(guest_ptr(mem, arg(0), 1));
            if (!text) {
                ret((uint64_t)-1);
                return true;
            }
        }
        const char* format = reinterpret_cast<const char*>(guest_ptr(mem, arg(1), 1));
        if (!format) {
            ret((uint64_t)-1);
            return true;
        }
        auto vararg = [&](int index) -> uint64_t {
            if (index < 8) return cpu.x[index];
            uint64_t value = 0;
            std::memcpy(&value, reinterpret_cast<void*>(cpu.sp + 8ull * (uint64_t)(index - 8)), 8);
            return value;
        };
        int next = 2, assigned = 0;
        size_t consumed = 0;
        const char* at = format;
        bool input_failed = false;
        while (*at) {
            /* A piece: literal text and at most one conversion. */
            std::string piece;
            while (*at && *at != '%') piece += *at++;
            bool suppressed = false, converts = false;
            if (*at == '%') {
                const char* start = at++;
                if (*at == '%') {
                    piece += "%%";
                    ++at;
                } else {
                    std::string spec = "%";
                    if (*at == '*') {
                        suppressed = true;
                        spec += *at++;
                    }
                    while (*at >= '0' && *at <= '9') spec += *at++;
                    std::string length;
                    while (*at == 'h' || *at == 'l' || *at == 'z' || *at == 'j' || *at == 't' || *at == 'L' || *at == 'q')
                        length += *at++;
                    char kind = *at ? *at++ : 0;
                    if (!kind) break;
                    bool integer = std::strchr("diouxXn", kind) != nullptr;
                    if (integer && (length == "l" || length == "z" || length == "j" || length == "t" || length == "q"))
                        length = "ll";
                    if (!integer && (kind == 'f' || kind == 'e' || kind == 'g' || kind == 'a' || kind == 'E' ||
                                     kind == 'G' || kind == 'F' || kind == 'A') && length == "L")
                        length = "l"; /* no 128-bit long double here */
                    spec += length;
                    spec += kind;
                    if (kind == '[') {
                        if (*at == '^') spec += *at++;
                        if (*at == ']') spec += *at++;
                        while (*at && *at != ']') spec += *at++;
                        if (*at == ']') spec += *at++;
                    }
                    (void)start;
                    piece += spec;
                    converts = kind != 'n';
                    if (kind == 'n' && !suppressed) {
                        /* %n: what has been consumed so far, written now. */
                        piece.resize(piece.size() - spec.size());
                        uint64_t where = vararg(next++);
                        long long so_far = file ? (long long)_ftelli64(file) : (long long)consumed;
                        if (where) {
                            if (length == "ll") std::memcpy(reinterpret_cast<void*>(where), &so_far, 8);
                            else {
                                int small = (int)so_far;
                                std::memcpy(reinterpret_cast<void*>(where), &small, 4);
                            }
                        }
                    }
                }
            }
            if (piece.empty()) continue;
            piece += "%n";
            int used = -1, got = 0;
            void* target = (converts && !suppressed) ? reinterpret_cast<void*>(vararg(next++)) : nullptr;
            if (file) {
                got = target ? std::fscanf(file, piece.c_str(), target, &used) : std::fscanf(file, piece.c_str(), &used);
            } else {
                got = target ? std::sscanf(text + consumed, piece.c_str(), target, &used)
                             : std::sscanf(text + consumed, piece.c_str(), &used);
                if (used >= 0) consumed += (size_t)used;
            }
            if (used < 0) {
                input_failed = got == EOF;
                break;
            }
            if (target) ++assigned;
        }
        ret(input_failed && assigned == 0 ? (uint64_t)-1 : (uint64_t)assigned);
        return true;
    }
    if (name == "fread" || name == "fwrite") {
        FILE* file = stream(arg(3));
        if (!file) return false;
        uint64_t bytes = arg(1) * arg(2);
        uint8_t* buffer = guest_ptr(mem, arg(0), bytes);
        if (!buffer || !arg(1)) {
            ret(0);
            return true;
        }
        size_t done = name == "fread" ? std::fread(buffer, (size_t)arg(1), (size_t)arg(2), file)
                                      : std::fwrite(buffer, (size_t)arg(1), (size_t)arg(2), file);
        ret(done);
        return true;
    }
    if (name == "fseek" || name == "fseeko" || name == "fseeko64") {
        FILE* file = stream(arg(0));
        if (!file) return false;
        ret(_fseeki64(file, (int64_t)arg(1), (int)arg(2)) == 0 ? 0 : (uint64_t)-1);
        return true;
    }
    if (name == "ftell" || name == "ftello" || name == "ftello64") {
        FILE* file = stream(arg(0));
        if (!file) return false;
        ret((uint64_t)_ftelli64(file));
        return true;
    }
    if (name == "rewind") {
        FILE* file = stream(arg(0));
        if (file) std::rewind(file);
        return file != nullptr;
    }
    if (name == "feof" || name == "ferror" || name == "fileno" || name == "fflush" || name == "clearerr") {
        FILE* file = stream(arg(0));
        if (!file) {
            if (name == "fflush" || name == "clearerr") {
                ret(0);
                return true;
            }
            return false;
        }
        if (name == "feof") ret(std::feof(file) ? 1 : 0);
        else if (name == "ferror") ret(std::ferror(file) ? 1 : 0);
        else if (name == "fileno") ret(streams[arg(0)]);
        else if (name == "fflush") ret(std::fflush(file));
        else {
            std::clearerr(file);
            ret(0);
        }
        return true;
    }
    if (name == "fgets") {
        FILE* file = stream(arg(2));
        if (!file) return false;
        char* buffer = reinterpret_cast<char*>(guest_ptr(mem, arg(0), arg(1)));
        ret(buffer && std::fgets(buffer, (int)arg(1), file) ? arg(0) : 0);
        return true;
    }
    if (name == "fgetc" || name == "getc") {
        FILE* file = stream(arg(0));
        if (!file) return false;
        ret((uint64_t)(int64_t)std::fgetc(file));
        return true;
    }
    /* Directories. The listing is taken whole at opendir, which is what a
       snapshot of a directory nobody else is writing to looks like anyway.
       The DIR the guest holds is a block of its memory that also carries the
       dirent readdir hands back, laid out the way bionic's is. */
    if (name == "opendir" || name == "fdopendir") {
        if (name == "fdopendir") return fail(95);
        std::string path = host_path(arg(0));
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA((path + "\\*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) {
            fail(2);
            ret(0);
            return true;
        }
        GuestDir listing;
        do {
            listing.names.push_back(found.cFileName);
            listing.directories.push_back((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFileA(search, &found));
        FindClose(search);
        uint64_t va = guest_alloc(512);
        std::lock_guard<std::recursive_mutex> held(lock);
        directories[va] = std::move(listing);
        ret(va);
        return true;
    }
    if (name == "readdir" || name == "readdir64") {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = directories.find(arg(0));
        if (found == directories.end() || found->second.next >= found->second.names.size()) {
            ret(0);
            return true;
        }
        GuestDir& listing = found->second;
        size_t index = listing.next++;
        uint8_t* entry = guest_ptr(mem, arg(0), 512);
        if (!entry) {
            ret(0);
            return true;
        }
        std::memset(entry, 0, 280);
        uint64_t inode = index + 1, offset = index + 1;
        uint16_t length = 280;
        uint8_t type = listing.directories[index] ? 4 /* DT_DIR */ : 8 /* DT_REG */;
        std::memcpy(entry, &inode, 8);
        std::memcpy(entry + 8, &offset, 8);
        std::memcpy(entry + 16, &length, 2);
        entry[18] = type;
        const std::string& file = listing.names[index];
        std::memcpy(entry + 19, file.c_str(), std::min<size_t>(file.size(), 255));
        ret(arg(0));
        return true;
    }
    if (name == "closedir") {
        std::lock_guard<std::recursive_mutex> held(lock);
        directories.erase(arg(0));
        ret(0);
        return true;
    }
    if (name == "rewinddir") {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = directories.find(arg(0));
        if (found != directories.end()) found->second.next = 0;
        ret(0);
        return true;
    }
    if (name == "rename" || name == "renameat") {
        bool at = name == "renameat";
        std::string from = host_path(arg(at ? 1 : 0));
        std::string to = host_path(arg(at ? 3 : 1));
        if (!MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) return fail(2);
        ret(0);
        return true;
    }
    if (name == "mkdir" || name == "mkdirat") {
        std::string path = host_path(arg(name == "mkdirat" ? 1 : 0));
        if (!CreateDirectoryA(path.c_str(), nullptr))
            return fail(GetLastError() == ERROR_ALREADY_EXISTS ? 17 : 2);
        ret(0);
        return true;
    }
    if (name == "unlink" || name == "remove" || name == "rmdir") {
        std::string path = host_path(arg(0));
        bool ok = name == "rmdir" ? RemoveDirectoryA(path.c_str()) != 0 : DeleteFileA(path.c_str()) != 0;
        if (!ok) return fail(2);
        ret(0);
        return true;
    }
    if (name == "fsync" || name == "fdatasync" || name == "flock") {
        ret(0);
        return true;
    }
    return false;
}

namespace {
GuestLibc* g_signal_libc = nullptr;
}

/* Runs a handler on the interrupted thread's own stack, the way the kernel
   would: a siginfo and a ucontext holding every register go below the
   interrupted stack pointer, so a garbage collector scanning the stack from
   inside the handler sees the values the thread was holding in registers. */
void GuestLibc::deliver_signal(GuestCpu& cpu, int signal) {
    uint64_t handler = signal_handlers[signal];
    if (QB_ENV("QB_SIGNALS"))
        std::fprintf(stderr, "signal: %d to tp %llx at pc %s sp %llx, handler %s flags %llx\n", signal,
                     (unsigned long long)cpu.tpidr, guest_describe(cpu.pc).c_str(), (unsigned long long)cpu.sp,
                     guest_describe(handler).c_str(), (unsigned long long)signal_flags[signal]);
    if (handler <= 1) return; /* SIG_DFL and SIG_IGN: nothing to run */
    const uint64_t kFrame = 0x1400;
    uint64_t frame = ((cpu.sp - 256 - kFrame) & ~0xfull);
    uint8_t* p = guest_ptr(image->mem, frame, kFrame);
    if (!p) return;
    std::memset(p, 0, kFrame);
    uint64_t info = frame;
    uint64_t context = frame + 128;
    int32_t signo = signal;
    std::memcpy(p, &signo, 4);
    /* ucontext: flags, link, stack (24), sigmask (8), padding to 176, then
       mcontext: fault address, x0..x30, sp, pc, pstate. */
    uint8_t* mc = p + 128 + 176;
    std::memcpy(mc + 8, cpu.x, 31 * 8);
    std::memcpy(mc + 8 + 31 * 8, &cpu.sp, 8);
    std::memcpy(mc + 8 + 32 * 8, &cpu.pc, 8);
    GuestCpu local = cpu;
    local.sp = frame;
    local.x[0] = (uint64_t)signal;
    local.x[1] = info;
    local.x[2] = context;
    local.x[30] = 1;
    local.pc = handler;
    guest_run(local, image->mem, image->callback, image->callback_user, 0);
}

/* SIGILL for an undefined instruction, delivered in place: the thread
   itself goes on into the handler with the kernel's frame below its stack,
   so a handler that siglongjmps (OpenSSL's CPU probes do) simply lands at
   its sigsetjmp. One that returns comes back through qb_sigreturn. */
bool GuestLibc::deliver_sigill(GuestCpu& cpu) {
    const int kSigill = 4;
    uint64_t handler = signal_handlers[kSigill];
    if (handler <= 1) return false; /* no handler: stop, as before */
    const uint64_t kFrame = 0x1400;
    uint64_t frame = ((cpu.sp - 256 - kFrame) & ~0xfull);
    uint8_t* p = guest_ptr(image->mem, frame, kFrame);
    if (!p) return false;
    std::memset(p, 0, kFrame);
    int32_t signo = kSigill, code = 1; /* ILL_ILLOPC */
    std::memcpy(p, &signo, 4);
    std::memcpy(p + 8, &code, 4);
    std::memcpy(p + 16, &cpu.pc, 8); /* si_addr */
    uint8_t* mc = p + 128 + 176;
    std::memcpy(mc, &cpu.pc, 8); /* fault address */
    std::memcpy(mc + 8, cpu.x, 31 * 8);
    std::memcpy(mc + 8 + 31 * 8, &cpu.sp, 8);
    std::memcpy(mc + 8 + 32 * 8, &cpu.pc, 8);
    if (QB_ENV("QB_SIGNALS"))
        std::fprintf(stderr, "signal: SIGILL at %s to handler %s\n", guest_describe(cpu.pc).c_str(),
                     guest_describe(handler).c_str());
    cpu.sp = frame;
    cpu.x[0] = kSigill;
    cpu.x[1] = frame;
    cpu.x[2] = frame + 128;
    cpu.x[30] = image->linker.thunk_for("qb_sigreturn");
    cpu.pc = handler;
    return true;
}

bool GuestLibc::poll_signals(GuestCpu& cpu) {
    uint64_t pending = guest_take_pending(cpu.tpidr);
    for (int signal = 1; signal < 64; ++signal)
        if (pending & (1ull << signal)) deliver_signal(cpu, signal);
    return pending != 0;
}

void GuestLibc::install_signal_hook() {
    g_signal_libc = this;
    guest_set_describe_hook([](uint64_t address) -> std::string {
        char text[160];
        for (GuestObject* object : g_signal_libc->image->linker.objects)
            if (address >= object->base && address < object->base + object->image.size()) {
                std::snprintf(text, sizeof(text), "%s+%llx", object->name.c_str(),
                              (unsigned long long)(address - object->base));
                return text;
            }
        std::snprintf(text, sizeof(text), "%llx", (unsigned long long)address);
        return text;
    });
    guest_set_undefined_hook([](GuestCpu& cpu) { return g_signal_libc->deliver_sigill(cpu); });
    guest_set_signal_hook([](GuestCpu& cpu, uint64_t pending) {
        for (int signal = 1; signal < 64; ++signal)
            if (pending & (1ull << signal)) g_signal_libc->deliver_signal(cpu, signal);
    });
}

void GuestLibc::set_errno(GuestCpu& cpu, int value) {
    std::lock_guard<std::recursive_mutex> held(lock);
    uint64_t& slot = errno_slots[cpu.tpidr];
    if (!slot) slot = guest_alloc(16);
    uint8_t* p = guest_ptr(image->mem, slot, 4);
    if (p) std::memcpy(p, &value, 4);
}

const std::string& GuestLibc::property(const std::string& key) {
    static const std::string empty;
    if (!properties_loaded) {
        properties_loaded = true;
        const char* root = QB_ENV("QB_ROOT");
        std::string path = std::string(root ? root : ".") + "/props.txt";
        if (FILE* file = std::fopen(path.c_str(), "rb")) {
            char line[4096];
            while (std::fgets(line, sizeof(line), file)) {
                /* [key]: [value] */
                char* key_end = std::strstr(line, "]: [");
                if (line[0] != '[' || !key_end) continue;
                std::string name(line + 1, key_end);
                char* value = key_end + 4;
                char* value_end = std::strrchr(value, ']');
                if (!value_end) continue;
                properties[name] = std::string(value, value_end);
            }
            std::fclose(file);
        }
    }
    auto found = properties.find(key);
    return found == properties.end() ? empty : found->second;
}

/* Opens a library the way the device would: by its file name, from the
   application's library directory, with its dependencies, relocations and
   initialisers, and, the first time, its JNI_OnLoad. On the device the
   application's Java side System.loadLibrary's its libraries and ART calls
   JNI_OnLoad then; native code opening the same file later gets the one that
   is already there. Returns a handle: which object it is, one based. */
/* The one clock every guest clock reads: nanoseconds since this process
   started. Locks' timed waits compare their deadlines against it too. */
int64_t guest_clock_ns() {
    static const auto begin = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
}

uint64_t GuestLibc::open_library(const std::string& wanted) {
    std::string soname = wanted.substr(wanted.find_last_of("/\\") + 1);
    auto handle_of = [&]() -> uint64_t {
        for (size_t i = 0; i < image->linker.objects.size(); ++i)
            if (image->linker.objects[i]->name == soname) return i + 1;
        return 0;
    };
    uint64_t handle = handle_of();
    if (!handle) {
        if (image->linker.is_system(soname)) return 0xd10000; /* a library we answer ourselves */
        size_t before = image->linker.objects.size();
        bool ok = false;
        for (const std::string& where : image->linker.search) {
            std::string candidate = where + "/" + soname;
            if (image->linker.load(candidate.c_str(), image->mem)) {
                ok = true;
                break;
            }
        }
        if (!ok || image->linker.objects.size() == before) return 0;
        image->linker.relocate(image->mem);
        /* Whatever arrived has to be set up before anything calls into it,
           dependencies first. */
        for (size_t i = image->linker.objects.size(); i > before; --i)
            for (uint64_t entry : image->linker.objects[i - 1]->initialisers) call_guest(entry, 0, 0);
        handle = handle_of();
    }
    if (!handle) return 0;
    GuestObject* object = image->linker.objects[(size_t)handle - 1];
    if (object->jni_onload && !object->jni_onload_done) {
        object->jni_onload_done = true;
        uint64_t version = call_guest(object->jni_onload, vm_va, 0);
        std::printf("library: %s at %llx JNI_OnLoad -> %llx\n", object->name.c_str(),
                    (unsigned long long)object->base, (unsigned long long)version);
        /* QB_DUMPLIB=name: the object as it sits in memory, relocations
           applied, for tools that need the final pointer tables (IL2CPP's
           method maps, say). Written next to QB_ROOT as <name>.mem. */
        const char* dump = QB_ENV("QB_DUMPLIB");
        const char* root = QB_ENV("QB_ROOT");
        if (dump && root && object->name == dump) {
            std::string path = std::string(root) + "/" + object->name + ".mem";
            if (FILE* out = std::fopen(path.c_str(), "wb")) {
                std::fwrite(reinterpret_cast<const void*>(object->base), 1, object->image.size(), out);
                std::fclose(out);
                std::printf("library: dumped %s (%llu bytes)\n", path.c_str(),
                            (unsigned long long)object->image.size());
            }
        }
    }
    return handle;
}
