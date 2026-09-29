#pragma once
#include <algorithm>
// Optional performance-overlay module; excluded from default host builds.
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace axrb::host {
// File I/O never blocks OpenXR. Optional collector writes a small atomic snapshot.
class GuestPerformanceReader {
public:
    ~GuestPerformanceReader() {
        { std::lock_guard lock(mutex_); stop_=true; }
        wake_.notify_all(); if(worker_.joinable()) worker_.join();
    }
    void start() {
        const char* path=std::getenv("AXRB_PERFORMANCE_CPU_FILE");
        if(!path || worker_.joinable()) return;
        worker_=std::thread([this,path=std::string(path)] {
            for(;;) {
                long long stamp=0; std::string cpu,threads;
                std::ifstream file(path); file>>stamp; file.ignore(1024,'\n');
                std::getline(file,cpu); std::getline(file,threads);
                file.close(); // Do not hold a Windows delete-denying handle during the wait.
                const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const bool valid=stamp>0 && now>=stamp && now-stamp<6000 && !cpu.empty() && !threads.empty();
                const auto clean=[](std::string s){s.resize((std::min)(s.size(),size_t(85)));for(char& c:s)if(c<32 || c>126)c='?';return s;};
                std::unique_lock lock(mutex_);
                cpu_=valid?clean(cpu):"ANDROID CPU: unavailable / stale";
                threads_=valid?clean(threads):"THREADS: unavailable / stale";
                if(wake_.wait_for(lock,std::chrono::milliseconds(500),[&]{return stop_;})) return;
            }
        });
    }
    std::pair<std::string,std::string> snapshot() {
        std::lock_guard lock(mutex_); return {cpu_,threads_};
    }
private:
    std::mutex mutex_; std::condition_variable wake_; std::thread worker_; bool stop_=false;
    std::string cpu_="ANDROID CPU: collector unavailable", threads_="THREADS: unavailable";
};
}
