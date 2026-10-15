#include "arun/vm/runtime.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <format>

#include "arun/log.h"
#include "arun/vm/interpreter.h"
#include "arun/vm/natives.h"

namespace arun::vm {
namespace {

std::atomic<Runtime*> g_runtime{nullptr};

std::string Dotted(std::string_view descriptor) {
  std::string out;
  int dims = 0;
  while (!descriptor.empty() && descriptor[0] == '[') {
    ++dims;
    descriptor.remove_prefix(1);
  }
  if (descriptor.size() >= 2 && descriptor[0] == 'L') {
    out.assign(descriptor.substr(1, descriptor.size() - 2));
    std::replace(out.begin(), out.end(), '/', '.');
  } else {
    out.assign(descriptor);
  }
  for (int i = 0; i < dims; ++i) out += "[]";
  return out;
}

}  // namespace

Runtime::Runtime(RuntimeOptions options) : options_(std::move(options)) {
  // Identity hashes are seeded from the host CSPRNG (spec 11: no predictable PRNG).
  std::uint32_t seed = 0;
  BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  hash_seed_.store(seed | 1);
}

Runtime::~Runtime() {
  if (main_thread_ != nullptr && Thread::Current() == main_thread_.get()) {
    main_thread_->DetachCurrent();
  }
  class_linker_.reset();
  heap_.reset();
  g_runtime.store(nullptr);
}

Runtime* Runtime::Current() { return g_runtime.load(std::memory_order_acquire); }

Result<std::unique_ptr<Runtime>> Runtime::Create(RuntimeOptions options) {
  Runtime* expected = nullptr;
  auto rt = std::unique_ptr<Runtime>(new Runtime(std::move(options)));
  if (!g_runtime.compare_exchange_strong(expected, rt.get())) {
    return Err(ReasonCode::kAlreadyExists, "a managed runtime already exists in this process");
  }
  Status s = rt->Init();
  if (!s.ok()) return s;
  return rt;
}

Status Runtime::Init() {
  heap_ = std::make_unique<Heap>(this);
  class_linker_ = std::make_unique<ClassLinker>(this);
  if (options_.gc_stress_interval != 0) heap_->SetStressInterval(options_.gc_stress_interval);

  for (BootDex& b : options_.boot_class_path) {
    auto dex = DexFile::Open(std::move(b.bytes), b.name);
    if (!dex.ok()) return dex.status().WithContext("boot class path");
    class_linker_->AddBootDex(std::move(*dex));
  }
  options_.boot_class_path.clear();

  // Defaults an Android process reports. java.vm.version "2.x" is the protocol
  // by which libraries (androidx MultiDex) ask whether the VM loads every
  // classesN.dex itself -- which this runtime does.
  properties_ = {
      {"file.encoding", "UTF-8"},
      {"file.separator", "/"},
      {"java.class.path", "."},
      {"java.io.tmpdir", "/tmp"},
      {"java.specification.version", "0.9"},
      {"java.vm.name", "Dalvik"},
      {"java.vm.version", "2.1.0"},
      {"java.vendor", "The Android Project"},
      {"java.version", "0"},
      {"line.separator", "\n"},
      {"os.arch", "x86_64"},
      {"os.name", "Linux"},
      {"path.separator", ":"},
      {"user.dir", "/"},
      {"user.home", ""},
      {"user.language", "en"},
      {"user.region", "US"},
  };
  for (auto& [k, v] : options_.properties) properties_[k] = v;

  RegisterCoreNatives(this);

  main_thread_ = std::make_unique<Thread>(this, "main");
  main_thread_->AttachCurrent();
  ARUN_RETURN_IF_ERROR(BindWellKnown());

  Thread* self = main_thread_.get();
  for (RtClass* c : {wk.object, wk.string, wk.class_, wk.throwable, wk.thread}) {
    if (!class_linker_->EnsureInitialized(self, c)) {
      std::string why = DescribeException(self, self->exception());
      self->ClearException();
      return Err(ReasonCode::kVmLinkageError, "initialising core class " + c->descriptor + ": " + why);
    }
  }
  if (!AttachPeer(self, "main", false)) {
    std::string why = DescribeException(self, self->exception());
    self->ClearException();
    return Err(ReasonCode::kVmLinkageError, "creating the main java.lang.Thread: " + why);
  }
  return Status::Ok();
}

Status Runtime::BindWellKnown() {
  Thread* self = main_thread_.get();
  auto cls = [&](const char* d, RtClass** out) -> Status {
    *out = class_linker_->FindClassNoThrow(self, d, nullptr);
    if (*out == nullptr) {
      return Err(ReasonCode::kVmClassNotFound,
                 std::format("boot class path does not define {}", d));
    }
    return Status::Ok();
  };
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/Object;", &wk.object));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/String;", &wk.string));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/Class;", &wk.class_));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/Throwable;", &wk.throwable));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/Thread;", &wk.thread));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/ClassLoader;", &wk.class_loader));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/StackTraceElement;", &wk.stack_trace_element));
  ARUN_RETURN_IF_ERROR(cls("Ljava/lang/Cloneable;", &wk.cloneable));
  ARUN_RETURN_IF_ERROR(cls("Ljava/io/Serializable;", &wk.serializable));
  ARUN_RETURN_IF_ERROR(cls("[C", &wk.char_array));
  ARUN_RETURN_IF_ERROR(cls("[B", &wk.byte_array));
  ARUN_RETURN_IF_ERROR(cls("[Ljava/lang/Object;", &wk.object_array));
  ARUN_RETURN_IF_ERROR(cls("[Ljava/lang/String;", &wk.string_array));

  auto field = [&](RtClass* c, const char* name, const char* type, RtField** out) -> Status {
    *out = c->FindDeclaredField(name, type);
    if (*out == nullptr) {
      return Err(ReasonCode::kVmLinkageError,
                 std::format("{} lacks the field {} {} this runtime requires", c->descriptor, type, name));
    }
    return Status::Ok();
  };
  ARUN_RETURN_IF_ERROR(field(wk.string, "value", "[C", &wk.string_value));
  ARUN_RETURN_IF_ERROR(field(wk.class_, "vmClass", "J", &wk.class_vm_class));
  ARUN_RETURN_IF_ERROR(field(wk.throwable, "backtrace", "Ljava/lang/Object;", &wk.throwable_backtrace));
  ARUN_RETURN_IF_ERROR(field(wk.thread, "vmThread", "J", &wk.thread_vm_thread));
  ARUN_RETURN_IF_ERROR(field(wk.thread, "name", "Ljava/lang/String;", &wk.thread_name));
  ARUN_RETURN_IF_ERROR(field(wk.thread, "daemon", "Z", &wk.thread_daemon));
  ARUN_RETURN_IF_ERROR(field(wk.class_loader, "vmLoader", "J", &wk.class_loader_vm_loader));
  return Status::Ok();
}

