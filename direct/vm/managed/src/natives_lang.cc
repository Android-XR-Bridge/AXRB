// java.lang natives other than reflection and threads.
#include <windows.h>
#include <bcrypt.h>
#include <icu.h>

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <thread>

#include "arun/vm/interpreter.h"
#include "arun/vm/monitor.h"
#include "arun/vm/natives.h"
#include "arun/vm/runtime.h"

namespace arun::vm {
namespace natives {

std::string BinaryName(const RtClass* klass) {
  if (klass->IsPrimitive()) {
    switch (klass->primitive) {
      case 'Z': return "boolean";
      case 'B': return "byte";
      case 'C': return "char";
      case 'S': return "short";
      case 'I': return "int";
      case 'J': return "long";
      case 'F': return "float";
      case 'D': return "double";
      default: return "void";
    }
  }
  std::string out = klass->descriptor;
  if (out[0] == 'L') out = out.substr(1, out.size() - 2);
  for (char& c : out) {
    if (c == '/') c = '.';
  }
  return out;
}

bool ClassNameToDescriptor(std::string_view name, std::string* descriptor) {
  if (name.empty() || name.find('/') != std::string_view::npos) return false;
  std::string d(name);
  for (char& c : d) {
    if (c == '.') c = '/';
  }
  if (d[0] == '[') {
    // Array names are already descriptors ("[Ljava.lang.String;", "[I").
    std::size_t i = 0;
    while (i < d.size() && d[i] == '[') ++i;
    if (i >= d.size()) return false;
    if (d[i] == 'L') {
      if (d.back() != ';' || d.size() - i < 3) return false;
    } else if (i + 1 != d.size() || std::string_view("ZBCSIJFD").find(d[i]) == std::string_view::npos) {
      return false;
    }
    *descriptor = std::move(d);
    return true;
  }
  if (d.find(';') != std::string::npos || d.find('[') != std::string::npos) return false;
  *descriptor = "L" + d + ";";
  return true;
}

std::vector<std::string> ParameterDescriptors(std::string_view sig) {
  std::vector<std::string> out;
  std::size_t i = 1;
  while (i < sig.size() && sig[i] != ')') {
    const std::size_t start = i;
    while (sig[i] == '[') ++i;
    if (sig[i] == 'L') {
      i = sig.find(';', i) + 1;
    } else {
      ++i;
    }
    out.emplace_back(sig.substr(start, i - start));
  }
  return out;
}

std::string_view ReturnDescriptor(std::string_view sig) { return sig.substr(sig.find(')') + 1); }

ClassLoaderRt* LoaderFromJava(Runtime* rt, Object* loader) {
  if (loader == nullptr) return nullptr;
  const std::int64_t v = FieldSlots(loader)[rt->wk.class_loader_vm_loader->slot].j;
  return reinterpret_cast<ClassLoaderRt*>(static_cast<std::uintptr_t>(v));
}

ClassLoaderRt* CallerLoader(Thread* self, int depth) {
  Frame* f = self->top_frame();  // the native itself
  if (f != nullptr) f = f->prev;  // its caller
  for (int i = 0; i < depth && f != nullptr; ++i) f = f->prev;
  return f != nullptr ? f->method->declaring->loader : nullptr;
}

}  // namespace natives

namespace {

using natives::BinaryName;

Runtime* Rt(Thread* self) { return self->runtime(); }

void Throw(Thread* self, std::string_view descriptor, std::string_view message) {
  Rt(self)->ThrowNew(self, descriptor, message);
}

void ThrowNpe(Thread* self, std::string_view what) { Throw(self, "Ljava/lang/NullPointerException;", what); }

std::uint32_t IdentityHash(Runtime* rt, Object* o) {
  std::uint32_t h = o->hash.load(std::memory_order_relaxed);
  if (h != 0) return h;
  std::uint32_t fresh = rt->NextIdentityHash();
  if (o->hash.compare_exchange_strong(h, fresh, std::memory_order_relaxed)) return fresh;
  return h;
}

RtClass* ThisClass(Thread* self, Frame* a) { return Rt(self)->ClassFromMirror(ArgRef(a, 0)); }

void ReturnClass(Thread* self, RtClass* c, JValue* r) {
  r->l = c == nullptr ? nullptr : Rt(self)->ClassMirror(self, c);
}

void ReturnString(Thread* self, std::string_view utf8, JValue* r) { r->l = Rt(self)->NewStringUtf8(self, utf8); }

// --- java.lang.Object ---------------------------------------------------------

void Object_getClass(Thread* self, Frame* a, JValue* r) { ReturnClass(self, ArgRef(a, 0)->klass, r); }

void Object_hashCode(Thread* self, Frame* a, JValue* r) {
  r->i = static_cast<std::int32_t>(IdentityHash(Rt(self), ArgRef(a, 0)));
}

void Object_clone(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  Object* o = ArgRef(a, 0);
  if (!o->klass->IsArray() && !rt->wk.cloneable->IsAssignableFrom(o->klass)) {
    Throw(self, "Ljava/lang/CloneNotSupportedException;",
          std::format("Class {} doesn't implement Cloneable", BinaryName(o->klass)));
    return;
  }
  r->l = rt->heap().Clone(self, o);
}

void Object_notify(Thread* self, Frame* a, JValue*) { MonitorNotify(self, ArgRef(a, 0), false); }
void Object_notifyAll(Thread* self, Frame* a, JValue*) { MonitorNotify(self, ArgRef(a, 0), true); }
void Object_wait(Thread* self, Frame* a, JValue*) {
  MonitorWait(self, ArgRef(a, 0), ArgLong(a, 1), ArgInt(a, 3));
}

// --- java.lang.Class (non-reflective part) ------------------------------------

void Class_nativeGetName(Thread* self, Frame* a, JValue* r) { ReturnString(self, BinaryName(ThisClass(self, a)), r); }

void Class_getSuperclass(Thread* self, Frame* a, JValue* r) {
  RtClass* c = ThisClass(self, a);
  ReturnClass(self, c->IsInterface() || c->IsPrimitive() ? nullptr : c->super, r);
}

void Class_getInterfaces(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = ThisClass(self, a);
  std::vector<RtClass*> list = c->IsArray() ? std::vector<RtClass*>{rt->wk.cloneable, rt->wk.serializable}
                                            : c->interfaces;
  RtClass* array_class = rt->class_linker().FindClass(self, "[Ljava/lang/Class;", nullptr);
  if (array_class == nullptr) return;
  RootScope roots(self);
  auto arr = roots.Add(rt->heap().AllocArray(self, array_class, static_cast<std::int32_t>(list.size())));
  if (arr.Get() == nullptr) return;
  for (std::size_t i = 0; i < list.size(); ++i) {
    Object* m = rt->ClassMirror(self, list[i]);
    if (m == nullptr) return;
    static_cast<Array*>(arr.Get())->elements<Object*>()[i] = m;
  }
  r->l = arr.Get();
}

void Class_getComponentType(Thread* self, Frame* a, JValue* r) { ReturnClass(self, ThisClass(self, a)->component, r); }
void Class_isArray(Thread* self, Frame* a, JValue* r) { r->i = ThisClass(self, a)->IsArray() ? 1 : 0; }
void Class_isInterface(Thread* self, Frame* a, JValue* r) { r->i = ThisClass(self, a)->IsInterface() ? 1 : 0; }
void Class_isPrimitive(Thread* self, Frame* a, JValue* r) { r->i = ThisClass(self, a)->IsPrimitive() ? 1 : 0; }

void Class_getModifiers(Thread* self, Frame* a, JValue* r) {
  RtClass* c = ThisClass(self, a);
  if (c->IsPrimitive()) {
    r->i = kAccPublic | kAccFinal | kAccAbstract;
  } else if (c->IsArray()) {
    RtClass* e = c;
    while (e->IsArray()) e = e->component;
    const std::uint32_t vis = e->IsPrimitive() ? kAccPublic : (e->access & (kAccPublic | kAccPrivate | kAccProtected));
    r->i = static_cast<std::int32_t>(vis | kAccFinal | kAccAbstract);
  } else {
    r->i = static_cast<std::int32_t>(c->access & 0xffff);
  }
}

void Class_isInstance(Thread* self, Frame* a, JValue* r) {
  Object* o = ArgRef(a, 1);
  r->i = o != nullptr && ThisClass(self, a)->IsAssignableFrom(o->klass) ? 1 : 0;
}

void Class_isAssignableFrom(Thread* self, Frame* a, JValue* r) {
  Object* other = ArgRef(a, 1);
  if (other == nullptr) {
    ThrowNpe(self, "cls == null");
    return;
  }
  r->i = ThisClass(self, a)->IsAssignableFrom(Rt(self)->ClassFromMirror(other)) ? 1 : 0;
}

void Class_getClassLoader(Thread* self, Frame* a, JValue* r) {
  RtClass* c = ThisClass(self, a);
  while (c->IsArray()) c = c->component;
  r->l = c->loader != nullptr ? c->loader->java_object : nullptr;
}

void Class_newInstance(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = ThisClass(self, a);
  if (c->IsPrimitive() || c->IsArray() || c->IsInterface() || c->IsAbstract()) {
    Throw(self, "Ljava/lang/InstantiationException;", BinaryName(c));
    return;
  }
  RtMethod* ctor = c->FindDeclaredMethod("<init>", "()V");
  if (ctor == nullptr) {
    Throw(self, "Ljava/lang/InstantiationException;", BinaryName(c) + " has no zero argument constructor");
    return;
  }
  if (!rt->class_linker().EnsureInitialized(self, c)) return;
  RootScope roots(self);
  auto obj = roots.Add(rt->heap().AllocObject(self, c));
  if (obj.Get() == nullptr) return;
  JValue unused;
  // Exceptions from the constructor propagate unwrapped, as Class.newInstance
  // specifies.
  if (!rt->Invoke(self, ctor, obj.Get(), {}, &unused)) return;
  r->l = obj.Get();
}

RtClass* ForName(Thread* self, Object* jname, bool initialize, ClassLoaderRt* loader) {
  Runtime* rt = Rt(self);
  if (jname == nullptr) {
    ThrowNpe(self, "name == null");
    return nullptr;
  }
  const std::string name = rt->GetStringUtf8(jname);
  std::string desc;
  RtClass* c = nullptr;
  if (natives::ClassNameToDescriptor(name, &desc)) c = rt->class_linker().FindClassNoThrow(self, desc, loader);
  if (c == nullptr || c->IsPrimitive()) {
    Throw(self, "Ljava/lang/ClassNotFoundException;", name);
    return nullptr;
  }
  if (initialize && !rt->class_linker().EnsureInitialized(self, c)) return nullptr;
  return c;
}

void Class_forName1(Thread* self, Frame* a, JValue* r) {
  ReturnClass(self, ForName(self, ArgRef(a, 0), true, natives::CallerLoader(self, 0)), r);
}

void Class_forName3(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  Object* jloader = ArgRef(a, 2);
  ClassLoaderRt* loader = natives::LoaderFromJava(rt, jloader);
  if (jloader != nullptr && loader == nullptr && jloader->klass->descriptor != "Ljava/lang/BootClassLoader;") {
    // A loader implemented in Java: ask it.
    RtMethod* load = rt->FindMethod(jloader->klass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (load == nullptr) {
      Throw(self, "Ljava/lang/ClassNotFoundException;", rt->GetStringUtf8(ArgRef(a, 0)));
      return;
    }
    JValue arg, res;
    arg.l = ArgRef(a, 0);
    if (!rt->Invoke(self, load, jloader, std::span<const JValue>(&arg, 1), &res)) return;
    RtClass* c = rt->ClassFromMirror(res.l);
    if (c != nullptr && ArgInt(a, 1) != 0 && !rt->class_linker().EnsureInitialized(self, c)) return;
    r->l = res.l;
    return;
  }
  ReturnClass(self, ForName(self, ArgRef(a, 0), ArgInt(a, 1) != 0, loader), r);
}

void Class_getPrimitiveClass(Thread* self, Frame* a, JValue* r) {
  const std::string n = Rt(self)->GetStringUtf8(ArgRef(a, 0));
  static const std::pair<const char*, char> kNames[] = {{"boolean", 'Z'}, {"byte", 'B'}, {"char", 'C'},
                                                        {"short", 'S'},   {"int", 'I'},  {"long", 'J'},
                                                        {"float", 'F'},   {"double", 'D'}, {"void", 'V'}};
  for (auto& [name, t] : kNames) {
    if (n == name) {
      ReturnClass(self, Rt(self)->class_linker().PrimitiveClass(t), r);
      return;
    }
  }
  Throw(self, "Ljava/lang/IllegalArgumentException;", "unknown primitive type " + n);
}

void Class_getEnumConstantsShared(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  RtClass* c = ThisClass(self, a);
  r->l = nullptr;
  if ((c->access & kAccEnum) == 0 || c->super == nullptr || c->super->descriptor != "Ljava/lang/Enum;") return;
  RtMethod* values = c->FindDeclaredMethod("values", "()[" + c->descriptor);
  if (values == nullptr || !values->IsStatic()) return;
  JValue res;
  if (!rt->Invoke(self, values, nullptr, {}, &res)) return;
  r->l = res.l;
}

// --- java.lang.ClassLoader ------------------------------------------------------

void ClassLoader_nativeFindClass(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  ClassLoaderRt* loader = natives::LoaderFromJava(rt, ArgRef(a, 0));
  std::string desc;
  if (loader == nullptr || !natives::ClassNameToDescriptor(rt->GetStringUtf8(ArgRef(a, 1)), &desc)) {
    r->l = nullptr;
    return;
  }
  ReturnClass(self, rt->class_linker().FindClassNoThrow(self, desc, loader), r);
}

void ClassLoader_nativeFindLoadedClass(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  ClassLoaderRt* loader = natives::LoaderFromJava(rt, ArgRef(a, 0));
  std::string desc;
  if (!natives::ClassNameToDescriptor(rt->GetStringUtf8(ArgRef(a, 1)), &desc)) {
    r->l = nullptr;
    return;
  }
  ReturnClass(self, rt->class_linker().LookupClass(desc, loader), r);
}

// --- java.lang.String ------------------------------------------------------------

void String_intern(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  r->l = rt->InternString(self, rt->GetString(ArgRef(a, 0)));
}

// --- java.lang.System ------------------------------------------------------------

char ArrayKind(RtClass* array_class) {
  RtClass* c = array_class->component;
  return c->IsPrimitive() ? c->primitive : 'L';
}

void System_arraycopy(Thread* self, Frame* a, JValue*) {
  Object* src = ArgRef(a, 0);
  const std::int32_t src_pos = ArgInt(a, 1);
  Object* dst = ArgRef(a, 2);
  const std::int32_t dst_pos = ArgInt(a, 3);
  const std::int32_t length = ArgInt(a, 4);
  if (src == nullptr) return ThrowNpe(self, "src == null");
  if (dst == nullptr) return ThrowNpe(self, "dst == null");
  if (!src->klass->IsArray()) {
    return Throw(self, "Ljava/lang/ArrayStoreException;",
                 std::format("source of type {} is not an array", BinaryName(src->klass)));
  }
  if (!dst->klass->IsArray()) {
    return Throw(self, "Ljava/lang/ArrayStoreException;",
                 std::format("destination of type {} is not an array", BinaryName(dst->klass)));
  }
  auto* s = static_cast<Array*>(src);
  auto* d = static_cast<Array*>(dst);
  if (src_pos < 0 || dst_pos < 0 || length < 0 || src_pos > s->length - length || dst_pos > d->length - length) {
    return Throw(self, "Ljava/lang/ArrayIndexOutOfBoundsException;",
                 std::format("src.length={} srcPos={} dst.length={} dstPos={} length={}", s->length, src_pos,
                             d->length, dst_pos, length));
  }
  const char sk = ArrayKind(s->klass), dk = ArrayKind(d->klass);
  if (sk != dk) {
    return Throw(self, "Ljava/lang/ArrayStoreException;",
                 std::format("Incompatible types: src={}, dst={}", BinaryName(s->klass), BinaryName(d->klass)));
  }
  if (sk != 'L') {
    const std::size_t w = s->klass->component_size;
    std::memmove(d->data() + static_cast<std::size_t>(dst_pos) * w, s->data() + static_cast<std::size_t>(src_pos) * w,
                 static_cast<std::size_t>(length) * w);
    return;
  }
  Object** se = s->elements<Object*>();
  Object** de = d->elements<Object*>();
  RtClass* dc = d->klass->component;
  if (dc->IsAssignableFrom(s->klass->component)) {
    std::memmove(de + dst_pos, se + src_pos, static_cast<std::size_t>(length) * sizeof(Object*));
    return;
  }
  // Element-wise with a store check; elements before a failing one stay copied.
  for (std::int32_t i = 0; i < length; ++i) {
    Object* e = se[src_pos + i];
    if (e != nullptr && !dc->IsAssignableFrom(e->klass)) {
      return Throw(self, "Ljava/lang/ArrayStoreException;",
                   std::format("source[{}] of type {} cannot be stored in destination array of type {}",
                               src_pos + i, BinaryName(e->klass), BinaryName(d->klass)));
    }
    de[dst_pos + i] = e;
  }
}

void System_currentTimeMillis(Thread*, Frame*, JValue* r) {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  r->j = static_cast<std::int64_t>((u.QuadPart - 116444736000000000ull) / 10000ull);
}

void System_nanoTime(Thread*, Frame*, JValue* r) {
  static const std::int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  r->j = (c.QuadPart / freq) * 1000000000ll + (c.QuadPart % freq) * 1000000000ll / freq;
}

void System_identityHashCode(Thread* self, Frame* a, JValue* r) {
  Object* o = ArgRef(a, 0);
  r->i = o == nullptr ? 0 : static_cast<std::int32_t>(IdentityHash(Rt(self), o));
}

void System_logNative(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  rt->Log(ArgInt(a, 0), rt->GetStringUtf8(ArgRef(a, 1)), rt->GetStringUtf8(ArgRef(a, 2)));
}

void System_nativeGetProperty(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  auto v = rt->GetProperty(rt->GetStringUtf8(ArgRef(a, 0)));
  r->l = v ? rt->NewStringUtf8(self, *v) : nullptr;
}

void System_nativeSetProperty(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  if (ArgRef(a, 0) == nullptr) return ThrowNpe(self, "key == null");
  if (ArgRef(a, 1) == nullptr) return ThrowNpe(self, "value == null");
  auto old = rt->SetProperty(rt->GetStringUtf8(ArgRef(a, 0)), rt->GetStringUtf8(ArgRef(a, 1)));
  r->l = old ? rt->NewStringUtf8(self, *old) : nullptr;
}

// --- java.lang.Runtime -----------------------------------------------------------

void Runtime_gc(Thread* self, Frame*, JValue*) { Rt(self)->heap().CollectGarbage(self); }

void Runtime_availableProcessors(Thread*, Frame*, JValue* r) {
  const unsigned n = std::thread::hardware_concurrency();
  r->i = n == 0 ? 1 : static_cast<std::int32_t>(n);
}

void Runtime_totalMemory(Thread* self, Frame*, JValue* r) {
  r->j = static_cast<std::int64_t>(Rt(self)->heap().stats().bytes_live);
}
void Runtime_freeMemory(Thread*, Frame*, JValue* r) {
  // The heap grows on demand up to maxMemory(); nothing is reserved but unused.
  r->j = 0;
}
void Runtime_maxMemory(Thread* self, Frame*, JValue* r) {
  r->j = static_cast<std::int64_t>(Rt(self)->heap().max_bytes());
}

void Runtime_nativeExit(Thread* self, Frame* a, JValue*) { Rt(self)->Exit(ArgInt(a, 0)); }

void Runtime_nativeLoadLibrary(Thread* self, Frame* a, JValue* r) {
  // Guest native libraries need the JNI bridge (Probe D), which is not wired to
  // this runtime yet. Fail loudly and specifically rather than pretend.
  Runtime* rt = Rt(self);
  ReturnString(self,
               std::format("loading native library \"{}\" is not implemented yet (guest JNI bridge pending)",
                           rt->GetStringUtf8(ArgRef(a, 0))),
               r);
}

// --- java.lang.Math ---------------------------------------------------------------

template <double (*F)(double)>
void Math1(Thread*, Frame* a, JValue* r) {
  r->d = F(ArgDouble(a, 0));
}
template <double (*F)(double, double)>
void Math2(Thread*, Frame* a, JValue* r) {
  r->d = F(ArgDouble(a, 0), ArgDouble(a, 2));
}
double Sin(double x) { return std::sin(x); }
double Cos(double x) { return std::cos(x); }
double Tan(double x) { return std::tan(x); }
double Asin(double x) { return std::asin(x); }
double Acos(double x) { return std::acos(x); }
double Atan(double x) { return std::atan(x); }
double Sinh(double x) { return std::sinh(x); }
double Cosh(double x) { return std::cosh(x); }
double Tanh(double x) { return std::tanh(x); }
double Exp(double x) { return std::exp(x); }
double Expm1(double x) { return std::expm1(x); }
double Log(double x) { return std::log(x); }
double Log10(double x) { return std::log10(x); }
double Log1p(double x) { return std::log1p(x); }
double Sqrt(double x) { return std::sqrt(x); }
double Cbrt(double x) { return std::cbrt(x); }
double Ceil(double x) { return std::ceil(x); }
double Floor(double x) { return std::floor(x); }
double Rint(double x) { return std::nearbyint(x); }  // default mode: round half to even
double Atan2(double y, double x) { return std::atan2(y, x); }
double Pow(double x, double y) {
  // Java: pow(x, NaN) is NaN even for x == 1 (C returns 1).
  if (std::isnan(y)) return y;
  if (std::isnan(x) && y != 0.0) return x;
  if ((x == 1.0 || x == -1.0) && std::isinf(y)) return std::nan("");
  return std::pow(x, y);
}
double Hypot(double x, double y) { return std::hypot(x, y); }
double IeeeRemainder(double x, double y) { return std::remainder(x, y); }

// --- java.lang.Double / Float -------------------------------------------------------

void Double_doubleToRawLongBits(Thread*, Frame* a, JValue* r) { r->j = ArgLong(a, 0); }
void Double_longBitsToDouble(Thread*, Frame* a, JValue* r) { r->j = ArgLong(a, 0); }
void Float_floatToRawIntBits(Thread*, Frame* a, JValue* r) { r->i = ArgInt(a, 0); }
void Float_intBitsToFloat(Thread*, Frame* a, JValue* r) { r->i = ArgInt(a, 0); }

// Java's Double.toString / Float.toString layout around the shortest
// round-tripping decimal digits.
std::string JavaFloatingToString(double d, bool is_float) {
  if (std::isnan(d)) return "NaN";
  if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
  if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
  char buf[64];
  std::to_chars_result res = is_float
      ? std::to_chars(buf, buf + sizeof(buf), static_cast<float>(d), std::chars_format::scientific)
      : std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::scientific);
  const std::string_view sci(buf, static_cast<std::size_t>(res.ptr - buf));
  // "-1.2345e+06" -> sign, digits "12345", exponent 6
  const bool neg = sci[0] == '-';
  const std::size_t e_pos = sci.find('e');
  std::string digits;
  for (char c : sci.substr(neg ? 1 : 0, e_pos - (neg ? 1 : 0))) {
    if (c != '.') digits.push_back(c);
  }
  int exp = 0;
  std::from_chars(sci.data() + e_pos + (sci[e_pos + 1] == '+' ? 2 : 1), sci.data() + sci.size(), exp);
  std::string out = neg ? "-" : "";
  const double mag = std::fabs(d);
  if (mag >= 1e-3 && mag < 1e7) {
    if (exp >= 0) {
      const std::size_t int_len = static_cast<std::size_t>(exp) + 1;
      std::string int_part = digits.substr(0, std::min(int_len, digits.size()));
      int_part.append(int_len > digits.size() ? int_len - digits.size() : 0, '0');
      std::string frac = int_len < digits.size() ? digits.substr(int_len) : "0";
      out += int_part + "." + frac;
    } else {
      out += "0." + std::string(static_cast<std::size_t>(-exp - 1), '0') + digits;
    }
  } else {
    out += digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp);
  }
  return out;
}

void Double_nativeToString(Thread* self, Frame* a, JValue* r) {
  ReturnString(self, JavaFloatingToString(ArgDouble(a, 0), ArgInt(a, 2) != 0), r);
}

// Validates Java's FloatingDecimal grammar and returns the text from_chars
// accepts (sign, body, whether it is hex). NaN/Infinity are handled by caller.
bool ParseJavaFloating(std::string_view s, bool* neg, std::string* body, bool* hex) {
  *neg = false;
  *hex = false;
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
    *neg = s[0] == '-';
    s.remove_prefix(1);
  }
  if (!s.empty() && std::string_view("fFdD").find(s.back()) != std::string_view::npos) s.remove_suffix(1);
  if (s.empty()) return false;
  auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  auto is_hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
  std::size_t i = 0;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    *hex = true;
    i = 2;
    std::size_t mant = 0;
    while (i < s.size() && is_hex(s[i])) ++i, ++mant;
    if (i < s.size() && s[i] == '.') {
      ++i;
      while (i < s.size() && is_hex(s[i])) ++i, ++mant;
    }
    if (mant == 0 || i >= s.size() || (s[i] != 'p' && s[i] != 'P')) return false;  // exponent required
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    std::size_t ed = 0;
    while (i < s.size() && is_digit(s[i])) ++i, ++ed;
    if (ed == 0 || i != s.size()) return false;
    *body = std::string(s.substr(2));
    return true;
  }
  std::size_t mant = 0;
  while (i < s.size() && is_digit(s[i])) ++i, ++mant;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && is_digit(s[i])) ++i, ++mant;
  }
  if (mant == 0) return false;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    std::size_t ed = 0;
    while (i < s.size() && is_digit(s[i])) ++i, ++ed;
    if (ed == 0) return false;
  }
  if (i != s.size()) return false;
  *body = std::string(s);
  return true;
}

