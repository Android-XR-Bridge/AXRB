#include "arun/framework_host.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <deque>
#include <format>
#include <mutex>
#include <set>
#include <unordered_map>

#include "arun/vm/runtime.h"
#include "arun/zip_reader.h"
#include "graphics_natives.h"
#include "render.h"
#include "resources_host.h"

namespace arun::fw {

using vm::Array;
using vm::ArgFloat;
using vm::ArgInt;
using vm::ArgLong;
using vm::ArgRef;
using vm::Frame;
using vm::JValue;
using vm::Object;
using vm::RootScope;
using vm::Runtime;
using vm::RtClass;
using vm::RtMethod;
using vm::ScopedBlocking;
using vm::Thread;

namespace {

constexpr std::uint64_t kMaxAssetBytes = 256ull << 20;
// The script runs only when the main queue has nothing due this soon.
constexpr int kIdleThresholdMs = 250;
constexpr std::uint32_t kWindowBackground = 0xFFFAFAFA;

// MotionEvent / KeyEvent actions.
constexpr int kActionDown = 0;
constexpr int kActionUp = 1;
constexpr int kActionMove = 2;
constexpr int kActionCancel = 3;

// android.view.KeyEvent key codes the host produces.
constexpr int kKeycodeUnknown = 0;
constexpr int kKeycodeBack = 4;
constexpr int kKeycodeDpadUp = 19;
constexpr int kKeycodeDpadDown = 20;
constexpr int kKeycodeDpadLeft = 21;
constexpr int kKeycodeDpadRight = 22;
constexpr int kKeycodeTab = 61;
constexpr int kKeycodeSpace = 62;
constexpr int kKeycodeEnter = 66;
constexpr int kKeycodeDel = 67;
constexpr int kKeycodeEscape = 111;
constexpr int kKeycodeForwardDel = 112;
constexpr int kKeycodeMoveHome = 122;
constexpr int kKeycodeMoveEnd = 123;
constexpr int kKeycodePageUp = 92;
constexpr int kKeycodePageDown = 93;
constexpr int kMetaShiftOn = 0x1;
constexpr int kMetaCtrlOn = 0x1000;

std::int64_t UptimeMillis() {
  ULONGLONG t = 0;  // 100 ns units, excluding time asleep: uptimeMillis semantics
  QueryUnbiasedInterruptTime(&t);
  return static_cast<std::int64_t>(t / 10000);
}

std::wstring Widen(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

struct HostEvent {
  enum Kind { kPointer, kResize, kClose, kBack, kKey, kScroll } kind;
  int action = 0;
  float x = 0, y = 0;
  std::int64_t time = 0;
  int width = 0, height = 0;
  int key_code = 0;
  int unicode = 0;
  int meta = 0;
  float scroll = 0;  // kScroll: lines, positive = away from the user (up)
};

struct NativeQueue {
  HANDLE wake = nullptr;  // auto-reset
  bool ui = false;
};

const wchar_t kWindowClass[] = L"AndroitainerAppWindow";

// Key code for a non-character virtual key, or -1.
int KeyCodeForVk(WPARAM vk) {
  switch (vk) {
    case VK_BACK: return kKeycodeDel;
    case VK_RETURN: return kKeycodeEnter;
    case VK_TAB: return kKeycodeTab;
    case VK_LEFT: return kKeycodeDpadLeft;
    case VK_RIGHT: return kKeycodeDpadRight;
    case VK_UP: return kKeycodeDpadUp;
    case VK_DOWN: return kKeycodeDpadDown;
    case VK_DELETE: return kKeycodeForwardDel;
    case VK_HOME: return kKeycodeMoveHome;
    case VK_END: return kKeycodeMoveEnd;
    case VK_PRIOR: return kKeycodePageUp;
    case VK_NEXT: return kKeycodePageDown;
    default: return -1;
  }
}

int CurrentMeta() {
  int meta = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) meta |= kMetaShiftOn;
  if (GetKeyState(VK_CONTROL) & 0x8000) meta |= kMetaCtrlOn;
  return meta;
}

}  // namespace

struct FrameworkHost::Impl {
  HostConfig config;
  std::unique_ptr<ResourcesHost> resources;
  Graphics* gfx = nullptr;
  Runtime* runtime = nullptr;
  DWORD ui_thread = 0;
  Object* app_loader = nullptr;

  // Main-queue handles and their pids/tids.
  std::mutex queue_lock;
  std::unordered_map<std::int64_t, std::unique_ptr<NativeQueue>> queues;
  std::int64_t next_queue = 1;
  std::mutex tid_lock;
  std::unordered_map<DWORD, int> guest_tids;
  int next_tid = 0;

