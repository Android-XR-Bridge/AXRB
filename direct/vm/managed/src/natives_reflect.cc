// java.lang.reflect natives and the reflective parts of java.lang.Class.
//
// Access checks (private/protected/package) are not enforced -- a documented
// gap (docs/managed-runtime.md). What IS enforced: the runtime's own
// bookkeeping fields (Class.vmClass, Method.vmMethod, Throwable.backtrace, ...)
// cannot be read or written reflectively at all. They hold host pointers and
// method-table indices; letting Java code forge them would break memory
// safety, so they are treated like Android's blocked hidden APIs.
#include <atomic>
#include <cstring>
#include <format>

#include "arun/vm/interpreter.h"
#include "arun/vm/natives.h"
#include "arun/vm/runtime.h"

namespace arun::vm {
namespace natives {

namespace {

Runtime* Rt(Thread* self) { return self->runtime(); }

void Throw(Thread* self, std::string_view descriptor, std::string_view message) {
  Rt(self)->ThrowNew(self, descriptor, message);
}

const char* BoxDescriptor(char kind) {
  switch (kind) {
    case 'Z': return "Ljava/lang/Boolean;";
    case 'B': return "Ljava/lang/Byte;";
    case 'C': return "Ljava/lang/Character;";
    case 'S': return "Ljava/lang/Short;";
    case 'I': return "Ljava/lang/Integer;";
    case 'J': return "Ljava/lang/Long;";
    case 'F': return "Ljava/lang/Float;";
    case 'D': return "Ljava/lang/Double;";
    default: return nullptr;
  }
}

char BoxedKind(RtClass* c) {
  const std::string& d = c->descriptor;
  if (c->loader != nullptr) return 0;
  if (d == "Ljava/lang/Boolean;") return 'Z';
  if (d == "Ljava/lang/Byte;") return 'B';
  if (d == "Ljava/lang/Character;") return 'C';
  if (d == "Ljava/lang/Short;") return 'S';
  if (d == "Ljava/lang/Integer;") return 'I';
  if (d == "Ljava/lang/Long;") return 'J';
  if (d == "Ljava/lang/Float;") return 'F';
  if (d == "Ljava/lang/Double;") return 'D';
  return 0;
}

bool IsRuntimeInternalField(const RtField* f) {
  if (f->declaring->loader != nullptr) return false;
  const std::string& c = f->declaring->descriptor;
  const std::string& n = f->name;
  return (c == "Ljava/lang/Class;" && n == "vmClass") ||
         ((c == "Ljava/lang/reflect/Method;" || c == "Ljava/lang/reflect/Constructor;") && n == "vmMethod") ||
         (c == "Ljava/lang/reflect/Field;" && n == "vmField") ||
         (c == "Ljava/lang/ClassLoader;" && n == "vmLoader") ||
         (c == "Ljava/lang/Thread;" && n == "vmThread") ||
         (c == "Ljava/lang/Throwable;" && n == "backtrace");
}

// Reads a primitive or reference slot as raw bits (narrow kinds are stored
// canonicalised by the interpreter).
std::uint64_t LoadRaw(Slot* s, const RtField* f) {
  if (f->IsVolatile()) return std::atomic_ref<std::uint64_t>(s->raw).load(std::memory_order_seq_cst);
  return s->raw;
}

void StoreRaw(Slot* s, const RtField* f, std::uint64_t v) {
  if (f->IsVolatile()) {
    std::atomic_ref<std::uint64_t>(s->raw).store(v, std::memory_order_seq_cst);
  } else {
    s->raw = v;
  }
}

std::uint64_t Canonical(char kind, std::uint64_t raw) {
  switch (kind) {
    case 'Z': return raw & 1u;
    case 'B': return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(raw)));
    case 'C': return raw & 0xffffu;
    case 'S': return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(raw)));
    case 'I': case 'F': return raw & 0xffffffffu;
    default: return raw;
  }
}

RtClass* ClassOfDescriptor(Thread* self, std::string_view desc, ClassLoaderRt* loader) {
  return Rt(self)->class_linker().FindClass(self, desc, loader);
}

