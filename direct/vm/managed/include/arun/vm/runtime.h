// The managed runtime instance (ADR 0004): one per worker process (spec 14.2).
//
// Ownership: Runtime owns the heap, class linker, monitor table, intern table
//   and native registry. Threads are owned by whoever attached them.
// Thread affinity: thread-safe. Host code enters Java only through Invoke*,
//   from an attached, runnable thread.
// Error propagation: host-facing APIs return Status/Result; Java-facing helpers
//   leave a Java exception pending on the thread (interpreter contract).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arun/status.h"
#include "arun/vm/class.h"
#include "arun/vm/class_linker.h"
#include "arun/vm/heap.h"
#include "arun/vm/monitor.h"
#include "arun/vm/object.h"
#include "arun/vm/thread.h"

namespace arun::vfs {
class FileSystem;
}

namespace arun::vm {

struct BootDex {
  std::string name;
  std::vector<std::uint8_t> bytes;
};

// Where android.util.Log, System.out and System.err end up. Priority uses the
// android.util.Log constants (2 = VERBOSE ... 7 = ASSERT).
using LogSink = std::function<void(int priority, std::string_view tag, std::string_view message)>;

struct RuntimeOptions {
  std::vector<BootDex> boot_class_path;
  LogSink log_sink;
  std::uint32_t gc_stress_interval = 0;  // tests only
  // System properties visible through System.getProperty (no host environment
  // leaks through here -- spec 11). Added to / override the runtime defaults.
  std::vector<std::pair<std::string, std::string>> properties;
  // The guest file system java.io uses. Without one, every path is ENOENT.
  vfs::FileSystem* file_system = nullptr;
  // System.exit / Runtime.halt. Default: terminate the worker process.
  std::function<void(int status)> exit_hook;
  // An exception nobody handled (no uncaught-exception handler), which kills
  // the process on Android. Default: log it and terminate with status 10.
  std::function<void(std::string_view description)> fatal_hook;

  // AXRB Direct: Java threads are guest threads. When set, Thread.start()
  // calls this (on the starting thread) instead of CreateThread; it must run
  // entry(arg) on a new thread and return false if none could be created.
  std::function<bool(void (*entry)(void*), void* arg)> spawn_thread;
  // A `native` method with no host implementation: the guest JNI bridge binds
  // and calls it. Returns false (with nothing pending) if it is not bound.
  std::function<bool(Thread* self, RtMethod* method, Frame* args, JValue* result)> invoke_guest_native;
  // Extra GC roots (JNI global references), visited during marking.
  std::function<void(const std::function<void(Object*)>& visit)> extra_roots;
};

class Runtime {
 public:
  static Result<std::unique_ptr<Runtime>> Create(RuntimeOptions options);
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // The runtime of this process (there is exactly one while it exists).
  static Runtime* Current();

  Heap& heap() { return *heap_; }
  ClassLinker& class_linker() { return *class_linker_; }
  MonitorTable& monitors() { return monitors_; }
  const RuntimeOptions& options() const { return options_; }
  void Log(int priority, std::string_view tag, std::string_view message);

  // --- threads ------------------------------------------------------------
  // The thread that created the runtime is attached as "main".
  Thread* main_thread() { return main_thread_.get(); }
  void RegisterThread(Thread* t);
  void UnregisterThread(Thread* t);
  // Stop-the-world. `self` must be runnable; on return every other attached
  // thread is outside kRunnable and cannot re-enter it until ResumeAll.
  void SuspendAll(Thread* self);
  void ResumeAll(Thread* self);
  // Suspension protocol internals, used by Thread.
  std::mutex& thread_lock() { return thread_lock_; }
  std::condition_variable& thread_cv() { return thread_cv_; }
  bool suspend_requested() const { return suspend_requested_.load(std::memory_order_acquire); }
  std::int32_t& runnable_count() { return runnable_count_; }
  // Blocks until no non-daemon thread other than `self` is alive.
  void WaitForNonDaemonThreads(Thread* self);
  // Creates the java.lang.Thread peer for an attached host thread.
  bool AttachPeer(Thread* self, std::string_view name, bool daemon);

  // --- well-known classes and fields ---------------------------------------
  struct WellKnown {
    RtClass* object = nullptr;
    RtClass* string = nullptr;
    RtClass* class_ = nullptr;
    RtClass* throwable = nullptr;
    RtClass* thread = nullptr;
    RtClass* class_loader = nullptr;
    RtClass* stack_trace_element = nullptr;
    RtClass* char_array = nullptr;
    RtClass* byte_array = nullptr;
    RtClass* object_array = nullptr;
    RtClass* string_array = nullptr;
    RtClass* cloneable = nullptr;
    RtClass* serializable = nullptr;
    RtField* string_value = nullptr;       // char[] String.value
    RtField* class_vm_class = nullptr;     // long Class.vmClass
    RtField* throwable_backtrace = nullptr;
    RtField* thread_vm_thread = nullptr;   // long Thread.vmThread
    RtField* thread_name = nullptr;
    RtField* thread_daemon = nullptr;
    RtField* class_loader_vm_loader = nullptr;  // long ClassLoader.vmLoader
  } wk;

  // --- strings ------------------------------------------------------------
  Object* NewString(Thread* self, std::u16string_view chars);
  Object* NewStringUtf8(Thread* self, std::string_view utf8);
  Object* InternString(Thread* self, std::u16string_view chars);
  // Contents of a java.lang.String (empty for null).
  std::u16string GetString(Object* str);
  std::string GetStringUtf8(Object* str);

