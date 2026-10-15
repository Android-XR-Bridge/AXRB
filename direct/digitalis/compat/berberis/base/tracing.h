#pragma once
#include <cstdarg>
#include <cstdio>
namespace berberis {
class Tracing {
 public:
  static bool IsOn() { return false; }
  static void Init() {}
  static void TraceF(const char* format, ...) {
    va_list args; va_start(args, format); std::vfprintf(stderr, format, args); va_end(args);
  }
};
}
#define TRACE(...) do { if (::berberis::Tracing::IsOn()) ::berberis::Tracing::TraceF(__VA_ARGS__); } while (false)
#define TRACE_AND_ALOGD(...) TRACE(__VA_ARGS__)
