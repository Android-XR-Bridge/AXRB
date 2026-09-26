#pragma once

#include "loader.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <vector>

/* The part of the C library a guest actually calls.

   These are answered on this side rather than by running bionic's own code.
   A game uses the library through its interface, so standing in for that
   interface is enough, and it avoids emulating the system calls underneath. */
/* One guest thread: its own registers, its own stack and thread local block,
   and the host thread running the interpreter over them. */
struct GuestThread {
    GuestCpu cpu{};
    uint64_t stack_va = 0;
    uint64_t stack_bytes = 0;
    uint64_t tls_va = 0;
    uint64_t result = 0;
    std::thread runner;
    bool joined = false;
    std::atomic<bool> finished{false};
};

/* One end to end pipe, which is how the application glue talks to itself. */
struct GuestPipe {
    std::mutex lock;
    std::condition_variable ready;
    std::deque<uint8_t> bytes;
    /* An eventfd is a counter rather than a byte stream: write adds to it,
       read takes it (or one, in semaphore mode). */
    bool event = false;
    bool semaphore = false;
    bool nonblocking = false;
    uint64_t counter = 0;
    /* One end of a socketpair: what is written to it lands in the other
       end's pipe (each end reads its own). */
    std::weak_ptr<GuestPipe> peer;
};

/* A descriptor the looper was asked to watch. */
struct Watched {
    uint64_t looper;
    int fd;
    int ident;
    uint64_t callback;
    uint64_t data;
    int events = 1;
};

/* Nanoseconds since the process started: every guest clock. */
int64_t guest_clock_ns();
struct GuestLibc;
struct GuestCpu;
/* A guest pthread mutex, on its own memory (threads.cpp). */
int guest_mutex_lock(GuestLibc& libc, GuestCpu& cpu, uint64_t va, bool try_only, int64_t deadline_ns);
uint32_t guest_mutex_unlock(GuestLibc& libc, GuestCpu& cpu, uint64_t va, bool full);
uint64_t guest_specific_get(uint64_t tp, uint64_t key);
void guest_specific_set(uint64_t tp, uint64_t key, uint64_t value);

struct GuestLibc {
    /* Gives the guest a heap and remembers the image for pointer translation. */
    void start(GuestImage* image, uint64_t heap_bytes);

    /* Answers one import. False means the name is not one of ours. */
    bool call(const std::string& name, GuestCpu& cpu);
    /* The hottest calls, straight to their code (fastcalls.cpp): fast_id
       names one once, by import name, -1 for the rest; fast_call runs it, or
       returns false to send it the ordinary way. */
    static int fast_id(const std::string& name);
    bool fast_call(int id, GuestCpu& cpu, int index, const std::string& name);
    /* A Vulkan function passed straight through, by import (vulkan.cpp). */
    bool vulkan_fast(int index, const std::string& name, GuestCpu& cpu);

    /* Runs the guest's own init_array entries, which is where a constructor
       and anything the toolchain registers at startup lives. */
    void run_initialisers();

    /* Calls back into the guest, for a comparator or any other callback a
       library function is handed. */
    uint64_t call_guest(uint64_t function, uint64_t a0, uint64_t a1);
    /* Calling back into the guest from a thunk must start from the registers
       of the thread that is calling, not from a shared set: two threads doing
       this at once would otherwise trample each other's stack pointer. */
    uint64_t call_guest_on(GuestCpu& from, uint64_t function, uint64_t a0, uint64_t a1, uint64_t a2 = 0);
    uint64_t call_guest_three(uint64_t function, uint64_t a0, uint64_t a1, uint64_t a2);
    /* Up to eight integer arguments, which is what a JNI native takes:
       the environment, the object, then its own parameters. */
    uint64_t call_guest_args(uint64_t function, const std::vector<uint64_t>& args);

    GuestImage* image = nullptr;
    void* host_heap = nullptr; /* a private Windows heap, which the guest uses as its own */
    uint64_t heap_va = 0;
    uint64_t allocated = 0;
    int allocations = 0;
    int tls_resolves = 0;

