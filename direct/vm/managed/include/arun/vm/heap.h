// Managed heap and a precise, non-moving, stop-the-world mark-sweep collector
// (ADR 0004).
//
// Precise: roots come from interpreter reference arrays, registered host roots,
// class statics and runtime tables -- never from scanning host stacks. So an
// integer that happens to look like a pointer never keeps garbage alive, and an
// unregistered host pointer is a bug the stress mode below is designed to expose.
//
// Ownership: the Heap owns every object. Thread affinity: Alloc from any runnable
//   thread; a collection runs on the allocating thread with all others parked.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "arun/vm/object.h"

namespace arun::vm {

class RtClass;
class Runtime;
class Thread;

struct HeapStats {
  std::uint64_t collections = 0;
  std::uint64_t objects_live = 0;
  std::uint64_t bytes_live = 0;
  std::uint64_t objects_freed_total = 0;
  std::uint64_t bytes_allocated_total = 0;
};

class Heap {
 public:
  explicit Heap(Runtime* runtime);
  ~Heap();

  // Allocates a zeroed instance of `klass` (which must be linked, not an array,
  // not abstract). Returns nullptr with OutOfMemoryError pending on failure.
  Object* AllocObject(Thread* self, RtClass* klass);
  // Allocates a zeroed array. `array_class` must be an array class; `length` >= 0.
  Array* AllocArray(Thread* self, RtClass* array_class, std::int32_t length);
  // Shallow copy (Object.clone for Cloneable objects and arrays).
  Object* Clone(Thread* self, Object* original);

  void CollectGarbage(Thread* self);
  HeapStats stats();

  // GC stress: collect before every Nth allocation (0 = off), and poison + never
  // reuse freed memory, so a missing root becomes a deterministic crash instead
  // of silent corruption. For tests only.
  void SetStressInterval(std::uint32_t every_n) { stress_every_ = every_n; }
  // Soft limit that triggers a collection; the hard limit fails allocation.
  void SetLimits(std::uint64_t trigger_bytes, std::uint64_t max_bytes);
  std::uint64_t max_bytes() const { return max_bytes_; }

  static std::size_t SizeOf(Object* o);

  // Native memory owned by a Java object (android.graphics.Bitmap pixels, ...):
  // when owner is found unreachable, ree_fn(data) runs during the sweep,
  // with every other thread stopped -- it must not call into Java or block.
  // ytes counts toward the collection trigger so native-heavy objects are
  // collected promptly.
  using NativeFree = void (*)(void* data);
  void RegisterNativeAllocation(Object* owner, NativeFree free_fn, void* data, std::uint64_t bytes);

 private:
  Object* Allocate(Thread* self, RtClass* klass, std::size_t bytes);
  void MarkAndSweep(Thread* self);

  Runtime* runtime_;
  std::mutex lock_;
  std::vector<Object*> objects_;
  std::uint64_t bytes_live_ = 0;
  std::uint64_t bytes_since_gc_ = 0;
  std::uint64_t trigger_bytes_ = 64ull << 20;
  std::uint64_t max_bytes_ = 1ull << 30;
  std::uint32_t stress_every_ = 0;
  std::uint64_t alloc_count_ = 0;
  HeapStats stats_;
  std::vector<void*> quarantine_;  // stress mode: freed blocks never reused
  struct NativeAllocation {
    Object* owner;
    NativeFree free_fn;
    void* data;
    std::uint64_t bytes;
  };
  std::vector<NativeAllocation> native_allocations_;
};

}  // namespace arun::vm
