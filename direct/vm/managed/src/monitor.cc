#include "arun/vm/monitor.h"

#include <algorithm>
#include <chrono>

#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

namespace arun::vm {

Monitor* MonitorTable::Get(Object* obj) {
  std::lock_guard<std::mutex> g(lock_);
  auto& slot = table_[obj];
  if (!slot) slot = std::make_unique<Monitor>();
  return slot.get();
}

namespace {

Monitor* MonitorFor(Thread* self, Object* obj) { return self->runtime()->monitors().Get(obj); }

// Throwing allocates, so callers must not hold a monitor mutex when calling this.
bool ThrowNotOwner(Thread* self, const char* what) {
  self->runtime()->ThrowNew(self, "Ljava/lang/IllegalMonitorStateException;",
                            std::string("object not locked by thread before ") + what);
  return false;
}

}  // namespace

bool MonitorEnter(Thread* self, Object* obj) {
  Monitor* mon = MonitorFor(self, obj);
  std::unique_lock<std::mutex> l(mon->mutex);
  if (mon->owner == self) {
    ++mon->recursion;
    return true;
  }
  if (mon->owner == nullptr) {
    mon->owner = self;
    mon->recursion = 1;
    return true;
  }
  // Contended: park as blocked so a collection can proceed meanwhile. The
  // object stays reachable through this thread's frame, so the monitor survives.
  l.unlock();
  self->TransitionFromRunnable(ThreadState::kBlocked);
  l.lock();
  mon->cv.wait(l, [&] { return mon->owner == nullptr; });
  mon->owner = self;
  mon->recursion = 1;
  l.unlock();
  self->TransitionToRunnable();
  return true;
}

bool MonitorExit(Thread* self, Object* obj) {
  Monitor* mon = MonitorFor(self, obj);
  {
    std::lock_guard<std::mutex> g(mon->mutex);
    if (mon->owner == self) {
      if (--mon->recursion == 0) {
        mon->owner = nullptr;
        mon->cv.notify_all();
      }
      return true;
    }
  }
  return ThrowNotOwner(self, "notify/exit");
}

bool HoldsLock(Thread* self, Object* obj) {
  Monitor* mon = MonitorFor(self, obj);
  std::lock_guard<std::mutex> g(mon->mutex);
  return mon->owner == self;
}

bool MonitorWait(Thread* self, Object* obj, std::int64_t timeout_ms, std::int32_t nanos) {
  Runtime* rt = self->runtime();
  if (timeout_ms < 0 || nanos < 0 || nanos > 999999) {
    rt->ThrowNew(self, "Ljava/lang/IllegalArgumentException;", "timeout value is negative or nanos out of range");
    return false;
  }
  Monitor* mon = MonitorFor(self, obj);
  std::unique_lock<std::mutex> l(mon->mutex);
  if (mon->owner != self) {
    l.unlock();
    return ThrowNotOwner(self, "wait()");
  }
  if (self->interrupted.exchange(false)) {
    l.unlock();
    rt->ThrowNew(self, "Ljava/lang/InterruptedException;", "");
    return false;
  }
  // Release the monitor completely (it may be held recursively).
  const std::uint32_t saved = mon->recursion;
  mon->owner = nullptr;
  mon->recursion = 0;
  self->notified = false;
  self->waiting_on = mon;
  mon->wait_set.push_back(self);
  mon->cv.notify_all();

  l.unlock();
  self->TransitionFromRunnable(ThreadState::kWaiting);
  l.lock();
  const bool forever = timeout_ms == 0 && nanos == 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms) +
                        std::chrono::nanoseconds(nanos);
  auto woken = [&] { return self->notified || self->interrupted.load(); };
  if (forever) {
    mon->cv.wait(l, woken);
  } else {
    mon->cv.wait_until(l, deadline, woken);
  }
  // Leave the wait set (a notify may already have removed us), then reacquire.
  auto it = std::find(mon->wait_set.begin(), mon->wait_set.end(), self);
  if (it != mon->wait_set.end()) mon->wait_set.erase(it);
  self->waiting_on = nullptr;
  mon->cv.wait(l, [&] { return mon->owner == nullptr; });
  mon->owner = self;
  mon->recursion = saved;
  const bool was_interrupted = self->interrupted.exchange(false);
  l.unlock();
  self->TransitionToRunnable();
  if (was_interrupted) {
    rt->ThrowNew(self, "Ljava/lang/InterruptedException;", "");
    return false;
  }
  return true;
}

bool MonitorNotify(Thread* self, Object* obj, bool all) {
  Monitor* mon = MonitorFor(self, obj);
  {
    std::lock_guard<std::mutex> g(mon->mutex);
    if (mon->owner == self) {
      if (all) {
        for (Thread* t : mon->wait_set) t->notified = true;
        mon->wait_set.clear();
      } else if (!mon->wait_set.empty()) {
        mon->wait_set.front()->notified = true;
        mon->wait_set.erase(mon->wait_set.begin());
      }
      mon->cv.notify_all();
      return true;
    }
  }
  return ThrowNotOwner(self, "notify()");
}

}  // namespace arun::vm
