// Runtime class, method and field metadata (ADR 0004).
//
// Ownership: RtClass objects are owned by the ClassLinker and live until the
//   runtime is destroyed (no class unloading). RtMethod/RtField live inside their
//   class in std::deque, so their addresses are stable.
// Thread affinity: immutable after linking except `state` (atomic), the
//   interface-dispatch cache (own lock), and `statics` (Java memory model).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arun/vm/dex_file.h"
#include "arun/vm/object.h"

namespace arun::vm {

class RtClass;
class Thread;
struct ClassLoaderRt;
struct DexCache;
struct Frame;

// A host implementation of a Java `native` method (core library / framework).
// Arguments are in `args` registers [0, ins); the result goes to `*result`. A
// native reports failure by leaving an exception pending on `self`.
using NativeFn = void (*)(Thread* self, Frame* args, JValue* result);

struct RtField {
  RtClass* declaring = nullptr;
  std::string name;
  std::string type;  // descriptor
  char kind = 0;     // 'Z' 'B' 'C' 'S' 'I' 'J' 'F' 'D' 'L'
  std::uint32_t access = 0;
  std::uint32_t slot = 0;  // index into instance slots or class statics

  bool IsStatic() const { return (access & kAccStatic) != 0; }
  bool IsVolatile() const { return (access & kAccVolatile) != 0; }
  bool IsReference() const { return kind == 'L'; }
};

enum class PrepareState : std::uint8_t { kUnprepared, kPrepared, kFailed };

struct RtMethod {
  RtClass* declaring = nullptr;
  std::string name;
  std::string signature;  // "(Ljava/lang/String;)V"
  std::string shorty;     // "VL"
  std::uint32_t access = 0;
  std::uint32_t dex_method_idx = kDexNoIndex;
  std::uint32_t code_off = 0;
  // Registers occupied by the arguments, including `this` (wide args take two).
  std::uint32_t arg_words = 0;
  std::int32_t vtable_index = -1;

  // Filled by the interpreter the first time the method runs.
  std::atomic<PrepareState> prepare_state{PrepareState::kUnprepared};
  CodeItem code;
  std::string prepare_error;

  NativeFn native = nullptr;  // host native, bound at link time or first call
  void* jni_entry = nullptr;  // guest JNI entry point (ELF), bound by System.loadLibrary
  RtMethod* proxy_target = nullptr;  // for java.lang.reflect.Proxy methods: the interface method

  bool IsStatic() const { return (access & kAccStatic) != 0; }
  bool IsPrivate() const { return (access & kAccPrivate) != 0; }
  bool IsAbstract() const { return (access & kAccAbstract) != 0; }
  bool IsNative() const { return (access & kAccNative) != 0; }
  bool IsSynchronized() const { return (access & (kAccSynchronized | kAccDeclaredSynchronized)) != 0; }
  bool IsConstructor() const { return name == "<init>" || name == "<clinit>"; }
  bool IsDirect() const { return IsStatic() || IsPrivate() || name == "<init>"; }
  char ReturnKind() const { return shorty[0]; }
  // "Lpkg/Cls;.name(sig)" for diagnostics.
  std::string PrettyName() const;
};

enum class ClassState : std::uint8_t {
  kLoading,      // defined, supertypes being resolved (detects circularity)
  kLinked,       // layout, vtable, statics ready
  kInitializing, // <clinit> running on `init_thread`
  kInitialized,
  kError,        // linking or initialisation failed; `error` says why
};

class RtClass {
 public:
  std::string descriptor;  // "Ljava/lang/String;", "[I", "I"
  std::uint32_t access = 0;
  RtClass* super = nullptr;
  std::vector<RtClass*> interfaces;      // direct
  std::vector<RtClass*> all_interfaces;  // transitive closure, deduplicated
  ClassLoaderRt* loader = nullptr;       // nullptr = boot class loader
  DexCache* dex_cache = nullptr;         // nullptr for array/primitive classes
  std::uint32_t class_def_idx = kDexNoIndex;

  std::deque<RtField> sfields;
  std::deque<RtField> ifields;  // declared by this class only
  std::deque<RtMethod> methods;
  std::vector<RtMethod*> vtable;

  std::uint32_t num_instance_slots = 0;        // including superclasses
  std::vector<std::uint32_t> ref_slots;        // instance reference slots, incl. supers
  std::vector<Slot> statics;
  std::vector<std::uint32_t> static_ref_slots;

  // Arrays and primitives.
  RtClass* component = nullptr;
  char primitive = 0;               // 'I', 'Z', ... for primitive classes
  std::uint32_t component_size = 0; // for array classes

  std::atomic<ClassState> state{ClassState::kLoading};
  Thread* init_thread = nullptr;
  std::string error;  // why the class is erroneous
  // The throwable that made initialisation fail (a GC root); the cause of the
  // NoClassDefFoundError every later use of the class gets.
  Object* init_error = nullptr;

  Object* mirror = nullptr;  // java.lang.Class instance, created on demand

  bool IsInterface() const { return (access & kAccInterface) != 0; }
  bool IsAbstract() const { return (access & kAccAbstract) != 0; }
  bool IsArray() const { return component != nullptr; }
  bool IsPrimitive() const { return primitive != 0; }
  bool IsFinal() const { return (access & kAccFinal) != 0; }

  // Java assignability: can a value of class `from` be stored in a variable of
  // this type?
  bool IsAssignableFrom(const RtClass* from) const;
  bool IsSubclassOf(const RtClass* base) const;

  RtMethod* FindDeclaredMethod(std::string_view name, std::string_view signature);
  RtField* FindDeclaredField(std::string_view name, std::string_view type);
  // Java package, e.g. "java/lang" -- for package-private access checks.
  std::string_view PackageName() const;

  // Interface dispatch cache: resolved interface method -> implementation.
  std::mutex dispatch_lock;
  std::unordered_map<const RtMethod*, RtMethod*> dispatch_cache;

  // Initialisation protocol (JLS 12.4.2).
  std::mutex init_lock;
  std::condition_variable init_cv;
};

}  // namespace arun::vm
