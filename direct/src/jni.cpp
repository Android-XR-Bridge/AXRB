// The Java Native Interface between guest native code (arm64, under Dynarmic)
// and the managed runtime (Androitainer's DEX interpreter, on the host).
//
// JNIEnv and JavaVM are host structures whose function tables hold host-call
// trampolines, so guest code calls (*env)->FindClass(...) exactly as on
// Android. References are tagged handles: locals index the calling VM
// thread's root list, globals a process table; jmethodID/jfieldID are the
// runtime's method and field records. Java -> native calls go through
// RuntimeOptions::invoke_guest_native: arguments are laid out per AAPCS64 and
// the guest function runs on the same thread (which is always a guest thread).
//
// GC: a VM thread is "native" (kNative) while guest code runs, so collections
// never wait for it; each JNI function makes it runnable for its duration.

#include "jni.h"

#include "cpu.h"
#include "log.h"
#include "memory.h"

#include "arun/vm/class.h"
#include "arun/vm/class_linker.h"
#include "arun/vm/heap.h"
#include "arun/vm/monitor.h"
#include "arun/vm/natives.h"
#include "arun/vm/object.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace axrb {

GuestApi& guest_api() {
    static GuestApi api{};
    return api;
}

namespace jni {
using namespace arun::vm;

namespace {

constexpr uint64_t kLocal = 1, kGlobal = 2, kWeak = 3;
constexpr uint32_t kEnvMagic = 0x4a4e4945;  // 'JNIE'
constexpr int JNI_OK = 0, JNI_ERR = -1, JNI_EDETACHED = -2, JNI_EVERSION = -3;

Runtime* g_rt = nullptr;
ClassLoaderRt* g_app_loader = nullptr;

// What guest code sees as JNIEnv*: the table pointer first, as jni.h requires.
struct Env {
    uint64_t functions;
    uint32_t magic;
    Thread* vm;
    GuestThread* guest;
    std::vector<size_t> frames;  // PushLocalFrame bases
};

struct Vm {
    uint64_t functions;
};

std::vector<uint64_t> g_env_table;
std::vector<uint64_t> g_vm_table;
Vm g_vm;

std::mutex g_globals_lock;
std::vector<Object*> g_globals;
std::vector<size_t> g_free_globals;

std::mutex g_libs_lock;
std::vector<std::pair<std::string, uint64_t>> g_libraries;  // path, dlopen handle

// --- references ----------------------------------------------------------------

uint64_t make_local(Thread* self, Object* o) {
    if (!o) return 0;
    size_t index = self->PushRoot(o);
    return ((index + 1) << 2) | kLocal;
}

// The JNI function in progress on this thread, for diagnostics.
struct CurrentCall {
    const char* name = nullptr;
    uint64_t caller = 0;
};
thread_local CurrentCall t_call;

std::string current_call() {
    if (!t_call.name) return "host code";
    return std::string(t_call.name) + " from " + describe_address(t_call.caller);
}

Object* decode_ref(Thread* self, uint64_t ref) {
    if (!ref) return nullptr;
    uint64_t index = (ref >> 2) - 1;
    switch (ref & 3) {
        case kLocal:
            if (index < self->root_count()) return self->GetRoot(index);
            LOGE("jni", "stale local reference %llx in %s", (unsigned long long)ref, current_call().c_str());
            return nullptr;
        case kGlobal:
        case kWeak: {
            std::lock_guard<std::mutex> held(g_globals_lock);
            if (index < g_globals.size()) return g_globals[index];
            LOGE("jni", "stale global reference %llx in %s", (unsigned long long)ref, current_call().c_str());
            return nullptr;
        }
        default:
            LOGE("jni", "invalid reference %llx", (unsigned long long)ref);
            return nullptr;
    }
}

uint64_t make_global(Object* o, uint64_t kind = kGlobal) {
    if (!o) return 0;
    std::lock_guard<std::mutex> held(g_globals_lock);
    size_t index;
    if (!g_free_globals.empty()) {
        index = g_free_globals.back();
        g_free_globals.pop_back();
        g_globals[index] = o;
    } else {
        index = g_globals.size();
        g_globals.push_back(o);
    }
    return ((index + 1) << 2) | kind;
}

void delete_global(uint64_t ref) {
    if (!ref || (ref & 3) == kLocal) return;
    std::lock_guard<std::mutex> held(g_globals_lock);
    uint64_t index = (ref >> 2) - 1;
    if (index < g_globals.size() && g_globals[index]) {
        g_globals[index] = nullptr;
        g_free_globals.push_back(index);
    }
}

// --- threads ---------------------------------------------------------------------

Env* new_env(Thread* vm, GuestThread* guest) {
    auto* env = new Env();
    env->functions = reinterpret_cast<uint64_t>(g_env_table.data());
    env->magic = kEnvMagic;
    env->vm = vm;
    env->guest = guest;
    return env;
}

// The Env of the calling guest thread, attaching it to the VM if needed.
Env* attach(bool daemon, const char* name) {
    GuestThread* guest = current_thread();
    if (!guest) fatal("JNI used from a host thread");
    if (guest->jni_env) return static_cast<Env*>(guest->jni_env);
    Thread* vm = Thread::Current();
    if (!vm) {
        std::string thread_name = name ? name : guest->name;
        vm = new Thread(g_rt, thread_name);
        vm->daemon = daemon;
        vm->AttachCurrent();
        if (!g_rt->AttachPeer(vm, thread_name, daemon)) LOGW("jni", "no java.lang.Thread peer for %s", thread_name.c_str());
        vm->TransitionFromRunnable(ThreadState::kNative);
        TRACE("jni", "attached thread %d (%s)", guest->tid, thread_name.c_str());
    }
    Env* env = new_env(vm, guest);
    guest->jni_env = env;
    return env;
}

Env* env_of(HostCall& c) {
    auto* env = reinterpret_cast<Env*>(untag(c.x(0)));
    if (!env || env->magic != kEnvMagic) {
        GuestThread* guest = current_thread();
        if (guest && guest->jni_env) return static_cast<Env*>(guest->jni_env);
        fatal("JNI call with an invalid JNIEnv* %llx from %s", (unsigned long long)c.x(0),
              describe_address(c.lr()).c_str());
    }
    return env;
}

// Runnable for the duration of a JNI function called from native code.
class Runnable {
public:
    explicit Runnable(Thread* t) : t_(t), was_native_(t->state() != ThreadState::kRunnable) {
        if (was_native_) t_->TransitionToRunnable();
    }
    ~Runnable() {
        if (was_native_) t_->TransitionFromRunnable(ThreadState::kNative);
    }

private:
    Thread* t_;
    bool was_native_;
};

// --- classes, methods, fields ----------------------------------------------------

std::string to_descriptor(const std::string& jni_name) {
    if (!jni_name.empty() && jni_name[0] == '[') return jni_name;
    if (jni_name.size() > 1 && jni_name[0] == 'L' && jni_name.back() == ';') return jni_name;
    return "L" + jni_name + ";";
}

ClassLoaderRt* caller_loader(Thread* self) {
    for (Frame* f = self->top_frame(); f; f = f->prev)
        if (f->method && f->method->declaring) return f->method->declaring->loader;
    return nullptr;
}

RtClass* find_class(Thread* self, const std::string& name) {
    std::string descriptor = to_descriptor(name);
    auto& linker = g_rt->class_linker();
    ClassLoaderRt* loader = caller_loader(self);
    // Native threads see the system class loader on Android; engines that
    // cache classes in JNI_OnLoad never notice, the rest are helped by also
    // trying the application's loader.
    if (RtClass* c = linker.FindClassNoThrow(self, descriptor, loader ? loader : g_app_loader)) return c;
    if (g_app_loader && loader != g_app_loader)
        if (RtClass* c = linker.FindClassNoThrow(self, descriptor, g_app_loader)) return c;
    if (RtClass* c = linker.FindClassNoThrow(self, descriptor, nullptr)) return c;
    LOGW("jni", "FindClass(%s): not found", name.c_str());
    linker.NoteMissing(descriptor);
    g_rt->ThrowNew(self, "Ljava/lang/NoClassDefFoundError;", name);
    return nullptr;
}

RtClass* class_of(Thread* self, uint64_t ref) {
    Object* mirror = decode_ref(self, ref);
    return mirror ? g_rt->ClassFromMirror(mirror) : nullptr;
}

RtMethod* find_method(RtClass* klass, const std::string& name, const std::string& sig, bool is_static) {
    for (RtClass* c = klass; c; c = c->super) {
        if (RtMethod* m = c->FindDeclaredMethod(name, sig))
            if (m->IsStatic() == is_static) return m;
    }
    // Interface methods (abstract or default) for calls through an interface type.
    for (RtClass* i : klass->all_interfaces) {
        if (RtMethod* m = i->FindDeclaredMethod(name, sig))
            if (m->IsStatic() == is_static) return m;
    }
    return nullptr;
}

RtField* find_field(RtClass* klass, const std::string& name, const std::string& sig, bool is_static) {
    for (RtClass* c = klass; c; c = c->super) {
        if (RtField* f = c->FindDeclaredField(name, sig))
            if (f->IsStatic() == is_static) return f;
        if (is_static) {
            for (RtClass* i : c->interfaces)
                if (RtField* f = i->FindDeclaredField(name, sig)) return f;
        }
    }
    return nullptr;
}

// The implementation a virtual call reaches for `receiver`.
RtMethod* dispatch(Thread* self, Object* receiver, RtMethod* m) {
    if (!receiver || m->IsDirect()) return m;
    RtClass* k = receiver->klass;
    if (m->declaring->IsInterface()) {
        RtMethod* target = g_rt->class_linker().FindInterfaceTarget(self, k, m);
        return target ? target : m;
    }
    if (m->vtable_index >= 0 && static_cast<size_t>(m->vtable_index) < k->vtable.size()) return k->vtable[m->vtable_index];
    for (RtClass* c = k; c; c = c->super)
        if (RtMethod* found = c->FindDeclaredMethod(m->name, m->signature))
            if (!found->IsAbstract()) return found;
    return m;
}

// --- argument sources for Call*Method --------------------------------------------

// Reads integer and floating-point arguments the way an AAPCS64 callee does.
class ArgSource {
public:
    virtual ~ArgSource() = default;
    virtual uint64_t next_int() = 0;
    virtual uint64_t next_fp(bool is_float_in_jvalue) = 0;  // raw bits: double, or float for jvalue arrays
    virtual bool fp_is_double() const { return true; }
};

// The "..." of CallXMethod: variadic arguments sit in the next free argument
// registers (AAPCS64 for Linux), then on the stack.
class VarArgs : public ArgSource {
public:
    VarArgs(HostCall& c, int first_int) : c_(c), x_(first_int) {}
    uint64_t next_int() override {
        if (x_ < 8) return c_.x(x_++);
        return c_.stack(stack_++);
    }
    uint64_t next_fp(bool) override {
        if (v_ < 8) return c_.v(v_++)[0];
        return c_.stack(stack_++);
    }

private:
    HostCall& c_;
    int x_;
    int v_ = 0;
    int stack_ = 0;
};

// A va_list: { __stack, __gr_top, __vr_top, __gr_offs, __vr_offs }.
class VaList : public ArgSource {
public:
    explicit VaList(uint64_t address) {
        uint64_t words[4] = {};
        guest_read(address, words, sizeof words);
        stack_ = words[0];
        gr_top_ = words[1];
        vr_top_ = words[2];
        int32_t offs[2] = {};
        guest_read(address + 24, offs, sizeof offs);
        gr_offs_ = offs[0];
        vr_offs_ = offs[1];
    }
    uint64_t next_int() override {
        uint64_t value = 0;
        if (gr_offs_ < 0) {
            guest_read(gr_top_ + gr_offs_, &value, 8);
            gr_offs_ += 8;
        } else {
            guest_read(stack_, &value, 8);
            stack_ += 8;
        }
        return value;
    }
    uint64_t next_fp(bool) override {
        uint64_t value = 0;
        if (vr_offs_ < 0) {
            guest_read(vr_top_ + vr_offs_, &value, 8);
            vr_offs_ += 16;
        } else {
            guest_read(stack_, &value, 8);
            stack_ += 8;
        }
        return value;
    }

private:
    uint64_t stack_, gr_top_, vr_top_;
    int32_t gr_offs_, vr_offs_;
};

// A jvalue array.
class JValues : public ArgSource {
public:
    explicit JValues(uint64_t address) : at_(address) {}
    uint64_t next_int() override { return read(); }
    uint64_t next_fp(bool) override { return read(); }
    bool fp_is_double() const override { return false; }

private:
    uint64_t read() {
        uint64_t value = 0;
        guest_read(at_, &value, 8);
        at_ += 8;
        return value;
    }
    uint64_t at_;
};

// Converts guest arguments to JValues per the method's shorty.
std::vector<JValue> collect_args(Thread* self, RtMethod* m, ArgSource& src) {
    std::vector<JValue> args;
    for (size_t i = 1; i < m->shorty.size(); ++i) {
        JValue v{};
        switch (m->shorty[i]) {
            case 'L': v.l = decode_ref(self, src.next_int()); break;
            case 'J': v.j = static_cast<int64_t>(src.next_int()); break;
            case 'Z': v.i = static_cast<uint8_t>(src.next_int()) != 0; break;
            case 'B': v.i = static_cast<int8_t>(src.next_int()); break;
            case 'C': v.i = static_cast<uint16_t>(src.next_int()); break;
            case 'S': v.i = static_cast<int16_t>(src.next_int()); break;
            case 'I': v.i = static_cast<int32_t>(src.next_int()); break;
            case 'F': {
                uint64_t bits = src.next_fp(true);
                if (src.fp_is_double()) {  // promoted to double through "..."/va_list
                    double d;
                    std::memcpy(&d, &bits, 8);
                    v.f = static_cast<float>(d);
                } else {
                    uint32_t b32 = static_cast<uint32_t>(bits);
                    std::memcpy(&v.f, &b32, 4);
                }
                break;
            }
            case 'D': {
                uint64_t bits = src.next_fp(false);
                std::memcpy(&v.d, &bits, 8);
                break;
            }
            default: break;
        }
        args.push_back(v);
    }
    return args;
}

enum class CallForm { Var, VaList, Array };

// Call<Type>Method{,V,A}, CallNonvirtual..., CallStatic...
void do_call(HostCall& c, char ret, bool is_static, bool nonvirtual, CallForm form) {
    Env* e = env_of(c);
    Thread* self = e->vm;
    Runnable runnable(self);
    // (env, obj|clazz, [clazz,] methodID, args...)
    int mid_reg = nonvirtual ? 3 : 2;
    auto* m = reinterpret_cast<RtMethod*>(c.x(mid_reg));
    Object* receiver = is_static ? nullptr : decode_ref(self, c.x(1));
    if (!m) {
        g_rt->ThrowNew(self, "Ljava/lang/NullPointerException;", "jmethodID is null");
        c.ret(0);
        return;
    }
    std::vector<JValue> args;
    if (form == CallForm::Var) {
        VarArgs src(c, mid_reg + 1);
        args = collect_args(self, m, src);
    } else if (form == CallForm::VaList) {
        VaList src(c.x(mid_reg + 1));
        args = collect_args(self, m, src);
    } else {
        JValues src(c.x(mid_reg + 1));
        args = collect_args(self, m, src);
    }
    if (!is_static && !receiver) {
        g_rt->ThrowNew(self, "Ljava/lang/NullPointerException;", "JNI call of " + m->PrettyName() + " on null");
        c.ret(0);
        return;
    }
    RtMethod* target = (is_static || nonvirtual) ? m : dispatch(self, receiver, m);
    if (is_static) g_rt->class_linker().EnsureInitialized(self, target->declaring);
    JValue result{};
    bool ok;
    {
        RootScope roots(self);
        if (receiver) roots.Add(receiver);
        for (auto& a : args)
            if (a.l) roots.Add(a.l);
        ok = !self->IsExceptionPending() && g_rt->Invoke(self, target, receiver, args, &result);
    }
    if (!ok) {
        c.ret(0);
        c.set_v(0, {0, 0});
        return;
    }
    switch (ret) {
        case 'V': break;
        case 'L': c.ret(make_local(self, result.l)); break;
        case 'Z': c.ret(result.i ? 1 : 0); break;
        case 'B': c.ret(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(result.i)))); break;
        case 'C': c.ret(static_cast<uint16_t>(result.i)); break;
        case 'S': c.ret(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(result.i)))); break;
        case 'I': c.ret(static_cast<uint64_t>(static_cast<int64_t>(result.i))); break;
        case 'J': c.ret(static_cast<uint64_t>(result.j)); break;
        case 'F': c.ret_float(result.f); break;
        case 'D': c.ret_double(result.d); break;
    }
}