  // Window and surface (UI thread only).
  NativeBitmap* surface = nullptr;  // pinned: owned by the host
  bool surface_open = false;
  std::vector<std::uint32_t> present_pixels;
  int present_width = 0, present_height = 0;
  HWND hwnd = nullptr;
  bool pointer_down = false;

  // Input recorded by the window procedure or the script (UI thread only).
  std::deque<HostEvent> events;

  // Script state (UI thread only).
  std::size_t script_pos = 0;
  std::int64_t script_wait_until = 0;
  std::int64_t session_deadline = 0;
  bool close_requested_by_timeout = false;

  std::atomic<int> frames{0};
  mutable std::mutex report_lock;
  SessionReport report;

  // Cached callbacks into android.view.ViewRootImpl.
  RtMethod* dispatch_pointer = nullptr;
  RtMethod* dispatch_resize = nullptr;
  RtMethod* dispatch_close = nullptr;
  RtMethod* dispatch_back = nullptr;
  RtMethod* dispatch_key = nullptr;
  RtMethod* dispatch_scroll = nullptr;

  bool OnUiThread() const { return GetCurrentThreadId() == ui_thread; }

  NativeQueue* Queue(std::int64_t id) {
    std::lock_guard<std::mutex> hold(queue_lock);
    auto it = queues.find(id);
    return it == queues.end() ? nullptr : it->second.get();
  }

  void SetScriptError(std::string error) {
    std::lock_guard<std::mutex> hold(report_lock);
    if (report.script_error.empty()) report.script_error = std::move(error);
  }

  // --- window -------------------------------------------------------------------

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  bool OpenWindow(const std::wstring& title, std::string* error);
  void Present();

  void PushPointer(int action, LPARAM lp) {
    HostEvent e{HostEvent::kPointer};
    e.action = action;
    e.x = static_cast<float>(static_cast<short>(LOWORD(lp)));
    e.y = static_cast<float>(static_cast<short>(HIWORD(lp)));
    e.time = UptimeMillis();
    events.push_back(e);
  }

  void PushKey(int key_code, int unicode, int meta) {
    const std::int64_t now = UptimeMillis();
    HostEvent down{HostEvent::kKey};
    down.action = kActionDown;
    down.key_code = key_code;
    down.unicode = unicode;
    down.meta = meta;
    down.time = now;
    HostEvent up = down;
    up.action = kActionUp;
    events.push_back(down);
    events.push_back(up);
  }

  // --- script ---------------------------------------------------------------------

