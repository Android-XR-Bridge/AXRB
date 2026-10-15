#include "arun/vm/heap.h"

#include <cstdlib>
#include <cstring>
#include <format>
#include <new>

#include "arun/log.h"
#include "arun/vm/class.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

namespace arun::vm {
namespace {

constexpr std::uint32_t kMarkBit = 1;
constexpr std::size_t kHeaderSize = sizeof(Object);

std::size_t RoundUp8(std::size_t n) { return (n + 7) & ~static_cast<std::size_t>(7); }

}  // namespace

Heap::Heap(Runtime* runtime) : runtime_(runtime) {}

Heap::~Heap() {
  for (const NativeAllocation& n : native_allocations_) n.free_fn(n.data);
  for (Object* o : objects_) std::free(o);
  for (void* p : quarantine_) std::free(p);
}

std::size_t Heap::SizeOf(Object* o) {
  RtClass* k = o->klass;
  if (k->IsArray()) {
    const auto* a = static_cast<Array*>(o);
    return RoundUp8(Array::kDataOffset + static_cast<std::size_t>(a->length) * k->component_size);
  }
  return kHeaderSize + sizeof(Slot) * k->num_instance_slots;
}

void Heap::SetLimits(std::uint64_t trigger_bytes, std::uint64_t max_bytes) {
  std::lock_guard<std::mutex> g(lock_);
  trigger_bytes_ = trigger_bytes;
  max_bytes_ = max_bytes;
}

Object* Heap::AllocObject(Thread* self, RtClass* klass) {
  return Allocate(self, klass, kHeaderSize + sizeof(Slot) * klass->num_instance_slots);
}

Array* Heap::AllocArray(Thread* self, RtClass* array_class, std::int32_t length) {
  if (length < 0) {
    runtime_->ThrowNew(self, "Ljava/lang/NegativeArraySizeException;", std::to_string(length));
    return nullptr;
  }
  const std::uint64_t bytes =
      Array::kDataOffset + static_cast<std::uint64_t>(length) * array_class->component_size;
  if (bytes > max_bytes_) {
    runtime_->ThrowNew(self, "Ljava/lang/OutOfMemoryError;",
                       std::format("Failed to allocate a {} byte array", bytes));
    return nullptr;
  }
  auto* a = static_cast<Array*>(Allocate(self, array_class, RoundUp8(static_cast<std::size_t>(bytes))));
  if (a != nullptr) a->length = length;
  return a;
}

Object* Heap::Clone(Thread* self, Object* original) {
  RootScope roots(self);
  auto h = roots.Add(original);
  const std::size_t size = SizeOf(original);
  Object* copy = Allocate(self, original->klass, size);
  if (copy == nullptr) return nullptr;
  Object* src = h.Get();
  std::memcpy(reinterpret_cast<std::uint8_t*>(copy) + kHeaderSize,
              reinterpret_cast<std::uint8_t*>(src) + kHeaderSize, size - kHeaderSize);
  return copy;
}

Object* Heap::Allocate(Thread* self, RtClass* klass, std::size_t bytes) {
  self->Safepoint();
  bool collect = false;
  {
    std::lock_guard<std::mutex> g(lock_);
    ++alloc_count_;
    collect = bytes_since_gc_ + bytes > trigger_bytes_ ||
              (stress_every_ != 0 && alloc_count_ % stress_every_ == 0);
  }
  if (collect) CollectGarbage(self);

  bool over_limit;
  {
    std::lock_guard<std::mutex> g(lock_);
    over_limit = bytes_live_ + bytes > max_bytes_;
  }
  if (over_limit) {
    CollectGarbage(self);
    std::lock_guard<std::mutex> g(lock_);
    over_limit = bytes_live_ + bytes > max_bytes_;
  }
  if (over_limit) {
    // Leave headroom so the OutOfMemoryError itself can be allocated.
    static thread_local bool throwing_oom = false;
    if (throwing_oom) return nullptr;
    throwing_oom = true;
    {
      std::lock_guard<std::mutex> g(lock_);
      max_bytes_ += 1 << 20;
    }
    runtime_->ThrowNew(self, "Ljava/lang/OutOfMemoryError;",
                       std::format("Failed to allocate {} bytes; heap limit reached", bytes));
    {
      std::lock_guard<std::mutex> g(lock_);
      max_bytes_ -= 1 << 20;
    }
    throwing_oom = false;
    return nullptr;
  }

  void* mem = std::calloc(1, bytes);
  if (mem == nullptr) {
    runtime_->ThrowNew(self, "Ljava/lang/OutOfMemoryError;", "host allocation failed");
    return nullptr;
  }
  auto* o = static_cast<Object*>(mem);
  o->klass = klass;
  o->hash.store(0, std::memory_order_relaxed);
  o->gc_bits = 0;
  std::lock_guard<std::mutex> g(lock_);
  objects_.push_back(o);
  bytes_live_ += bytes;
  bytes_since_gc_ += bytes;
  stats_.bytes_allocated_total += bytes;
  return o;
}

void Heap::RegisterNativeAllocation(Object* owner, NativeFree free_fn, void* data, std::uint64_t bytes) {
  std::lock_guard<std::mutex> g(lock_);
  native_allocations_.push_back(NativeAllocation{owner, free_fn, data, bytes});
  bytes_since_gc_ += bytes;
}

void Heap::CollectGarbage(Thread* self) {
  runtime_->SuspendAll(self);
  MarkAndSweep(self);
  runtime_->ResumeAll(self);
}

void Heap::MarkAndSweep(Thread* self) {
  // Everything else is stopped: no lock is needed for objects_, but take it
  // anyway so stats() from a parked thread's perspective stays consistent.
  std::lock_guard<std::mutex> g(lock_);

  RtClass* reference_class = runtime_->class_linker().FindClassNoThrow(
      self, "Ljava/lang/ref/Reference;", nullptr);
  const RtField* referent =
      reference_class != nullptr ? reference_class->FindDeclaredField("referent", "Ljava/lang/Object;") : nullptr;

  std::vector<Object*> work;
  std::vector<Object*> references;  // Reference objects whose referent is traced last
  work.reserve(4096);
  auto mark = [&](Object* o) {
    if ((o->gc_bits & kMarkBit) == 0) {
      o->gc_bits |= kMarkBit;
      work.push_back(o);
    }
  };
  runtime_->VisitRoots(mark);
  while (!work.empty()) {
    Object* o = work.back();
    work.pop_back();
    RtClass* k = o->klass;
    if (k->IsArray()) {
      if (!k->component->IsPrimitive()) {
        auto* a = static_cast<Array*>(o);
        Object** elems = a->elements<Object*>();
        for (std::int32_t i = 0; i < a->length; ++i) {
          if (elems[i] != nullptr) mark(elems[i]);
        }
      }
      continue;
    }
    const bool is_reference =
        referent != nullptr && reference_class != nullptr && k->IsSubclassOf(reference_class);
    Slot* slots = FieldSlots(o);
    for (std::uint32_t s : k->ref_slots) {
      if (is_reference && s == referent->slot) continue;  // weakly held
      if (slots[s].l != nullptr) mark(slots[s].l);
    }
    if (is_reference) references.push_back(o);
  }
  // Clear references whose referents are not otherwise reachable. (All
  // Reference kinds are treated as weak; soft references are cleared at the
  // first collection that finds them softly reachable, which the spec permits.)
  for (Object* r : references) {
    Slot& s = FieldSlots(r)[referent->slot];
    if (s.l != nullptr && (s.l->gc_bits & kMarkBit) == 0) s.l = nullptr;
  }

  runtime_->monitors().Sweep([](Object* o) { return (o->gc_bits & kMarkBit) != 0; });

  // Native memory of unreachable owners (before the owners themselves go).
  {
    std::size_t keep = 0;
    for (NativeAllocation& n : native_allocations_) {
      if ((n.owner->gc_bits & kMarkBit) != 0) {
        native_allocations_[keep++] = n;
      } else {
        n.free_fn(n.data);
      }
    }
    native_allocations_.resize(keep);
  }

  std::size_t kept = 0;
  std::uint64_t freed_objects = 0;
  for (Object* o : objects_) {
    if (o->gc_bits & kMarkBit) {
      o->gc_bits &= ~kMarkBit;
      objects_[kept++] = o;
      continue;
    }
    const std::size_t size = SizeOf(o);
    bytes_live_ -= size;
    ++freed_objects;
    if (stress_every_ != 0) {
      // Poison and never reuse: a stale pointer now fails loudly.
      std::memset(o, 0xdb, size);
      quarantine_.push_back(o);
    } else {
      std::free(o);
    }
  }
  objects_.resize(kept);
  bytes_since_gc_ = 0;
  stats_.collections++;
  stats_.objects_freed_total += freed_objects;
  if (stress_every_ == 0) {
    // Grow the trigger with the live set so a large steady-state heap does not
    // collect continuously.
    const std::uint64_t target = bytes_live_ * 2;
    if (target > trigger_bytes_) trigger_bytes_ = target;
  }
  ARUN_LOG_DEBUG(log::Category::kRuntime, "gc #{}: freed {} objects, {} live ({} bytes)",
                 stats_.collections, freed_objects, objects_.size(), bytes_live_);
}

HeapStats Heap::stats() {
  std::lock_guard<std::mutex> g(lock_);
  HeapStats s = stats_;
  s.objects_live = objects_.size();
  s.bytes_live = bytes_live_;
  return s;
}

}  // namespace arun::vm