// --- arrays ---------------------------------------------------------------------

size_t element_size(char kind) {
    switch (kind) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'I': case 'F': return 4;
        default: return 8;
    }
}

RtClass* primitive_array_class(Thread* self, char kind) {
    char descriptor[3] = {'[', kind, 0};
    return g_rt->class_linker().FindClass(self, descriptor, nullptr);
}

Array* array_of(Thread* self, uint64_t ref) { return static_cast<Array*>(decode_ref(self, ref)); }

// --- the function table -------------------------------------------------------

using Impl = std::function<void(HostCall&)>;
std::map<std::string, Impl> g_impls;

void def(const std::string& name, Impl fn) { g_impls[name] = std::move(fn); }

std::string utf8_of(Thread* self, Object* s) { return s ? g_rt->GetStringUtf8(s) : std::string(); }

std::string guest_str(uint64_t address) {
    std::string s;
    guest_string(address, s, 1 << 20);
    return s;
}

void* host_alloc(size_t bytes) { return std::malloc(bytes ? bytes : 1); }

void define_functions() {
    def("GetVersion", [](HostCall& c) { c.ret(0x00010006); });

    def("DefineClass", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        g_rt->ThrowNew(e->vm, "Ljava/lang/UnsupportedOperationException;", "DefineClass");
        c.ret(0);
    });

    def("FindClass", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        std::string name = guest_str(c.x(1));
        RtClass* k = find_class(e->vm, name);
        TRACE("jni", "FindClass(%s) = %s", name.c_str(), k ? "ok" : "missing");
        c.ret(k ? make_local(e->vm, g_rt->ClassMirror(e->vm, k)) : 0);
    });

    def("FromReflectedMethod", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        if (!o) return c.ret(0);
        RtField* f = find_field(o->klass, "vmMethod", "J", false);
        c.ret(f ? static_cast<uint64_t>(FieldSlots(o)[f->slot].j) : 0);
    });
    def("FromReflectedField", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        if (!o) return c.ret(0);
        RtField* f = find_field(o->klass, "vmField", "J", false);
        c.ret(f ? static_cast<uint64_t>(FieldSlots(o)[f->slot].j) : 0);
    });

    def("GetSuperclass", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        RtClass* k = class_of(e->vm, c.x(1));
        c.ret(k && !k->IsInterface() && k->super ? make_local(e->vm, g_rt->ClassMirror(e->vm, k->super)) : 0);
    });
    def("IsAssignableFrom", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        RtClass* from = class_of(e->vm, c.x(1));
        RtClass* to = class_of(e->vm, c.x(2));
        c.ret(from && to && to->IsAssignableFrom(from) ? 1 : 0);
    });

    def("Throw", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* t = decode_ref(e->vm, c.x(1));
        if (t) e->vm->SetException(t);
        c.ret(t ? 0 : static_cast<uint64_t>(JNI_ERR));
    });
    def("ThrowNew", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        RtClass* k = class_of(e->vm, c.x(1));
        if (!k) return c.ret(static_cast<uint64_t>(JNI_ERR));
        g_rt->ThrowNew(e->vm, k->descriptor, c.x(2) ? guest_str(c.x(2)) : std::string());
        c.ret(0);
    });
    def("ExceptionOccurred", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(make_local(e->vm, e->vm->exception()));
    });
    def("ExceptionDescribe", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        if (Object* t = e->vm->exception()) {
            LOGW("jni", "%s", g_rt->DescribeException(e->vm, t).c_str());
        }
    });
    def("ExceptionClear", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        if (e->vm->IsExceptionPending()) TRACE("jni", "ExceptionClear");
        e->vm->ClearException();
    });
    def("FatalError", [](HostCall& c) {
        fatal("JNI FatalError: %s (from %s)", guest_str(c.x(1)).c_str(), describe_address(c.lr()).c_str());
    });
    def("ExceptionCheck", [](HostCall& c) {
        Env* e = env_of(c);
        c.ret(e->vm->IsExceptionPending() ? 1 : 0);
    });

    def("PushLocalFrame", [](HostCall& c) {
        Env* e = env_of(c);
        e->frames.push_back(e->vm->root_count());
        c.ret(0);
    });
    def("PopLocalFrame", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* keep = decode_ref(e->vm, c.x(1));
        if (!e->frames.empty()) {
            e->vm->TruncateRoots(e->frames.back());
            e->frames.pop_back();
        }
        c.ret(make_local(e->vm, keep));
    });
    def("NewGlobalRef", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(make_global(decode_ref(e->vm, c.x(1))));
    });
    def("DeleteGlobalRef", [](HostCall& c) { delete_global(c.x(1)); });
    def("DeleteLocalRef", [](HostCall& c) {
        Env* e = env_of(c);
        uint64_t ref = c.x(1);
        if ((ref & 3) != kLocal || !ref) return;
        size_t index = (ref >> 2) - 1;
        Thread* t = e->vm;
        if (index < t->root_count()) {
            t->SetRoot(index, nullptr);
            size_t floor = e->frames.empty() ? 0 : e->frames.back();
            size_t n = t->root_count();
            while (n > floor && n - 1 >= index && t->GetRoot(n - 1) == nullptr) --n;
            if (n < t->root_count() && n >= floor) t->TruncateRoots(n);
        }
    });
    def("IsSameObject", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(decode_ref(e->vm, c.x(1)) == decode_ref(e->vm, c.x(2)) ? 1 : 0);
    });
    def("NewLocalRef", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(make_local(e->vm, decode_ref(e->vm, c.x(1))));
    });
    def("EnsureLocalCapacity", [](HostCall& c) { c.ret(0); });

    def("AllocObject", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        RtClass* k = class_of(e->vm, c.x(1));
        if (!k || !g_rt->class_linker().EnsureInitialized(e->vm, k)) return c.ret(0);
        c.ret(make_local(e->vm, g_rt->heap().AllocObject(e->vm, k)));
    });
    for (auto [name, form] : {std::pair{"NewObject", CallForm::Var}, std::pair{"NewObjectV", CallForm::VaList},
                              std::pair{"NewObjectA", CallForm::Array}}) {
        def(name, [form = form](HostCall& c) {
            Env* e = env_of(c);
            Thread* self = e->vm;
            Runnable r(self);
            RtClass* k = class_of(self, c.x(1));
            auto* ctor = reinterpret_cast<RtMethod*>(c.x(2));
            if (!k || !ctor || !g_rt->class_linker().EnsureInitialized(self, k)) return c.ret(0);
            std::vector<JValue> args;
            if (form == CallForm::Var) {
                VarArgs src(c, 3);
                args = collect_args(self, ctor, src);
            } else if (form == CallForm::VaList) {
                VaList src(c.x(3));
                args = collect_args(self, ctor, src);
            } else {
                JValues src(c.x(3));
                args = collect_args(self, ctor, src);
            }
            Object* created = nullptr;
            {
                RootScope roots(self);
                for (auto& a : args)
                    if (a.l) roots.Add(a.l);
                auto object = roots.Add(g_rt->heap().AllocObject(self, k));
                if (!object.Get()) return c.ret(0);
                JValue unused{};
                if (!g_rt->Invoke(self, ctor, object.Get(), args, &unused)) return c.ret(0);
                created = object.Get();
            }
            // After the scope: a local reference made inside it would be truncated with it.
            c.ret(make_local(self, created));
        });
    }

    def("GetObjectClass", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        c.ret(o ? make_local(e->vm, g_rt->ClassMirror(e->vm, o->klass)) : 0);
    });
    def("IsInstanceOf", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        RtClass* k = class_of(e->vm, c.x(2));
        c.ret(!o || (k && k->IsAssignableFrom(o->klass)) ? 1 : 0);
    });

    for (bool is_static : {false, true}) {
        def(is_static ? "GetStaticMethodID" : "GetMethodID", [is_static](HostCall& c) {
            Env* e = env_of(c);
            Runnable r(e->vm);
            RtClass* k = class_of(e->vm, c.x(1));
            std::string name = guest_str(c.x(2)), sig = guest_str(c.x(3));
            if (!k) return c.ret(0);
            if (!g_rt->class_linker().EnsureInitialized(e->vm, k)) return c.ret(0);
            RtMethod* m = find_method(k, name, sig, is_static);
            if (!m) {
                LOGW("jni", "Get%sMethodID(%s, %s%s): not found", is_static ? "Static" : "", k->descriptor.c_str(),
                     name.c_str(), sig.c_str());
                g_rt->ThrowNew(e->vm, "Ljava/lang/NoSuchMethodError;", k->descriptor + "." + name + sig);
            }
            c.ret(reinterpret_cast<uint64_t>(m));
        });
        def(is_static ? "GetStaticFieldID" : "GetFieldID", [is_static](HostCall& c) {
            Env* e = env_of(c);
            Runnable r(e->vm);
            RtClass* k = class_of(e->vm, c.x(1));
            std::string name = guest_str(c.x(2)), sig = guest_str(c.x(3));
            if (!k) return c.ret(0);
            if (!g_rt->class_linker().EnsureInitialized(e->vm, k)) return c.ret(0);
            RtField* f = find_field(k, name, sig, is_static);
            if (!f) {
                LOGW("jni", "Get%sFieldID(%s, %s %s): not found", is_static ? "Static" : "", k->descriptor.c_str(),
                     name.c_str(), sig.c_str());
                g_rt->ThrowNew(e->vm, "Ljava/lang/NoSuchFieldError;", k->descriptor + "." + name);
            }
            c.ret(reinterpret_cast<uint64_t>(f));
        });
    }

    static const std::pair<const char*, char> kTypes[] = {{"Object", 'L'}, {"Boolean", 'Z'}, {"Byte", 'B'}, {"Char", 'C'},
                                                          {"Short", 'S'},  {"Int", 'I'},     {"Long", 'J'}, {"Float", 'F'},
                                                          {"Double", 'D'}, {"Void", 'V'}};
    for (auto [type, kind] : kTypes) {
        for (auto [suffix, form] : {std::pair{"", CallForm::Var}, std::pair{"V", CallForm::VaList},
                                    std::pair{"A", CallForm::Array}}) {
            std::string t = type;
            def("Call" + t + "Method" + suffix, [kind = kind, form = form](HostCall& c) { do_call(c, kind, false, false, form); });
            def("CallNonvirtual" + t + "Method" + suffix,
                [kind = kind, form = form](HostCall& c) { do_call(c, kind, false, true, form); });
            def("CallStatic" + t + "Method" + suffix, [kind = kind, form = form](HostCall& c) { do_call(c, kind, true, false, form); });
        }
        if (kind == 'V') continue;
        std::string t = type;
        // Get/Set<Type>Field and the static variants.
        for (bool is_static : {false, true}) {
            def(std::string(is_static ? "GetStatic" : "Get") + t + "Field", [kind = kind, is_static](HostCall& c) {
                Env* e = env_of(c);
                Runnable r(e->vm);
                auto* f = reinterpret_cast<RtField*>(c.x(2));
                if (!f) return c.ret(0);
                Slot* slot;
                if (is_static) {
                    g_rt->class_linker().EnsureInitialized(e->vm, f->declaring);
                    slot = &f->declaring->statics[f->slot];
                } else {
                    Object* o = decode_ref(e->vm, c.x(1));
                    if (!o) {
                        g_rt->ThrowNew(e->vm, "Ljava/lang/NullPointerException;", "field read on null");
                        return c.ret(0);
                    }
                    slot = &FieldSlots(o)[f->slot];
                }
                switch (kind) {
                    case 'L': c.ret(make_local(e->vm, slot->l)); break;
                    case 'Z': c.ret(slot->i ? 1 : 0); break;
                    case 'B': c.ret(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(slot->i)))); break;
                    case 'C': c.ret(static_cast<uint16_t>(slot->i)); break;
                    case 'S': c.ret(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(slot->i)))); break;
                    case 'I': c.ret(static_cast<uint64_t>(static_cast<int64_t>(slot->i))); break;
                    case 'J': c.ret(static_cast<uint64_t>(slot->j)); break;
                    case 'F': c.ret_float(slot->f); break;
                    case 'D': c.ret_double(slot->d); break;
                }
            });
            def(std::string(is_static ? "SetStatic" : "Set") + t + "Field", [kind = kind, is_static](HostCall& c) {
                Env* e = env_of(c);
                Runnable r(e->vm);
                auto* f = reinterpret_cast<RtField*>(c.x(2));
                if (!f) return;
                Slot* slot;
                if (is_static) {
                    g_rt->class_linker().EnsureInitialized(e->vm, f->declaring);
                    slot = &f->declaring->statics[f->slot];
                } else {
                    Object* o = decode_ref(e->vm, c.x(1));
                    if (!o) {
                        g_rt->ThrowNew(e->vm, "Ljava/lang/NullPointerException;", "field write on null");
                        return;
                    }
                    slot = &FieldSlots(o)[f->slot];
                }
                uint64_t v = c.x(3);
                switch (kind) {
                    case 'L': slot->l = decode_ref(e->vm, v); break;
                    case 'Z': slot->i = static_cast<uint8_t>(v) != 0; break;
                    case 'B': slot->i = static_cast<int8_t>(v); break;
                    case 'C': slot->i = static_cast<uint16_t>(v); break;
                    case 'S': slot->i = static_cast<int16_t>(v); break;
                    case 'I': slot->i = static_cast<int32_t>(v); break;
                    case 'J': slot->j = static_cast<int64_t>(v); break;
                    case 'F': slot->f = c.s(0); break;
                    case 'D': slot->d = c.d(0); break;
                }
            });
        }
        if (kind == 'L') continue;
        // Primitive arrays. The heap does not move objects, so element access
        // hands out the array's own storage.
        def("New" + t + "Array", [kind = kind](HostCall& c) {
            Env* e = env_of(c);
            Runnable r(e->vm);
            RtClass* k = primitive_array_class(e->vm, kind);
            int32_t n = static_cast<int32_t>(c.x(1));
            if (!k || n < 0) return c.ret(0);
            c.ret(make_local(e->vm, g_rt->heap().AllocArray(e->vm, k, n)));
        });
        def("Get" + t + "ArrayElements", [](HostCall& c) {
            Env* e = env_of(c);
            Runnable r(e->vm);
            Array* a = array_of(e->vm, c.x(1));
            if (c.x(2)) {
                uint8_t no = 0;
                guest_write(c.x(2), &no, 1);
            }
            c.ret(a ? reinterpret_cast<uint64_t>(a->data()) : 0);
        });
        def("Release" + t + "ArrayElements", [](HostCall&) {});
        for (bool set : {false, true}) {
            def(std::string(set ? "Set" : "Get") + t + "ArrayRegion", [kind = kind, set](HostCall& c) {
                Env* e = env_of(c);
                Runnable r(e->vm);
                Array* a = array_of(e->vm, c.x(1));
                int32_t start = static_cast<int32_t>(c.x(2)), len = static_cast<int32_t>(c.x(3));
                if (!a || start < 0 || len < 0 || start + len > a->length) {
                    g_rt->ThrowNew(e->vm, "Ljava/lang/ArrayIndexOutOfBoundsException;", "JNI array region");
                    return;
                }
                size_t es = element_size(kind);
                uint8_t* data = a->data() + static_cast<size_t>(start) * es;
                if (set) guest_read(c.x(4), data, static_cast<size_t>(len) * es);
                else guest_write(c.x(4), data, static_cast<size_t>(len) * es);
            });
        }
    }

    def("NewString", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        int32_t n = static_cast<int32_t>(c.x(2));
        std::u16string chars(static_cast<size_t>(std::max(0, n)), u'\0');
        if (n > 0) guest_read(c.x(1), chars.data(), chars.size() * 2);
        c.ret(make_local(e->vm, g_rt->NewString(e->vm, chars)));
    });
    def("GetStringLength", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(g_rt->GetString(decode_ref(e->vm, c.x(1))).size());
    });
    auto string_chars = [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        std::u16string s = g_rt->GetString(decode_ref(e->vm, c.x(1)));
        auto* out = static_cast<char16_t*>(host_alloc((s.size() + 1) * 2));
        std::memcpy(out, s.c_str(), (s.size() + 1) * 2);
        if (c.x(2)) {
            uint8_t yes = 1;
            guest_write(c.x(2), &yes, 1);
        }
        c.ret(reinterpret_cast<uint64_t>(out));
    };
    def("GetStringChars", string_chars);
    def("GetStringCritical", string_chars);
    def("ReleaseStringChars", [](HostCall& c) { std::free(reinterpret_cast<void*>(c.x(2))); });
    def("ReleaseStringCritical", [](HostCall& c) { std::free(reinterpret_cast<void*>(c.x(2))); });
    def("NewStringUTF", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        if (!c.x(1)) return c.ret(0);
        c.ret(make_local(e->vm, g_rt->NewStringUtf8(e->vm, guest_str(c.x(1)))));
    });
    def("GetStringUTFLength", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(utf8_of(e->vm, decode_ref(e->vm, c.x(1))).size());
    });
    def("GetStringUTFChars", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        std::string s = utf8_of(e->vm, decode_ref(e->vm, c.x(1)));
        auto* out = static_cast<char*>(host_alloc(s.size() + 1));
        std::memcpy(out, s.c_str(), s.size() + 1);
        if (c.x(2)) {
            uint8_t yes = 1;
            guest_write(c.x(2), &yes, 1);
        }
        c.ret(reinterpret_cast<uint64_t>(out));
    });
    def("ReleaseStringUTFChars", [](HostCall& c) { std::free(reinterpret_cast<void*>(c.x(2))); });
    def("GetStringRegion", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        std::u16string s = g_rt->GetString(decode_ref(e->vm, c.x(1)));
        int32_t start = static_cast<int32_t>(c.x(2)), len = static_cast<int32_t>(c.x(3));
        if (start < 0 || len < 0 || static_cast<size_t>(start + len) > s.size()) {
            g_rt->ThrowNew(e->vm, "Ljava/lang/StringIndexOutOfBoundsException;", "GetStringRegion");
            return;
        }
        guest_write(c.x(4), s.data() + start, static_cast<size_t>(len) * 2);
    });
    def("GetStringUTFRegion", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        std::u16string s = g_rt->GetString(decode_ref(e->vm, c.x(1)));
        int32_t start = static_cast<int32_t>(c.x(2)), len = static_cast<int32_t>(c.x(3));
        if (start < 0 || len < 0 || static_cast<size_t>(start + len) > s.size()) {
            g_rt->ThrowNew(e->vm, "Ljava/lang/StringIndexOutOfBoundsException;", "GetStringUTFRegion");
            return;
        }
        Object* sub = g_rt->NewString(e->vm, std::u16string_view(s).substr(static_cast<size_t>(start), static_cast<size_t>(len)));
        std::string u = utf8_of(e->vm, sub);
        guest_write(c.x(4), u.c_str(), u.size() + 1);
    });

    def("GetArrayLength", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Array* a = array_of(e->vm, c.x(1));
        c.ret(a ? static_cast<uint64_t>(a->length) : 0);
    });
    def("NewObjectArray", [](HostCall& c) {
        Env* e = env_of(c);
        Thread* self = e->vm;
        Runnable r(self);
        int32_t n = static_cast<int32_t>(c.x(1));
        RtClass* element = class_of(self, c.x(2));
        Object* init = decode_ref(self, c.x(3));
        if (!element || n < 0) return c.ret(0);
        RtClass* k = g_rt->class_linker().FindClass(self, "[" + element->descriptor, element->loader);
        if (!k) return c.ret(0);
        Array* a;
        {
            RootScope roots(self);
            if (init) roots.Add(init);
            a = g_rt->heap().AllocArray(self, k, n);
            if (a && init)
                for (int32_t i = 0; i < n; ++i) a->elements<Object*>()[i] = init;
        }
        c.ret(make_local(self, a));
    });
    def("GetObjectArrayElement", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Array* a = array_of(e->vm, c.x(1));
        int32_t i = static_cast<int32_t>(c.x(2));
        if (!a || i < 0 || i >= a->length) {
            g_rt->ThrowNew(e->vm, "Ljava/lang/ArrayIndexOutOfBoundsException;", std::to_string(i));
            return c.ret(0);
        }
        c.ret(make_local(e->vm, a->elements<Object*>()[i]));
    });
    def("SetObjectArrayElement", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Array* a = array_of(e->vm, c.x(1));
        int32_t i = static_cast<int32_t>(c.x(2));
        if (!a || i < 0 || i >= a->length) {
            g_rt->ThrowNew(e->vm, "Ljava/lang/ArrayIndexOutOfBoundsException;", std::to_string(i));
            return;
        }
        a->elements<Object*>()[i] = decode_ref(e->vm, c.x(3));
    });
    def("GetPrimitiveArrayCritical", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Array* a = array_of(e->vm, c.x(1));
        if (c.x(2)) {
            uint8_t no = 0;
            guest_write(c.x(2), &no, 1);
        }
        c.ret(a ? reinterpret_cast<uint64_t>(a->data()) : 0);
    });
    def("ReleasePrimitiveArrayCritical", [](HostCall&) {});

    def("RegisterNatives", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        RtClass* k = class_of(e->vm, c.x(1));
        int32_t n = static_cast<int32_t>(c.x(3));
        if (!k) return c.ret(static_cast<uint64_t>(JNI_ERR));
        for (int32_t i = 0; i < n; ++i) {
            uint64_t entry[3] = {};
            guest_read(c.x(2) + 24ull * static_cast<uint64_t>(i), entry, sizeof entry);
            std::string name = guest_str(entry[0]), sig = guest_str(entry[1]);
            RtMethod* m = k->FindDeclaredMethod(name, sig);
            if (!m || !m->IsNative()) {
                LOGW("jni", "RegisterNatives: %s.%s%s is not a native method", k->descriptor.c_str(), name.c_str(), sig.c_str());
                g_rt->ThrowNew(e->vm, "Ljava/lang/NoSuchMethodError;", k->descriptor + "." + name + sig);
                return c.ret(static_cast<uint64_t>(JNI_ERR));
            }
            m->jni_entry = reinterpret_cast<void*>(entry[2]);
            TRACE("jni", "RegisterNatives %s.%s%s -> %s", k->descriptor.c_str(), name.c_str(), sig.c_str(),
                  describe_address(entry[2]).c_str());
        }
        c.ret(0);
    });
    def("UnregisterNatives", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        if (RtClass* k = class_of(e->vm, c.x(1)))
            for (auto& m : k->methods)
                if (m.IsNative()) m.jni_entry = nullptr;
        c.ret(0);
    });

    def("MonitorEnter", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        c.ret(o && MonitorEnter(e->vm, o) ? 0 : static_cast<uint64_t>(JNI_ERR));
    });
    def("MonitorExit", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        c.ret(o && MonitorExit(e->vm, o) ? 0 : static_cast<uint64_t>(JNI_ERR));
    });
    def("GetJavaVM", [](HostCall& c) {
        uint64_t vm = reinterpret_cast<uint64_t>(&g_vm);
        c.ret(guest_write(c.x(1), &vm, 8) ? 0 : static_cast<uint64_t>(JNI_ERR));
    });

    def("NewWeakGlobalRef", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        c.ret(make_global(decode_ref(e->vm, c.x(1)), kWeak));
    });
    def("DeleteWeakGlobalRef", [](HostCall& c) { delete_global(c.x(1)); });
    def("GetObjectRefType", [](HostCall& c) {
        uint64_t ref = c.x(1);
        c.ret(ref ? (ref & 3) : 0);
    });

    // Direct buffers: java.nio.DirectByteBuffer(long address, int capacity).
    def("NewDirectByteBuffer", [](HostCall& c) {
        Env* e = env_of(c);
        Thread* self = e->vm;
        Runnable r(self);
        RtClass* k = g_rt->class_linker().FindClass(self, "Ljava/nio/DirectByteBuffer;", nullptr);
        if (!k || !g_rt->class_linker().EnsureInitialized(self, k)) return c.ret(0);
        RtMethod* ctor = k->FindDeclaredMethod("<init>", "(JI)V");
        if (!ctor) return c.ret(0);
        Object* created = nullptr;
        {
            RootScope roots(self);
            auto object = roots.Add(g_rt->heap().AllocObject(self, k));
            JValue args[2];
            args[0].j = static_cast<int64_t>(untag(c.x(1)));
            args[1].i = static_cast<int32_t>(c.x(2));
            JValue unused{};
            if (!object.Get() || !g_rt->Invoke(self, ctor, object.Get(), args, &unused)) return c.ret(0);
            created = object.Get();
        }
        c.ret(make_local(self, created));
    });
    def("GetDirectBufferAddress", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        RtField* f = o ? find_field(o->klass, "address", "J", false) : nullptr;
        c.ret(f ? static_cast<uint64_t>(FieldSlots(o)[f->slot].j) : 0);
    });
    def("GetDirectBufferCapacity", [](HostCall& c) {
        Env* e = env_of(c);
        Runnable r(e->vm);
        Object* o = decode_ref(e->vm, c.x(1));
        RtField* f = o ? find_field(o->klass, "capacity", "I", false) : nullptr;
        c.ret(f ? static_cast<uint64_t>(static_cast<int64_t>(FieldSlots(o)[f->slot].i)) : static_cast<uint64_t>(-1));
    });
}

