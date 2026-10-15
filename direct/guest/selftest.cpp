// Exercises the parts of the Linux/bionic contract that games depend on:
// threads, futex-based locks, TLS, C++ exceptions and unwinding, atomics
// (both LL/SC and LSE encodings), files, directories, dlopen, signals, time.
// Every check compares a value; a crash or a wrong value fails the run.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

static thread_local int tls_value = 7;
static volatile sig_atomic_t got_signal = 0;
static void on_usr1(int sig, siginfo_t* info, void*) { got_signal = sig + (info->si_code == SI_TKILL ? 1000 : 0); }

static int thrower(int depth) {
    if (depth == 0) throw std::runtime_error("deep");
    return thrower(depth - 1) + 1;
}

static uint32_t lse_add(uint32_t* p, uint32_t v) {
    uint32_t old;
    asm volatile(".arch_extension lse\n ldaddal %w[v], %w[old], [%[p]]" : [old] "=r"(old) : [v] "r"(v), [p] "r"(p) : "memory");
    return old;
}
static uint64_t lse_cas(uint64_t* p, uint64_t expected, uint64_t desired) {
    asm volatile(".arch_extension lse\n casal %[e], %[d], [%[p]]" : [e] "+r"(expected) : [d] "r"(desired), [p] "r"(p) : "memory");
    return expected;
}

int main() {
    std::printf("selftest: start\n");
    // TLS and threads.
    {
        std::atomic<int> sum{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&sum, i] {
                tls_value = i * 10;
                for (int k = 0; k < 10000; ++k) sum.fetch_add(1, std::memory_order_relaxed);
                CHECK(tls_value == i * 10);
            });
        for (auto& t : threads) t.join();
        CHECK(sum.load() == 80000);
        CHECK(tls_value == 7);
    }
    // Mutex + condition variable handoff (futex wait/wake).
    {
        std::mutex m;
        std::condition_variable cv;
        int stage = 0;
        std::thread other([&] {
            for (int i = 0; i < 1000; ++i) {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] { return stage % 2 == 1; });
                ++stage;
                cv.notify_all();
            }
        });
        for (int i = 0; i < 1000; ++i) {
            std::unique_lock<std::mutex> lock(m);
            ++stage;
            cv.notify_all();
            cv.wait(lock, [&] { return stage % 2 == 0; });
        }
        other.join();
        CHECK(stage == 2000);
    }
    // Timed waits.
    {
        std::mutex m;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(m);
        auto start = std::chrono::steady_clock::now();
        cv.wait_for(lock, std::chrono::milliseconds(30));
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        CHECK(ms >= 25 && ms < 1000);
    }
    // C++ exceptions through frames (unwinder, eh_frame, dl_iterate_phdr).
    {
        std::string what;
        try {
            thrower(20);
        } catch (const std::exception& e) {
            what = e.what();
        }
        CHECK(what == "deep");
    }
    // LSE atomics (not in Dynarmic: served by the fallback interpreter).
    {
        uint32_t counter = 5;
        CHECK(lse_add(&counter, 3) == 5);
        CHECK(counter == 8);
        uint64_t word = 11;
        CHECK(lse_cas(&word, 11, 99) == 11);
        CHECK(word == 99);
        CHECK(lse_cas(&word, 11, 5) == 99);
        CHECK(word == 99);
    }
    // Files and directories.
    {
        mkdir("/data/local/tmp/selftest", 0755);
        FILE* f = std::fopen("/data/local/tmp/selftest/a.txt", "w");
        CHECK(f != nullptr);
        if (f) {
            std::fprintf(f, "line %d\n", 42);
            std::fclose(f);
        }
        f = std::fopen("/data/local/tmp/selftest/a.txt", "r");
        char buffer[64] = {};
        if (f) {
            std::fgets(buffer, sizeof buffer, f);
            std::fclose(f);
        }
        CHECK(std::strcmp(buffer, "line 42\n") == 0);
        struct stat st {};
        CHECK(stat("/data/local/tmp/selftest/a.txt", &st) == 0 && st.st_size == 8 && S_ISREG(st.st_mode));
        int seen = 0;
        if (DIR* d = opendir("/data/local/tmp/selftest")) {
            while (dirent* e = readdir(d)) seen += std::strcmp(e->d_name, "a.txt") == 0;
            closedir(d);
        }
        CHECK(seen == 1);
        CHECK(unlink("/data/local/tmp/selftest/a.txt") == 0);
        CHECK(access("/data/local/tmp/selftest/a.txt", F_OK) != 0);
    }
    // mmap / mprotect.
    {
        void* p = mmap(nullptr, 1 << 20, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(p != MAP_FAILED);
        CHECK(mprotect(p, 8192, PROT_READ | PROT_WRITE) == 0);
        static_cast<char*>(p)[8191] = 3;
        CHECK(static_cast<char*>(p)[0] == 0);
        CHECK(munmap(p, 1 << 20) == 0);
    }
    // Transitive system dependencies must also resolve libraries in APEXes.
    // Unity loads harfbuzz, which needs libicu from com.android.i18n.
    for (const char* name : {"libicu.so", "libharfbuzz_ng.so"}) {
        void* library = dlopen(name, RTLD_NOW);
        if (!library) std::printf("dlopen %s: %s\n", name, dlerror());
        CHECK(library != nullptr);
        if (library) dlclose(library);
    }
    // dlopen of a system library.
    {
        void* m = dlopen("libm.so", RTLD_NOW);
        CHECK(m != nullptr);
        auto cosf_ptr = m ? reinterpret_cast<float (*)(float)>(dlsym(m, "cosf")) : nullptr;
        CHECK(cosf_ptr && std::fabs(cosf_ptr(0.0f) - 1.0f) < 1e-6f);
    }
    // Signals delivered to a thread.
    {
        struct sigaction sa {};
        sa.sa_sigaction = on_usr1;
        sa.sa_flags = SA_SIGINFO;
        CHECK(sigaction(SIGUSR1, &sa, nullptr) == 0);
        pthread_kill(pthread_self(), SIGUSR1);
        CHECK(got_signal == SIGUSR1 + 1000);
    }
    // Clocks.
    {
        timespec a{}, b{};
        clock_gettime(CLOCK_MONOTONIC, &a);
        usleep(20000);
        clock_gettime(CLOCK_MONOTONIC, &b);
        long long ns = (b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec);
        CHECK(ns >= 15000000 && ns < 2000000000LL);
        CHECK(time(nullptr) > 1700000000);
    }
    // System properties come from the generated property area.
    {
        char value[PROP_VALUE_MAX] = {};
        __system_property_get("ro.build.version.sdk", value);
        CHECK(std::atoi(value) >= 29);
    }
    std::printf("selftest: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
