// Host side of the in-tree Android framework (ADR 0004): the natives behind
// android.os.MessageQueue, SystemClock, Process, util.Log, content.res
// (assets, display metrics), graphics.Canvas/Paint and view.ViewRootImpl.
//
// The application's main thread is the Win32 UI thread. MessageQueue's native
// poll pumps the thread's Win32 messages while it waits, the window procedure
// only records input, and recorded input is delivered to Java after the wait
// (ViewRootImpl.dispatch*), the way Android's native Looper services its input
// channel. In headless mode there is no window: the surface is an off-screen
// buffer and a script supplies input when the queue is idle.
//
// Ownership: FrameworkHost owns the surfaces, the window and the queue table.
// Thread affinity: natives may run on any attached thread; window and surface
//   operations happen on the UI thread (the one that calls ActivityThread.main).
// Error propagation: natives leave Java exceptions pending; Create returns Status.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "arun/status.h"

namespace arun::pkg {
class ZipReader;
}
namespace arun::vm {
class Runtime;
class Thread;
struct Object;
}  // namespace arun::vm

namespace arun::fw {

struct HostConfig {
  // Window client area / headless surface size in device pixels.
  int width_px = 360;
  int height_px = 640;
  // Android densityDpi (160 = mdpi). Derived from the host DPI by the runner.
  int density_dpi = 160;
  bool headless = false;
  // Headless (or scripted window) input, one command per entry, run when the
  // main queue is idle: "tap X Y", "wait MS", "screenshot PATH", "back", "close".
  std::vector<std::string> script;
  // Session wall-clock limit; 0 = none. On expiry the window is asked to close.
  std::uint32_t timeout_ms = 0;
  // Guest process id reported by android.os.Process.myPid (spec 11: a guest id).
  int guest_pid = 4200;
  // The installed, unmodified APK (assets and resources are read from it).
  std::shared_ptr<pkg::ZipReader> apk;
  // The runtime's framework resource package (package id 0x01).
  std::shared_ptr<pkg::ZipReader> framework_res;
};

struct SessionReport {
  int frames_presented = 0;
  bool timed_out = false;
  std::string script_error;  // non-empty if a script command failed
  int script_commands_run = 0;
};

class FrameworkHost {
 public:
  static Result<std::unique_ptr<FrameworkHost>> Create(HostConfig config);
  ~FrameworkHost();
  FrameworkHost(const FrameworkHost&) = delete;
  FrameworkHost& operator=(const FrameworkHost&) = delete;

  // Registers the framework natives. Call once, before any framework class runs.
  void RegisterNatives(vm::Runtime* rt);
  // Marks the calling host thread as the UI thread (the Java main thread).
  void BindUiThread();
  // The application's class loader object (ActivityThread.nativeAppClassLoader).
  // Must stay reachable through the runtime (a ClassLoaderRt's java_object is).
  void SetAppClassLoader(vm::Object* loader);

  SessionReport report() const;

  struct Impl;

 private:
  explicit FrameworkHost(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace arun::fw