void define_invoke_functions(std::map<std::string, Impl>& impls) {
    impls["DestroyJavaVM"] = [](HostCall& c) { c.ret(static_cast<uint64_t>(JNI_ERR)); };
    auto attach_fn = [](bool daemon) {
        return [daemon](HostCall& c) {
            const char* name = nullptr;
            std::string name_storage;
            if (c.x(2)) {  // JavaVMAttachArgs { jint version; const char* name; jobject group; }
                uint64_t name_ptr = 0;
                guest_read(c.x(2) + 8, &name_ptr, 8);
                if (name_ptr && guest_string(name_ptr, name_storage, 256)) name = name_storage.c_str();
            }
            Env* env = attach(daemon, name);
            uint64_t value = reinterpret_cast<uint64_t>(env);
            c.ret(guest_write(c.x(1), &value, 8) ? JNI_OK : static_cast<uint64_t>(JNI_ERR));
        };
    };
    impls["AttachCurrentThread"] = attach_fn(false);
    impls["AttachCurrentThreadAsDaemon"] = attach_fn(true);
    impls["DetachCurrentThread"] = [](HostCall& c) {
        GuestThread* guest = current_thread();
        if (!guest || !guest->jni_env) return c.ret(0);
        auto* env = static_cast<Env*>(guest->jni_env);
        Thread* vm = env->vm;
        // Threads the VM started (Java threads) stay attached until their run() ends.
        if (vm->top_frame()) return c.ret(static_cast<uint64_t>(JNI_ERR));
        guest->jni_env = nullptr;
        vm->TransitionToRunnable();
        vm->TruncateRoots(0);
        vm->peer = nullptr;
        vm->DetachCurrent();
        delete vm;
        delete env;
        TRACE("jni", "detached thread %d", guest->tid);
        c.ret(0);
    };
    impls["GetEnv"] = [](HostCall& c) {
        GuestThread* guest = current_thread();
        uint64_t value = 0;
        int result = JNI_EDETACHED;
        if (guest && guest->jni_env) {
            value = reinterpret_cast<uint64_t>(guest->jni_env);
            result = JNI_OK;
        } else if (guest && Thread::Current()) {
            value = reinterpret_cast<uint64_t>(attach(false, nullptr));
            result = JNI_OK;
        }
        guest_write(c.x(1), &value, 8);
        c.ret(static_cast<uint64_t>(static_cast<int64_t>(result)));
    };
}