// A Class[] for a method's parameter descriptors.
Object* ParameterClasses(Thread* self, RtMethod* m) {
  Runtime* rt = Rt(self);
  const std::vector<std::string> params = ParameterDescriptors(m->signature);
  RtClass* array_class = rt->class_linker().FindClass(self, "[Ljava/lang/Class;", nullptr);
  if (array_class == nullptr) return nullptr;
  RootScope roots(self);
  auto arr = roots.Add(rt->heap().AllocArray(self, array_class, static_cast<std::int32_t>(params.size())));
  if (arr.Get() == nullptr) return nullptr;
  for (std::size_t i = 0; i < params.size(); ++i) {
    RtClass* c = ClassOfDescriptor(self, params[i], m->declaring->loader);
    if (c == nullptr) return nullptr;
    Object* mirror = rt->ClassMirror(self, c);
    if (mirror == nullptr) return nullptr;
    static_cast<Array*>(arr.Get())->elements<Object*>()[i] = mirror;
  }
  return arr.Get();
}

void SetField(Runtime* rt, Object* obj, RtClass* c, const char* name, const char* type, Slot v) {
  RtField* f = c->FindDeclaredField(name, type);
  if (f != nullptr) FieldSlots(obj)[f->slot] = v;
}

Object* NewMethodObject(Thread* self, RtMethod* m) {
  Runtime* rt = Rt(self);
  const bool ctor = m->name == "<init>";
  RtClass* c = rt->class_linker().FindClass(
      self, ctor ? "Ljava/lang/reflect/Constructor;" : "Ljava/lang/reflect/Method;", nullptr);
  if (c == nullptr || !rt->class_linker().EnsureInitialized(self, c)) return nullptr;
  RootScope roots(self);
  auto obj = roots.Add(rt->heap().AllocObject(self, c));
  if (obj.Get() == nullptr) return nullptr;
  auto params = roots.Add(ParameterClasses(self, m));
  if (params.Get() == nullptr) return nullptr;
  auto decl = roots.Add(rt->ClassMirror(self, m->declaring));
  if (decl.Get() == nullptr) return nullptr;
  Slot s;
  s.j = reinterpret_cast<std::intptr_t>(m);
  SetField(rt, obj.Get(), c, "vmMethod", "J", s);
  s.l = decl.Get();
  SetField(rt, obj.Get(), c, "declaringClass", "Ljava/lang/Class;", s);
  s.l = params.Get();
  SetField(rt, obj.Get(), c, "parameterTypes", "[Ljava/lang/Class;", s);
  s.raw = 0;
  s.i = static_cast<std::int32_t>(m->access & 0xffff);
  SetField(rt, obj.Get(), c, "modifiers", "I", s);
  if (!ctor) {
    auto name = roots.Add(rt->NewStringUtf8(self, m->name));
    if (name.Get() == nullptr) return nullptr;
    RtClass* ret = ClassOfDescriptor(self, ReturnDescriptor(m->signature), m->declaring->loader);
    if (ret == nullptr) return nullptr;
    auto ret_mirror = roots.Add(rt->ClassMirror(self, ret));
    if (ret_mirror.Get() == nullptr) return nullptr;
    s.l = name.Get();
    SetField(rt, obj.Get(), c, "name", "Ljava/lang/String;", s);
    s.l = ret_mirror.Get();
    SetField(rt, obj.Get(), c, "returnType", "Ljava/lang/Class;", s);
  }
  return obj.Get();
}

Object* NewFieldObject(Thread* self, RtField* f) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->class_linker().FindClass(self, "Ljava/lang/reflect/Field;", nullptr);
  if (c == nullptr || !rt->class_linker().EnsureInitialized(self, c)) return nullptr;
  RtClass* type = ClassOfDescriptor(self, f->type, f->declaring->loader);
  if (type == nullptr) return nullptr;
  RootScope roots(self);
  auto obj = roots.Add(rt->heap().AllocObject(self, c));
  if (obj.Get() == nullptr) return nullptr;
  auto decl = roots.Add(rt->ClassMirror(self, f->declaring));
  auto type_mirror = roots.Add(rt->ClassMirror(self, type));
  auto name = roots.Add(rt->NewStringUtf8(self, f->name));
  if (self->IsExceptionPending()) return nullptr;
  Slot s;
  s.j = reinterpret_cast<std::intptr_t>(f);
  SetField(rt, obj.Get(), c, "vmField", "J", s);
  s.l = decl.Get();
  SetField(rt, obj.Get(), c, "declaringClass", "Ljava/lang/Class;", s);
  s.l = name.Get();
  SetField(rt, obj.Get(), c, "name", "Ljava/lang/String;", s);
  s.l = type_mirror.Get();
  SetField(rt, obj.Get(), c, "type", "Ljava/lang/Class;", s);
  s.raw = 0;
  s.i = static_cast<std::int32_t>(f->access & 0xffff);
  SetField(rt, obj.Get(), c, "modifiers", "I", s);
  return obj.Get();
}

