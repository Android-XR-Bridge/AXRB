#include "frame_history.h"
#include "frame_pool.h"
#include <memory>
#include <cstdio>
#include <stdexcept>
using namespace std::chrono_literals;
struct Snapshot {
    struct { uint64_t sequence=0; } header;
    std::shared_ptr<int> lease;
    std::chrono::steady_clock::time_point receivedAt{};
};
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() try {
    axrb::host::detail::FrameHistory<Snapshot> history;
    std::vector<Snapshot> retired;
    const auto now=std::chrono::steady_clock::now();
    auto push=[&](uint64_t sequence) { history.push(Snapshot{{sequence},std::make_shared<int>(int(sequence)),now},retired); };
    push(0); push(1);
    check(history.size()==2 && history.front()->header.sequence==0,"first frame or FIFO ordering lost");
    history.submitted(0,retired);
    check(history.size()==1 && history.front()->header.sequence==1,"acknowledgment removed wrong frame");
    std::weak_ptr<int> evicted=history.front()->lease;
    push(2); push(3);
    check(history.size()==2 && history.front()->header.sequence==2,"overflow did not keep newest two");
    check(!evicted.expired(),"lease released before caller left publication lock");
    retired.clear();
    check(evicted.expired(),"retired lease leaked");
    check(history.overflowDrops==1,"overflow accounting wrong");
    history.submitted(2,retired); history.submitted(1,retired);
    push(2); push(3); // Regressed and duplicate inputs must not requeue.
    check(history.size()==1 && history.front()->header.sequence==3,"regression or duplicate requeued");
    history.expire(now+34ms,retired);
    check(history.size()==0 && history.expiredDrops==1,"stale frame was not expired");
    push(4); history.clear(retired); check(history.size()==0,"empty stream did not clear history");
    for(size_t capacity: {3u,4u}) {
        axrb::host::FramePool<int,4> pool(capacity);
        std::vector<std::shared_ptr<int>> leases;
        for(size_t i=0;i<capacity;++i) { auto slot=pool.acquire(); check(bool(slot),"pool smaller than requested"); leases.push_back(std::move(slot)); }
        check(!pool.acquire(),"pool exceeded bounded capacity");
        leases.pop_back(); check(bool(pool.acquire()),"released slot not reusable");
    }
    std::puts("PASS frame zero, FIFO, success acknowledgment, overflow, stale expiry, duplicate/regression rejection, out-of-lock retirement, and 3/4-slot bounds");
} catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