template <typename T>
bool ParseFloating(Thread* self, Object* jstr, T* out) {
  Runtime* rt = Rt(self);
  const std::string s = rt->GetStringUtf8(jstr);
  auto fail = [&] {
    Throw(self, "Ljava/lang/NumberFormatException;", s.empty() ? "empty String" : "For input string: \"" + s + "\"");
    return false;
  };
  std::string_view v = s;
  bool neg = false;
  std::string_view unsigned_v = v;
  if (!unsigned_v.empty() && (unsigned_v[0] == '+' || unsigned_v[0] == '-')) {
    neg = unsigned_v[0] == '-';
    unsigned_v.remove_prefix(1);
  }
  if (unsigned_v == "NaN") {
    *out = std::numeric_limits<T>::quiet_NaN();
    return true;
  }
  if (unsigned_v == "Infinity") {
    *out = neg ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
    return true;
  }
  std::string body;
  bool hex = false;
  if (!ParseJavaFloating(v, &neg, &body, &hex)) return fail();
  T value{};
  auto res = std::from_chars(body.data(), body.data() + body.size(), value,
                             hex ? std::chars_format::hex : std::chars_format::general);
  if (res.ec == std::errc::result_out_of_range) {
    // Overflow rounds to infinity, underflow to zero (Java semantics); strtod
    // reports which one happened.
    const std::string c = (hex ? "0x" : "") + body;
    const double approx = std::strtod(c.c_str(), nullptr);
    value = std::fabs(approx) >= 1.0 ? std::numeric_limits<T>::infinity() : T(0);
  } else if (res.ec != std::errc() || res.ptr != body.data() + body.size()) {
    return fail();
  }
  *out = neg ? -value : value;
  return true;
}