void build_table(const std::vector<std::string>& slots, const std::map<std::string, Impl>& impls, const char* prefix,
                 std::vector<uint64_t>& table) {
    table.assign(slots.size(), 0);
    for (size_t i = 0; i < slots.size(); ++i) {
        const std::string& name = slots[i];
        if (name.rfind("reserved", 0) == 0) continue;
        auto it = impls.find(name);
        Impl fn;
        if (it != impls.end()) {
            fn = [name, inner = it->second](HostCall& c) {
                CurrentCall saved = t_call;
                t_call = {name.c_str(), c.lr()};
                inner(c);
                t_call = saved;
            };
        } else {
            fn = [name](HostCall& c) {
                LOGE("jni", "unimplemented JNI function %s called from %s", name.c_str(), describe_address(c.lr()).c_str());
                c.ret(0);
            };
        }
        table[i] = host_trampoline(std::string(prefix) + name, std::move(fn));
    }
}

// --- Java -> guest native calls ------------------------------------------------

std::string mangle(const std::string& s) {
    std::string out;
    for (unsigned char ch : s) {
        if (ch == '/') out += '_';
        else if (ch == '_') out += "_1";
        else if (ch == ';') out += "_2";
        else if (ch == '[') out += "_3";
        else if (std::isalnum(ch)) out += static_cast<char>(ch);
        else {
            char buf[8];
            std::snprintf(buf, sizeof buf, "_0%04x", ch);
            out += buf;
        }
    }
    return out;
}