bool Runtime::AttachPeer(Thread* self, std::string_view name, bool daemon) {
  // java.lang.Thread(String name, long vmThread, boolean daemon): the runtime's
  // attach constructor, so the Java field initialisers run as normal.
  RtMethod* ctor = wk.thread->FindDeclaredMethod("<init>", "(Ljava/lang/String;JZ)V");
  if (ctor == nullptr) {
    ThrowNew(self, "Ljava/lang/InternalError;", "java.lang.Thread lacks the attach constructor");
    return false;
  }
  RootScope roots(self);
  auto peer = roots.Add(heap_->AllocObject(self, wk.thread));
  if (peer.Get() == nullptr) return false;
  auto jname = roots.Add(NewStringUtf8(self, name));
  if (jname.Get() == nullptr) return false;
  JValue args[3];
  args[0].l = jname.Get();
  args[1].j = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(self));
  args[2].raw = 0;
  args[2].i = daemon ? 1 : 0;
  JValue unused;
  if (!Invoke(self, ctor, peer.Get(), args, &unused)) return false;
  self->peer = peer.Get();
  self->daemon = daemon;
  return true;
}

void Runtime::Log(int priority, std::string_view tag, std::string_view message) {
  if (options_.log_sink) {
    options_.log_sink(priority, tag, message);
    return;
  }
  const log::Level level = priority >= 6 ? log::Level::kError : priority == 5 ? log::Level::kWarn
                                                                              : log::Level::kInfo;
  log::Write(log::Category::kRuntime, level, "{}: {}", tag, message);
}

