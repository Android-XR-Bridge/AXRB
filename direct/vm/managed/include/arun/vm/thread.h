// Managed threads, interpreter frames, and the stop-the-world protocol (ADR 0004).
//
// GC safety rule for ALL host code in this runtime: an Object* that must survive
// an allocation (anything that can trigger a collection) has to be reachable
// from a frame's `refs`, a Thread root (RootScope), a class static, or another
// runtime root. The collector does not move objects, but it frees unreachable
// ones, and it only sees what is registered.
//
// Thread states: a thread in kRunnable may touch the heap; any other state
// promises not to. Blocking operations (monitor contention, wait, sleep,
// host I/O, the UI message pump) must leave kRunnable first -- ScopedBlocking does
// it -- or a collection requested by another thread would wait forever.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "arun/vm/class.h"
#include "arun/vm/object.h"

namespace arun::vm {

class Runtime;
class Monitor;

// Host stack reserved for every thread that runs Java code. The interpreter
// recurses on the host stack (one activation per Java call), so this is far
// more than ART's 1 MiB default; it is a reservation, committed only as used.
inline constexpr std::size_t kJavaThreadStackBytes = 64u << 20;

struct Frame {
  Frame* prev = nullptr;
  RtMethod* method = nullptr;
  std::uint32_t dex_pc = 0;
  std::uint32_t num_regs = 0;
  std::uint32_t* regs = nullptr;  // primitive view: ints, floats, wide halves
  Object** refs = nullptr;        // reference view: valid objects or null only
  Object* caught = nullptr;       // exception handed to a catch block
  Object* sync = nullptr;         // monitor held because the method is synchronized
};

enum class ThreadState : std::uint8_t {
  kNew,
  kRunnable,
  kNative,      // in host code that does not touch the heap
  kBlocked,     // waiting to enter a monitor or a class initialiser
  kWaiting,     // Object.wait / Thread.sleep / join / UI message wait
  kTerminated,
};

class Thread {
 public:
  explicit Thread(Runtime* runtime, std::string name);
  ~Thread();
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  static Thread* Current();

  // Registers the calling host thread with the runtime and makes it runnable.
  // Detach unregisters it; a detached thread must not touch managed objects.
  void AttachCurrent();
  // Registers a thread that will attach later on a new host thread, so its
  // peer is a GC root from the moment Thread.start() returns.
  void PreRegister();
  void DetachCurrent();

  Runtime* runtime() const { return runtime_; }
  const std::string& name() const { return name_; }
  void set_name(std::string n) { name_ = std::move(n); }
  std::uint32_t host_tid() const { return host_tid_; }

  // --- frames -----------------------------------------------------------
  // Allocates a frame and its register arrays (zeroed) from the thread's frame
  // arena. Returns nullptr if the arena or the host stack is exhausted; the
  // caller throws StackOverflowError.
  Frame* PushFrame(RtMethod* method, std::uint32_t num_regs);
  void PopFrame(Frame* frame);
  Frame* top_frame() const { return top_; }
  std::uint32_t depth() const { return depth_; }
  // While a StackOverflowError is being constructed, frames may use the reserve
  // that normal calls are kept out of.
  void set_in_overflow(bool v) { in_overflow_ = v; }
  bool in_overflow() const { return in_overflow_; }

  // --- exceptions -------------------------------------------------------
  Object* exception() const { return exception_; }
  bool IsExceptionPending() const { return exception_ != nullptr; }
  void SetException(Object* e) { exception_ = e; }
  void ClearException() { exception_ = nullptr; }

  // --- host-held roots --------------------------------------------------
  std::size_t PushRoot(Object* o) {
    roots_.push_back(o);
    return roots_.size() - 1;
  }
  Object* GetRoot(std::size_t i) const { return roots_[i]; }
  void SetRoot(std::size_t i, Object* o) { roots_[i] = o; }
  std::size_t root_count() const { return roots_.size(); }
  void TruncateRoots(std::size_t n) { roots_.resize(n); }