template <typename T>
T* MemberFromObject(Runtime* rt, Object* member, const char* field) {
  RtField* f = member->klass->FindDeclaredField(field, "J");
  if (f == nullptr) return nullptr;
  return reinterpret_cast<T*>(static_cast<std::uintptr_t>(FieldSlots(member)[f->slot].j));
}

// Does `params` (a Class[]) equal the method's parameter types exactly?
bool ParamsMatch(Thread* self, RtMethod* m, Array* params) {
  Runtime* rt = Rt(self);
  const std::vector<std::string> want = ParameterDescriptors(m->signature);
  const std::int32_t n = params == nullptr ? 0 : params->length;
  if (static_cast<std::size_t>(n) != want.size()) return false;
  for (std::int32_t i = 0; i < n; ++i) {
    RtClass* c = rt->ClassFromMirror(params->elements<Object*>()[i]);
    if (c == nullptr || c->descriptor != want[static_cast<std::size_t>(i)]) return false;
  }
  return true;
}

// --- Class member lookup -------------------------------------------------------

void Class_nativeGetMethod(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  const std::string name = rt->GetStringUtf8(ArgRef(a, 1));
  auto* params = static_cast<Array*>(ArgRef(a, 2));
  const bool public_only = ArgInt(a, 3) != 0;
  r->l = nullptr;
  if (name == "<init>" || name == "<clinit>") return;
  auto match = [&](RtClass* k) -> RtMethod* {
    for (RtMethod& m : k->methods) {
      if (m.name != name || (public_only && (m.access & kAccPublic) == 0)) continue;
      if (ParamsMatch(self, &m, params)) return &m;
    }
    return nullptr;
  };
  RtMethod* found = nullptr;
  if (!public_only) {
    found = match(c);
  } else {
    for (RtClass* k = c; k != nullptr && found == nullptr; k = k->super) found = match(k);
    for (std::size_t i = 0; found == nullptr && i < c->all_interfaces.size(); ++i) found = match(c->all_interfaces[i]);
  }
  if (self->IsExceptionPending() || found == nullptr) return;
  r->l = NewMethodObject(self, found);
}

void Class_nativeGetConstructor(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  auto* params = static_cast<Array*>(ArgRef(a, 1));
  r->l = nullptr;
  for (RtMethod& m : c->methods) {
    if (m.name == "<init>" && ParamsMatch(self, &m, params)) {
      r->l = NewMethodObject(self, &m);
      return;
    }
  }
}

void Class_nativeGetField(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  const std::string name = rt->GetStringUtf8(ArgRef(a, 1));
  const bool public_only = ArgInt(a, 2) != 0;
  r->l = nullptr;
  auto match = [&](RtClass* k) -> RtField* {
    for (auto* list : {&k->ifields, &k->sfields}) {
      for (RtField& f : *list) {
        if (f.name == name && (!public_only || (f.access & kAccPublic) != 0)) return &f;
      }
    }
    return nullptr;
  };
  RtField* found = nullptr;
  if (!public_only) {
    found = match(c);
  } else {
    for (RtClass* k = c; k != nullptr && found == nullptr; k = k->super) {
      found = match(k);
      for (std::size_t i = 0; found == nullptr && i < k->interfaces.size(); ++i) found = match(k->interfaces[i]);
    }
  }
  if (found != nullptr) r->l = NewFieldObject(self, found);
}

void Class_getDeclaredMethods(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  RootScope roots(self);
  std::vector<RootScope::Handle> handles;
  for (RtMethod& m : c->methods) {
    if (m.name == "<init>" || m.name == "<clinit>") continue;
    Object* o = NewMethodObject(self, &m);
    if (o == nullptr) return;
    handles.push_back(roots.Add(o));
  }
  RtClass* ac = rt->class_linker().FindClass(self, "[Ljava/lang/reflect/Method;", nullptr);
  if (ac == nullptr) return;
  Array* arr = rt->heap().AllocArray(self, ac, static_cast<std::int32_t>(handles.size()));
  if (arr == nullptr) return;
  for (std::size_t i = 0; i < handles.size(); ++i) arr->elements<Object*>()[i] = handles[i].Get();
  r->l = arr;
}

void Class_getDeclaredFields(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  RootScope roots(self);
  std::vector<RootScope::Handle> handles;
  for (auto* list : {&c->ifields, &c->sfields}) {
    for (RtField& f : *list) {
      Object* o = NewFieldObject(self, &f);
      if (o == nullptr) return;
      handles.push_back(roots.Add(o));
    }
  }
  RtClass* ac = rt->class_linker().FindClass(self, "[Ljava/lang/reflect/Field;", nullptr);
  if (ac == nullptr) return;
  Array* arr = rt->heap().AllocArray(self, ac, static_cast<std::int32_t>(handles.size()));
  if (arr == nullptr) return;
  for (std::size_t i = 0; i < handles.size(); ++i) arr->elements<Object*>()[i] = handles[i].Get();
  r->l = arr;
}

