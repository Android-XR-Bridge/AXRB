#pragma once
#include <stdexcept>
// Optional performance-overlay module; excluded from default host builds.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace axrb::host {
struct PerformanceEvent {
    char kind; // R=complete image received, S=xrEndFrame result
    int64_t ns;
    uint64_t sequence;
    // R: bit0=new sequence, bit1=nonempty, bit2=GPU shared
    // S: bit0=success, bit1=game projection, bit2=fresh game sequence
    unsigned flags, width, height;
    int64_t periodNs;
};
// Never perform filesystem I/O on the render/transport thread. Finite queue,
// try_lock + explicit lost-event count, finite file. Missing samples are NOT FPS.
class PerformanceJournal {
public:
    PerformanceJournal() {
        const char* path=std::getenv("AXRB_PERFORMANCE_FRAME_LOG");
        if(!path || !*path)return;
        path_=std::filesystem::u8path(path);
        enabled_=true;
        worker_=std::thread([this]{run();});
    }
    ~PerformanceJournal(){stop_=true;cv_.notify_all();if(worker_.joinable())worker_.join();}
    void record(PerformanceEvent event) {
        if(!enabled_)return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock() || queue_.size()>=8192){++dropped_;return;}
        queue_.push_back(event);
    }
private:
    void run() noexcept {
        try {
            // New run path only. Never overwrite an earlier measurement.
            if(std::filesystem::exists(path_))throw std::runtime_error("frame log already exists");
            std::ofstream file(path_,std::ios::binary);
            if(!file)throw std::runtime_error("cannot open frame log");
            const auto steady=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            file<<"# AXRB raw frame events v1; new image sequences, NOT optical scanout\n# steady_anchor_ns="<<steady<<" utc_anchor_ns="<<utc
                <<"\n# R flags: new=1 nonempty=2 gpu_shared=4; S flags: success=1 projection=2 fresh=4\nkind,steady_ns,sequence,flags,width,height,period_ns\n";
            auto spaceChecked=std::chrono::steady_clock::now()-std::chrono::seconds(10);
            for(;;){
                std::deque<PerformanceEvent> pending;
                {std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(250),[this]{return stop_.load();});pending.swap(queue_);}
                for(const auto& e:pending)file<<e.kind<<','<<e.ns<<','<<e.sequence<<','<<e.flags<<','<<e.width<<','<<e.height<<','<<e.periodNs<<'\n';
                const auto dropped=dropped_.exchange(0);
                if(dropped)file<<"# DROPPED_EVENTS="<<dropped<<'\n';
                file.flush();
                if(!file)throw std::runtime_error("frame log write failed");
                if(file.tellp()>=256LL*1024*1024){file<<"# STOPPED_SIZE_LIMIT_256_MIB\n";break;}
                const auto now=std::chrono::steady_clock::now();
                if(now-spaceChecked>=std::chrono::seconds(10)){
                    spaceChecked=now;
                    if(std::filesystem::space(path_.parent_path()).available<60ULL*1024*1024*1024){file<<"# STOPPED_FREE_SPACE_RESERVE_60_GIB\n";break;}
                }
                if(stop_)break;
            }
        }catch(const std::exception& e){std::fprintf(stderr,"AXRB performance journal stopped: %s\n",e.what());}
        enabled_=false;
    }
    std::filesystem::path path_;
    std::atomic<bool> enabled_{false},stop_{false};
    std::atomic<uint64_t> dropped_{0};
    std::mutex mutex_;std::condition_variable cv_;
    std::deque<PerformanceEvent> queue_;std::thread worker_;
};
inline PerformanceJournal& performance_journal(){static PerformanceJournal journal;return journal;}
}
