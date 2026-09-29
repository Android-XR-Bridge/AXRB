#pragma once
// Background threads for texture transcoding, kept off the game's main thread.
//
// Each worker runs at a lower priority (nice +10) and, before every job,
// restricts itself to the CPUs other than the one the process's main thread
// last ran on, so transcoding competes with the render and job threads at
// most, never with the main thread. With a single CPU there is no other core
// and the affinity is left alone.
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace axrb::texture {

// Jobs grouped so a caller can wait for all of them.
class Batch {
public:
    void add() { std::lock_guard lock(mutex_); ++remaining_; }
    void done() {
        std::lock_guard lock(mutex_);
        if (--remaining_ == 0) finished_.notify_all();
    }
    void wait() {
        std::unique_lock lock(mutex_);
        finished_.wait(lock, [this] { return remaining_ == 0; });
    }
    bool finished() { std::lock_guard lock(mutex_); return remaining_ == 0; }

private:
    std::mutex mutex_;
    std::condition_variable finished_;
    int remaining_ = 0;
};

class Workers {
public:
    static Workers& instance() {
        static Workers workers;
        return workers;
    }
    int count() const { return int(threads_.size()); }

    void submit(const std::shared_ptr<Batch>& batch, std::function<void()> job) {
        batch->add();
        {
            std::lock_guard lock(mutex_);
            jobs_.push_back({batch, std::move(job)});
        }
        wake_.notify_one();
    }

private:
    struct Job {
        std::shared_ptr<Batch> batch;
        std::function<void()> run;
    };

    Workers() {
        const long cpus = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
        const int count = int(std::clamp(cpus - 1, 1L, 3L));
        for (int i = 0; i < count; ++i) threads_.emplace_back([this] { loop(); });
        for (auto& thread : threads_) thread.detach();
    }

    // The CPU the main thread (the thread group leader) last ran on: field 39
    // of its stat line, counted after the parenthesised command name.
    static int main_thread_cpu() {
        char path[64], line[1024];
        std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", int(getpid()));
        FILE* file = std::fopen(path, "re");
        if (!file) return -1;
        const bool read = std::fgets(line, sizeof(line), file) != nullptr;
        std::fclose(file);
        if (!read) return -1;
        const char* at = std::strrchr(line, ')');
        if (!at) return -1;
        int field = 2;
        for (const char* p = at + 1; *p; ++p)
            if (*p == ' ' && ++field == 39) return std::atoi(p + 1);
        return -1;
    }

    static void avoid_main_thread() {
        const int avoid = main_thread_cpu();
        const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (avoid < 0 || cpus < 2) return;
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int cpu = 0; cpu < cpus && cpu < CPU_SETSIZE; ++cpu)
            if (cpu != avoid) CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }

    void loop() {
        setpriority(PRIO_PROCESS, 0, 10);
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return !jobs_.empty(); });
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            avoid_main_thread();
            job.run();
            job.batch->done();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::vector<std::thread> threads_;
};

} // namespace axrb::texture