// --- invocation --------------------------------------------------------------------

// Converts reflective arguments to JValues for `m`. False with
// IllegalArgumentException pending on mismatch.
bool ConvertArgs(Thread* self, RtMethod* m, Array* args, std::vector<JValue>* out) {
  Runtime* rt = Rt(self);
  const std::vector<std::string> params = ParameterDescriptors(m->signature);
  const std::int32_t n = args == nullptr ? 0 : args->length;
  if (static_cast<std::size_t>(n) != params.size()) {
    Throw(self, "Ljava/lang/IllegalArgumentException;",
          std::format("Wrong number of arguments; expected {}, got {}", params.size(), n));
    return false;
  }
  out->resize(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    Object* v = args->elements<Object*>()[i];
    const std::string& p = params[i];
    JValue& jv = (*out)[i];
    jv.raw = 0;
    if (p[0] == 'L' || p[0] == '[') {
      if (v != nullptr) {
        RtClass* pc = rt->class_linker().FindClass(self, p, m->declaring->loader);
        if (pc == nullptr) return false;
        if (!pc->IsAssignableFrom(v->klass)) {
          Throw(self, "Ljava/lang/IllegalArgumentException;",
                std::format("method {} argument {} has type {}, got {}", m->PrettyName(), i + 1, BinaryName(pc),
                            BinaryName(v->klass)));
          return false;
        }
      }
      jv.l = v;
    } else {
      std::uint64_t raw;
      if (!Unbox(self, v, p[0], &raw)) return false;
      jv.raw = raw;
    }
  }
  return true;
}

// Wraps a pending exception from the invoked code in InvocationTargetException.
void WrapTargetException(Thread* self) {
  Runtime* rt = Rt(self);
  RootScope roots(self);
  auto cause = roots.Add(self->exception());
  self->ClearException();
  RtClass* ite = rt->class_linker().FindClass(self, "Ljava/lang/reflect/InvocationTargetException;", nullptr);
  if (ite == nullptr || !rt->class_linker().EnsureInitialized(self, ite)) return;
  RtMethod* ctor = ite->FindDeclaredMethod("<init>", "(Ljava/lang/Throwable;)V");
  auto ex = roots.Add(rt->heap().AllocObject(self, ite));
  if (ex.Get() == nullptr || ctor == nullptr) {
    if (!self->IsExceptionPending()) self->SetException(cause.Get());
    return;
  }
  JValue arg, unused;
  arg.l = cause.Get();
  if (!rt->Invoke(self, ctor, ex.Get(), std::span<const JValue>(&arg, 1), &unused)) return;
  self->SetException(ex.Get());
}

void Method_invoke(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtMethod* m = MemberFromObject<RtMethod>(rt, ArgRef(a, 0), "vmMethod");
  Object* receiver = ArgRef(a, 1);
  auto* args = static_cast<Array*>(ArgRef(a, 2));
  if (m == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "unbound Method");
  if (!m->IsStatic()) {
    if (receiver == nullptr) {
      return Throw(self, "Ljava/lang/NullPointerException;",
                   "null receiver for non-static method " + m->PrettyName());
    }
    if (!m->declaring->IsAssignableFrom(receiver->klass)) {
      return Throw(self, "Ljava/lang/IllegalArgumentException;",
                   std::format("Expected receiver of type {}, but got {}", BinaryName(m->declaring),
                               BinaryName(receiver->klass)));
    }
  }
  if (m->IsAbstract() && m->IsStatic()) return Throw(self, "Ljava/lang/AbstractMethodError;", m->PrettyName());
  std::vector<JValue> jargs;
  if (!ConvertArgs(self, m, args, &jargs)) return;
  JValue res;
  // Runtime::Invoke dispatches virtually for non-direct methods.
  if (!rt->Invoke(self, m, m->IsStatic() ? nullptr : receiver, jargs, &res)) return WrapTargetException(self);
  const char rk = m->ReturnKind();
  if (rk == 'V') {
    r->l = nullptr;
  } else if (rk == 'L') {
    r->l = res.l;
  } else {
    r->l = Box(self, rk, Canonical(rk, res.raw));
  }
}

