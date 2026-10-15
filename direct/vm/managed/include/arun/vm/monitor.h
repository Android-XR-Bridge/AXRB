// Java monitors: synchronized, Object.wait/notify/notifyAll (ADR 0004).
//
// Monitors live in a side table keyed by object, created on first use and
// removed when the collector frees the object. A monitor is a host mutex plus a
// condition variable; the Java-level owner and recursion count are tracked
// explicitly because Java monitors are reentrant and must be released in full
// by wait().
//
// Every blocking path leaves kRunnable first (see thread.h), so a thread blocked
// on a monitor never stalls a collection.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "arun/vm/object.h"

namespace arun::vm {

class Thread;

class Monitor {
 public:
  std::mutex mutex;
  std::condition_variable cv;
  Thread* owner = nullptr;
  std::uint32_t recursion = 0;
  std::vector<Thread*> wait_set;
};

class MonitorTable {
 public:
  Monitor* Get(Object* obj);
  // Drops monitors whose objects the collector found dead.
  template <typename IsLive>
  void Sweep(IsLive&& is_live) {
    std::lock_guard<std::mutex> g(lock_);
    for (auto it = table_.begin(); it != table_.end();) {
      if (!is_live(it->first)) {
        it = table_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  std::mutex lock_;
  std::unordered_map<Object*, std::unique_ptr<Monitor>> table_;
};

// All of these may leave an exception pending on `self` and return false.
bool MonitorEnter(Thread* self, Object* obj);
bool MonitorExit(Thread* self, Object* obj);
// timeout_ms == 0 && nanos == 0 means wait forever.
bool MonitorWait(Thread* self, Object* obj, std::int64_t timeout_ms, std::int32_t nanos);
bool MonitorNotify(Thread* self, Object* obj, bool all);
bool HoldsLock(Thread* self, Object* obj);

}  // namespace arun::vm
