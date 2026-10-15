#include "arun/log.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace arun::log {
namespace {

constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::kCount);

// Hot-path categories start silenced (spec 22).
constexpr Level kDefaultLevel(Category c) {
  switch (c) {
    case Category::kVfs:
    case Category::kMemory:
    case Category::kAbi:
    case Category::kJni:
    case Category::kGraphics:
    case Category::kAudio:
    case Category::kInput:
      return Level::kWarn;
    default:
      return Level::kInfo;
  }
}

struct State {
  std::mutex mutex;
  std::array<Level, kCategoryCount> levels{};
  HANDLE file = INVALID_HANDLE_VALUE;

  State() {
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
      levels[i] = kDefaultLevel(static_cast<Category>(i));
    }
  }
};

State& state() {
  static State s;
  return s;
}

const char* LevelName(Level l) {
  switch (l) {
    case Level::kTrace: return "TRACE";
    case Level::kDebug: return "DEBUG";
    case Level::kInfo:  return "INFO ";
    case Level::kWarn:  return "WARN ";
    case Level::kError: return "ERROR";
    case Level::kOff:   return "OFF  ";
  }
  return "?????";
}

}  // namespace

const char* CategoryName(Category c) {
  switch (c) {
    case Category::kGeneral:   return "general";
    case Category::kInstall:   return "install";
    case Category::kManifest:  return "manifest";
    case Category::kVfs:       return "vfs";
    case Category::kMemory:    return "memory";
    case Category::kElf:       return "elf";
    case Category::kAbi:       return "abi";
    case Category::kThreads:   return "threads";
    case Category::kRuntime:   return "runtime";
    case Category::kJni:       return "jni";
    case Category::kLifecycle: return "lifecycle";
    case Category::kGraphics:  return "graphics";
    case Category::kAudio:     return "audio";
    case Category::kInput:     return "input";
    case Category::kSecurity:  return "security";
    case Category::kCount:     break;
  }
  return "unknown";
}

void SetLevel(Category category, Level level) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  s.levels[static_cast<std::size_t>(category)] = level;
}

void SetGlobalLevel(Level level) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  s.levels.fill(level);
}

bool Enabled(Category category, Level level) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  return static_cast<int>(level) >= static_cast<int>(s.levels[static_cast<std::size_t>(category)]);
}

void Emit(Category category, Level level, std::string_view message) {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);

  char line[4096];
  const int n = std::snprintf(
      line, sizeof(line), "%02d:%02d:%02d.%03lld [%s] %-9s tid=%lu %.*s\n",
      tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ms.count()),
      LevelName(level), CategoryName(category), GetCurrentThreadId(),
      static_cast<int>(message.size()), message.data());
  if (n <= 0) return;
  const std::size_t len = (static_cast<std::size_t>(n) < sizeof(line))
                              ? static_cast<std::size_t>(n)
                              : sizeof(line) - 1;

  State& s = state();
  std::lock_guard lock(s.mutex);
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, static_cast<DWORD>(len), &written, nullptr);
  if (s.file != INVALID_HANDLE_VALUE) {
    WriteFile(s.file, line, static_cast<DWORD>(len), &written, nullptr);
  }
}

bool OpenFileSink(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  State& s = state();
  std::lock_guard lock(s.mutex);
  if (s.file != INVALID_HANDLE_VALUE) CloseHandle(s.file);
  s.file = h;
  return true;
}

void CloseFileSink() {
  State& s = state();
  std::lock_guard lock(s.mutex);
  if (s.file != INVALID_HANDLE_VALUE) {
    CloseHandle(s.file);
    s.file = INVALID_HANDLE_VALUE;
  }
}

}  // namespace arun::log