void Double_nativeParse(Thread* self, Frame* a, JValue* r) {
  double d;
  if (ParseFloating(self, ArgRef(a, 0), &d)) r->d = d;
}

void Float_nativeParse(Thread* self, Frame* a, JValue* r) {
  float f;
  if (ParseFloating(self, ArgRef(a, 0), &f)) r->f = f;
}

// --- java.lang.Character (Unicode data from the Windows ICU) -------------------------

constexpr std::int32_t kLetter = 1, kDigit = 2, kSpace = 4, kUpper = 8, kLower = 16, kControl = 32;

void Character_nativeClassify(Thread*, Frame* a, JValue* r) {
  const UChar32 cp = ArgInt(a, 0);
  std::int32_t bits = 0;
  if (cp < 0 || cp > 0x10ffff) {
    r->i = 0;
    return;
  }
  switch (u_charType(cp)) {
    case U_UPPERCASE_LETTER:
    case U_LOWERCASE_LETTER:
    case U_TITLECASE_LETTER:
    case U_MODIFIER_LETTER:
    case U_OTHER_LETTER:
      bits |= kLetter;
      break;
    case U_DECIMAL_DIGIT_NUMBER:
      bits |= kDigit;
      break;
    case U_SPACE_SEPARATOR:
    case U_LINE_SEPARATOR:
    case U_PARAGRAPH_SEPARATOR:
      bits |= kSpace;
      break;
    case U_CONTROL_CHAR:
      bits |= kControl;
      // Java whitespace controls: \t \n \v \f \r and FS/GS/RS/US.
      if ((cp >= 0x09 && cp <= 0x0d) || (cp >= 0x1c && cp <= 0x1f)) bits |= kSpace;
      break;
    default:
      break;
  }
  if (u_hasBinaryProperty(cp, UCHAR_UPPERCASE)) bits |= kUpper;
  if (u_hasBinaryProperty(cp, UCHAR_LOWERCASE)) bits |= kLower;
  r->i = bits;
}

