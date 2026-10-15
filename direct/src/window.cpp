// Desktop windows for Android surfaces. They run on their own message thread so
// that guest threads (which block in Vulkan, futexes, ...) never have to pump.

#include "window.h"

#include "app.h"

#include "log.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace axrb {
namespace {

constexpr UINT kCreateWindow = WM_APP + 1;
constexpr wchar_t kClassName[] = L"AXRBDirectSurface";

struct CreateRequest {
    int32_t width, height;
    HWND result = nullptr;
    HANDLE done = nullptr;
};

std::mutex g_lock;
DWORD g_thread_id = 0;
std::map<uint64_t, HWND> g_windows;

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
            LOGI("window", "surface window closed: ending the application");
            request_session_end("window closed");
            return 0;
        case WM_ERASEBKGND:
            return 1;  // the swapchain paints everything
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND create_on_thread(int32_t width, int32_t height) {
    // Fit the surface into three quarters of the work area, keeping its aspect.
    RECT work{0, 0, 1920, 1080};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const double max_w = (work.right - work.left) * 0.75, max_h = (work.bottom - work.top) * 0.75;
    double scale = 1.0;
    if (width > max_w) scale = max_w / width;
    if (height * scale > max_h) scale = max_h / height;
    RECT r{0, 0, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExW(0, kClassName, L"AXRB Direct", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd;
}

void message_thread(HANDLE ready) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // creates this thread's queue
    SetEvent(ready);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == kCreateWindow) {
            auto* req = reinterpret_cast<CreateRequest*>(msg.lParam);
            req->result = create_on_thread(req->width, req->height);
            SetEvent(req->done);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

DWORD ensure_thread() {
    if (g_thread_id) return g_thread_id;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread t(message_thread, ready);
    g_thread_id = GetThreadId(t.native_handle());
    t.detach();
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    return g_thread_id;
}

}  // namespace

HWND host_window_for(uint64_t native_window, int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_windows.find(native_window);
    if (it != g_windows.end() && IsWindow(it->second)) return it->second;
    CreateRequest req{width > 0 ? width : 1920, height > 0 ? height : 1080};
    req.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    PostThreadMessageW(ensure_thread(), kCreateWindow, 0, reinterpret_cast<LPARAM>(&req));
    WaitForSingleObject(req.done, INFINITE);
    CloseHandle(req.done);
    if (req.result) g_windows[native_window] = req.result;
    LOGI("window", "surface window %p for ANativeWindow %llx (%dx%d)", req.result,
         static_cast<unsigned long long>(native_window), req.width, req.height);
    return req.result;
}

}  // namespace axrb
