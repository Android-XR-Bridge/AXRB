#include "arun/vm/thread.h"

#include <windows.h>

#include <cstring>
#include <new>

#include "arun/vm/runtime.h"

namespace arun::vm {
namespace {

thread_local Thread* t_current = nullptr;

// Frame arena per thread, in 8-byte words. Deep recursion is bounded by the host
// stack well before this in practice.
constexpr std::size_t kArenaWords = 1u << 20;  // 8 MiB
// Host stack kept free below the soft limit: room for the StackOverflowError's
// own construction and for host code called from Java.
constexpr std::uintptr_t kHostStackReserve = 192 * 1024;
constexpr std::uintptr_t kHostStackHardReserve = 64 * 1024;
constexpr std::size_t kArenaReserveWords = 16 * 1024;

}  // namespace

Thread::Thread(Runtime* runtime, std::string name) : runtime_(runtime), name_(std::move(name)) {
  arena_.reset(new std::uint64_t[kArenaWords]);
  arena_words_ = kArenaWords;
}

Thread::~Thread() = default;

Thread* Thread::Current() { return t_current; }

void Thread::AttachCurrent() {
  host_tid_ = GetCurrentThreadId();
  ULONG_PTR low = 0, high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  host_stack_floor_ = static_cast<std::uintptr_t>(low);
  t_current = this;
  if (!registered_) runtime_->RegisterThread(this);
  registered_ = true;
  TransitionToRunnable();
}

void Thread::PreRegister() {
  runtime_->RegisterThread(this);
  registered_ = true;
}

void Thread::DetachCurrent() {
  if (state() == ThreadState::kRunnable) TransitionFromRunnable(ThreadState::kTerminated);
  state_.store(ThreadState::kTerminated, std::memory_order_release);
  runtime_->UnregisterThread(this);
  if (t_current == this) t_current = nullptr;
}

Frame* Thread::PushFrame(RtMethod* method, std::uint32_t num_regs) {
  // The StackOverflowError being constructed may use the reserves.
  const bool overflowing = in_overflow_;
  char probe;
  const auto sp = reinterpret_cast<std::uintptr_t>(&probe);
  const std::uintptr_t stack_reserve = overflowing ? kHostStackHardReserve : kHostStackReserve;
  if (sp < host_stack_floor_ + stack_reserve) return nullptr;

  const std::size_t frame_words = (sizeof(Frame) + 7) / 8;
  const std::size_t regs_words = (num_regs + 1) / 2;
  const std::size_t words = frame_words + regs_words + num_regs;
  const std::size_t limit = overflowing ? arena_words_ : arena_words_ - kArenaReserveWords;
  if (arena_top_ + words > limit) return nullptr;

  std::uint64_t* base = arena_.get() + arena_top_;
  arena_top_ += words;
  std::memset(base, 0, words * sizeof(std::uint64_t));
  auto* f = new (base) Frame();
  f->method = method;
  f->num_regs = num_regs;
  f->regs = reinterpret_cast<std::uint32_t*>(base + frame_words);
  f->refs = reinterpret_cast<Object**>(base + frame_words + regs_words);
  f->prev = top_;
  top_ = f;
  ++depth_;
  return f;
}

void Thread::PopFrame(Frame* frame) {
  // Frames are strictly LIFO.
  top_ = frame->prev;
  --depth_;
  arena_top_ = static_cast<std::size_t>(reinterpret_cast<std::uint64_t*>(frame) - arena_.get());
}

void Thread::TransitionToRunnable() {
  std::unique_lock<std::mutex> l(runtime_->thread_lock());
  runtime_->thread_cv().wait(l, [&] { return !runtime_->suspend_requested(); });
  ++runtime_->runnable_count();
  state_.store(ThreadState::kRunnable, std::memory_order_release);
}

void Thread::TransitionFromRunnable(ThreadState new_state) {
  std::lock_guard<std::mutex> g(runtime_->thread_lock());
  if (state_.load(std::memory_order_relaxed) == ThreadState::kRunnable) --runtime_->runnable_count();
  state_.store(new_state, std::memory_order_release);
  runtime_->thread_cv().notify_all();
}

void Thread::Safepoint() {
  if (!runtime_->suspend_requested()) return;
  TransitionFromRunnable(ThreadState::kNative);
  TransitionToRunnable();
}

}  // namespace arun::vm
