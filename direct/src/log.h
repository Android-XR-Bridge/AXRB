#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

namespace axrb {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Trace = 4 };

// AXRB_DIRECT_LOG=<file> mirrors everything into a file; AXRB_DIRECT_TRACE is a
// comma-separated list of categories (syscall, files, mmap, thread, signal,
// futex, net, jni, vulkan, xr, java, all) that enables trace output for them.
void log_init();
void log_write(LogLevel level, const char* tag, const char* format, ...);
void log_writev(LogLevel level, const char* tag, const char* format, va_list args);
bool trace_enabled(const char* category);
// Raw text, e.g. guest stdout/logcat lines, written without a prefix.
void log_raw(const char* text, size_t length);
void log_flush();

[[noreturn]] void fatal(const char* format, ...);

}  // namespace axrb

#define LOGE(tag, ...) ::axrb::log_write(::axrb::LogLevel::Error, tag, __VA_ARGS__)
#define LOGW(tag, ...) ::axrb::log_write(::axrb::LogLevel::Warn, tag, __VA_ARGS__)
#define LOGI(tag, ...) ::axrb::log_write(::axrb::LogLevel::Info, tag, __VA_ARGS__)
#define LOGD(tag, ...) ::axrb::log_write(::axrb::LogLevel::Debug, tag, __VA_ARGS__)
#define TRACE(category, ...)                                                       \
    do {                                                                           \
        if (::axrb::trace_enabled(category))                                       \
            ::axrb::log_write(::axrb::LogLevel::Trace, category, __VA_ARGS__);     \
    } while (0)
