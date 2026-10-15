#include "log.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace axrb {
namespace {

std::mutex g_lock;
FILE* g_file = nullptr;
std::set<std::string> g_trace;
bool g_trace_all = false;
LogLevel g_level = LogLevel::Info;
const auto g_start = std::chrono::steady_clock::now();

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "E";
        case LogLevel::Warn: return "W";
        case LogLevel::Info: return "I";
        case LogLevel::Debug: return "D";
        default: return "T";
    }
}

}  // namespace

void log_init() {
    if (const char* path = std::getenv("AXRB_DIRECT_LOG")) g_file = std::fopen(path, "wb");
    if (const char* level = std::getenv("AXRB_DIRECT_LOG_LEVEL")) g_level = static_cast<LogLevel>(std::atoi(level));
    if (const char* list = std::getenv("AXRB_DIRECT_TRACE")) {
        std::string text = list;
        size_t at = 0;
        while (at <= text.size()) {
            size_t comma = text.find(',', at);
            if (comma == std::string::npos) comma = text.size();
            std::string item = text.substr(at, comma - at);
            if (item == "all") g_trace_all = true;
            if (!item.empty()) g_trace.insert(item);
            at = comma + 1;
        }
    }
}

bool trace_enabled(const char* category) {
    if (g_trace_all) return true;
    if (g_trace.empty()) return false;
    return g_trace.count(category) != 0;
}

void log_writev(LogLevel level, const char* tag, const char* format, va_list args) {
    if (level > g_level && level != LogLevel::Trace) return;
    char body[4096];
    std::vsnprintf(body, sizeof body, format, args);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    char line[4300];
    int n = std::snprintf(line, sizeof line, "%9.3f %5lu %s/%s: %s\n", seconds, GetCurrentThreadId(), level_name(level),
                          tag, body);
    if (n < 0) return;
    if (n > static_cast<int>(sizeof line) - 1) n = sizeof line - 1;
    std::lock_guard<std::mutex> held(g_lock);
    std::fwrite(line, 1, n, stderr);
    if (g_file) std::fwrite(line, 1, n, g_file);
    // Unbuffered in effect: a killed or crashed process keeps its last lines.
    std::fflush(stderr);
    if (g_file) std::fflush(g_file);
}

void log_write(LogLevel level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_writev(level, tag, format, args);
    va_end(args);
}

void log_raw(const char* text, size_t length) {
    std::lock_guard<std::mutex> held(g_lock);
    std::fwrite(text, 1, length, stdout);
    std::fflush(stdout);
    if (g_file) {
        std::fwrite(text, 1, length, g_file);
        std::fflush(g_file);
    }
}

void log_flush() {
    std::lock_guard<std::mutex> held(g_lock);
    std::fflush(stdout);
    std::fflush(stderr);
    if (g_file) std::fflush(g_file);
}

void fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_writev(LogLevel::Error, "fatal", format, args);
    va_end(args);
    log_flush();
    TerminateProcess(GetCurrentProcess(), 70);
    for (;;) {
    }
}

}  // namespace axrb