uint64_t resolve_native(RtMethod* m) {
    std::string cls = m->declaring->descriptor.substr(1, m->declaring->descriptor.size() - 2);
    std::string short_name = "Java_" + mangle(cls) + "_" + mangle(m->name);
    if (uint64_t f = find_symbol(short_name)) return f;
    std::string params = m->signature.substr(1, m->signature.find(')') - 1);
    return find_symbol(short_name + "__" + mangle(params));
}

bool invoke_guest_native(Thread* self, RtMethod* m, Frame* frame, JValue* result) {
    uint64_t entry = reinterpret_cast<uint64_t>(m->jni_entry);
    if (!entry) {
        entry = resolve_native(m);
        if (!entry) {
            LOGW("jni", "no native implementation for %s", m->PrettyName().c_str());
            return false;
        }
        m->jni_entry = reinterpret_cast<void*>(entry);
    }
    GuestThread* guest = current_thread();
    if (!guest) fatal("native method %s called on a host thread", m->PrettyName().c_str());
    Env* env = static_cast<Env*>(guest->jni_env);
    if (!env) {
        env = new_env(self, guest);
        guest->jni_env = env;
    }
    size_t base = self->root_count();
    GuestArgs args;
    std::vector<uint64_t> spill;  // stack arguments, in order
    auto put_int = [&](uint64_t v) {
        if (args.ints.size() < 8) args.ints.push_back(v);
        else args.stack.push_back(v);
    };
    auto put_fp = [&](uint64_t bits) {
        if (args.fps.size() < 8) args.fps.push_back({bits, 0});
        else args.stack.push_back(bits);
    };
    put_int(reinterpret_cast<uint64_t>(env));
    uint32_t reg = 0;
    if (m->IsStatic()) {
        put_int(make_local(self, g_rt->ClassMirror(self, m->declaring)));
    } else {
        put_int(make_local(self, frame->refs[0]));
        reg = 1;
    }
    for (size_t i = 1; i < m->shorty.size(); ++i) {
        switch (m->shorty[i]) {
            case 'L': put_int(make_local(self, frame->refs[reg])); reg += 1; break;
            case 'J': put_int(static_cast<uint64_t>(frame->regs[reg]) | (static_cast<uint64_t>(frame->regs[reg + 1]) << 32)); reg += 2; break;
            case 'D': put_fp(static_cast<uint64_t>(frame->regs[reg]) | (static_cast<uint64_t>(frame->regs[reg + 1]) << 32)); reg += 2; break;
            case 'F': put_fp(frame->regs[reg]); reg += 1; break;
            case 'Z': put_int(frame->regs[reg] & 0xff ? 1 : 0); reg += 1; break;
            case 'B': put_int(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(frame->regs[reg])))); reg += 1; break;
            case 'C': put_int(static_cast<uint16_t>(frame->regs[reg])); reg += 1; break;
            case 'S': put_int(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(frame->regs[reg])))); reg += 1; break;
            default: put_int(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(frame->regs[reg])))); reg += 1; break;
        }
    }
    TRACE("jni", "-> %s", m->PrettyName().c_str());
    Vec128 fp{};
    self->TransitionFromRunnable(ThreadState::kNative);
    uint64_t r = call_guest_args(*guest, entry, args, &fp);
    self->TransitionToRunnable();
    TRACE("jni", "<- %s", m->PrettyName().c_str());
    switch (m->ReturnKind()) {
        case 'V': break;
        case 'L': result->l = decode_ref(self, r); break;
        case 'Z': result->i = (r & 0xff) != 0; break;
        case 'B': result->i = static_cast<int8_t>(r); break;
        case 'C': result->i = static_cast<uint16_t>(r); break;
        case 'S': result->i = static_cast<int16_t>(r); break;
        case 'I': result->i = static_cast<int32_t>(r); break;
        case 'J': result->j = static_cast<int64_t>(r); break;
        case 'F': {
            uint32_t bits = static_cast<uint32_t>(fp[0]);
            std::memcpy(&result->f, &bits, 4);
            break;
        }
        case 'D': std::memcpy(&result->d, &fp[0], 8); break;
    }
    // The returned object must survive the truncation: the interpreter roots
    // it through the thread's result_ref once we return.
    if (m->ReturnKind() == 'L') self->result_ref = result->l;
    self->TruncateRoots(base);
    if (env->frames.size() && env->frames.back() > base) env->frames.clear();
    return true;
}

