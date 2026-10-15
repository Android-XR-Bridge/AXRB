#include "arun/vm/class_linker.h"

#include <algorithm>
#include <format>

#include "arun/log.h"
#include "arun/vm/interpreter.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

namespace arun::vm {
namespace {

constexpr char kObject[] = "Ljava/lang/Object;";

std::uint32_t ShortyWords(std::string_view shorty, bool is_static) {
  std::uint32_t words = is_static ? 0 : 1;
  for (std::size_t i = 1; i < shorty.size(); ++i) words += (shorty[i] == 'J' || shorty[i] == 'D') ? 2 : 1;
  return words;
}

char KindOf(std::string_view descriptor) {
  return (descriptor[0] == 'L' || descriptor[0] == '[') ? 'L' : descriptor[0];
}

std::uint32_t PrimitiveSize(char p) {
  switch (p) {
    case 'Z': case 'B': return 1;
    case 'C': case 'S': return 2;
    case 'I': case 'F': return 4;
    case 'J': case 'D': return 8;
    default: return 0;
  }
}

std::string Dotted(std::string_view descriptor) {
  std::string_view d = descriptor;
  if (d.size() >= 2 && d[0] == 'L' && d.back() == ';') d = d.substr(1, d.size() - 2);
  std::string out(d);
  for (char& c : out) if (c == '/') c = '.';
  return out;
}

bool CanOverride(const RtMethod* super_method, const RtClass* klass) {
  const std::uint32_t a = super_method->access;
  if (a & kAccPrivate) return false;
  if (a & (kAccPublic | kAccProtected)) return true;
  // Package-private: only within the same runtime package.
  return super_method->declaring->loader == klass->loader &&
         super_method->declaring->PackageName() == klass->PackageName();
}

}  // namespace

ClassLinker::ClassLinker(Runtime* runtime) : runtime_(runtime) {
  boot_ = std::make_unique<ClassLoaderRt>();
  boot_->name = "boot";
  CreatePrimitiveClasses();
}

ClassLinker::~ClassLinker() = default;

void ClassLinker::CreatePrimitiveClasses() {
  for (char p : std::string_view("ZBCSIJFDV")) {
    auto c = std::make_unique<RtClass>();
    c->descriptor = std::string(1, p);
    c->primitive = p;
    c->access = kAccPublic | kAccFinal | kAccAbstract;
    c->state.store(ClassState::kInitialized);
    primitives_[static_cast<unsigned char>(p)] = c.get();
    all_classes_.push_back(c.get());
    owned_.push_back(std::move(c));
  }
}

RtClass* ClassLinker::PrimitiveClass(char type) const {
  return primitives_[static_cast<unsigned char>(type) & 0x7f];
}

std::unique_ptr<DexCache> ClassLinker::MakeCache(const DexFile* dex, ClassLoaderRt* loader) {
  auto c = std::make_unique<DexCache>();
  c->dex = dex;
  c->loader = loader;
  c->types.reset(new std::atomic<RtClass*>[dex->num_types()]());
  c->methods.reset(new std::atomic<RtMethod*>[dex->num_methods()]());
  c->fields.reset(new std::atomic<RtField*>[dex->num_fields()]());
  c->strings.reset(new std::atomic<Object*>[dex->num_strings()]());
  return c;
}

void ClassLinker::AddBootDex(std::unique_ptr<DexFile> dex) {
  std::lock_guard<std::recursive_mutex> g(lock_);
  auto cache = MakeCache(dex.get(), boot_.get());
  all_caches_.push_back(cache.get());
  boot_->caches.push_back(std::move(cache));
  boot_->dex_files.push_back(std::move(dex));
}

ClassLoaderRt* ClassLinker::CreateLoader(std::string name, ClassLoaderRt* parent,
                                         std::vector<std::unique_ptr<DexFile>> dex_files) {
  std::lock_guard<std::recursive_mutex> g(lock_);
  auto loader = std::make_unique<ClassLoaderRt>();
  loader->name = std::move(name);
  loader->parent = parent != nullptr ? parent : boot_.get();
  for (auto& dex : dex_files) {
    auto cache = MakeCache(dex.get(), loader.get());
    all_caches_.push_back(cache.get());
    loader->caches.push_back(std::move(cache));
    loader->dex_files.push_back(std::move(dex));
  }
  ClassLoaderRt* raw = loader.get();
  app_loaders_.push_back(std::move(loader));
  return raw;
}

RtClass* ClassLinker::LookupLocked(std::string_view descriptor, ClassLoaderRt* loader) {
  auto it = loader->classes.find(std::string(descriptor));
  return it == loader->classes.end() ? nullptr : it->second;
}

RtClass* ClassLinker::LookupClass(std::string_view descriptor, ClassLoaderRt* loader) {
  std::lock_guard<std::recursive_mutex> g(lock_);
  RtClass* c = LookupLocked(descriptor, loader != nullptr ? loader : boot_.get());
  return c != nullptr && c->state.load() != ClassState::kLoading && c->state.load() != ClassState::kError ? c : nullptr;
}

RtClass* ClassLinker::FindClassNoThrow(Thread* self, std::string_view descriptor, ClassLoaderRt* loader) {
  std::string error;
  std::lock_guard<std::recursive_mutex> g(lock_);
  return DefineLocked(descriptor, loader != nullptr ? loader : boot_.get(), &error);
}

RtClass* ClassLinker::FindClass(Thread* self, std::string_view descriptor, ClassLoaderRt* loader) {
  std::string error;
  RtClass* c;
  {
    std::lock_guard<std::recursive_mutex> g(lock_);
    c = DefineLocked(descriptor, loader != nullptr ? loader : boot_.get(), &error);
  }
  if (c == nullptr) {
    NoteMissing(std::string(descriptor));
    // Thrown after the lock is released: creating the error allocates.
    runtime_->ThrowNew(self, "Ljava/lang/NoClassDefFoundError;",
                       error.empty() ? Dotted(descriptor) : error);
  }
  return c;
}

RtClass* ClassLinker::DefineLocked(std::string_view descriptor, ClassLoaderRt* loader,
                                   std::string* error, bool* absent) {
  if (absent != nullptr) *absent = false;
  if (descriptor.empty()) {
    *error = "empty class descriptor";
    return nullptr;
  }
  if (descriptor.size() == 1) {
    RtClass* p = PrimitiveClass(descriptor[0]);
    if (p == nullptr) *error = std::format("'{}' is not a primitive type", descriptor);
    return p;
  }
  if (descriptor[0] == '[') return ArrayClassLocked(descriptor, loader, error);

  if (RtClass* existing = LookupLocked(descriptor, loader)) {
    const ClassState st = existing->state.load();
    if (st == ClassState::kLoading) {
      *error = std::format("ClassCircularityError: {}", Dotted(descriptor));
      return nullptr;
    }
    if (st == ClassState::kError) {
      *error = existing->error;
      return nullptr;
    }
    return existing;
  }

  // Parent-first delegation (dalvik.system.BaseDexClassLoader semantics).
  if (loader != boot_.get()) {
    std::string parent_error;
    bool parent_absent = false;
    ClassLoaderRt* parent = loader->parent != nullptr ? loader->parent : boot_.get();
    if (RtClass* c = DefineLocked(descriptor, parent, &parent_error, &parent_absent)) {
      loader->classes.emplace(std::string(descriptor), c);  // initiating loader
      return c;
    }
    // A class that exists in the parent but failed to link is an error, not
    // something the child may shadow.
    if (!parent_absent) {
      *error = parent_error;
      return nullptr;
    }
  }

  for (std::size_t i = 0; i < loader->dex_files.size(); ++i) {
    if (auto idx = loader->dex_files[i]->FindClassDef(descriptor)) {
      return DefineFromDexLocked(loader, loader->caches[i].get(), *idx, error);
    }
  }
  *error = std::format("Class not found: {} (loader {})", Dotted(descriptor), loader->name);
  if (absent != nullptr) *absent = true;
  return nullptr;
}

RtClass* ClassLinker::ArrayClassLocked(std::string_view descriptor, ClassLoaderRt* loader,
                                       std::string* error) {
  RtClass* component = DefineLocked(descriptor.substr(1), loader, error);
  if (component == nullptr) return nullptr;
  if (component->primitive == 'V') {
    *error = "array of void";
    return nullptr;
  }
  // The array class belongs to the component's defining loader.
  ClassLoaderRt* owner = component->loader != nullptr ? component->loader : boot_.get();
  if (RtClass* existing = LookupLocked(descriptor, owner)) {
    if (owner != loader) loader->classes.emplace(std::string(descriptor), existing);
    return existing;
  }
  RtClass* object = DefineLocked(kObject, boot_.get(), error);
  RtClass* cloneable = DefineLocked("Ljava/lang/Cloneable;", boot_.get(), error);
  RtClass* serializable = DefineLocked("Ljava/io/Serializable;", boot_.get(), error);
  if (object == nullptr || cloneable == nullptr || serializable == nullptr) return nullptr;

  auto c = std::make_unique<RtClass>();
  c->descriptor = std::string(descriptor);
  c->component = component;
  c->component_size = component->IsPrimitive() ? PrimitiveSize(component->primitive) : 8;
  c->access = kAccFinal | kAccAbstract | (component->access & (kAccPublic | kAccProtected | kAccPrivate));
  c->super = object;
  c->interfaces = {cloneable, serializable};
  c->all_interfaces = {cloneable, serializable};
  c->loader = component->loader;
  c->vtable = object->vtable;
  c->num_instance_slots = 0;
  c->state.store(ClassState::kInitialized);
  RtClass* raw = c.get();
  owner->classes.emplace(raw->descriptor, raw);
  if (owner != loader) loader->classes.emplace(raw->descriptor, raw);
  all_classes_.push_back(raw);
  owned_.push_back(std::move(c));
  return raw;
}

RtClass* ClassLinker::DefineFromDexLocked(ClassLoaderRt* loader, DexCache* cache,
                                          std::uint32_t class_def_idx, std::string* error) {
  const DexFile& dex = *cache->dex;
  const ClassDef& def = dex.GetClassDef(class_def_idx);
  const std::string_view descriptor = dex.TypeDescriptor(def.class_idx);

  auto owned = std::make_unique<RtClass>();
  RtClass* c = owned.get();
  c->descriptor = std::string(descriptor);
  c->access = def.access_flags & 0xffff;
  c->loader = loader == boot_.get() ? nullptr : loader;
  c->dex_cache = cache;
  c->class_def_idx = class_def_idx;
  c->state.store(ClassState::kLoading);
  loader->classes.emplace(c->descriptor, c);
  all_classes_.push_back(c);
  owned_.push_back(std::move(owned));

  auto fail = [&](std::string why) -> RtClass* {
    c->error = why;
    c->state.store(ClassState::kError);
    *error = std::move(why);
    ARUN_LOG_WARN(log::Category::kRuntime, "class {} failed to load: {}", c->descriptor, *error);
    return nullptr;
  };

  if (descriptor == kObject) {
    if (def.superclass_idx != kDexNoIndex) return fail("java.lang.Object declares a superclass");
  } else {
    if (def.superclass_idx == kDexNoIndex) return fail(Dotted(descriptor) + " has no superclass");
    std::string super_error;
    RtClass* super = DefineLocked(dex.TypeDescriptor(def.superclass_idx), loader, &super_error);
    if (super == nullptr) {
      return fail(std::format("{} (superclass of {})", super_error, Dotted(descriptor)));
    }
    if (super->IsInterface()) return fail(std::format("IncompatibleClassChangeError: superclass of {} is an interface", Dotted(descriptor)));
    if (super->IsFinal() || super->IsArray() || super->IsPrimitive()) {
      return fail(std::format("VerifyError: {} extends final class {}", Dotted(descriptor), Dotted(super->descriptor)));
    }
    c->super = super;
  }
  for (std::uint16_t t : dex.ClassInterfaces(def)) {
    std::string iface_error;
    RtClass* iface = DefineLocked(dex.TypeDescriptor(t), loader, &iface_error);
    if (iface == nullptr) return fail(std::format("{} (interface of {})", iface_error, Dotted(descriptor)));
    if (!iface->IsInterface()) {
      return fail(std::format("IncompatibleClassChangeError: {} implements non-interface {}",
                              Dotted(descriptor), Dotted(iface->descriptor)));
    }
    c->interfaces.push_back(iface);
  }
  if (!LinkLocked(c, error)) return fail(*error);
  c->state.store(ClassState::kLinked);
  return c;
}

bool ClassLinker::LinkLocked(RtClass* c, std::string* error) {
  const DexFile& dex = *c->dex_cache->dex;
  const ClassDef& def = dex.GetClassDef(c->class_def_idx);
  auto data = dex.ReadClassData(def);
  if (!data.ok()) {
    *error = data.status().message();
    return false;
  }

  // Transitive interfaces: the superclass's, then each direct interface and its own.
  if (c->super != nullptr) c->all_interfaces = c->super->all_interfaces;
  for (RtClass* i : c->interfaces) {
    auto add = [&](RtClass* x) {
      if (std::find(c->all_interfaces.begin(), c->all_interfaces.end(), x) == c->all_interfaces.end()) {
        c->all_interfaces.push_back(x);
      }
    };
    add(i);
    for (RtClass* j : i->all_interfaces) add(j);
  }

  // Fields.
  const std::uint32_t super_slots = c->super != nullptr ? c->super->num_instance_slots : 0;
  if (c->super != nullptr) c->ref_slots = c->super->ref_slots;
  std::uint32_t slot = super_slots;
  for (const EncodedField& ef : data->instance_fields) {
    const FieldId& fid = dex.GetFieldId(ef.field_idx);
    if (fid.class_idx != def.class_idx) {
      *error = "instance field declared for another class";
      return false;
    }
    RtField& f = c->ifields.emplace_back();
    f.declaring = c;
    f.name = std::string(dex.StringMutf8(fid.name_idx));
    f.type = std::string(dex.TypeDescriptor(fid.type_idx));
    f.kind = KindOf(f.type);
    f.access = ef.access_flags & ~kAccStatic;
    f.slot = slot++;
    if (f.kind == 'L') c->ref_slots.push_back(f.slot);
  }
  c->num_instance_slots = slot;
  std::uint32_t sslot = 0;
  for (const EncodedField& ef : data->static_fields) {
    const FieldId& fid = dex.GetFieldId(ef.field_idx);
    if (fid.class_idx != def.class_idx) {
      *error = "static field declared for another class";
      return false;
    }
    RtField& f = c->sfields.emplace_back();
    f.declaring = c;
    f.name = std::string(dex.StringMutf8(fid.name_idx));
    f.type = std::string(dex.TypeDescriptor(fid.type_idx));
    f.kind = KindOf(f.type);
    f.access = ef.access_flags | kAccStatic;
    f.slot = sslot++;
    if (f.kind == 'L') c->static_ref_slots.push_back(f.slot);
  }
  c->statics.assign(sslot, Slot{});

  // Methods.
  auto add_method = [&](const EncodedMethod& em, bool is_virtual) -> RtMethod* {
    const MethodId& mid = dex.GetMethodId(em.method_idx);
    if (mid.class_idx != def.class_idx) return nullptr;
    RtMethod& m = c->methods.emplace_back();
    m.declaring = c;
    m.name = std::string(dex.StringMutf8(mid.name_idx));
    m.signature = dex.ProtoSignature(mid.proto_idx);
    m.shorty = std::string(dex.ProtoShorty(mid.proto_idx));
    m.access = em.access_flags;
    m.dex_method_idx = em.method_idx;
    m.code_off = em.code_off;
    m.arg_words = ShortyWords(m.shorty, m.IsStatic());
    (void)is_virtual;
    return &m;
  };
  for (const EncodedMethod& em : data->direct_methods) {
    if (add_method(em, false) == nullptr) {
      *error = "direct method declared for another class";
      return false;
    }
  }
  std::vector<RtMethod*> virtuals;
  for (const EncodedMethod& em : data->virtual_methods) {
    RtMethod* m = add_method(em, true);
    if (m == nullptr) {
      *error = "virtual method declared for another class";
      return false;
    }
    if (m->IsStatic() || m->IsPrivate() || m->IsConstructor()) {
      *error = std::format("{} is listed as virtual but is static/private/a constructor", m->PrettyName());
      return false;
    }
    virtuals.push_back(m);
  }

  // Virtual table (classes only; interface methods are dispatched by lookup).
  if (!c->IsInterface()) {
    if (c->super != nullptr) c->vtable = c->super->vtable;
    for (RtMethod* m : virtuals) {
      bool replaced = false;
      for (std::size_t i = 0; i < c->vtable.size(); ++i) {
        RtMethod* sm = c->vtable[i];
        if (sm->name == m->name && sm->signature == m->signature && CanOverride(sm, c)) {
          if (sm->access & kAccFinal) {
            *error = std::format("VerifyError: {} overrides final method {}", m->PrettyName(), sm->PrettyName());
            return false;
          }
          c->vtable[i] = m;
          m->vtable_index = static_cast<std::int32_t>(i);
          replaced = true;
          // Keep scanning: a package-private and a public method with the same
          // signature can both be overridden by this one.
        }
      }
      if (!replaced) {
        m->vtable_index = static_cast<std::int32_t>(c->vtable.size());
        c->vtable.push_back(m);
      }
    }
    if (!c->IsAbstract()) {
      // A concrete class must not leave abstract class methods in its vtable.
      for (RtMethod* vm : c->vtable) {
        if (vm->IsAbstract() && !vm->declaring->IsInterface()) {
          // Tolerated by ART until call time (AbstractMethodError), so tolerated here.
          break;
        }
      }
    }
  }
  return true;
}

RtClass* ClassLinker::DefineProxyClass(Thread* self, std::string descriptor, const std::vector<RtClass*>& interfaces,
                                       ClassLoaderRt* loader, NativeFn invoke) {
  RtClass* super = FindClass(self, "Ljava/lang/reflect/Proxy;", nullptr);
  if (super == nullptr || !EnsureInitialized(self, super)) return nullptr;
  RtClass* object = FindClass(self, kObject, nullptr);
  if (object == nullptr) return nullptr;
  RtClass* c = nullptr;
  {
    std::lock_guard<std::recursive_mutex> g(lock_);
    ClassLoaderRt* owner = loader != nullptr ? loader : boot_.get();
    if (LookupLocked(descriptor, owner) != nullptr) {
      c = nullptr;
    } else {
      auto owned = std::make_unique<RtClass>();
      c = owned.get();
      c->descriptor = descriptor;
      c->access = kAccPublic | kAccFinal;
      c->loader = loader == boot_.get() ? nullptr : loader;
      c->super = super;
      c->interfaces = interfaces;
      c->all_interfaces = super->all_interfaces;
      for (RtClass* i : interfaces) {
        auto add = [&](RtClass* x) {
          if (std::find(c->all_interfaces.begin(), c->all_interfaces.end(), x) == c->all_interfaces.end()) {
            c->all_interfaces.push_back(x);
          }
        };
        add(i);
        for (RtClass* j : i->all_interfaces) add(j);
      }
      c->num_instance_slots = super->num_instance_slots;
      c->ref_slots = super->ref_slots;

      // One native method per distinct name+signature, Object's first.
      std::vector<RtMethod*> targets;
      auto consider = [&](RtMethod* m) {
        if (m->IsStatic() || m->IsPrivate() || m->IsConstructor()) return;
        for (RtMethod* t : targets) {
          if (t->name == m->name && t->signature == m->signature) return;
        }
        targets.push_back(m);
      };
      for (const char* sig : {"equals(Ljava/lang/Object;)Z", "hashCode()I", "toString()Ljava/lang/String;"}) {
        std::string_view s(sig);
        const std::size_t paren = s.find('(');
        RtMethod* m = object->FindDeclaredMethod(s.substr(0, paren), s.substr(paren));
        if (m != nullptr) consider(m);
      }
      for (RtClass* i : c->all_interfaces) {
        for (RtMethod& m : i->methods) consider(&m);
      }
      c->vtable = super->vtable;
      for (RtMethod* t : targets) {
        RtMethod& m = c->methods.emplace_back();
        m.declaring = c;
        m.name = t->name;
        m.signature = t->signature;
        m.shorty = t->shorty;
        m.access = kAccPublic | kAccFinal | kAccNative;
        m.arg_words = ShortyWords(m.shorty, false);
        m.native = invoke;
        m.proxy_target = t;
        bool replaced = false;
        for (std::size_t i = 0; i < c->vtable.size(); ++i) {
          if (c->vtable[i]->name == m.name && c->vtable[i]->signature == m.signature) {
            c->vtable[i] = &m;
            m.vtable_index = static_cast<std::int32_t>(i);
            replaced = true;
          }
        }
        if (!replaced) {
          m.vtable_index = static_cast<std::int32_t>(c->vtable.size());
          c->vtable.push_back(&m);
        }
      }
      owner->classes.emplace(c->descriptor, c);
      all_classes_.push_back(c);
      owned_.push_back(std::move(owned));
      c->state.store(ClassState::kInitialized, std::memory_order_release);
    }
  }
  if (c == nullptr) {
    runtime_->ThrowNew(self, "Ljava/lang/IllegalArgumentException;", "proxy class " + descriptor + " already exists");
  }
  return c;
}

void ClassLinker::ThrowEarlierInitFailure(Thread* self, RtClass* c, const std::string& why, Object* cause) {
  RootScope roots(self);
  auto hcause = roots.Add(cause);
  // With a recorded cause the message stays short, as on the JDK; the cause
  // carries the details.
  runtime_->ThrowNew(self, "Ljava/lang/NoClassDefFoundError;",
                     hcause.Get() != nullptr ? std::format("Could not initialize class {}", Dotted(c->descriptor))
                                             : std::format("Could not initialize class {}: {}", Dotted(c->descriptor), why));
  if (hcause.Get() == nullptr) return;
  auto ncdfe = roots.Add(self->exception());
  RtMethod* init_cause = runtime_->FindMethod(ncdfe.Get()->klass, "initCause",
                                              "(Ljava/lang/Throwable;)Ljava/lang/Throwable;");
  if (init_cause == nullptr) return;
  self->ClearException();
  JValue arg, unused;
  arg.l = hcause.Get();
  if (runtime_->Invoke(self, init_cause, ncdfe.Get(), std::span<const JValue>(&arg, 1), &unused)) {
    self->SetException(ncdfe.Get());
  }
}

bool ClassLinker::EnsureInitialized(Thread* self, RtClass* c) {
  ClassState st = c->state.load(std::memory_order_acquire);
  if (st == ClassState::kInitialized) return true;

  {
    std::unique_lock<std::mutex> l(c->init_lock);
    for (;;) {
      st = c->state.load(std::memory_order_acquire);
      if (st == ClassState::kInitialized) return true;
      if (st == ClassState::kError) {
        const std::string why = c->error;
        Object* cause = c->init_error;
        l.unlock();
        ThrowEarlierInitFailure(self, c, why, cause);
        return false;
      }
      if (st == ClassState::kInitializing) {
        if (c->init_thread == self) return true;  // recursive request (JLS 12.4.2 step 3)
        // Wait for the other thread, without holding up a collection meanwhile.
        self->TransitionFromRunnable(ThreadState::kBlocked);
        c->init_cv.wait(l);
        l.unlock();
        self->TransitionToRunnable();
        l.lock();
        continue;
      }
      c->state.store(ClassState::kInitializing, std::memory_order_release);
      c->init_thread = self;
      break;
    }
  }

  auto finish = [&](ClassState result, std::string why) {
    std::lock_guard<std::mutex> g(c->init_lock);
    if (result == ClassState::kError) {
      c->error = std::move(why);
      c->init_error = self->exception();
    }
    c->init_thread = nullptr;
    c->state.store(result, std::memory_order_release);
    c->init_cv.notify_all();
  };

  if (c->super != nullptr && !EnsureInitialized(self, c->super)) {
    finish(ClassState::kError, "superclass initialisation failed");
    return false;
  }

  // Static field initial values from the DEX (ConstantValue equivalents).
  const DexFile& dex = *c->dex_cache->dex;
  const ClassDef& def = dex.GetClassDef(c->class_def_idx);
  auto values = dex.ReadEncodedArray(def.static_values_off);
  if (!values.ok()) {
    finish(ClassState::kError, values.status().message());
    runtime_->ThrowNew(self, "Ljava/lang/VerifyError;", values.status().message());
    return false;
  }
  for (std::size_t i = 0; i < values->size() && i < c->sfields.size(); ++i) {
    const EncodedValue& v = (*values)[i];
    Slot& s = c->statics[c->sfields[i].slot];
    switch (v.type) {
      case EncodedValueType::kString: {
        Object* str = ResolveString(self, c->dex_cache, static_cast<std::uint32_t>(v.bits));
        if (str == nullptr) {
          finish(ClassState::kError, "string constant");
          return false;
        }
        s.l = str;
        break;
      }
      case EncodedValueType::kType: {
        RtClass* t = ResolveType(self, c->dex_cache, static_cast<std::uint32_t>(v.bits));
        Object* mirror = t != nullptr ? runtime_->ClassMirror(self, t) : nullptr;
        if (mirror == nullptr) {
          finish(ClassState::kError, "class constant");
          return false;
        }
        s.l = mirror;
        break;
      }
      case EncodedValueType::kNull:
        s.l = nullptr;
        break;
      default:
        // Numerics arrive already sign/zero-extended, floats and doubles as their
        // exact IEEE bits -- a plain copy is the slot value.
        s.raw = v.bits;
        break;
    }
  }

  if (RtMethod* clinit = c->FindDeclaredMethod("<clinit>", "()V"); clinit != nullptr && clinit->IsStatic()) {
    JValue unused;
    runtime_->Invoke(self, clinit, nullptr, {}, &unused);
  }
  if (self->IsExceptionPending()) {
    Object* ex = self->exception();
    RootScope roots(self);
    auto h = roots.Add(ex);
    self->ClearException();
    std::string why = runtime_->DescribeException(self, h.Get());
    RtClass* error_class = FindClassNoThrow(self, "Ljava/lang/Error;", nullptr);
    if (error_class != nullptr && !error_class->IsAssignableFrom(h.Get()->klass)) {
      // Wrap per JLS: ExceptionInInitializerError(cause).
      RtClass* eiie = FindClassNoThrow(self, "Ljava/lang/ExceptionInInitializerError;", nullptr);
      RtMethod* ctor = eiie != nullptr ? eiie->FindDeclaredMethod("<init>", "(Ljava/lang/Throwable;)V") : nullptr;
      Object* wrapped = (ctor != nullptr && EnsureInitialized(self, eiie))
                            ? runtime_->heap().AllocObject(self, eiie) : nullptr;
      if (wrapped != nullptr) {
        auto hw = roots.Add(wrapped);
        JValue arg;
        arg.l = h.Get();
        JValue unused;
        if (runtime_->Invoke(self, ctor, hw.Get(), std::span<const JValue>(&arg, 1), &unused)) {
          self->SetException(hw.Get());
        }
      }
      if (!self->IsExceptionPending()) self->SetException(h.Get());
    } else {
      self->SetException(h.Get());
    }
    finish(ClassState::kError, why);
    return false;
  }
  finish(ClassState::kInitialized, {});
  return true;
}

RtClass* ClassLinker::ResolveType(Thread* self, DexCache* cache, std::uint32_t type_idx) {
  if (RtClass* c = cache->types[type_idx].load(std::memory_order_acquire)) return c;
  RtClass* c = FindClass(self, cache->dex->TypeDescriptor(type_idx),
                         cache->loader != nullptr ? cache->loader : boot_.get());
  if (c != nullptr) cache->types[type_idx].store(c, std::memory_order_release);
  return c;
}

namespace {

RtMethod* FindInClassChain(RtClass* c, std::string_view name, std::string_view sig) {
  for (RtClass* k = c; k != nullptr; k = k->super) {
    if (RtMethod* m = k->FindDeclaredMethod(name, sig)) return m;
  }
  return nullptr;
}

RtMethod* FindInInterfaces(RtClass* c, std::string_view name, std::string_view sig) {
  for (RtClass* i : c->all_interfaces) {
    if (RtMethod* m = i->FindDeclaredMethod(name, sig)) {
      if (!m->IsStatic() || i == c) return m;
    }
  }
  return nullptr;
}

}  // namespace

RtMethod* ClassLinker::ResolveMethod(Thread* self, DexCache* cache, std::uint32_t method_idx,
                                     InvokeKind kind) {
  if (RtMethod* m = cache->methods[method_idx].load(std::memory_order_acquire)) return m;
  const DexFile& dex = *cache->dex;
  const MethodId& mid = dex.GetMethodId(method_idx);
  RtClass* c = ResolveType(self, cache, mid.class_idx);
  if (c == nullptr) return nullptr;
  if (c->IsArray()) c = c->super;  // array methods are Object's (clone is special-cased natively)
  const std::string_view name = dex.StringMutf8(mid.name_idx);
  const std::string sig = dex.ProtoSignature(mid.proto_idx);

  RtMethod* m = nullptr;
  if (c->IsInterface()) {
    m = c->FindDeclaredMethod(name, sig);
    if (m == nullptr) m = FindInInterfaces(c, name, sig);
    if (m == nullptr) {
      // Interfaces inherit Object's public methods for resolution purposes.
      RtClass* object = c->super;
      if (object != nullptr) {
        RtMethod* om = object->FindDeclaredMethod(name, sig);
        if (om != nullptr && (om->access & kAccPublic)) m = om;
      }
    }
  } else {
    if (kind == InvokeKind::kInterface) {
      runtime_->ThrowNew(self, "Ljava/lang/IncompatibleClassChangeError;",
                         std::format("Found class {}, but interface was expected", Dotted(c->descriptor)));
      return nullptr;
    }
    m = FindInClassChain(c, name, sig);
    if (m == nullptr) m = FindInInterfaces(c, name, sig);
  }
  if (m == nullptr) {
    const std::string what = std::format("{}.{}{}", c->descriptor, name, sig);
    NoteMissing(what);
    runtime_->ThrowNew(self, "Ljava/lang/NoSuchMethodError;",
                       std::format("No {} method {}{} in class {} or its super classes",
                                   kind == InvokeKind::kStatic ? "static" : kind == InvokeKind::kInterface ? "interface" : "virtual/direct",
                                   name, sig, c->descriptor));
    return nullptr;
  }
  cache->methods[method_idx].store(m, std::memory_order_release);
  return m;
}

RtField* ClassLinker::ResolveField(Thread* self, DexCache* cache, std::uint32_t field_idx, bool is_static) {
  if (RtField* f = cache->fields[field_idx].load(std::memory_order_acquire)) {
    if (f->IsStatic() == is_static) return f;
  }
  const DexFile& dex = *cache->dex;
  const FieldId& fid = dex.GetFieldId(field_idx);
  RtClass* c = ResolveType(self, cache, fid.class_idx);
  if (c == nullptr) return nullptr;
  const std::string_view name = dex.StringMutf8(fid.name_idx);
  const std::string_view type = dex.TypeDescriptor(fid.type_idx);

  // JVMS 5.4.3.2: the class, then its superinterfaces, then its superclass.
  RtField* f = nullptr;
  for (RtClass* k = c; k != nullptr && f == nullptr; k = k->super) {
    f = k->FindDeclaredField(name, type);
    if (f == nullptr) {
      for (RtClass* i : k->all_interfaces) {
        if ((f = i->FindDeclaredField(name, type)) != nullptr) break;
      }
    }
  }
  if (f == nullptr) {
    NoteMissing(std::format("{}.{}:{}", c->descriptor, name, type));
    runtime_->ThrowNew(self, "Ljava/lang/NoSuchFieldError;",
                       std::format("No field {} of type {} in class {} or its superclasses", name, type,
                                   c->descriptor));
    return nullptr;
  }
  if (f->IsStatic() != is_static) {
    runtime_->ThrowNew(self, "Ljava/lang/IncompatibleClassChangeError;",
                       std::format("Expected {} field {}.{}", is_static ? "static" : "instance",
                                   c->descriptor, name));
    return nullptr;
  }
  cache->fields[field_idx].store(f, std::memory_order_release);
  return f;
}

Object* ClassLinker::ResolveString(Thread* self, DexCache* cache, std::uint32_t string_idx) {
  if (Object* s = cache->strings[string_idx].load(std::memory_order_acquire)) return s;
  Object* s = runtime_->InternString(self, cache->dex->StringUtf16(string_idx));
  if (s != nullptr) cache->strings[string_idx].store(s, std::memory_order_release);
  return s;
}

RtMethod* ClassLinker::FindInterfaceTarget(Thread* self, RtClass* receiver, RtMethod* resolved) {
  {
    std::lock_guard<std::mutex> g(receiver->dispatch_lock);
    auto it = receiver->dispatch_cache.find(resolved);
    if (it != receiver->dispatch_cache.end()) return it->second;
  }
  RtMethod* target = nullptr;
  // 1. The class hierarchy (JVMS 5.4.6): the first non-static, non-private
  //    declaration of the method in the receiver's class or its superclasses.
  for (RtClass* k = receiver; k != nullptr && target == nullptr; k = k->super) {
    RtMethod* m = k->FindDeclaredMethod(resolved->name, resolved->signature);
    if (m != nullptr && !m->IsStatic() && !m->IsPrivate()) target = m;
  }
  // 2. The maximally specific superinterface method that is not abstract.
  if (target == nullptr) {
    std::vector<RtMethod*> candidates;
    for (RtClass* i : receiver->all_interfaces) {
      RtMethod* m = i->FindDeclaredMethod(resolved->name, resolved->signature);
      if (m != nullptr && !m->IsStatic() && !m->IsPrivate()) candidates.push_back(m);
    }
    std::vector<RtMethod*> specific;
    for (RtMethod* m : candidates) {
      bool shadowed = false;
      for (RtMethod* other : candidates) {
        if (other != m && other->declaring != m->declaring &&
            m->declaring->IsAssignableFrom(other->declaring)) {
          shadowed = true;
          break;
        }
      }
      if (!shadowed) specific.push_back(m);
    }
    std::vector<RtMethod*> concrete;
    for (RtMethod* m : specific) if (!m->IsAbstract()) concrete.push_back(m);
    if (concrete.size() == 1) {
      target = concrete[0];
    } else if (concrete.size() > 1) {
      runtime_->ThrowNew(self, "Ljava/lang/IncompatibleClassChangeError;",
                         std::format("Conflicting default methods for {} in {}", resolved->PrettyName(),
                                     receiver->descriptor));
      return nullptr;
    } else if (!specific.empty()) {
      target = specific[0];  // abstract: the invoke raises AbstractMethodError
    }
  }
  if (target == nullptr) {
    runtime_->ThrowNew(self, "Ljava/lang/AbstractMethodError;",
                       std::format("{} has no implementation of {}", receiver->descriptor, resolved->PrettyName()));
    return nullptr;
  }
  std::lock_guard<std::mutex> g(receiver->dispatch_lock);
  receiver->dispatch_cache.emplace(resolved, target);
  return target;
}

std::vector<std::string> ClassLinker::MissingClasses() {
  std::lock_guard<std::mutex> g(missing_lock_);
  std::vector<std::string> out = missing_;
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void ClassLinker::NoteMissing(std::string what) {
  std::lock_guard<std::mutex> g(missing_lock_);
  if (missing_.size() < 10000) missing_.push_back(std::move(what));
}

}  // namespace arun::vm
