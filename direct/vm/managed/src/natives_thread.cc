// java.lang.Thread natives: host threads for Java threads, sleep, interrupt.
#include <windows.h>

#include <chrono>
#include <format>

#include "arun/log.h"
#include "arun/vm/dex_file.h"
#include "arun/vm/monitor.h"
#include "arun/vm/natives.h"
#include "arun/vm/runtime.h"

namespace arun::vm {
namespace natives {
namespace {

Runtime* Rt(Thread* self) { return self->runtime(); }

Thread* VmThreadOf(Runtime* rt, Object* peer) {
  const std::int64_t v = FieldSlots(peer)[rt->wk.thread_vm_thread->slot].j;
  return reinterpret_cast<Thread*>(static_cast<std::uintptr_t>(v));
}

void Thread_currentThread(Thread* self, Frame*, JValue* r) { r->l = self->peer; }

void Thread_yield(Thread* self, Frame*, JValue*) {
  self->Safepoint();
  SwitchToThread();
}

void Thread_nativeSleep(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  const std::int64_t ms = ArgLong(a, 0);
  const std::int32_t ns = ArgInt(a, 2);
  if (self->interrupted.exchange(false)) {
    rt->ThrowNew(self, "Ljava/lang/InterruptedException;", "sleep interrupted");
    return;
  }
  bool interrupted = false;
  {
    ScopedBlocking blocking(self, ThreadState::kWaiting);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms) + std::chrono::nanoseconds(ns);
    std::unique_lock<std::mutex> l(self->sleep_lock);
    self->sleep_cv.wait_until(l, deadline, [&] { return self->interrupted.load(); });
    interrupted = self->interrupted.exchange(false);
  }
  if (interrupted) rt->ThrowNew(self, "Ljava/lang/InterruptedException;", "sleep interrupted");
}

void Thread_interrupted(Thread* self, Frame*, JValue* r) { r->i = self->interrupted.exchange(false) ? 1 : 0; }

void Thread_holdsLock(Thread* self, Frame* a, JValue* r) {
  Object* o = ArgRef(a, 0);
  if (o == nullptr) {
    Rt(self)->ThrowNew(self, "Ljava/lang/NullPointerException;", "object == null");
    return;
  }
  r->i = HoldsLock(self, o) ? 1 : 0;
}

// The target host thread is kept alive by holding the peer's monitor: a thread
// clears vmThread and exits only while holding that monitor (ThreadMain).
void Thread_interrupt(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  Object* peer = ArgRef(a, 0);
  if (!MonitorEnter(self, peer)) return;
  if (Thread* t = VmThreadOf(rt, peer)) {
    t->interrupted.store(true);
    if (Monitor* m = t->waiting_on.load()) {
      std::lock_guard<std::mutex> g(m->mutex);
      m->cv.notify_all();
    }
    std::lock_guard<std::mutex> g(t->sleep_lock);
    t->sleep_cv.notify_all();
  }
  MonitorExit(self, peer);
}

void Thread_isInterrupted(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  Object* peer = ArgRef(a, 0);
  r->i = 0;
  if (!MonitorEnter(self, peer)) return;
  if (Thread* t = VmThreadOf(rt, peer)) r->i = t->interrupted.load() ? 1 : 0;
  MonitorExit(self, peer);
}

void ThreadMain(Thread* t) {
  Runtime* rt = t->runtime();
  t->AttachCurrent();
  const std::u16string wide_name = Utf8ToUtf16(t->name());
  SetThreadDescription(GetCurrentThread(), reinterpret_cast<const wchar_t*>(wide_name.c_str()));
  {
    RootScope roots(t);
    auto peer = roots.Add(t->peer);
    if (RtMethod* run = rt->FindMethod(peer.Get()->klass, "run", "()V")) {
      JValue unused;
      if (!rt->Invoke(t, run, peer.Get(), {}, &unused)) rt->HandleUncaughtException(t);
    }
    // Termination: vmThread = 0 and notify joiners, under the peer's monitor.
    if (MonitorEnter(t, peer.Get())) {
      FieldSlots(peer.Get())[rt->wk.thread_vm_thread->slot].j = 0;
      MonitorNotify(t, peer.Get(), true);
      MonitorExit(t, peer.Get());
    }
    t->ClearException();
  }
  t->peer = nullptr;
  t->DetachCurrent();
  delete t;
}

DWORD WINAPI ThreadEntry(LPVOID arg) {
  ThreadMain(static_cast<Thread*>(arg));
  return 0;
}

void Thread_nativeStart(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  Object* peer = ArgRef(a, 0);
  const std::string name = rt->GetStringUtf8(FieldSlots(peer)[rt->wk.thread_name->slot].l);
  auto* t = new Thread(rt, name);
  t->peer = peer;
  t->daemon = FieldSlots(peer)[rt->wk.thread_daemon->slot].i != 0;
  // Registered (so `peer` is a GC root) and marked alive before start()
  // returns, as Thread.isAlive() requires.
  t->PreRegister();
  FieldSlots(peer)[rt->wk.thread_vm_thread->slot].j = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(t));
  if (rt->options().spawn_thread) {
    if (rt->options().spawn_thread([](void* arg) { ThreadMain(static_cast<Thread*>(arg)); }, t)) return;
    FieldSlots(peer)[rt->wk.thread_vm_thread->slot].j = 0;
    t->peer = nullptr;
    t->DetachCurrent();
    delete t;
    rt->ThrowNew(self, "Ljava/lang/OutOfMemoryError;", "pthread_create failed: could not create a guest thread");
    return;
  }
  HANDLE h = CreateThread(nullptr, kJavaThreadStackBytes, ThreadEntry, t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (h == nullptr) {
    const DWORD err = GetLastError();
    FieldSlots(peer)[rt->wk.thread_vm_thread->slot].j = 0;
    t->peer = nullptr;
    t->DetachCurrent();
    delete t;
    rt->ThrowNew(self, "Ljava/lang/OutOfMemoryError;",
                 std::format("pthread_create failed: could not create a host thread (Win32 error {})", err));
    return;
  }
  CloseHandle(h);
}

}  // namespace

void RegisterThread(Runtime* rt) {
  auto reg = [rt](const char* key, NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Ljava/lang/Thread;.currentThread()Ljava/lang/Thread;", Thread_currentThread);
  reg("Ljava/lang/Thread;.yield()V", Thread_yield);
  reg("Ljava/lang/Thread;.nativeSleep(JI)V", Thread_nativeSleep);
  reg("Ljava/lang/Thread;.interrupted()Z", Thread_interrupted);
  reg("Ljava/lang/Thread;.holdsLock(Ljava/lang/Object;)Z", Thread_holdsLock);
  reg("Ljava/lang/Thread;.interrupt()V", Thread_interrupt);
  reg("Ljava/lang/Thread;.isInterrupted()Z", Thread_isInterrupted);
  reg("Ljava/lang/Thread;.nativeStart()V", Thread_nativeStart);
}

}  // namespace natives
}  // namespace arun::vm