void Constructor_newInstance(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtMethod* m = MemberFromObject<RtMethod>(rt, ArgRef(a, 0), "vmMethod");
  auto* args = static_cast<Array*>(ArgRef(a, 1));
  if (m == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "unbound Constructor");
  RtClass* c = m->declaring;
  if (c->IsAbstract() || c->IsInterface()) {
    return Throw(self, "Ljava/lang/InstantiationException;", "Can't instantiate abstract class " + BinaryName(c));
  }
  std::vector<JValue> jargs;
  if (!ConvertArgs(self, m, args, &jargs)) return;
  if (!rt->class_linker().EnsureInitialized(self, c)) return;
  RootScope roots(self);
  // jargs holds references that were already rooted in the argument array,
  // which stays reachable from this native's frame.
  auto obj = roots.Add(rt->heap().AllocObject(self, c));
  if (obj.Get() == nullptr) return;
  JValue unused;
  if (!rt->Invoke(self, m, obj.Get(), jargs, &unused)) return WrapTargetException(self);
  r->l = obj.Get();
}

// --- fields ------------------------------------------------------------------------

// Locates the slot for `f` on `obj`; false with an exception pending.
bool FieldSlot(Thread* self, RtField* f, Object* obj, Slot** slot) {
  Runtime* rt = Rt(self);
  if (IsRuntimeInternalField(f)) {
    Throw(self, "Ljava/lang/IllegalAccessException;",
          std::format("field {}.{} is internal to the runtime", BinaryName(f->declaring), f->name));
    return false;
  }
  if (f->IsStatic()) {
    if (!rt->class_linker().EnsureInitialized(self, f->declaring)) return false;
    *slot = &f->declaring->statics[f->slot];
    return true;
  }
  if (obj == nullptr) {
    Throw(self, "Ljava/lang/NullPointerException;", "null object for instance field " + f->name);
    return false;
  }
  if (!f->declaring->IsAssignableFrom(obj->klass)) {
    Throw(self, "Ljava/lang/IllegalArgumentException;",
          std::format("Expected receiver of type {}, but got {}", BinaryName(f->declaring), BinaryName(obj->klass)));
    return false;
  }
  *slot = &FieldSlots(obj)[f->slot];
  return true;
}

void Field_get(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtField* f = MemberFromObject<RtField>(rt, ArgRef(a, 0), "vmField");
  if (f == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "unbound Field");
  Slot* s;
  if (!FieldSlot(self, f, ArgRef(a, 1), &s)) return;
  const std::uint64_t raw = LoadRaw(s, f);
  if (f->kind == 'L') {
    r->l = reinterpret_cast<Object*>(static_cast<std::uintptr_t>(raw));
  } else {
    r->l = Box(self, f->kind, raw);
  }
}

void Field_set(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  RtField* f = MemberFromObject<RtField>(rt, ArgRef(a, 0), "vmField");
  Object* value = ArgRef(a, 2);
  if (f == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "unbound Field");
  if (f->IsStatic() && (f->access & kAccFinal) != 0) {
    return Throw(self, "Ljava/lang/IllegalAccessException;", "Cannot set static final field " + f->name);
  }
  std::uint64_t raw;
  if (f->kind == 'L') {
    if (value != nullptr) {
      RtClass* type = rt->class_linker().FindClass(self, f->type, f->declaring->loader);
      if (type == nullptr) return;
      if (!type->IsAssignableFrom(value->klass)) {
        return Throw(self, "Ljava/lang/IllegalArgumentException;",
                     std::format("field {} has type {}, got {}", f->name, BinaryName(type), BinaryName(value->klass)));
      }
    }
    raw = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
  } else if (!Unbox(self, value, f->kind, &raw)) {
    return;
  }
  Slot* s;
  if (!FieldSlot(self, f, ArgRef(a, 1), &s)) return;  // may run <clinit>; `value` is rooted in args
  if (f->kind == 'L') raw = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ArgRef(a, 2)));
  StoreRaw(s, f, raw);
}

// --- java.lang.reflect.Array ---------------------------------------------------------

void Array_getLength(Thread* self, Frame* a, JValue* r) {
  Object* o = ArgRef(a, 0);
  if (o == nullptr) return Throw(self, "Ljava/lang/NullPointerException;", "array == null");
  if (!o->klass->IsArray()) return Throw(self, "Ljava/lang/IllegalArgumentException;", "Argument is not an array");
  r->i = static_cast<Array*>(o)->length;
}

