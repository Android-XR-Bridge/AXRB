#include "mirror_window.h"
#if defined(_WIN32)
#include "performance_hud_bitmap.h"
namespace axrb::host {
LRESULT CALLBACK MirrorWindow::hud_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self=reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        self=static_cast<MirrorWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT && self) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint);
        if(!self->hudPixels_.empty()) {
            RECT r{}; GetClientRect(window,&r); BITMAPINFO info{};
            info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=performanceHudWidth;
            info.bmiHeader.biHeight=-performanceHudHeight; info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,nullptr);
            StretchDIBits(dc,0,0,r.right,r.bottom,0,0,performanceHudWidth,performanceHudHeight,self->hudPixels_.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        EndPaint(window,&paint); return 0;
    }
    return DefWindowProcW(window,message,w,l);
}

void MirrorWindow::performance_hud(const std::vector<uint8_t>& bgra,bool visible) {
    if(!window_) return;
    if(!visible) { if(hud_) ShowWindow(hud_,SW_HIDE); return; }
    if(!hud_) {
        WNDCLASSW wc{}; wc.lpfnWndProc=hud_proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"AXRBPerformanceHUD";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return;
        hud_=CreateWindowExW(0,wc.lpszClassName,L"AXRB fresh-frame performance",WS_CHILD|WS_CLIPSIBLINGS,8,8,performanceHudWidth,performanceHudHeight,window_,nullptr,wc.hInstance,this);
        if(!hud_) return; layout();
    }
    if(!bgra.empty()) hudPixels_=bgra;
    ShowWindow(hud_,SW_SHOWNOACTIVATE); InvalidateRect(hud_,nullptr,FALSE);
}

}
#endif