  // --- class mirrors ------------------------------------------------------
  Object* ClassMirror(Thread* self, RtClass* klass);
  RtClass* ClassFromMirror(Object* mirror);

  // --- exceptions ---------------------------------------------------------
  // Creates and sets a pending exception of class `descriptor` with a message.
  // If that class cannot be created, a more basic error is pending instead --
  // never nothing.
  void ThrowNew(Thread* self, std::string_view descriptor, std::string_view message);
  // Human-readable exception with its stack trace, for host diagnostics. Runs no
  // Java code, so it is safe to call in any failure state.
  std::string DescribeException(Thread* self, Object* throwable);
  // "(MainActivity.java:42)" for a frame.
  std::string FrameLocation(RtMethod* method, std::int64_t dex_pc);
  // Every thread's Java stack, for diagnostics (racy by design: it does not
  // stop the threads).
  std::string DumpThreads();

  // --- invocation from host code ------------------------------------------
  // Calls `method` (static: receiver ignored). Arguments are given as JValues in
  // declaration order; wide values take one JValue. Returns false with an
  // exception pending on failure. `dispatch` false calls exactly `method`
  // (invokespecial semantics) instead of the receiver's override.
  bool Invoke(Thread* self, RtMethod* method, Object* receiver, std::span<const JValue> args,
              JValue* result, bool dispatch = true);
  // Convenience: find a method by name/signature on `klass` (searching supers)
  // and invoke it.
  RtMethod* FindMethod(RtClass* klass, std::string_view name, std::string_view signature);

  // --- natives ------------------------------------------------------------
  // Key: "Ljava/lang/Object;.hashCode()I"
  void RegisterNative(std::string key, NativeFn fn);
  NativeFn FindNative(const RtMethod* method) const;

  // Everything the GC must treat as live, other than thread stacks.
  template <typename Visitor>
  void VisitRoots(Visitor&& visit);

  std::uint32_t NextIdentityHash();

  // --- process-level services ---------------------------------------------
  std::optional<std::string> GetProperty(std::string_view key);
  std::optional<std::string> SetProperty(std::string key, std::string value);
  vfs::FileSystem* file_system() const { return options_.file_system; }
  // Never returns when the default hook is used.
  void Exit(int status);
  // Called on a thread whose run() (or entry point) ended with an exception
  // pending: offers it to Thread.dispatchUncaughtException, then treats it as
  // fatal if no handler took it. Clears the exception.
  void HandleUncaughtException(Thread* self);

  // Stack traces store methods as indices into this table rather than raw
  // pointers, so a Java-visible backtrace can never name arbitrary memory.
  std::int64_t MethodIndex(RtMethod* method);
  RtMethod* MethodAt(std::int64_t index);

 private:
  explicit Runtime(RuntimeOptions options);
  Status Init();
  Status BindWellKnown();

  RuntimeOptions options_;
  std::unique_ptr<Heap> heap_;
  std::unique_ptr<ClassLinker> class_linker_;
  MonitorTable monitors_;

  std::mutex thread_lock_;
  std::condition_variable thread_cv_;
  std::vector<Thread*> threads_;
  std::int32_t runnable_count_ = 0;
  std::atomic<bool> suspend_requested_{false};
  bool gc_in_progress_ = false;
  std::unique_ptr<Thread> main_thread_;

  std::mutex intern_lock_;
  std::unordered_map<std::u16string, Object*> interned_;

  std::unordered_map<std::string, NativeFn> natives_;
  std::atomic<std::uint32_t> hash_seed_;

  std::mutex property_lock_;
  std::unordered_map<std::string, std::string> properties_;

  std::mutex method_index_lock_;
  std::unordered_map<RtMethod*, std::int64_t> method_indices_;
  std::vector<RtMethod*> methods_by_index_;
};

template <typename Visitor>
void Runtime::VisitRoots(Visitor&& visit) {
  for (Thread* t : threads_) t->VisitRoots(visit);
  class_linker_->ForEachClass([&](RtClass* c) {
    if (c->mirror != nullptr) visit(c->mirror);
    if (c->init_error != nullptr) visit(c->init_error);
    for (std::uint32_t s : c->static_ref_slots) {
      if (c->statics[s].l != nullptr) visit(c->statics[s].l);
    }
  });
  class_linker_->ForEachDexCache([&](DexCache* dc) {
    const std::uint32_t n = dc->dex->num_strings();
    for (std::uint32_t i = 0; i < n; ++i) {
      Object* s = dc->strings[i].load(std::memory_order_relaxed);
      if (s != nullptr) visit(s);
    }
  });
  class_linker_->ForEachLoader([&](ClassLoaderRt* l) {
    if (l->java_object != nullptr) visit(l->java_object);
  });
  for (auto& [k, v] : interned_) visit(v);
  if (options_.extra_roots) {
    options_.extra_roots([&](Object* o) {
      if (o != nullptr) visit(o);
    });
  }
}

// Register helpers for natives. `args` is the native call frame; arguments start
// at register 0 (for instance methods, register 0 is `this`).
inline Object* ArgRef(Frame* args, std::uint32_t reg) { return args->refs[reg]; }
inline std::int32_t ArgInt(Frame* args, std::uint32_t reg) {
  return static_cast<std::int32_t>(args->regs[reg]);
}
inline std::int64_t ArgLong(Frame* args, std::uint32_t reg) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(args->regs[reg]) |
                                   (static_cast<std::uint64_t>(args->regs[reg + 1]) << 32));
}
float ArgFloat(Frame* args, std::uint32_t reg);
double ArgDouble(Frame* args, std::uint32_t reg);

}  // namespace arun::vm