void Character_nativeToUpper(Thread*, Frame* a, JValue* r) { r->i = u_toupper(ArgInt(a, 0)); }
void Character_nativeToLower(Thread*, Frame* a, JValue* r) { r->i = u_tolower(ArgInt(a, 0)); }

// --- java.lang.Throwable ---------------------------------------------------------------

void Throwable_nativeFillInStackTrace(Thread* self, Frame*, JValue* r) {
  Runtime* rt = Rt(self);
  // Skip this native, then Throwable.fillInStackTrace and the constructor chain
  // of the throwable being created.
  Frame* f = self->top_frame();
  if (f != nullptr) f = f->prev;
  while (f != nullptr && rt->wk.throwable->IsAssignableFrom(f->method->declaring) &&
         (f->method->name == "fillInStackTrace" || f->method->name == "<init>")) {
    f = f->prev;
  }
  std::vector<std::int64_t> pairs;
  for (; f != nullptr; f = f->prev) {
    pairs.push_back(rt->MethodIndex(f->method));
    pairs.push_back(f->method->IsNative() ? -1 : static_cast<std::int64_t>(f->dex_pc));
  }
  RtClass* long_array = rt->class_linker().FindClass(self, "[J", nullptr);
  if (long_array == nullptr) return;
  Array* arr = rt->heap().AllocArray(self, long_array, static_cast<std::int32_t>(pairs.size()));
  if (arr == nullptr) return;
  if (!pairs.empty()) std::memcpy(arr->data(), pairs.data(), pairs.size() * sizeof(std::int64_t));
  r->l = arr;
}