// --- threads ----------------------------------------------------------------

void Runtime::RegisterThread(Thread* t) {
  std::unique_lock<std::mutex> l(thread_lock_);
  thread_cv_.wait(l, [&] { return !gc_in_progress_; });
  threads_.push_back(t);
}

void Runtime::UnregisterThread(Thread* t) {
  std::unique_lock<std::mutex> l(thread_lock_);
  thread_cv_.wait(l, [&] { return !gc_in_progress_; });
  threads_.erase(std::remove(threads_.begin(), threads_.end(), t), threads_.end());
  thread_cv_.notify_all();
}

void Runtime::SuspendAll(Thread* self) {
  self->TransitionFromRunnable(ThreadState::kNative);
  std::unique_lock<std::mutex> l(thread_lock_);
  thread_cv_.wait(l, [&] { return !gc_in_progress_; });
  gc_in_progress_ = true;
  suspend_requested_.store(true, std::memory_order_release);
  thread_cv_.wait(l, [&] { return runnable_count_ == 0; });
}

void Runtime::ResumeAll(Thread* self) {
  {
    std::lock_guard<std::mutex> g(thread_lock_);
    suspend_requested_.store(false, std::memory_order_release);
    gc_in_progress_ = false;
  }
  thread_cv_.notify_all();
  self->TransitionToRunnable();
}

void Runtime::WaitForNonDaemonThreads(Thread* self) {
  ScopedBlocking blocking(self, ThreadState::kWaiting);
  std::unique_lock<std::mutex> l(thread_lock_);
  thread_cv_.wait(l, [&] {
    for (Thread* t : threads_) {
      if (t != self && !t->daemon && t->state() != ThreadState::kTerminated) return false;
    }
    return true;
  });
}

std::uint32_t Runtime::NextIdentityHash() {
  // xorshift over a CSPRNG seed; never 0 (0 means "unassigned").
  std::uint32_t x = hash_seed_.load(std::memory_order_relaxed);
  std::uint32_t next;
  do {
    next = x;
    next ^= next << 13;
    next ^= next >> 17;
    next ^= next << 5;
  } while (!hash_seed_.compare_exchange_weak(x, next, std::memory_order_relaxed));
  return next != 0 ? next & 0x7fffffffu : 1;
}

// --- process-level services -------------------------------------------------