void Array_nativeNewInstance(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* component = rt->ClassFromMirror(ArgRef(a, 0));
  const std::int32_t length = ArgInt(a, 1);
  if (component == nullptr) return Throw(self, "Ljava/lang/NullPointerException;", "componentType == null");
  if (component->IsPrimitive() && component->primitive == 'V') {
    return Throw(self, "Ljava/lang/IllegalArgumentException;", "Can't allocate an array of void");
  }
  if (length < 0) return Throw(self, "Ljava/lang/NegativeArraySizeException;", std::to_string(length));
  RtClass* ac = rt->class_linker().FindClass(self, "[" + component->descriptor, component->loader);
  if (ac == nullptr) return;
  r->l = rt->heap().AllocArray(self, ac, length);
}

// --- java.lang.reflect.Proxy -----------------------------------------------------

RtField* ProxyHandlerField(Thread* self) {
  RtClass* base = Rt(self)->class_linker().FindClass(self, "Ljava/lang/reflect/Proxy;", nullptr);
  return base != nullptr ? base->FindDeclaredField("h", "Ljava/lang/reflect/InvocationHandler;") : nullptr;
}

// The body of every proxy method: h.invoke(proxy, method, boxed args), then the
// result unboxed to the return type of the method.
void Proxy_dispatch(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtMethod* m = a->method;
  RtMethod* target = m->proxy_target;
  r->raw = 0;
  RtField* hf = ProxyHandlerField(self);
  if (hf == nullptr || target == nullptr) return Throw(self, "Ljava/lang/InternalError;", "malformed proxy class");
  RootScope roots(self);
  auto proxy = roots.Add(ArgRef(a, 0));
  auto handler = roots.Add(FieldSlots(proxy.Get())[hf->slot].l);
  if (handler.Get() == nullptr) return Throw(self, "Ljava/lang/NullPointerException;", "proxy has no InvocationHandler");
  auto method = roots.Add(NewMethodObject(self, target));
  if (method.Get() == nullptr) return;

  const std::vector<std::string> params = ParameterDescriptors(m->signature);
  auto args = roots.Add(nullptr);
  if (!params.empty()) {
    RtClass* ac = rt->class_linker().FindClass(self, "[Ljava/lang/Object;", nullptr);
    if (ac == nullptr) return;
    args.Set(rt->heap().AllocArray(self, ac, static_cast<std::int32_t>(params.size())));
    if (args.Get() == nullptr) return;
    std::uint32_t reg = 1;
    for (std::size_t i = 0; i < params.size(); ++i) {
      const char k = params[i][0];
      Object* v = nullptr;
      if (k == 'L' || k == '[') {
        v = ArgRef(a, reg++);
      } else if (k == 'J' || k == 'D') {
        v = Box(self, k, static_cast<std::uint64_t>(ArgLong(a, reg)));
        reg += 2;
        if (v == nullptr) return;
      } else {
        v = Box(self, k, Canonical(k, static_cast<std::uint32_t>(ArgInt(a, reg++))));
        if (v == nullptr) return;
      }
      static_cast<Array*>(args.Get())->elements<Object*>()[i] = v;
    }
  }

  RtClass* ih = rt->class_linker().FindClass(self, "Ljava/lang/reflect/InvocationHandler;", nullptr);
  if (ih == nullptr) return;
  RtMethod* invoke = ih->FindDeclaredMethod(
      "invoke", "(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;");
  if (invoke == nullptr) return Throw(self, "Ljava/lang/InternalError;", "InvocationHandler.invoke missing");
  JValue in[3], out;
  in[0].l = proxy.Get();
  in[1].l = method.Get();
  in[2].l = args.Get();
  if (!rt->Invoke(self, invoke, handler.Get(), std::span<const JValue>(in, 3), &out)) return;
  const char rk = m->ReturnKind();
  if (rk == 'V') return;
  if (rk == 'L' || rk == '[') {
    if (out.l != nullptr) {
      RtClass* rc = ClassOfDescriptor(self, ReturnDescriptor(m->signature), target->declaring->loader);
      if (rc == nullptr) return;
      if (!rc->IsAssignableFrom(out.l->klass)) {
        return Throw(self, "Ljava/lang/ClassCastException;",
                     std::format("{} cannot be cast to {}", BinaryName(out.l->klass), BinaryName(rc)));
      }
    }
    r->l = out.l;
    return;
  }
  if (out.l == nullptr) {
    return Throw(self, "Ljava/lang/NullPointerException;",
                 "null returned by an InvocationHandler for primitive return type of " + target->PrettyName());
  }
  std::uint64_t raw;
  if (!Unbox(self, out.l, rk, &raw)) return;
  r->raw = raw;
}

std::atomic<std::uint32_t> g_proxy_count{0};