void Throwable_nativeGetStackTrace(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  auto* bt = static_cast<Array*>(ArgRef(a, 0));
  RtClass* ste = rt->wk.stack_trace_element;
  RtClass* ste_array = rt->class_linker().FindClass(self, "[Ljava/lang/StackTraceElement;", nullptr);
  if (ste_array == nullptr) return;
  if (bt == nullptr || bt->klass->descriptor != "[J") {
    r->l = rt->heap().AllocArray(self, ste_array, 0);
    return;
  }
  // Copied out so the loop below, which allocates, never touches the array.
  std::vector<std::int64_t> pairs(bt->elements<std::int64_t>(),
                                  bt->elements<std::int64_t>() + (bt->length & ~1));
  RtMethod* ctor = ste->FindDeclaredMethod("<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
  if (ctor == nullptr || !rt->class_linker().EnsureInitialized(self, ste)) {
    if (!self->IsExceptionPending()) Throw(self, "Ljava/lang/InternalError;", "StackTraceElement constructor missing");
    return;
  }
  std::vector<RtMethod*> methods;
  std::vector<std::int64_t> pcs;
  for (std::size_t i = 0; i + 1 < pairs.size(); i += 2) {
    RtMethod* m = rt->MethodAt(pairs[i]);
    if (m == nullptr) continue;  // not a runtime-issued index: skip, never dereference
    methods.push_back(m);
    pcs.push_back(pairs[i + 1]);
  }
  RootScope roots(self);
  auto out = roots.Add(rt->heap().AllocArray(self, ste_array, static_cast<std::int32_t>(methods.size())));
  if (out.Get() == nullptr) return;
  for (std::size_t i = 0; i < methods.size(); ++i) {
    RtMethod* m = methods[i];
    std::string file;
    std::int32_t line = -1;
    if (m->IsNative() || pcs[i] < 0) {
      line = -2;
    } else if (m->declaring->dex_cache != nullptr) {
      const DexFile& dex = *m->declaring->dex_cache->dex;
      file = std::string(dex.SourceFile(dex.GetClassDef(m->declaring->class_def_idx)));
      if (m->prepare_state.load() == PrepareState::kPrepared && pcs[i] < static_cast<std::int64_t>(m->code.insns.size())) {
        if (auto ln = dex.LineNumber(m->code, static_cast<std::uint32_t>(pcs[i]))) line = static_cast<std::int32_t>(*ln);
      }
    }
    auto cls = roots.Add(rt->NewStringUtf8(self, BinaryName(m->declaring)));
    auto name = roots.Add(rt->NewStringUtf8(self, m->name));
    auto jfile = roots.Add(file.empty() ? nullptr : rt->NewStringUtf8(self, file));
    if (self->IsExceptionPending()) return;
    auto elem = roots.Add(rt->heap().AllocObject(self, ste));
    if (elem.Get() == nullptr) return;
    JValue args[4];
    args[0].l = cls.Get();
    args[1].l = name.Get();
    args[2].l = jfile.Get();
    args[3].raw = 0;
    args[3].i = line;
    JValue unused;
    if (!rt->Invoke(self, ctor, elem.Get(), args, &unused)) return;
    static_cast<Array*>(out.Get())->elements<Object*>()[i] = elem.Get();
  }
  r->l = out.Get();
}

// --- java.lang.VMStack, java.util.Random --------------------------------------------------

void VMStack_getCallingClassLoader(Thread* self, Frame*, JValue* r) {
  // depth 1: the caller of the method that called getCallingClassLoader().
  ClassLoaderRt* l = natives::CallerLoader(self, 1);
  r->l = l != nullptr ? l->java_object : nullptr;
}

void Random_nativeEntropySeed(Thread* self, Frame*, JValue* r) {
  std::int64_t seed = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    Throw(self, "Ljava/lang/InternalError;", "host entropy source unavailable");
    return;
  }
  r->j = seed;
}

}  // namespace