  // Runs script commands while the queue is idle. Returns after queuing input
  // or when the script must wait.
  void RunScript();
  bool ScriptPending() const { return script_pos < config.script.size(); }
};

// --- window ------------------------------------------------------------------------

LRESULT CALLBACK FrameworkHost::Impl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      const int w = self->present_width, h = self->present_height;
      if (w > 0 && h > 0 && self->present_pixels.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, static_cast<DWORD>(w), static_cast<DWORD>(h), 0, 0, 0, static_cast<UINT>(h),
                          self->present_pixels.data(), &bmi, DIB_RGB_COLORS);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE: {
      if (wp == SIZE_MINIMIZED || self->surface == nullptr) return 0;
      const int w = LOWORD(lp), h = HIWORD(lp);
      if (w > 0 && h > 0 && (w != self->gfx->Width(self->surface) || h != self->gfx->Height(self->surface))) {
        std::string error;
        if (self->gfx->Resize(self->surface, w, h, kWindowBackground, &error)) {
          HostEvent e{HostEvent::kResize};
          e.width = w;
          e.height = h;
          self->events.push_back(e);
        }
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      SetCapture(hwnd);
      SetFocus(hwnd);
      self->pointer_down = true;
      self->PushPointer(kActionDown, lp);
      return 0;
    case WM_MOUSEMOVE:
      if (self->pointer_down && (wp & MK_LBUTTON) != 0) self->PushPointer(kActionMove, lp);
      return 0;
    case WM_LBUTTONUP:
      if (self->pointer_down) {
        self->pointer_down = false;
        self->PushPointer(kActionUp, lp);
      }
      ReleaseCapture();
      return 0;
    case WM_CAPTURECHANGED:
      if (self->pointer_down && reinterpret_cast<HWND>(lp) != hwnd) {
        self->pointer_down = false;
        self->PushPointer(kActionCancel, 0);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
      ScreenToClient(hwnd, &pt);
      HostEvent e{HostEvent::kScroll};
      e.x = static_cast<float>(pt.x);
      e.y = static_cast<float>(pt.y);
      e.scroll = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
      e.time = UptimeMillis();
      self->events.push_back(e);
      return 0;
    }
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE || wp == VK_BROWSER_BACK) {
        self->events.push_back(HostEvent{HostEvent::kBack});
        return 0;
      }
      if (const int code = KeyCodeForVk(wp); code >= 0) {
        self->PushKey(code, 0, CurrentMeta());
        return 0;
      }
      break;
    case WM_CHAR:
      // Printable characters; control characters come through WM_KEYDOWN.
      if (wp >= 0x20 && wp != 0x7f) self->PushKey(wp == ' ' ? kKeycodeSpace : kKeycodeUnknown, static_cast<int>(wp), CurrentMeta());
      return 0;
    case WM_CLOSE:
      // The application decides (Activity.finish); DestroyWindow follows from
      // ViewRootImpl.nativeCloseWindow.
      self->events.push_back(HostEvent{HostEvent::kClose});
      return 0;
    case WM_DPICHANGED: {
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool FrameworkHost::Impl::OpenWindow(const std::wstring& title, std::string* error) {
  if (surface == nullptr) {
    surface = gfx->CreateBitmap(config.width_px, config.height_px, false, error);
    if (surface == nullptr) return false;
    gfx->Pin(surface);
  }
  if (!gfx->Resize(surface, config.width_px, config.height_px, kWindowBackground, error)) return false;
  surface_open = true;
  if (config.headless) return true;
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClass;
  if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    *error = std::format("RegisterClassExW failed ({})", GetLastError());
    return false;
  }
  const DWORD style = WS_OVERLAPPEDWINDOW;
  RECT rc{0, 0, config.width_px, config.height_px};
  AdjustWindowRectExForDpi(&rc, style, FALSE, 0, GetDpiForSystem());
  hwnd = CreateWindowExW(0, kWindowClass, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                         rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
  if (hwnd == nullptr) {
    *error = std::format("CreateWindowExW failed ({})", GetLastError());
    return false;
  }
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  // The client area may differ from the request (screen too small): use it.
  RECT client{};
  GetClientRect(hwnd, &client);
  if (client.right > 0 && client.bottom > 0) gfx->Resize(surface, client.right, client.bottom, kWindowBackground, error);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  return true;
}

void FrameworkHost::Impl::Present() {
  frames.fetch_add(1, std::memory_order_relaxed);
  if (hwnd != nullptr && surface != nullptr) {
    present_width = gfx->Width(surface);
    present_height = gfx->Height(surface);
    gfx->CopyOpaque(surface, &present_pixels);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
  }
}

// --- script ------------------------------------------------------------------------

namespace {

struct NamedKey {
  const char* name;
  int code;
};

constexpr NamedKey kNamedKeys[] = {
    {"ENTER", kKeycodeEnter},       {"DEL", kKeycodeDel},           {"BACKSPACE", kKeycodeDel},
    {"TAB", kKeycodeTab},           {"SPACE", kKeycodeSpace},       {"LEFT", kKeycodeDpadLeft},
    {"RIGHT", kKeycodeDpadRight},   {"UP", kKeycodeDpadUp},         {"DOWN", kKeycodeDpadDown},
    {"FORWARD_DEL", kKeycodeForwardDel}, {"HOME", kKeycodeMoveHome}, {"END", kKeycodeMoveEnd},
    {"ESCAPE", kKeycodeEscape},     {"PAGE_UP", kKeycodePageUp},    {"PAGE_DOWN", kKeycodePageDown},
};

// Decodes UTF-8 into UTF-16 code units (invalid bytes become U+FFFD).
std::u16string Utf16(std::string_view s) {
  std::wstring w = Widen(s);
  return std::u16string(w.begin(), w.end());
}

}  // namespace

void FrameworkHost::Impl::RunScript() {
  while (ScriptPending()) {
    if (UptimeMillis() < script_wait_until) return;
    const std::string& line = config.script[script_pos];
    ++script_pos;
    {
      std::lock_guard<std::mutex> hold(report_lock);
      ++report.script_commands_run;
    }
    std::vector<std::string> words;
    std::string rest;  // everything after the command word
    for (std::size_t i = 0; i < line.size();) {
      while (i < line.size() && line[i] == ' ') ++i;
      std::size_t j = i;
      while (j < line.size() && line[j] != ' ') ++j;
      if (j > i) words.push_back(line.substr(i, j - i));
      if (words.size() == 1) {
        std::size_t k = j;
        if (k < line.size() && line[k] == ' ') ++k;
        rest = k < line.size() ? line.substr(k) : std::string();
        if (words[0] == "screenshot" || words[0] == "type") {
          while (!rest.empty() && rest.front() == ' ' && words[0] == "screenshot") rest.erase(rest.begin());
          if (!rest.empty()) words.push_back(rest);
          break;
        }
      }
      i = j;
    }
    if (words.empty()) continue;
    auto number = [&](std::size_t idx, float* out) {
      if (idx >= words.size()) return false;
      const std::string& w = words[idx];
      auto [p, ec] = std::from_chars(w.data(), w.data() + w.size(), *out);
      return ec == std::errc() && p == w.data() + w.size();
    };
    const std::string& cmd = words[0];
    if (cmd == "tap") {
      float x = 0, y = 0;
      if (!number(1, &x) || !number(2, &y)) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      const std::int64_t now = UptimeMillis();
      HostEvent down{HostEvent::kPointer};
      down.action = kActionDown;
      down.x = x;
      down.y = y;
      down.time = now;
      HostEvent up = down;
      up.action = kActionUp;
      up.time = now + 50;
      events.push_back(down);
      events.push_back(up);
      return;  // let the application process the tap first
    }
    if (cmd == "longpress") {
      float x = 0, y = 0;
      if (!number(1, &x) || !number(2, &y)) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      HostEvent down{HostEvent::kPointer};
      down.action = kActionDown;
      down.x = x;
      down.y = y;
      down.time = UptimeMillis();
      events.push_back(down);
      // The release comes after the long-press timeout, as a separate command.
      config.script.insert(config.script.begin() + static_cast<std::ptrdiff_t>(script_pos),
                           {"wait 700", std::format("__up {} {}", x, y)});
      return;
    }
    if (cmd == "__up") {
      float x = 0, y = 0;
      number(1, &x);
      number(2, &y);
      HostEvent up{HostEvent::kPointer};
      up.action = kActionUp;
      up.x = x;
      up.y = y;
      up.time = UptimeMillis();
      events.push_back(up);
      return;
    }
    if (cmd == "swipe") {
      float x0 = 0, y0 = 0, x1 = 0, y1 = 0, ms = 300;
      if (!number(1, &x0) || !number(2, &y0) || !number(3, &x1) || !number(4, &y1) ||
          (words.size() > 5 && !number(5, &ms))) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      const std::int64_t now = UptimeMillis();
      constexpr int kSteps = 10;
      for (int s = 0; s <= kSteps + 1; ++s) {
        HostEvent e{HostEvent::kPointer};
        const float f = std::min(1.0f, static_cast<float>(s) / kSteps);
        e.action = s == 0 ? kActionDown : s == kSteps + 1 ? kActionUp : kActionMove;
        e.x = x0 + (x1 - x0) * f;
        e.y = y0 + (y1 - y0) * f;
        e.time = now + static_cast<std::int64_t>(ms * f);
        events.push_back(e);
      }
      return;
    }
    if (cmd == "scroll") {
      float x = 0, y = 0, lines = 0;
      if (!number(1, &x) || !number(2, &y) || !number(3, &lines)) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      HostEvent e{HostEvent::kScroll};
      e.x = x;
      e.y = y;
      e.scroll = lines;
      e.time = UptimeMillis();
      events.push_back(e);
      return;
    }
    if (cmd == "type") {
      if (words.size() < 2) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      for (char16_t c : Utf16(words[1])) PushKey(c == u' ' ? kKeycodeSpace : kKeycodeUnknown, c, 0);
      return;
    }
    if (cmd == "key") {
      int code = -1;
      if (words.size() >= 2) {
        for (const NamedKey& k : kNamedKeys) {
          if (words[1] == k.name) code = k.code;
        }
      }
      if (code < 0) {
        SetScriptError("unknown key in script command: " + line);
        continue;
      }
      PushKey(code, 0, 0);
      return;
    }
    if (cmd == "wait") {
      float ms = 0;
      if (!number(1, &ms) || ms < 0) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      script_wait_until = UptimeMillis() + static_cast<std::int64_t>(ms);
      return;
    }
    if (cmd == "screenshot") {
      if (words.size() < 2) {
        SetScriptError("bad script command: " + line);
        continue;
      }
      std::string err = surface_open && surface != nullptr
                            ? gfx->WritePng(surface, std::filesystem::path(Widen(words[1])))
                            : std::string("no window is open");
      if (!err.empty()) SetScriptError("screenshot " + words[1] + ": " + err);
      continue;
    }
    if (cmd == "back") {
      events.push_back(HostEvent{HostEvent::kBack});
      return;
    }
    if (cmd == "close") {
      events.push_back(HostEvent{HostEvent::kClose});
      return;
    }
    SetScriptError("unknown script command: " + line);
  }
}

// --- natives ---------------------------------------------------------------------------

namespace {

FrameworkHost::Impl* g_host = nullptr;

Runtime* Rt(Thread* self) { return self->runtime(); }

bool CheckSurface(Thread* self, std::int64_t handle) {
  FrameworkHost::Impl* h = g_host;
  if (h->surface == nullptr || handle != Graphics::Handle(h->surface) || !h->surface_open || !h->OnUiThread()) {
    Rt(self)->ThrowNew(self, "Ljava/lang/IllegalStateException;",
                       "no such surface, or not on the thread that owns it");
    return false;
  }
  return true;
}

// android.util.Log
void Log_println_native(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  const std::string tag = rt->GetStringUtf8(ArgRef(a, 2));
  const std::string msg = rt->GetStringUtf8(ArgRef(a, 3));
  rt->Log(ArgInt(a, 1), tag, msg);
  r->i = static_cast<std::int32_t>(msg.size());
}

// android.os.SystemClock
void SystemClock_uptimeMillis(Thread*, Frame*, JValue* r) { r->j = UptimeMillis(); }

// android.os.Process
void Process_nativeMyPid(Thread*, Frame*, JValue* r) { r->i = g_host->config.guest_pid; }

void Process_nativeMyTid(Thread*, Frame*, JValue* r) {
  // Guest thread ids: the main thread's equals the pid, as on Android.
  const DWORD host = GetCurrentThreadId();
  if (host == g_host->ui_thread) {
    r->i = g_host->config.guest_pid;
    return;
  }
  std::lock_guard<std::mutex> hold(g_host->tid_lock);
  auto [it, inserted] = g_host->guest_tids.try_emplace(host, 0);
  if (inserted) it->second = g_host->config.guest_pid + (++g_host->next_tid);
  r->i = it->second;
}

// android.os.MessageQueue
void MessageQueue_nativeInit(Thread*, Frame*, JValue* r) {
  auto q = std::make_unique<NativeQueue>();
  q->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  q->ui = g_host->OnUiThread();
  std::lock_guard<std::mutex> hold(g_host->queue_lock);
  const std::int64_t id = g_host->next_queue++;
  g_host->queues.emplace(id, std::move(q));
  r->j = id;
}

void MessageQueue_nativeDestroy(Thread*, Frame* a, JValue*) {
  std::unique_ptr<NativeQueue> q;
  {
    std::lock_guard<std::mutex> hold(g_host->queue_lock);
    auto it = g_host->queues.find(ArgLong(a, 0));
    if (it == g_host->queues.end()) return;
    q = std::move(it->second);
    g_host->queues.erase(it);
  }
  CloseHandle(q->wake);
}

void MessageQueue_nativeWake(Thread*, Frame* a, JValue*) {
  NativeQueue* q = g_host->Queue(ArgLong(a, 0));
  if (q != nullptr) SetEvent(q->wake);
}

// Delivers recorded host input to ViewRootImpl. Stops at the first exception.
void DispatchEvents(Thread* self) {
  Runtime* rt = Rt(self);
  FrameworkHost::Impl* h = g_host;
  if (h->dispatch_pointer == nullptr) {
    RtClass* c = rt->class_linker().FindClass(self, "Landroid/view/ViewRootImpl;", nullptr);
    if (c == nullptr) return;
    h->dispatch_pointer = rt->FindMethod(c, "dispatchPointer", "(IFFJ)V");
    h->dispatch_resize = rt->FindMethod(c, "dispatchResize", "(II)V");
    h->dispatch_close = rt->FindMethod(c, "dispatchCloseRequest", "()V");
    h->dispatch_back = rt->FindMethod(c, "dispatchBack", "()V");
    h->dispatch_key = rt->FindMethod(c, "dispatchKey", "(IIIIJ)V");
    h->dispatch_scroll = rt->FindMethod(c, "dispatchScroll", "(FFFJ)V");
    if (h->dispatch_pointer == nullptr || h->dispatch_resize == nullptr || h->dispatch_close == nullptr ||
        h->dispatch_back == nullptr || h->dispatch_key == nullptr || h->dispatch_scroll == nullptr) {
      rt->ThrowNew(self, "Ljava/lang/NoSuchMethodError;", "ViewRootImpl host callbacks");
      return;
    }
  }
  while (!h->events.empty() && self->exception() == nullptr) {
    const HostEvent e = h->events.front();
    h->events.pop_front();
    JValue result{};
    switch (e.kind) {
      case HostEvent::kPointer: {
        JValue args[4];
        args[0].i = e.action;
        args[1].f = e.x;
        args[2].f = e.y;
        args[3].j = e.time;
        rt->Invoke(self, h->dispatch_pointer, nullptr, args, &result);
        break;
      }
      case HostEvent::kResize: {
        JValue args[2];
        args[0].i = e.width;
        args[1].i = e.height;
        rt->Invoke(self, h->dispatch_resize, nullptr, args, &result);
        break;
      }
      case HostEvent::kClose:
        rt->Invoke(self, h->dispatch_close, nullptr, {}, &result);
        break;
      case HostEvent::kBack:
        rt->Invoke(self, h->dispatch_back, nullptr, {}, &result);
        break;
      case HostEvent::kKey: {
        JValue args[5];
        args[0].i = e.action;
        args[1].i = e.key_code;
        args[2].i = e.unicode;
        args[3].i = e.meta;
        args[4].j = e.time;
        rt->Invoke(self, h->dispatch_key, nullptr, args, &result);
        break;
      }
      case HostEvent::kScroll: {
        JValue args[4];
        args[0].f = e.x;
        args[1].f = e.y;
        args[2].f = e.scroll;
        args[3].j = e.time;
        rt->Invoke(self, h->dispatch_scroll, nullptr, args, &result);
        break;
      }
    }
  }
}

void MessageQueue_nativePollOnce(Thread* self, Frame* a, JValue*) {
  FrameworkHost::Impl* h = g_host;
  NativeQueue* q = h->Queue(ArgLong(a, 0));
  if (q == nullptr) {
    Rt(self)->ThrowNew(self, "Ljava/lang/IllegalStateException;", "message queue has been disposed");
    return;
  }
  const std::int32_t timeout = ArgInt(a, 2);
  if (!q->ui) {
    ScopedBlocking blocking(self);
    WaitForSingleObject(q->wake, timeout < 0 ? INFINITE : static_cast<DWORD>(timeout));
    return;
  }

  const std::int64_t start = UptimeMillis();
  const bool idle = timeout < 0 || timeout >= kIdleThresholdMs;
  {
    ScopedBlocking blocking(self);
    for (;;) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      const std::int64_t now = UptimeMillis();
      if (h->session_deadline != 0 && now >= h->session_deadline && !h->close_requested_by_timeout) {
        h->close_requested_by_timeout = true;
        {
          std::lock_guard<std::mutex> hold(h->report_lock);
          h->report.timed_out = true;
        }
        h->events.push_back(HostEvent{HostEvent::kClose});
      }
      if (idle && h->events.empty() && h->ScriptPending()) h->RunScript();
      if (!h->events.empty()) break;
      if (timeout == 0) break;
      std::int64_t wait = timeout < 0 ? -1 : start + timeout - now;
      if (timeout >= 0 && wait <= 0) break;
      auto limit = [&](std::int64_t until) {
        if (until <= 0) return;
        const std::int64_t d = std::max<std::int64_t>(0, until - now);
        wait = wait < 0 ? d : std::min(wait, d);
      };
      if (idle && h->ScriptPending()) limit(h->script_wait_until > now ? h->script_wait_until : now + 1);
      if (h->session_deadline != 0 && !h->close_requested_by_timeout) limit(h->session_deadline);
      const DWORD r = MsgWaitForMultipleObjectsEx(1, &q->wake, wait < 0 ? INFINITE : static_cast<DWORD>(wait),
                                                  QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      if (r == WAIT_OBJECT_0) break;  // woken: a message was enqueued
    }
  }
  DispatchEvents(self);
}

// android.content.res.ResourcesHost
void ResourcesHost_nativeDisplayInfo(Thread* self, Frame* a, JValue*) {
  auto* out = static_cast<Array*>(ArgRef(a, 0));
  if (out == nullptr || out->length < 3) {
    Rt(self)->ThrowNew(self, "Ljava/lang/IllegalArgumentException;", "display info array");
    return;
  }
  FrameworkHost::Impl* h = g_host;
  const bool open = h->surface_open && h->surface != nullptr;
  out->elements<std::int32_t>()[0] = open ? h->gfx->Width(h->surface) : h->config.width_px;
  out->elements<std::int32_t>()[1] = open ? h->gfx->Height(h->surface) : h->config.height_px;
  out->elements<std::int32_t>()[2] = h->config.density_dpi;
}

// android.content.res.AssetManager (assets/)
bool AssetName(Thread* self, Frame* a, std::string* name) {
  *name = Rt(self)->GetStringUtf8(ArgRef(a, 0));
  // Asset names are relative paths inside assets/; nothing may climb out.
  if (name->find('\0') != std::string::npos) return false;
  std::size_t start = 0;
  while (start <= name->size()) {
    std::size_t end = name->find('/', start);
    if (end == std::string::npos) end = name->size();
    const std::string_view part(name->data() + start, end - start);
    if (part == "..") return false;
    start = end + 1;
  }
  while (!name->empty() && name->front() == '/') name->erase(name->begin());
  return true;
}

void AssetManager_nativeOpenAsset(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  r->l = nullptr;
  std::string name;
  if (!AssetName(self, a, &name) || name.empty() || g_host->config.apk == nullptr) return;
  const pkg::ZipEntry* e = g_host->config.apk->Find("assets/" + name);
  if (e == nullptr || e->is_directory) return;
  if (e->uncompressed_size > kMaxAssetBytes) {
    rt->ThrowNew(self, "Ljava/io/IOException;", std::format("asset {} is too large ({} bytes)", name,
                                                             e->uncompressed_size));
    return;
  }
  std::vector<std::uint8_t> data;
  {
    ScopedBlocking blocking(self);
    auto bytes = g_host->config.apk->Extract(*e);
    if (bytes.ok()) data = std::move(*bytes);
    else name = std::string(bytes.status().message());
  }
  if (data.empty() && e->uncompressed_size != 0) {
    rt->ThrowNew(self, "Ljava/io/IOException;", "cannot read asset: " + name);
    return;
  }
  Array* arr = rt->heap().AllocArray(self, rt->wk.byte_array, static_cast<std::int32_t>(data.size()));
  if (arr == nullptr) return;
  if (!data.empty()) std::memcpy(arr->data(), data.data(), data.size());
  r->l = arr;
}

void AssetManager_nativeList(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  std::string dir;
  if (!AssetName(self, a, &dir)) {
    r->l = rt->heap().AllocArray(self, rt->wk.string_array, 0);
    return;
  }
  while (!dir.empty() && dir.back() == '/') dir.pop_back();
  const std::string prefix = dir.empty() ? "assets/" : "assets/" + dir + "/";
  std::set<std::string> names;
  if (g_host->config.apk != nullptr) {
    for (const pkg::ZipEntry& e : g_host->config.apk->entries()) {
      if (e.name.size() <= prefix.size() || e.name.compare(0, prefix.size(), prefix) != 0) continue;
      std::string rest = e.name.substr(prefix.size());
      const std::size_t slash = rest.find('/');
      if (slash != std::string::npos) rest.resize(slash);
      if (!rest.empty()) names.insert(rest);
    }
  }
  RootScope roots(self);
  auto arr = roots.Add(rt->heap().AllocArray(self, rt->wk.string_array, static_cast<std::int32_t>(names.size())));
  if (arr.Get() == nullptr) return;
  std::size_t i = 0;
  for (const std::string& n : names) {
    Object* s = rt->NewStringUtf8(self, n);
    if (s == nullptr) return;
    static_cast<Array*>(arr.Get())->elements<Object*>()[i++] = s;
  }
  r->l = arr.Get();
}

// android.view.ViewRootImpl
void ViewRootImpl_nativeOpenWindow(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  FrameworkHost::Impl* h = g_host;
  auto* size = static_cast<Array*>(ArgRef(a, 1));
  if (size == nullptr || size->length < 2) {
    rt->ThrowNew(self, "Ljava/lang/IllegalArgumentException;", "size array");
    return;
  }
  if (!h->OnUiThread()) {
    rt->ThrowNew(self, "Ljava/lang/IllegalStateException;", "windows can only be opened on the main thread");
    return;
  }
  if (h->surface_open) {
    rt->ThrowNew(self, "Ljava/lang/IllegalStateException;", "only one window per session is supported");
    return;
  }
  const std::string title = rt->GetStringUtf8(ArgRef(a, 0));
  std::string error;
  if (!h->OpenWindow(Widen(title), &error)) {
    rt->ThrowNew(self, "Ljava/lang/RuntimeException;", "cannot open the host window: " + error);
    return;
  }
  size->elements<std::int32_t>()[0] = h->gfx->Width(h->surface);
  size->elements<std::int32_t>()[1] = h->gfx->Height(h->surface);
  r->j = Graphics::Handle(h->surface);
}

void ViewRootImpl_nativeSetTitle(Thread* self, Frame* a, JValue*) {
  if (!CheckSurface(self, ArgLong(a, 0))) return;
  if (g_host->hwnd != nullptr) SetWindowTextW(g_host->hwnd, Widen(Rt(self)->GetStringUtf8(ArgRef(a, 2))).c_str());
}

void ViewRootImpl_nativePresent(Thread* self, Frame* a, JValue*) {
  if (!CheckSurface(self, ArgLong(a, 0))) return;
  g_host->Present();
}

void ViewRootImpl_nativeCloseWindow(Thread* self, Frame* a, JValue*) {
  if (!CheckSurface(self, ArgLong(a, 0))) return;
  FrameworkHost::Impl* h = g_host;
  if (h->hwnd != nullptr) {
    SetWindowLongPtrW(h->hwnd, GWLP_USERDATA, 0);
    DestroyWindow(h->hwnd);
    h->hwnd = nullptr;
  }
  h->surface_open = false;
  h->events.clear();
}

// android.app.ActivityThread
void ActivityThread_nativeAppClassLoader(Thread*, Frame*, JValue* r) { r->l = g_host->app_loader; }

// android.content.ClipboardManager: the host clipboard is deliberately not
// shared with the guest (spec 11); the guest gets a session-local one in Java.

}  // namespace

// --- FrameworkHost -------------------------------------------------------------------------

FrameworkHost::FrameworkHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

FrameworkHost::~FrameworkHost() {
  if (g_host == impl_.get()) g_host = nullptr;
  if (impl_->hwnd != nullptr) DestroyWindow(impl_->hwnd);
  for (auto& [id, q] : impl_->queues) CloseHandle(q->wake);
}

Result<std::unique_ptr<FrameworkHost>> FrameworkHost::Create(HostConfig config) {
  if (g_host != nullptr) return Status(ReasonCode::kBusy, "a framework host already exists in this process");
  if (config.width_px <= 0 || config.height_px <= 0 || config.width_px > 16384 || config.height_px > 16384) {
    return Status(ReasonCode::kInvalidArgument, "window size out of range");
  }
  if (config.density_dpi < 60 || config.density_dpi > 960) {
    return Status(ReasonCode::kInvalidArgument, "density out of range");
  }
  auto impl = std::make_unique<Impl>();
  impl->config = std::move(config);
  ARUN_ASSIGN_OR_RETURN(impl->resources, ResourcesHost::Create(impl->config.framework_res, impl->config.apk));
  std::string error;
  impl->gfx = Graphics::Get(&error);
  if (impl->gfx == nullptr) return Status(ReasonCode::kHostOsError, "graphics: " + error);
  if (impl->config.timeout_ms != 0) impl->session_deadline = UptimeMillis() + impl->config.timeout_ms;
  auto host = std::unique_ptr<FrameworkHost>(new FrameworkHost(std::move(impl)));
  g_host = host->impl_.get();
  return host;
}

void FrameworkHost::BindUiThread() { impl_->ui_thread = GetCurrentThreadId(); }

void FrameworkHost::SetAppClassLoader(Object* loader) { impl_->app_loader = loader; }

SessionReport FrameworkHost::report() const {
  std::lock_guard<std::mutex> hold(impl_->report_lock);
  SessionReport r = impl_->report;
  r.frames_presented = impl_->frames.load(std::memory_order_relaxed);
  return r;
}

void FrameworkHost::RegisterNatives(Runtime* rt) {
  impl_->runtime = rt;
  impl_->resources->RegisterNatives(rt);
  (void)RegisterGraphicsNatives(rt, impl_->config.density_dpi);  // Graphics::Get already succeeded in Create
  auto reg = [rt](const char* key, vm::NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Landroid/util/Log;.println_native(IILjava/lang/String;Ljava/lang/String;)I", Log_println_native);
  reg("Landroid/os/SystemClock;.uptimeMillis()J", SystemClock_uptimeMillis);
  reg("Landroid/os/Process;.nativeMyPid()I", Process_nativeMyPid);
  reg("Landroid/os/Process;.nativeMyTid()I", Process_nativeMyTid);
  reg("Landroid/os/MessageQueue;.nativeInit()J", MessageQueue_nativeInit);
  reg("Landroid/os/MessageQueue;.nativeDestroy(J)V", MessageQueue_nativeDestroy);
  reg("Landroid/os/MessageQueue;.nativePollOnce(JI)V", MessageQueue_nativePollOnce);
  reg("Landroid/os/MessageQueue;.nativeWake(J)V", MessageQueue_nativeWake);
  reg("Landroid/content/res/ResourcesHost;.nativeDisplayInfo([I)V", ResourcesHost_nativeDisplayInfo);
  reg("Landroid/content/res/AssetManager;.nativeOpenAsset(Ljava/lang/String;)[B", AssetManager_nativeOpenAsset);
  reg("Landroid/content/res/AssetManager;.nativeList(Ljava/lang/String;)[Ljava/lang/String;", AssetManager_nativeList);
  reg("Landroid/view/ViewRootImpl;.nativeOpenWindow(Ljava/lang/String;[I)J", ViewRootImpl_nativeOpenWindow);
  reg("Landroid/view/ViewRootImpl;.nativeSetTitle(JLjava/lang/String;)V", ViewRootImpl_nativeSetTitle);
  reg("Landroid/view/ViewRootImpl;.nativePresent(J)V", ViewRootImpl_nativePresent);
  reg("Landroid/view/ViewRootImpl;.nativeCloseWindow(J)V", ViewRootImpl_nativeCloseWindow);
  reg("Landroid/app/ActivityThread;.nativeAppClassLoader()Ljava/lang/ClassLoader;",
      ActivityThread_nativeAppClassLoader);
}

}  // namespace arun::fw
