#pragma once
#include <cstddef>
#include <memory_resource>
#include <new>
#include <utility>
namespace berberis {
class Arena {
 public:
  void* Alloc(size_t size, size_t alignment) { return memory_.allocate(size ? size : 1, alignment); }
 private:
  std::pmr::monotonic_buffer_resource memory_;
};
template<class T, class... Args> T* NewInArena(Arena* arena, Args&&... args) {
  return new (arena->Alloc(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
}
template<class T> T* NewArrayInArena(Arena* arena, size_t size) {
  return new (arena->Alloc(sizeof(T) * size, alignof(T))) T[size];
}
template<class T> class ArenaAllocator {
 public:
  using value_type = T;
  ArenaAllocator(Arena* arena) : arena_(arena) {}
  template<class U> ArenaAllocator(const ArenaAllocator<U>& other) : arena_(other.arena()) {}
  T* allocate(size_t n) { return static_cast<T*>(arena_->Alloc(n * sizeof(T), alignof(T))); }
  void deallocate(T*, size_t) {}
  Arena* arena() const { return arena_; }
  template<class U> bool operator==(const ArenaAllocator<U>& other) const { return arena_ == other.arena(); }
 private:
  Arena* arena_;
};
}