// Runtime.nativeLoad / nativeLoadLibrary(String, ClassLoader): String error.
void Runtime_nativeLoad(Thread* self, Frame* a, JValue* r) {
    std::string name = g_rt->GetStringUtf8(ArgRef(a, 0));
    std::string path = name;
    if (path.find('/') == std::string::npos) path = "lib" + name + ".so";  // nativeLoadLibrary: a bare name
    std::string error = load_library(self, path);
    r->l = error.empty() ? nullptr : g_rt->NewStringUtf8(self, error);
}

}  // namespace

void configure(RuntimeOptions& options) {
    options.invoke_guest_native = invoke_guest_native;
    options.extra_roots = [](const std::function<void(Object*)>& visit) {
        std::lock_guard<std::mutex> held(g_globals_lock);
        for (Object* o : g_globals)
            if (o) visit(o);
    };
}

void install(Runtime* runtime) {
    g_rt = runtime;
    static const std::vector<std::string> env_slots = {
#define JNI_SLOT(name) #name,
#include "jni_table.inc"
#undef JNI_SLOT
    };
    static const std::vector<std::string> vm_slots = {
#define JNI_SLOT(name) #name,
#include "jni_invoke_table.inc"
#undef JNI_SLOT
    };
    define_functions();
    build_table(env_slots, g_impls, "jni.", g_env_table);
    std::map<std::string, Impl> invoke;
    define_invoke_functions(invoke);
    build_table(vm_slots, invoke, "jvm.", g_vm_table);
    g_vm.functions = reinterpret_cast<uint64_t>(g_vm_table.data());
    runtime->RegisterNative("Ljava/lang/Runtime;.nativeLoadLibrary(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/String;",
                            Runtime_nativeLoad);
    runtime->RegisterNative("Ljava/lang/Runtime;.nativeLoad(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/String;",
                            Runtime_nativeLoad);
}

