/* The host calls a running engine makes most, answered without the name
   lookup.

   GuestLibc::call finds a function by comparing its name against each thing
   it answers, which is fine for the thousand functions called now and then
   and costs several microseconds for the dozen called hundreds of thousands
   of times a second: thread-specific data, pthread_self, mutexes, errno and
   the memory and string basics. Each import is classified once (fast_id) and
   those go straight to their code here. Behaviour is the same as the
   ordinary path; where that path does something first-time (a thread's
   errno slot, its pthread_self handle), this returns false and lets it. */

#include "android.h"

#include <chrono>
#include <cmath>
#include <cstring>

namespace {

enum FastId {
    kGetSpecific,
    kSetSpecific,
    kMutexLock,
    kMutexUnlock,
    kMutexTryLock,
    kSelf,
    kErrno,
    kMemcpy,
    kMemmove,
    kMemset,
    kMemcmp,
    kStrlen,
    kStrcmp,
    kVulkan,
    kClock,
    kTimeOfDay,
    kSinCosF,
    kMath, /* kMath + 2 * index into kMaths, + 1 for the float one */
};

/* The same maths GuestLibc::call answers (android.cpp), by table instead of
   by name: computed in double and rounded, as that path does. */
const struct {
    const char* name;
    double (*fn1)(double);
    double (*fn2)(double, double);
} kMaths[] = {
    {"sin", std::sin, nullptr},     {"cos", std::cos, nullptr},     {"tan", std::tan, nullptr},
    {"asin", std::asin, nullptr},   {"acos", std::acos, nullptr},   {"atan", std::atan, nullptr},
    {"exp", std::exp, nullptr},     {"log", std::log, nullptr},     {"log2", std::log2, nullptr},
    {"log10", std::log10, nullptr}, {"sqrt", std::sqrt, nullptr},   {"cbrt", std::cbrt, nullptr},
    {"floor", std::floor, nullptr}, {"ceil", std::ceil, nullptr},   {"round", std::round, nullptr},
    {"trunc", std::trunc, nullptr}, {"fabs", std::fabs, nullptr},   {"sinh", std::sinh, nullptr},
    {"cosh", std::cosh, nullptr},   {"tanh", std::tanh, nullptr},   {"exp2", std::exp2, nullptr},
    {"expm1", std::expm1, nullptr}, {"log1p", std::log1p, nullptr},
    {"pow", nullptr, std::pow},     {"atan2", nullptr, std::atan2}, {"fmod", nullptr, std::fmod},
    {"hypot", nullptr, std::hypot}, {"fmin", nullptr, std::fmin},   {"fmax", nullptr, std::fmax},
};

const struct {
    const char* name;
    FastId id;
} kFast[] = {
    {"pthread_getspecific", kGetSpecific}, {"pthread_setspecific", kSetSpecific},
    {"pthread_mutex_lock", kMutexLock},     {"pthread_mutex_unlock", kMutexUnlock},
    {"pthread_mutex_trylock", kMutexTryLock}, {"pthread_self", kSelf},
    {"__errno", kErrno},                   {"memcpy", kMemcpy},
    {"__memcpy_chk", kMemcpy},             {"memmove", kMemmove},
    {"__memmove_chk", kMemmove},           {"memset", kMemset},
    {"__memset_chk", kMemset},             {"memcmp", kMemcmp},
    {"strlen", kStrlen},                   {"__strlen_chk", kStrlen},
    {"strcmp", kStrcmp},
};

}  // namespace

int GuestLibc::fast_id(const std::string& name) {
    for (const auto& entry : kFast)
        if (name == entry.name) return entry.id;
    if (name.compare(0, 2, "vk") == 0) return kVulkan; /* vulkan_fast decides per function */
    if (name == "clock_gettime") return kClock;
    if (name == "gettimeofday") return kTimeOfDay;
    if (name == "sincosf") return kSinCosF;
    for (int i = 0; i < (int)(sizeof(kMaths) / sizeof(kMaths[0])); ++i) {
        if (name == kMaths[i].name) return kMath + 2 * i;
        if (name == std::string(kMaths[i].name) + "f") return kMath + 2 * i + 1;
    }
    return -1;
}