    /* Stacks and thread local blocks are carved out of one span mapped at the
       start, because adding regions while other threads are reading the list
       would be a race. */
    GuestBytes thread_area;
    uint64_t thread_area_va = 0;
    uint64_t thread_area_used = 0;
    std::recursive_mutex lock; /* guards the heap, the thread area and the tables below */
    std::unordered_map<uint64_t, std::unique_ptr<GuestThread>> threads;
    std::unordered_map<uint64_t, std::unique_ptr<std::timed_mutex>> mutexes;
    std::set<uint64_t> recursive_mutexes;
    std::set<uint64_t> known_mutexes;
    std::unordered_map<uint64_t, uint64_t> mutex_owners;
    std::unordered_map<uint64_t, uint64_t> mutex_sites; /* the lr each mutex was last taken from */
    std::unordered_map<uint64_t, int> mutex_depth;
    /* What sigaction installed: handler and flags for each signal. */
    uint64_t signal_handlers[65] = {};
    uint64_t signal_flags[65] = {};
    /* Runs any signal pending for this thread; true if one ran. */
    bool poll_signals(GuestCpu& cpu);
    /* Runs the callbacks of this thread's looper whose descriptors have
       something to read, as Android's Looper does between messages. */
    bool pump_looper(GuestCpu& cpu);
    void install_signal_hook();
    void set_errno(GuestCpu& cpu, int value);
    void deliver_signal(GuestCpu& cpu, int signal);
    bool deliver_sigill(GuestCpu& cpu);
    bool zlib_call(const std::string& name, GuestCpu& cpu); /* zlib.cpp */
    bool opensl_call(const std::string& name, GuestCpu& cpu); /* opensl.cpp */
    std::unordered_map<uint64_t, std::unique_ptr<std::condition_variable_any>> conditions;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> specifics;
    uint64_t next_thread = 1;
    uint64_t next_key = 1;

    /* A thread the guest never joined is still running at the end, and
       letting its object go while it is joinable would end the process. */
    ~GuestLibc();

    bool thread_call(const std::string& name, GuestCpu& cpu);
    bool egl_call(const std::string& name, GuestCpu& cpu);
    bool java_call(const std::string& name, GuestCpu& cpu);
    bool activity_call(const std::string& name, GuestCpu& cpu);
    /* Builds the activity the entry point is handed, and drives its callbacks. */
    uint64_t make_activity();
    void lifecycle(int which, uint64_t argument);
    int make_pipe();
    void link_socket_pair(int a, int b);
    void wake_loopers();

    uint64_t activity_va = 0;
    uint64_t callbacks_va = 0;
    std::unordered_map<int, std::shared_ptr<GuestPipe>> fds;
    std::set<int> pipe_writers;
    std::vector<Watched> watched;
    std::unordered_map<uint64_t, uint64_t> loopers;
    std::atomic<bool> woken{false};
    int next_fd = 3;

    /* Anonymous mappings. One span is reserved up front and mapped into the
       guest as a single region, so the region list never changes while a
       thread is walking it; pages are only committed as the guest maps them. */
    uint8_t* map_area = nullptr;
    uint64_t map_va = 0x4000000000ull;
    uint64_t map_size = 64ull << 30;
    uint64_t map_used = 0;
    /* Builds the JNIEnv and JavaVM tables in guest memory. */
    void start_java();
    uint64_t guest_alloc(uint64_t bytes);
    uint64_t guest_string(const std::string& text);
    /* The Oculus Platform SDK, answered as a Quest with nobody signed in (platform.cpp). */
    bool platform_call(const std::string& name, GuestCpu& cpu);
    uint64_t handle_for(const std::string& kind, const std::string& what);
    std::string format_from_guest(const char* format, GuestCpu& cpu, int next_int, int next_float);
    std::string format_from_valist(const char* format, uint64_t list_va);

