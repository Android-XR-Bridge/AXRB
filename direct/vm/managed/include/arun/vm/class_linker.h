// Class loading, linking, initialisation and constant-pool resolution (ADR 0004).
//
// Ownership: the ClassLinker owns every loader, DEX file, DexCache and class.
// Thread affinity: all entry points are thread-safe. Class *definition* happens
//   under one recursive lock and never allocates on the managed heap, so holding
//   it can never stall a collection. Exceptions are thrown only after the lock is
//   released. Class *initialisation* follows JLS 12.4.2 per class.
// Error propagation: methods that can fail return nullptr/false with a Java
//   exception pending on the calling thread -- the interpreter's contract.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arun/vm/class.h"
#include "arun/vm/dex_file.h"

namespace arun::vm {

class Runtime;
class Thread;

// Per (DEX file, loader) resolution cache. Entries are written once; a race
// between two threads resolving the same index stores the same value.
struct DexCache {
  const DexFile* dex = nullptr;
  ClassLoaderRt* loader = nullptr;
  std::unique_ptr<std::atomic<RtClass*>[]> types;
  std::unique_ptr<std::atomic<RtMethod*>[]> methods;
  std::unique_ptr<std::atomic<RtField*>[]> fields;
  std::unique_ptr<std::atomic<Object*>[]> strings;  // GC roots
};

struct ClassLoaderRt {
  std::string name;
  Object* java_object = nullptr;  // the java.lang.ClassLoader; null for boot
  ClassLoaderRt* parent = nullptr;
  std::vector<std::unique_ptr<DexFile>> dex_files;
  std::vector<std::unique_ptr<DexCache>> caches;  // parallel to dex_files
  std::unordered_map<std::string, RtClass*> classes;
};

enum class InvokeKind : std::uint8_t { kStatic, kDirect, kVirtual, kSuper, kInterface };

class ClassLinker {
 public:
  explicit ClassLinker(Runtime* runtime);
  ~ClassLinker();

  ClassLoaderRt* boot_loader() { return boot_.get(); }
  // Adds a DEX to the boot class path. Only before the runtime starts.
  void AddBootDex(std::unique_ptr<DexFile> dex);
  // An application class loader (dalvik.system.PathClassLoader semantics):
  // parent-first delegation to `parent`, then its own DEX files in order.
  ClassLoaderRt* CreateLoader(std::string name, ClassLoaderRt* parent,
                              std::vector<std::unique_ptr<DexFile>> dex_files);

  // Finds or loads a class. On failure returns nullptr with NoClassDefFoundError
  // pending (the interpreter's semantics for a missing referenced class).
  RtClass* FindClass(Thread* self, std::string_view descriptor, ClassLoaderRt* loader);
  // Like FindClass but never throws; returns nullptr if absent.
  RtClass* FindClassNoThrow(Thread* self, std::string_view descriptor, ClassLoaderRt* loader);
  RtClass* PrimitiveClass(char type) const;
  // A class `loader` has already loaded or been recorded as initiating loader
  // for; never loads anything.
  RtClass* LookupClass(std::string_view descriptor, ClassLoaderRt* loader);

  // A java.lang.reflect.Proxy subclass implementing `interfaces`: every
  // interface method (and Object's equals/hashCode/toString) becomes a native
  // method bound to `invoke`, with RtMethod::proxy_target naming the method it
  // stands for. Returned initialised; nullptr with an exception pending on failure.
  RtClass* DefineProxyClass(Thread* self, std::string descriptor, const std::vector<RtClass*>& interfaces,
                            ClassLoaderRt* loader, NativeFn invoke);

  // Runs <clinit> per JLS 12.4.2 if needed. False with an exception pending on
  // failure (ExceptionInInitializerError / NoClassDefFoundError).
  bool EnsureInitialized(Thread* self, RtClass* klass);

  RtClass* ResolveType(Thread* self, DexCache* cache, std::uint32_t type_idx);
  RtMethod* ResolveMethod(Thread* self, DexCache* cache, std::uint32_t method_idx, InvokeKind kind);
  RtField* ResolveField(Thread* self, DexCache* cache, std::uint32_t field_idx, bool is_static);
  Object* ResolveString(Thread* self, DexCache* cache, std::uint32_t string_idx);

  // The method that actually runs for an interface (or interface-declared)
  // method on a receiver of `receiver_class`. nullptr with AbstractMethodError /
  // IncompatibleClassChangeError pending if there is none.
  RtMethod* FindInterfaceTarget(Thread* self, RtClass* receiver_class, RtMethod* resolved);

  // Every loaded class, for GC root visiting and diagnostics.
  template <typename Fn>
  void ForEachClass(Fn&& fn) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    for (RtClass* c : all_classes_) fn(c);
  }
  template <typename Fn>
  void ForEachDexCache(Fn&& fn) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    for (DexCache* c : all_caches_) fn(c);
  }
  template <typename Fn>
  void ForEachLoader(Fn&& fn) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    fn(boot_.get());
    for (auto& l : app_loaders_) fn(l.get());
  }

  // Classes referenced but missing, for the compatibility report (spec 22).
  std::vector<std::string> MissingClasses();
  void NoteMissing(std::string what);

 private:
  // `absent` (optional) is set when the class exists nowhere in the loader
  // chain, as opposed to existing but failing to load.
  RtClass* DefineLocked(std::string_view descriptor, ClassLoaderRt* loader, std::string* error,
                        bool* absent = nullptr);
  RtClass* DefineFromDexLocked(ClassLoaderRt* loader, DexCache* cache, std::uint32_t class_def_idx,
                               std::string* error);
  RtClass* ArrayClassLocked(std::string_view descriptor, ClassLoaderRt* loader, std::string* error);
  bool LinkLocked(RtClass* klass, std::string* error);
  RtClass* LookupLocked(std::string_view descriptor, ClassLoaderRt* loader);
  void ThrowEarlierInitFailure(Thread* self, RtClass* c, const std::string& why, Object* cause);
  std::unique_ptr<DexCache> MakeCache(const DexFile* dex, ClassLoaderRt* loader);
  void CreatePrimitiveClasses();

  Runtime* runtime_;
  std::recursive_mutex lock_;
  std::unique_ptr<ClassLoaderRt> boot_;
  std::vector<std::unique_ptr<ClassLoaderRt>> app_loaders_;
  std::vector<std::unique_ptr<RtClass>> owned_;
  std::vector<RtClass*> all_classes_;
  std::vector<DexCache*> all_caches_;
  RtClass* primitives_[128] = {};
  std::mutex missing_lock_;
  std::vector<std::string> missing_;
};

}  // namespace arun::vm