bool GuestLibc::fast_call(int id, GuestCpu& cpu, int index, const std::string& name) {
    uint64_t* x = cpu.x;
    if (id >= kMath) {
        int which = (id - kMath) / 2;
        bool single = (id - kMath) & 1;
        auto value = [&](int r) {
            if (single) {
                float f;
                uint32_t bits = (uint32_t)cpu.q[r].lo;
                std::memcpy(&f, &bits, 4);
                return (double)f;
            }
            double d;
            std::memcpy(&d, &cpu.q[r].lo, 8);
            return d;
        };
        double result = kMaths[which].fn1 ? kMaths[which].fn1(value(0)) : kMaths[which].fn2(value(0), value(1));
        if (single) {
            float f = (float)result;
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            cpu.q[0].lo = bits;
        } else {
            std::memcpy(&cpu.q[0].lo, &result, 8);
        }
        cpu.q[0].hi = 0;
        return true;
    }
    switch (id) {
        case kVulkan: return vulkan_fast(index, name, cpu);
        case kClock: {
            /* Every clock is the one clock, as the ordinary path answers. */
            int64_t nanos = guest_clock_ns();
            if (x[1]) {
                int64_t pair[2] = {nanos / 1000000000, nanos % 1000000000};
                std::memcpy(reinterpret_cast<void*>(x[1]), pair, 16);
            }
            x[0] = 0;
            return true;
        }
        case kTimeOfDay: {
            int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            if (x[0]) {
                int64_t pair[2] = {micros / 1000000, micros % 1000000};
                std::memcpy(reinterpret_cast<void*>(x[0]), pair, 16);
            }
            x[0] = 0;
            return true;
        }
        case kSinCosF: {
            float angle;
            uint32_t bits = (uint32_t)cpu.q[0].lo;
            std::memcpy(&angle, &bits, 4);
            float sine = std::sin(angle), cosine = std::cos(angle);
            if (x[0]) std::memcpy(reinterpret_cast<void*>(x[0]), &sine, 4);
            if (x[1]) std::memcpy(reinterpret_cast<void*>(x[1]), &cosine, 4);
            return true;
        }
        case kGetSpecific: x[0] = guest_specific_get(cpu.tpidr, x[0]); return true;
        case kSetSpecific: guest_specific_set(cpu.tpidr, x[0], x[1]); x[0] = 0; return true;
        case kMutexLock: x[0] = (uint64_t)guest_mutex_lock(*this, cpu, x[0], false, -1); return true;
        case kMutexTryLock: x[0] = (uint64_t)guest_mutex_lock(*this, cpu, x[0], true, -1); return true;
        case kMutexUnlock: guest_mutex_unlock(*this, cpu, x[0], false); x[0] = 0; return true;
        case kSelf:
        case kErrno: {
            /* The ordinary path works these out the first time on a thread
               and remembers them; after that they are the same number. */
            thread_local uint64_t tp[2] = {0, 0}, answer[2] = {0, 0};
            int which = id == kSelf ? 0 : 1;
            if (tp[which] == cpu.tpidr && answer[which]) {
                x[0] = answer[which];
                return true;
            }
            if (!call(id == kSelf ? "pthread_self" : "__errno", cpu)) return false;
            tp[which] = cpu.tpidr;
            answer[which] = x[0];
            return true;
        }
        case kMemcpy:
        case kMemmove:
            if (x[2]) std::memmove(reinterpret_cast<void*>(x[0]), reinterpret_cast<const void*>(x[1]), (size_t)x[2]);
            return true; /* returns its first argument, already in x0 */
        case kMemset:
            if (x[2]) std::memset(reinterpret_cast<void*>(x[0]), (int)x[1], (size_t)x[2]);
            return true;
        case kMemcmp:
            x[0] = x[2] ? (uint64_t)(int64_t)std::memcmp(reinterpret_cast<const void*>(x[0]),
                                                        reinterpret_cast<const void*>(x[1]), (size_t)x[2])
                        : 0;
            return true;
        case kStrlen:
            x[0] = x[0] ? std::strlen(reinterpret_cast<const char*>(x[0])) : 0;
            return true;
        case kStrcmp:
            x[0] = x[0] && x[1] ? (uint64_t)(int64_t)std::strcmp(reinterpret_cast<const char*>(x[0]),
                                                                   reinterpret_cast<const char*>(x[1]))
                                : 0;
            return true;
    }
    return false;
}