    uint64_t env_va = 0;
    uint64_t vm_va = 0;
    int registered_natives = 0;
    std::unordered_map<std::string, uint64_t> handle_by_name;
    std::unordered_map<uint64_t, std::string> handle_names;
    std::unordered_map<uint64_t, std::string> strings_by_handle;
    /* What the guest has asked the Java side for, which is the specification
       of the Java surface that has to be answered. */
    std::set<std::string> java_wanted;
    /* Every native method the guest registered, by "class.method". */
    std::unordered_map<std::string, uint64_t> natives;
    std::unordered_map<std::string, std::string> native_signatures;
    /* The device's system properties, read from QB_ROOT/props.txt (the
       output of "adb shell getprop" on the headset being stood in for). */
    std::unordered_map<std::string, std::string> properties;
    bool properties_loaded = false;
    const std::string& property(const std::string& key);
    int objects_made = 0;
    std::unordered_map<uint64_t, std::string> file_paths;
    /* Java objects this side gives behaviour to: asset streams, and the
       Scanners reading them; and an exception that is pending, if any. */
    struct JavaStream {
        std::vector<uint8_t> bytes;
        size_t at = 0;
    };
    std::unordered_map<uint64_t, JavaStream> java_streams;
    std::unordered_map<uint64_t, uint64_t> scanner_streams;
    uint64_t pending_exception = 0;
    uint64_t java_argument(const std::string& which, GuestCpu& cpu, int n);
    /* The UI thread (guest/uithread.cpp) and the Java proxies it runs. */
    struct UiJob {
        uint64_t function = 0;
        std::vector<uint64_t> args;
        std::chrono::steady_clock::time_point due;
    };
    void post_to_ui(uint64_t function, const std::vector<uint64_t>& args, int delay_ms);
    std::mutex ui_lock;
    std::condition_variable ui_ready;
    std::deque<UiJob> ui_jobs;
    bool ui_started = false;
    uint64_t ui_stack_va = 0, ui_stack_top = 0, ui_tls_va = 0;
    std::unordered_map<uint64_t, uint64_t> proxy_pointers; /* JNIBridge proxy -> its native object */
    std::unordered_map<uint64_t, std::string> reflect_methods; /* Method object -> its name */
    /* android.os.Handler / Message / Choreographer, as far as a callback
       proxy needs: which callback a handler has, what a message says and
       where it goes, and boxed longs for doFrame's argument. */
    std::unordered_map<uint64_t, uint64_t> handler_callbacks;
    std::unordered_map<uint64_t, int64_t> message_what;
    std::unordered_map<uint64_t, uint64_t> message_target;
    std::unordered_map<uint64_t, int64_t> boxed_longs;
    std::unordered_map<uint64_t, uint64_t> reflected_ids; /* Method object -> methodID */
    void invoke_proxy(uint64_t proxy, const std::string& method, const std::vector<uint64_t>& args, int delay_ms);
    std::unordered_map<std::string, uint64_t> library_paths;
    std::unordered_map<uint64_t, std::vector<uint64_t>> object_arrays;
    std::unordered_map<uint64_t, std::vector<uint8_t>> primitive_arrays;
    std::unordered_map<uint64_t, int> primitive_sizes;
    std::unordered_map<uint64_t, uint64_t> errno_slots;
    std::unordered_map<int, FILE*> files;
    std::set<int> shared_files;
    struct GuestDir {
        std::vector<std::string> names;
        std::vector<bool> directories;
        size_t next = 0;
    };
    std::unordered_map<uint64_t, GuestDir> directories;
    std::unordered_map<uint64_t, int> streams; /* guest FILE* to descriptor */
    std::set<int> random_fds; /* open /dev/urandom and /dev/random */
    /* epoll instances (net.cpp): descriptor -> what it watches. */
    struct EpollEntry {
        uint32_t events = 0;
        uint64_t data = 0;
        bool disarmed = false;
    };
    std::unordered_map<int, std::unordered_map<int, EpollEntry>> epolls;
    bool file_call(const std::string& name, GuestCpu& cpu);
    /* Sockets and name lookup on Winsock (net.cpp). */
    bool net_call(const std::string& name, GuestCpu& cpu);
    bool vulkan_call(const std::string& name, GuestCpu& cpu);
    bool openxr_call(const std::string& name, GuestCpu& cpu);
    uint64_t open_library(const std::string& wanted);
    static uint64_t guest_tid();
    /* Strings handed to the guest have to live where it can read them. */
    std::unordered_map<std::string, uint64_t> strings;
};