void Proxy_generateProxy(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  auto* ifaces = static_cast<Array*>(ArgRef(a, 0));
  r->l = nullptr;
  if (ifaces == nullptr) return Throw(self, "Ljava/lang/NullPointerException;", "interfaces == null");
  std::vector<RtClass*> list;
  for (std::int32_t i = 0; i < ifaces->length; ++i) {
    RtClass* c = rt->ClassFromMirror(ifaces->elements<Object*>()[i]);
    if (c == nullptr || !c->IsInterface()) {
      return Throw(self, "Ljava/lang/IllegalArgumentException;",
                   (c != nullptr ? BinaryName(c) : std::string("null")) + " is not an interface");
    }
    list.push_back(c);
  }
  ClassLoaderRt* loader = LoaderFromJava(rt, ArgRef(a, 1));
  const std::string descriptor = std::format("L$Proxy{};", g_proxy_count.fetch_add(1));
  RtClass* c = rt->class_linker().DefineProxyClass(self, descriptor, list, loader, Proxy_dispatch);
  if (c == nullptr) return;
  r->l = rt->ClassMirror(self, c);
}

void Proxy_newInstance0(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  r->l = nullptr;
  RtField* hf = ProxyHandlerField(self);
  if (c == nullptr || hf == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "not a proxy class");
  Object* o = rt->heap().AllocObject(self, c);
  if (o == nullptr) return;
  FieldSlots(o)[hf->slot].l = ArgRef(a, 1);
  r->l = o;
}

void Method_invokeNonVirtual(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtMethod* m = MemberFromObject<RtMethod>(rt, ArgRef(a, 0), "vmMethod");
  Object* receiver = ArgRef(a, 1);
  auto* args = static_cast<Array*>(ArgRef(a, 2));
  if (m == nullptr) return Throw(self, "Ljava/lang/IllegalArgumentException;", "unbound Method");
  if (m->IsAbstract()) return Throw(self, "Ljava/lang/AbstractMethodError;", m->PrettyName());
  if (!m->IsStatic() && (receiver == nullptr || !m->declaring->IsAssignableFrom(receiver->klass))) {
    return Throw(self, "Ljava/lang/IllegalArgumentException;", "bad receiver for " + m->PrettyName());
  }
  std::vector<JValue> jargs;
  if (!ConvertArgs(self, m, args, &jargs)) return;
  JValue res;
  if (!rt->Invoke(self, m, m->IsStatic() ? nullptr : receiver, jargs, &res, false)) return WrapTargetException(self);
  const char rk = m->ReturnKind();
  if (rk == 'V') r->l = nullptr;
  else if (rk == 'L') r->l = res.l;
  else r->l = Box(self, rk, Canonical(rk, res.raw));
}

void Class_getDeclaredConstructors(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = rt->ClassFromMirror(ArgRef(a, 0));
  const bool public_only = ArgInt(a, 1) != 0;
  RootScope roots(self);
  std::vector<RootScope::Handle> handles;
  for (RtMethod& m : c->methods) {
    if (m.name != "<init>" || (public_only && (m.access & kAccPublic) == 0)) continue;
    Object* o = NewMethodObject(self, &m);
    if (o == nullptr) return;
    handles.push_back(roots.Add(o));
  }
  RtClass* ac = rt->class_linker().FindClass(self, "[Ljava/lang/reflect/Constructor;", nullptr);
  if (ac == nullptr) return;
  Array* arr = rt->heap().AllocArray(self, ac, static_cast<std::int32_t>(handles.size()));
  if (arr == nullptr) return;
  for (std::size_t i = 0; i < handles.size(); ++i) arr->elements<Object*>()[i] = handles[i].Get();
  r->l = arr;
}

}  // namespace

Object* Box(Thread* self, char kind, std::uint64_t raw) {
  Runtime* rt = Rt(self);
  const char* desc = BoxDescriptor(kind);
  RtClass* c = rt->class_linker().FindClass(self, desc, nullptr);
  if (c == nullptr) return nullptr;
  const std::string sig = std::format("({}){}", kind, desc);
  RtMethod* value_of = c->FindDeclaredMethod("valueOf", sig);
  if (value_of == nullptr) {
    Throw(self, "Ljava/lang/InternalError;", std::format("{} lacks valueOf{}", desc, sig));
    return nullptr;
  }
  JValue arg, res;
  arg.raw = raw;
  if (!rt->Invoke(self, value_of, nullptr, std::span<const JValue>(&arg, 1), &res)) return nullptr;
  return res.l;
}