void set_app_loader(ClassLoaderRt* loader) { g_app_loader = loader; }

uint64_t current_env() { return reinterpret_cast<uint64_t>(attach(false, nullptr)); }
uint64_t java_vm() { return reinterpret_cast<uint64_t>(&g_vm); }
uint64_t local_ref(Thread* self, Object* object) { return make_local(self, object); }
uint64_t global_ref(Object* object) { return make_global(object); }
Object* decode(Thread* self, uint64_t ref) { return decode_ref(self, ref); }

bool object_field(uint64_t ref, const char* name, const char* type, uint64_t* raw) {
    Env* e = attach(false, nullptr);
    Runnable r(e->vm);
    Object* o = decode_ref(e->vm, ref);
    RtField* f = o ? find_field(o->klass, name, type, false) : nullptr;
    if (!f) return false;
    *raw = FieldSlots(o)[f->slot].raw;
    return true;
}

uint64_t retain(uint64_t ref) {
    Env* e = attach(false, nullptr);
    Runnable r(e->vm);
    Object* o = decode_ref(e->vm, ref);
    return o ? make_global(o) : 0;
}

void release(uint64_t global) {
    if (global) delete_global(global);
}

std::string load_library(Thread* self, const std::string& path) {
    {
        std::lock_guard<std::mutex> held(g_libs_lock);
        for (auto& [p, h] : g_libraries)
            if (p == path) return {};
    }
    GuestThread* guest = current_thread();
    if (!guest) return "loadLibrary on a host thread";
    auto& api = guest_api();
    self->TransitionFromRunnable(ThreadState::kNative);
    uint64_t handle = call_guest(*guest, api.dlopen, {reinterpret_cast<uint64_t>(path.c_str()), 2 /*RTLD_NOW*/});
    std::string error;
    if (!handle) {
        uint64_t message = call_guest(*guest, api.dlerror, {});
        error = message ? guest_str(message) : "dlopen failed";
    }
    self->TransitionToRunnable();
    if (!handle) {
        LOGE("jni", "loadLibrary(%s): %s", path.c_str(), error.c_str());
        return error;
    }
    {
        std::lock_guard<std::mutex> held(g_libs_lock);
        g_libraries.push_back({path, handle});
    }
    LOGI("jni", "loaded %s", path.c_str());
    self->TransitionFromRunnable(ThreadState::kNative);
    uint64_t onload = call_guest(*guest, api.dlsym, {handle, reinterpret_cast<uint64_t>("JNI_OnLoad")});
    if (onload) {
        Env* env = static_cast<Env*>(guest->jni_env);
        if (!env) {
            env = new_env(self, guest);
            guest->jni_env = env;
        }
        uint64_t version = call_guest(*guest, onload, {reinterpret_cast<uint64_t>(&g_vm), 0});
        LOGI("jni", "%s: JNI_OnLoad returned %llx", path.c_str(), (unsigned long long)version);
    }
    self->TransitionToRunnable();
    return {};
}

uint64_t find_symbol(const std::string& name) {
    std::vector<uint64_t> handles;
    {
        std::lock_guard<std::mutex> held(g_libs_lock);
        for (auto& [p, h] : g_libraries) handles.push_back(h);
    }
    GuestThread* guest = current_thread();
    if (!guest) return 0;
    Thread* vm = Thread::Current();
    bool runnable = vm && vm->state() == ThreadState::kRunnable;
    if (runnable) vm->TransitionFromRunnable(ThreadState::kNative);
    uint64_t found = 0;
    for (uint64_t h : handles) {
        found = call_guest(*guest, guest_api().dlsym, {h, reinterpret_cast<uint64_t>(name.c_str())});
        if (found) break;
    }
    if (runnable) vm->TransitionToRunnable();
    return found;
}

}  // namespace jni
}  // namespace axrb