namespace natives {

void RegisterLang(Runtime* rt) {
  auto reg = [rt](const char* key, NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Ljava/lang/Object;.getClass()Ljava/lang/Class;", Object_getClass);
  reg("Ljava/lang/Object;.hashCode()I", Object_hashCode);
  reg("Ljava/lang/Object;.clone()Ljava/lang/Object;", Object_clone);
  reg("Ljava/lang/Object;.notify()V", Object_notify);
  reg("Ljava/lang/Object;.notifyAll()V", Object_notifyAll);
  reg("Ljava/lang/Object;.wait(JI)V", Object_wait);

  reg("Ljava/lang/Class;.nativeGetName()Ljava/lang/String;", Class_nativeGetName);
  reg("Ljava/lang/Class;.getSuperclass()Ljava/lang/Class;", Class_getSuperclass);
  reg("Ljava/lang/Class;.getInterfaces()[Ljava/lang/Class;", Class_getInterfaces);
  reg("Ljava/lang/Class;.getComponentType()Ljava/lang/Class;", Class_getComponentType);
  reg("Ljava/lang/Class;.isArray()Z", Class_isArray);
  reg("Ljava/lang/Class;.isInterface()Z", Class_isInterface);
  reg("Ljava/lang/Class;.isPrimitive()Z", Class_isPrimitive);
  reg("Ljava/lang/Class;.getModifiers()I", Class_getModifiers);
  reg("Ljava/lang/Class;.isInstance(Ljava/lang/Object;)Z", Class_isInstance);
  reg("Ljava/lang/Class;.isAssignableFrom(Ljava/lang/Class;)Z", Class_isAssignableFrom);
  reg("Ljava/lang/Class;.getClassLoader()Ljava/lang/ClassLoader;", Class_getClassLoader);
  reg("Ljava/lang/Class;.newInstance()Ljava/lang/Object;", Class_newInstance);
  reg("Ljava/lang/Class;.forName(Ljava/lang/String;)Ljava/lang/Class;", Class_forName1);
  reg("Ljava/lang/Class;.forName(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;", Class_forName3);
  reg("Ljava/lang/Class;.getPrimitiveClass(Ljava/lang/String;)Ljava/lang/Class;", Class_getPrimitiveClass);
  reg("Ljava/lang/Class;.getEnumConstantsShared()[Ljava/lang/Object;", Class_getEnumConstantsShared);

  reg("Ljava/lang/ClassLoader;.nativeFindClass(Ljava/lang/String;)Ljava/lang/Class;", ClassLoader_nativeFindClass);
  reg("Ljava/lang/ClassLoader;.nativeFindLoadedClass(Ljava/lang/String;)Ljava/lang/Class;",
      ClassLoader_nativeFindLoadedClass);

  reg("Ljava/lang/String;.intern()Ljava/lang/String;", String_intern);

  reg("Ljava/lang/System;.arraycopy(Ljava/lang/Object;ILjava/lang/Object;II)V", System_arraycopy);
  reg("Ljava/lang/System;.currentTimeMillis()J", System_currentTimeMillis);
  reg("Ljava/lang/System;.nanoTime()J", System_nanoTime);
  reg("Ljava/lang/System;.identityHashCode(Ljava/lang/Object;)I", System_identityHashCode);
  reg("Ljava/lang/System;.logNative(ILjava/lang/String;Ljava/lang/String;)V", System_logNative);
  reg("Ljava/lang/System;.nativeGetProperty(Ljava/lang/String;)Ljava/lang/String;", System_nativeGetProperty);
  reg("Ljava/lang/System;.nativeSetProperty(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
      System_nativeSetProperty);

  reg("Ljava/lang/Runtime;.gc()V", Runtime_gc);
  reg("Ljava/lang/Runtime;.availableProcessors()I", Runtime_availableProcessors);
  reg("Ljava/lang/Runtime;.totalMemory()J", Runtime_totalMemory);
  reg("Ljava/lang/Runtime;.freeMemory()J", Runtime_freeMemory);
  reg("Ljava/lang/Runtime;.maxMemory()J", Runtime_maxMemory);
  reg("Ljava/lang/Runtime;.nativeExit(I)V", Runtime_nativeExit);
  reg("Ljava/lang/Runtime;.nativeLoadLibrary(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/String;",
      Runtime_nativeLoadLibrary);
  reg("Ljava/lang/Runtime;.nativeLoad(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/String;",
      Runtime_nativeLoadLibrary);

  reg("Ljava/lang/Math;.sin(D)D", Math1<Sin>);
  reg("Ljava/lang/Math;.cos(D)D", Math1<Cos>);
  reg("Ljava/lang/Math;.tan(D)D", Math1<Tan>);
  reg("Ljava/lang/Math;.asin(D)D", Math1<Asin>);
  reg("Ljava/lang/Math;.acos(D)D", Math1<Acos>);
  reg("Ljava/lang/Math;.atan(D)D", Math1<Atan>);
  reg("Ljava/lang/Math;.sinh(D)D", Math1<Sinh>);
  reg("Ljava/lang/Math;.cosh(D)D", Math1<Cosh>);
  reg("Ljava/lang/Math;.tanh(D)D", Math1<Tanh>);
  reg("Ljava/lang/Math;.exp(D)D", Math1<Exp>);
  reg("Ljava/lang/Math;.expm1(D)D", Math1<Expm1>);
  reg("Ljava/lang/Math;.log(D)D", Math1<Log>);
  reg("Ljava/lang/Math;.log10(D)D", Math1<Log10>);
  reg("Ljava/lang/Math;.log1p(D)D", Math1<Log1p>);
  reg("Ljava/lang/Math;.sqrt(D)D", Math1<Sqrt>);
  reg("Ljava/lang/Math;.cbrt(D)D", Math1<Cbrt>);
  reg("Ljava/lang/Math;.ceil(D)D", Math1<Ceil>);
  reg("Ljava/lang/Math;.floor(D)D", Math1<Floor>);
  reg("Ljava/lang/Math;.rint(D)D", Math1<Rint>);
  reg("Ljava/lang/Math;.atan2(DD)D", Math2<Atan2>);
  reg("Ljava/lang/Math;.pow(DD)D", Math2<Pow>);
  reg("Ljava/lang/Math;.hypot(DD)D", Math2<Hypot>);
  reg("Ljava/lang/Math;.IEEEremainder(DD)D", Math2<IeeeRemainder>);

  reg("Ljava/lang/Double;.doubleToRawLongBits(D)J", Double_doubleToRawLongBits);
  reg("Ljava/lang/Double;.longBitsToDouble(J)D", Double_longBitsToDouble);
  reg("Ljava/lang/Double;.nativeToString(DZ)Ljava/lang/String;", Double_nativeToString);
  reg("Ljava/lang/Double;.nativeParse(Ljava/lang/String;)D", Double_nativeParse);
  reg("Ljava/lang/Float;.floatToRawIntBits(F)I", Float_floatToRawIntBits);
  reg("Ljava/lang/Float;.intBitsToFloat(I)F", Float_intBitsToFloat);
  reg("Ljava/lang/Float;.nativeParse(Ljava/lang/String;)F", Float_nativeParse);

  reg("Ljava/lang/Character;.nativeClassify(I)I", Character_nativeClassify);
  reg("Ljava/lang/Character;.nativeToUpper(I)I", Character_nativeToUpper);
  reg("Ljava/lang/Character;.nativeToLower(I)I", Character_nativeToLower);

  reg("Ljava/lang/Throwable;.nativeFillInStackTrace()Ljava/lang/Object;", Throwable_nativeFillInStackTrace);
  reg("Ljava/lang/Throwable;.nativeGetStackTrace(Ljava/lang/Object;)[Ljava/lang/StackTraceElement;",
      Throwable_nativeGetStackTrace);

  reg("Ljava/lang/VMStack;.getCallingClassLoader()Ljava/lang/ClassLoader;", VMStack_getCallingClassLoader);
  reg("Ljava/util/Random;.nativeEntropySeed()J", Random_nativeEntropySeed);
}

}  // namespace natives

void RegisterCoreNatives(Runtime* rt) {
  natives::RegisterLang(rt);
  natives::RegisterReflect(rt);
  natives::RegisterThread(rt);
  natives::RegisterIo(rt);
}

}  // namespace arun::vm
