// Heap object layout for the DEX interpreter (ADR 0004).
//
// Every Java object starts with a 16-byte header. Instance fields follow as
// 8-byte slots (one per field, whatever its type) -- simple, always aligned, and
// it lets the GC find references through a per-class slot list. Arrays store
// elements compactly after a 24-byte header, because byte[] and char[] dominate
// memory.
//
// Ownership: objects are owned by the Heap and freed only by the collector.
// Thread affinity: field access follows the Java memory model through the
//   interpreter (volatile fields use atomic access); nothing here locks.
// Pointers: an Object* is a HOST pointer. Guest (ELF/JNI) code never sees one --
//   it gets JNI handles (spec 15: "an object handle is not an arbitrary stable raw
//   C++ object pointer").
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace arun::vm {

class RtClass;

struct Object {
  RtClass* klass;
  std::atomic<std::uint32_t> hash;  // identity hash; 0 = not yet assigned
  std::uint32_t gc_bits;            // owned by the collector
};
static_assert(sizeof(Object) == 16);

// One instance/static field slot.
union Slot {
  std::int32_t i;
  std::int64_t j;
  float f;
  double d;
  Object* l;
  std::uint64_t raw;
};
static_assert(sizeof(Slot) == 8);

inline Slot* FieldSlots(Object* o) { return reinterpret_cast<Slot*>(o + 1); }

struct Array : Object {
  std::int32_t length;
  std::uint32_t reserved;
  std::uint8_t* data() { return reinterpret_cast<std::uint8_t*>(this) + kDataOffset; }
  template <typename T>
  T* elements() { return reinterpret_cast<T*>(data()); }
  static constexpr std::size_t kDataOffset = 24;
};
static_assert(sizeof(Array) == 24);

// A value crossing a call boundary.
union JValue {
  std::int32_t i;
  std::int64_t j;
  float f;
  double d;
  Object* l;
  std::uint64_t raw;
};

}  // namespace arun::vm
