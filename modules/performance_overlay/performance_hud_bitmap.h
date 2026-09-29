#pragma once
// Optional performance-overlay module; excluded from default host builds.
#if defined(_WIN32)
#include "fresh_frame_stats.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace axrb::host {
constexpr int performanceHudWidth = 800, performanceHudHeight = 270;
// BGRA DIB, rendered only four times/second. No hook, GPU readback, per-frame
// text rendering, or guest profiling runs on the OpenXR presentation thread.
inline std::vector<uint8_t> performance_hud_bitmap(const PerformanceSummary& s,
        const std::string& android = "ANDROID CPU: collector unavailable",
        const std::string& threads = "THREADS: unavailable (never inferred from host CPU)") {
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = performanceHudWidth; info.bmiHeader.biHeight = -performanceHudHeight;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) { DeleteDC(dc); return {}; }
    auto oldBitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH, "Consolas");
    auto oldFont = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
    RECT all{0,0,performanceHudWidth,performanceHudHeight};
    HBRUSH background = CreateSolidBrush(RGB(17,21,27)); FillRect(dc, &all, background); DeleteObject(background);
    const auto line = [&](int y, COLORREF color, const char* text) {
        SetTextColor(dc, color); TextOutA(dc, 12, y, text, static_cast<int>(std::strlen(text)));
    };
    char text[256];
    const bool waiting = s.fresh.ageMs < 0, stalled = s.fresh.ageMs > 250;
    std::snprintf(text, sizeof(text), "FRESH GAME  %3.0f FPS   %s", s.fresh.fps,
        waiting ? "WAITING FOR GAME" : stalled ? "STALL / NO NEW FRAME" : "new game images submitted to OpenXR");
    line(10, waiting ? RGB(210,210,210) : stalled ? RGB(255,95,95) : RGB(115,240,170), text);
    std::snprintf(text, sizeof(text), "Received %3.0f FPS   Host submissions %3.0f/s   Repeats %3.0f/s", s.received.fps, s.host.fps, s.repeats.fps);
    line(32, RGB(240,240,240), text);
    char low[32]; if(s.fresh.low1Ready) std::snprintf(low,sizeof(low),"%.1f FPS",s.fresh.low1Fps);
    else std::snprintf(low,sizeof(low),"warming up");
    std::snprintf(text,sizeof(text),"Fresh gaps  %.1f ms avg   %.1f ms worst   1%% low %s", s.fresh.averageMs,s.fresh.worstMs,low);
    line(54,RGB(240,240,240),text);
    std::snprintf(text,sizeof(text),"Render %u x %u / eye   Target %.1f Hz   %s",s.width,s.height,s.targetHz,s.gpuShared?"GPU shared textures":"CPU/video or waiting");
    line(76,RGB(240,240,240),text);
    if(waiting) std::snprintf(text,sizeof(text),"Last fresh frame: none   FPS window 1s / timing window 5s");
    else std::snprintf(text,sizeof(text),"Last fresh frame %.0f ms ago   FPS window 1s / timing window 5s",s.fresh.ageMs);
    line(98,stalled?RGB(255,95,95):RGB(200,210,220),text);
    line(120,RGB(200,210,220),android.c_str());
    line(142,RGB(200,210,220),threads.c_str());
    const double budget = s.targetHz > 0 ? 1000.0 / s.targetHz : 11.111;
    constexpr int bottom = 237, plotHeight = 62;
    const auto& graph = s.fresh.graph;
    const double maximum = (std::max)(budget * 4, 50.0);
    const int budgetY = bottom - int(plotHeight * budget / maximum);
    HPEN pen = CreatePen(PS_SOLID,1,RGB(115,135,155)); auto oldPen = SelectObject(dc,pen);
    MoveToEx(dc,12,budgetY,nullptr); LineTo(dc,788,budgetY); SelectObject(dc,oldPen); DeleteObject(pen);
    for(size_t i=0;i<graph.size();++i) {
        const int x = 12 + int(i * 776 / 240);
        const int bar = (std::max)(1, int(plotHeight * (std::min)(graph[i],maximum)/maximum));
        RECT rect{x,bottom-bar,x+2,bottom};
        const COLORREF color = graph[i] > budget*1.5 ? RGB(255,90,90) : graph[i] > budget*1.05 ? RGB(245,205,75) : RGB(110,220,145);
        HBRUSH brush = CreateSolidBrush(color); FillRect(dc,&rect,brush); DeleteObject(brush);
    }
    line(247,RGB(160,175,190),"F1 in mirror: hide/show   Fresh != SteamVR Hz; scanout not measured");
    GdiFlush();
    std::vector<uint8_t> out(static_cast<uint8_t*>(bits),static_cast<uint8_t*>(bits)+performanceHudWidth*performanceHudHeight*4);
    for(size_t i=3;i<out.size();i+=4) out[i]=255;
    SelectObject(dc,oldFont); DeleteObject(font); SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
    return out;
}
} // namespace axrb::host
#endif