  // The value most recently returned by an invoke, until move-result consumes it.
  JValue result{};
  Object* result_ref = nullptr;

  // --- state and suspension ---------------------------------------------
  ThreadState state() const { return state_.load(std::memory_order_acquire); }
  void TransitionToRunnable();
  void TransitionFromRunnable(ThreadState new_state);
  // Cheap check; parks the thread if a stop-the-world pause is requested.
  void Safepoint();

  // --- java.lang.Thread integration ------------------------------------
  Object* peer = nullptr;  // the java.lang.Thread object
  std::atomic<bool> interrupted{false};
  bool daemon = false;

  // Object.wait / Thread.sleep / join bookkeeping, guarded by the monitor's or
  // the thread's own lock.
  std::atomic<Monitor*> waiting_on{nullptr};  // read by Thread.interrupt from other threads
  bool notified = false;                      // guarded by the waited-on monitor's mutex
  std::mutex sleep_lock;
  std::condition_variable sleep_cv;

  // Exposes roots to the collector.
  template <typename Visitor>
  void VisitRoots(Visitor&& visit) {
    for (Frame* f = top_; f != nullptr; f = f->prev) {
      for (std::uint32_t i = 0; i < f->num_regs; ++i) {
        if (f->refs[i] != nullptr) visit(f->refs[i]);
      }
      if (f->caught != nullptr) visit(f->caught);
      if (f->sync != nullptr) visit(f->sync);
    }
    for (Object* o : roots_) {
      if (o != nullptr) visit(o);
    }
    if (exception_ != nullptr) visit(exception_);
    if (result_ref != nullptr) visit(result_ref);
    if (peer != nullptr) visit(peer);
  }

 private:
  Runtime* runtime_;
  std::string name_;
  std::uint32_t host_tid_ = 0;
  std::atomic<ThreadState> state_{ThreadState::kNew};

  Frame* top_ = nullptr;
  std::uint32_t depth_ = 0;
  // Frame arena: fixed capacity so pointers into it stay valid.
  std::unique_ptr<std::uint64_t[]> arena_;
  std::size_t arena_words_ = 0;
  std::size_t arena_top_ = 0;
  std::uintptr_t host_stack_floor_ = 0;
  bool in_overflow_ = false;
  bool registered_ = false;

  Object* exception_ = nullptr;
  std::vector<Object*> roots_;
};

// Keeps host-held references visible to the collector for a C++ scope.
class RootScope {
 public:
  explicit RootScope(Thread* self) : self_(self), base_(self->root_count()) {}
  ~RootScope() { self_->TruncateRoots(base_); }
  RootScope(const RootScope&) = delete;
  RootScope& operator=(const RootScope&) = delete;

  // Returns a handle; read the (possibly updated) object back with Get().
  class Handle {
   public:
    Handle() = default;
    Handle(Thread* t, std::size_t i) : t_(t), i_(i) {}
    Object* Get() const { return t_->GetRoot(i_); }
    void Set(Object* o) { t_->SetRoot(i_, o); }

   private:
    Thread* t_ = nullptr;
    std::size_t i_ = 0;
  };
  Handle Add(Object* o) { return Handle(self_, self_->PushRoot(o)); }

 private:
  Thread* self_;
  std::size_t base_;
};

// Leaves kRunnable for the duration of a blocking host operation.
class ScopedBlocking {
 public:
  ScopedBlocking(Thread* self, ThreadState state = ThreadState::kNative) : self_(self) {
    self_->TransitionFromRunnable(state);
  }
  ~ScopedBlocking() { self_->TransitionToRunnable(); }
  ScopedBlocking(const ScopedBlocking&) = delete;
  ScopedBlocking& operator=(const ScopedBlocking&) = delete;

 private:
  Thread* self_;
};

}  // namespace arun::vm
