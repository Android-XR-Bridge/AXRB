#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace axrb::runtime {
// One bounded in-flight publication. The caller submits Vulkan commands first;
// only fence waiting and network delivery run here. Drain before reusing storage.
class FramePublisher {
public:
    ~FramePublisher() {
        drain();
        { std::lock_guard lock(mutex_); stopping_ = true; }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    bool submit(std::function<bool()> work) {
        std::lock_guard lock(mutex_);
        if (busy_ || stopping_) return false;
        if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
        work_ = std::move(work);
        busy_ = true;
        changed_.notify_all();
        return true;
    }
    bool drain() {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return !busy_; });
        const bool result = result_;
        result_ = true;
        return result;
    }
private:
    void run() {
        std::unique_lock lock(mutex_);
        for (;;) {
            changed_.wait(lock, [this] { return busy_ || stopping_; });
            if (stopping_) return;
            auto work = std::move(work_);
            lock.unlock();
            bool result = false;
            try { result = work(); } catch (...) { result = false; }
            lock.lock();
            result_ = result;
            busy_ = false;
            changed_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::function<bool()> work_;
    bool busy_ = false, stopping_ = false, result_ = true;
};
}