std::optional<std::string> Runtime::GetProperty(std::string_view key) {
  std::lock_guard<std::mutex> g(property_lock_);
  auto it = properties_.find(std::string(key));
  if (it == properties_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::string> Runtime::SetProperty(std::string key, std::string value) {
  std::lock_guard<std::mutex> g(property_lock_);
  auto [it, inserted] = properties_.try_emplace(std::move(key), value);
  if (inserted) return std::nullopt;
  std::string old = std::move(it->second);
  it->second = std::move(value);
  return old;
}

void Runtime::Exit(int status) {
  ARUN_LOG_INFO(log::Category::kRuntime, "application called exit({})", status);
  if (options_.exit_hook) {
    options_.exit_hook(status);
    return;
  }
  ExitProcess(static_cast<UINT>(status));
}

void Runtime::HandleUncaughtException(Thread* self) {
  RootScope roots(self);
  auto ex = roots.Add(self->exception());
  if (ex.Get() == nullptr) return;
  self->ClearException();
  bool handled = false;
  if (self->peer != nullptr) {
    if (RtMethod* dispatch = wk.thread->FindDeclaredMethod("dispatchUncaughtException",
                                                           "(Ljava/lang/Throwable;)Z")) {
      JValue arg, r;
      arg.l = ex.Get();
      if (Invoke(self, dispatch, self->peer, std::span<const JValue>(&arg, 1), &r)) {
        handled = r.i != 0;
      } else {
        // The handler itself threw: Android reports the original and dies.
        ARUN_LOG_ERROR(log::Category::kRuntime, "uncaught-exception handler threw: {}",
                       DescribeException(self, self->exception()));
        self->ClearException();
      }
    }
  }
  if (handled) return;
  const std::string text = std::format("FATAL EXCEPTION: {}\n{}", self->name(), DescribeException(self, ex.Get()));
  Log(6, "AndroidRuntime", text);
  if (options_.fatal_hook) {
    options_.fatal_hook(text);
    return;
  }
  ExitProcess(10);
}

std::int64_t Runtime::MethodIndex(RtMethod* method) {
  std::lock_guard<std::mutex> g(method_index_lock_);
  auto [it, inserted] = method_indices_.try_emplace(method, static_cast<std::int64_t>(methods_by_index_.size()));
  if (inserted) methods_by_index_.push_back(method);
  return it->second;
}

RtMethod* Runtime::MethodAt(std::int64_t index) {
  std::lock_guard<std::mutex> g(method_index_lock_);
  if (index < 0 || static_cast<std::uint64_t>(index) >= methods_by_index_.size()) return nullptr;
  return methods_by_index_[static_cast<std::size_t>(index)];
}

// --- strings ----------------------------------------------------------------

Object* Runtime::NewString(Thread* self, std::u16string_view chars) {
  RootScope roots(self);
  Array* value = heap_->AllocArray(self, wk.char_array, static_cast<std::int32_t>(chars.size()));
  if (value == nullptr) return nullptr;
  auto hv = roots.Add(value);
  if (!chars.empty()) std::memcpy(value->data(), chars.data(), chars.size() * sizeof(char16_t));
  Object* s = heap_->AllocObject(self, wk.string);
  if (s == nullptr) return nullptr;
  FieldSlots(s)[wk.string_value->slot].l = hv.Get();
  return s;
}

Object* Runtime::NewStringUtf8(Thread* self, std::string_view utf8) {
  return NewString(self, Utf8ToUtf16(utf8));
}

Object* Runtime::InternString(Thread* self, std::u16string_view chars) {
  {
    std::lock_guard<std::mutex> g(intern_lock_);
    auto it = interned_.find(std::u16string(chars));
    if (it != interned_.end()) return it->second;
  }
  // Allocate without the lock held: an allocation can stop the world.
  Object* s = NewString(self, chars);
  if (s == nullptr) return nullptr;
  std::lock_guard<std::mutex> g(intern_lock_);
  auto [it, inserted] = interned_.emplace(std::u16string(chars), s);
  return it->second;
}

std::u16string Runtime::GetString(Object* str) {
  if (str == nullptr) return {};
  auto* value = static_cast<Array*>(FieldSlots(str)[wk.string_value->slot].l);
  if (value == nullptr) return {};
  return std::u16string(value->elements<char16_t>(), static_cast<std::size_t>(value->length));
}

std::string Runtime::GetStringUtf8(Object* str) { return Utf16ToUtf8(GetString(str)); }

// --- class mirrors ------------------------------------------------------------

Object* Runtime::ClassMirror(Thread* self, RtClass* klass) {
  std::atomic_ref<Object*> slot(klass->mirror);
  if (Object* m = slot.load(std::memory_order_acquire)) return m;
  Object* m = heap_->AllocObject(self, wk.class_);
  if (m == nullptr) return nullptr;
  FieldSlots(m)[wk.class_vm_class->slot].j = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(klass));
  Object* expected = nullptr;
  if (!slot.compare_exchange_strong(expected, m, std::memory_order_acq_rel)) return expected;
  return m;
}

RtClass* Runtime::ClassFromMirror(Object* mirror) {
  if (mirror == nullptr) return nullptr;
  return reinterpret_cast<RtClass*>(
      static_cast<std::uintptr_t>(FieldSlots(mirror)[wk.class_vm_class->slot].j));
}

// --- exceptions ---------------------------------------------------------------

void Runtime::ThrowNew(Thread* self, std::string_view descriptor, std::string_view message) {
  self->ClearException();
  const bool overflow = descriptor == "Ljava/lang/StackOverflowError;";
  if (overflow) self->set_in_overflow(true);
  RtClass* c = class_linker_->FindClassNoThrow(self, descriptor, nullptr);
  bool ok = false;
  if (c != nullptr && class_linker_->EnsureInitialized(self, c)) {
    RootScope roots(self);
    Object* ex = heap_->AllocObject(self, c);
    if (ex != nullptr) {
      auto hex = roots.Add(ex);
      Object* msg = message.empty() && descriptor != "Ljava/lang/InterruptedException;"
                        ? nullptr : NewStringUtf8(self, message);
      auto hmsg = roots.Add(msg);
      RtMethod* ctor = c->FindDeclaredMethod("<init>", "(Ljava/lang/String;)V");
      JValue unused;
      if (ctor != nullptr) {
        JValue arg;
        arg.l = hmsg.Get();
        ok = Invoke(self, ctor, hex.Get(), std::span<const JValue>(&arg, 1), &unused);
      } else if (RtMethod* noarg = c->FindDeclaredMethod("<init>", "()V")) {
        ok = Invoke(self, noarg, hex.Get(), {}, &unused);
      }
      if (ok) self->SetException(hex.Get());
    }
  }
  if (overflow) self->set_in_overflow(false);
  if (!ok && !self->IsExceptionPending()) {
    // The requested class is missing or failed; never leave nothing pending.
    ARUN_LOG_ERROR(log::Category::kRuntime, "could not construct {} ({}); substituting InternalError",
                   descriptor, message);
    if (descriptor != "Ljava/lang/InternalError;") {
      ThrowNew(self, "Ljava/lang/InternalError;", std::format("{}: {}", Dotted(descriptor), message));
    }
  }
}

std::string Runtime::DescribeException(Thread* self, Object* throwable) {
  if (throwable == nullptr) return "(no exception)";
  std::string out;
  RtField* msg_field = wk.throwable->FindDeclaredField("detailMessage", "Ljava/lang/String;");
  RtField* cause_field = wk.throwable->FindDeclaredField("cause", "Ljava/lang/Throwable;");
  Object* t = throwable;
  for (int depth = 0; t != nullptr && depth < 8; ++depth) {
    if (depth > 0) out += "\nCaused by: ";
    out += Dotted(t->klass->descriptor);
    if (msg_field != nullptr) {
      Object* m = FieldSlots(t)[msg_field->slot].l;
      if (m != nullptr) {
        out += ": ";
        out += GetStringUtf8(m);
      }
    }
    auto* bt = static_cast<Array*>(FieldSlots(t)[wk.throwable_backtrace->slot].l);
    if (bt != nullptr && bt->klass->descriptor == "[J") {
      const std::int64_t* e = bt->elements<std::int64_t>();
      for (std::int32_t i = 0; i + 1 < bt->length && i < 64; i += 2) {
        RtMethod* m = MethodAt(e[i]);
        if (m == nullptr) continue;
        out += "\n\tat ";
        out += Dotted(m->declaring->descriptor);
        out += '.';
        out += m->name;
        out += FrameLocation(m, e[i + 1]);
      }
    }
    Object* cause = cause_field != nullptr ? FieldSlots(t)[cause_field->slot].l : nullptr;
    if (cause == t) break;
    t = cause;
  }
  return out;
}

std::string Runtime::FrameLocation(RtMethod* m, std::int64_t dex_pc) {
  if (m->IsNative() || dex_pc < 0) return "(Native Method)";
  std::string file;
  std::optional<std::uint32_t> line;
  if (m->declaring->dex_cache != nullptr) {
    const DexFile& dex = *m->declaring->dex_cache->dex;
    file = std::string(dex.SourceFile(dex.GetClassDef(m->declaring->class_def_idx)));
    if (m->prepare_state.load() == PrepareState::kPrepared) {
      line = dex.LineNumber(m->code, static_cast<std::uint32_t>(dex_pc));
    }
  }
  if (file.empty()) file = "Unknown Source";
  return line ? std::format("({}:{})", file, *line) : std::format("({})", file);
}

std::string Runtime::DumpThreads() {
  std::string out;
  std::lock_guard<std::mutex> g(thread_lock_);
  for (Thread* t : threads_) {
    out += std::format("\"{}\" state={}\n", t->name(), static_cast<int>(t->state()));
    int depth = 0;
    for (Frame* f = t->top_frame(); f != nullptr && depth < 40; f = f->prev, ++depth) {
      if (f->method == nullptr) continue;
      out += "    at " + f->method->PrettyName() + " " + FrameLocation(f->method, f->dex_pc) + "\n";
    }
  }
  return out;
}

// --- invocation -----------------------------------------------------------------

RtMethod* Runtime::FindMethod(RtClass* klass, std::string_view name, std::string_view signature) {
  for (RtClass* c = klass; c != nullptr; c = c->super) {
    if (RtMethod* m = c->FindDeclaredMethod(name, signature)) return m;
  }
  for (RtClass* i : klass->all_interfaces) {
    if (RtMethod* m = i->FindDeclaredMethod(name, signature)) return m;
  }
  return nullptr;
}

bool Runtime::Invoke(Thread* self, RtMethod* method, Object* receiver, std::span<const JValue> args,
                     JValue* result, bool dispatch) {
  result->raw = 0;
  RtMethod* target = method;
  if (!method->IsStatic()) {
    if (receiver == nullptr) {
      ThrowNew(self, "Ljava/lang/NullPointerException;",
               "Attempt to invoke " + method->PrettyName() + " on a null object reference");
      return false;
    }
    if (dispatch && !method->IsDirect()) {
      if (method->declaring->IsInterface()) {
        target = class_linker_->FindInterfaceTarget(self, receiver->klass, method);
        if (target == nullptr) return false;
      } else if (method->vtable_index >= 0 && receiver->klass->IsSubclassOf(method->declaring)) {
        target = receiver->klass->vtable[static_cast<std::size_t>(method->vtable_index)];
      }
    }
  } else if (!class_linker_->EnsureInitialized(self, method->declaring)) {
    return false;
  }
  if (!target->IsNative() && !target->IsAbstract()) {
    Status s = PrepareMethod(target);
    if (!s.ok()) {
      ThrowNew(self, "Ljava/lang/VerifyError;", s.message());
      return false;
    }
  }
  // Count the argument words the signature needs and check the caller agrees.
  const std::string_view shorty = target->shorty;
  if (args.size() != shorty.size() - 1) {
    ThrowNew(self, "Ljava/lang/IllegalArgumentException;",
             std::format("{} takes {} arguments, {} given", target->PrettyName(), shorty.size() - 1, args.size()));
    return false;
  }
  const std::uint32_t size = FrameSizeFor(target);
  Frame* frame = self->PushFrame(target, size);
  if (frame == nullptr) {
    ThrowNew(self, "Ljava/lang/StackOverflowError;", "stack size exceeded");
    return false;
  }
  std::uint32_t reg = size - target->arg_words;
  if (!target->IsStatic()) {
    frame->refs[reg] = receiver;
    frame->regs[reg] = 1;
    ++reg;
  }
  for (std::size_t i = 0; i < args.size(); ++i) {
    const char k = shorty[i + 1];
    if (k == 'J' || k == 'D') {
      const auto u = static_cast<std::uint64_t>(args[i].j);
      frame->regs[reg] = static_cast<std::uint32_t>(u);
      frame->regs[reg + 1] = static_cast<std::uint32_t>(u >> 32);
      reg += 2;
    } else if (k == 'L') {
      frame->refs[reg] = args[i].l;
      frame->regs[reg] = args[i].l != nullptr ? 1u : 0u;
      reg += 1;
    } else {
      frame->regs[reg] = static_cast<std::uint32_t>(args[i].i);
      reg += 1;
    }
  }
  InvokeFrame(self, target, frame, result);
  self->PopFrame(frame);
  return !self->IsExceptionPending();
}

// --- natives --------------------------------------------------------------------

void Runtime::RegisterNative(std::string key, NativeFn fn) { natives_[std::move(key)] = fn; }

NativeFn Runtime::FindNative(const RtMethod* method) const {
  auto it = natives_.find(method->declaring->descriptor + "." + method->name + method->signature);
  return it == natives_.end() ? nullptr : it->second;
}

}  // namespace arun::vm