bool Unbox(Thread* self, Object* value, char kind, std::uint64_t* raw) {
  if (value == nullptr) {
    Throw(self, "Ljava/lang/IllegalArgumentException;", std::format("null cannot be converted to {}", kind));
    return false;
  }
  const char from = BoxedKind(value->klass);
  RtField* vf = from != 0 ? value->klass->FindDeclaredField("value", std::string(1, from)) : nullptr;
  if (vf == nullptr) {
    Throw(self, "Ljava/lang/IllegalArgumentException;",
          std::format("{} cannot be converted to primitive {}", BinaryName(value->klass), kind));
    return false;
  }
  const Slot s = FieldSlots(value)[vf->slot];
  // Widening primitive conversions (JLS 5.1.2), plus identity.
  auto widen_ok = [&]() {
    if (from == kind) return true;
    switch (from) {
      case 'B': return std::string_view("SIJFD").find(kind) != std::string_view::npos;
      case 'S': return std::string_view("IJFD").find(kind) != std::string_view::npos;
      case 'C': return std::string_view("IJFD").find(kind) != std::string_view::npos;
      case 'I': return std::string_view("JFD").find(kind) != std::string_view::npos;
      case 'J': return std::string_view("FD").find(kind) != std::string_view::npos;
      case 'F': return kind == 'D';
      default: return false;
    }
  };
  if (!widen_ok()) {
    Throw(self, "Ljava/lang/IllegalArgumentException;",
          std::format("{} cannot be converted to primitive {}", BinaryName(value->klass), kind));
    return false;
  }
  // Source value as the widest representations.
  std::int64_t as_long = 0;
  double as_double = 0;
  switch (from) {
    case 'Z': case 'B': case 'S': case 'C': case 'I':
      as_long = s.i;
      as_double = static_cast<double>(s.i);
      break;
    case 'J':
      as_long = s.j;
      as_double = static_cast<double>(s.j);
      break;
    case 'F':
      as_double = s.f;
      break;
    case 'D':
      as_double = s.d;
      break;
  }
  Slot out;
  out.raw = 0;
  switch (kind) {
    case 'Z': case 'B': case 'S': case 'C': case 'I':
      out.raw = Canonical(kind, static_cast<std::uint64_t>(as_long));
      break;
    case 'J':
      out.j = as_long;
      break;
    case 'F':
      out.f = from == 'F' ? s.f : (from == 'J' ? static_cast<float>(s.j) : static_cast<float>(as_long));
      break;
    case 'D':
      out.d = as_double;
      break;
  }
  *raw = out.raw;
  return true;
}

void RegisterReflect(Runtime* rt) {
  auto reg = [rt](const char* key, NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Ljava/lang/Class;.nativeGetMethod(Ljava/lang/String;[Ljava/lang/Class;Z)Ljava/lang/reflect/Method;",
      Class_nativeGetMethod);
  reg("Ljava/lang/Class;.nativeGetConstructor([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;",
      Class_nativeGetConstructor);
  reg("Ljava/lang/Class;.nativeGetField(Ljava/lang/String;Z)Ljava/lang/reflect/Field;", Class_nativeGetField);
  reg("Ljava/lang/Class;.getDeclaredMethods()[Ljava/lang/reflect/Method;", Class_getDeclaredMethods);
  reg("Ljava/lang/Class;.getDeclaredFields()[Ljava/lang/reflect/Field;", Class_getDeclaredFields);
  reg("Ljava/lang/reflect/Method;.invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", Method_invoke);
  reg("Ljava/lang/reflect/Constructor;.newInstance([Ljava/lang/Object;)Ljava/lang/Object;", Constructor_newInstance);
  reg("Ljava/lang/reflect/Field;.get(Ljava/lang/Object;)Ljava/lang/Object;", Field_get);
  reg("Ljava/lang/reflect/Field;.set(Ljava/lang/Object;Ljava/lang/Object;)V", Field_set);
  reg("Ljava/lang/reflect/Array;.getLength(Ljava/lang/Object;)I", Array_getLength);
  reg("Ljava/lang/reflect/Proxy;.generateProxy([Ljava/lang/Class;Ljava/lang/ClassLoader;)Ljava/lang/Class;",
      Proxy_generateProxy);
  reg("Ljava/lang/reflect/Proxy;.newInstance0(Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;",
      Proxy_newInstance0);
  reg("Ljava/lang/reflect/Method;.invokeNonVirtual(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
      Method_invokeNonVirtual);
  reg("Ljava/lang/Class;.getDeclaredConstructors0(Z)[Ljava/lang/reflect/Constructor;", Class_getDeclaredConstructors);
  reg("Ljava/lang/reflect/Array;.nativeNewInstance(Ljava/lang/Class;I)Ljava/lang/Object;", Array_nativeNewInstance);
}

}  // namespace natives
}  // namespace arun::vm
