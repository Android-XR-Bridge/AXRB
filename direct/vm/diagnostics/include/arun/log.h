// Ownership: global sink, process-wide.
// Thread affinity: all functions are thread-safe (a mutex serialises the sink).
// Error propagation: logging never fails and never throws.
// Pointers: formatted values only; no host/guest pointer ownership implied.
//
// Categories exist so hot paths (spec 22: "do not flood hot paths by default")
// can be silenced independently of control-plane logging.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace arun::log {

enum class Level : int { kTrace = 0, kDebug = 1, kInfo = 2, kWarn = 3, kError = 4, kOff = 5 };

enum class Category : std::uint32_t {
  kGeneral,
  kInstall,
  kManifest,
  kVfs,        // hot
  kMemory,     // hot
  kElf,
  kAbi,        // hot
  kThreads,
  kRuntime,    // ART
  kJni,        // hot
  kLifecycle,
  kGraphics,   // hot
  kAudio,      // hot
  kInput,      // hot
  kSecurity,
  kCount,
};

const char* CategoryName(Category c);

// Default: kInfo for control-plane categories, kOff for the hot ones above.
void SetLevel(Category category, Level level);
void SetGlobalLevel(Level level);
bool Enabled(Category category, Level level);

// Writes to stderr, plus the session log file when one is open.
void Emit(Category category, Level level, std::string_view message);

// Opens an additional file sink. Returns false if the file cannot be created;
// logging continues to stderr in that case.
bool OpenFileSink(const std::wstring& path);
void CloseFileSink();

template <typename... Args>
void Write(Category category, Level level, std::format_string<Args...> fmt, Args&&... args) {
  if (!Enabled(category, level)) return;
  Emit(category, level, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace arun::log

#define ARUN_LOG_TRACE(cat, ...) ::arun::log::Write((cat), ::arun::log::Level::kTrace, __VA_ARGS__)
#define ARUN_LOG_DEBUG(cat, ...) ::arun::log::Write((cat), ::arun::log::Level::kDebug, __VA_ARGS__)
#define ARUN_LOG_INFO(cat, ...)  ::arun::log::Write((cat), ::arun::log::Level::kInfo,  __VA_ARGS__)
#define ARUN_LOG_WARN(cat, ...)  ::arun::log::Write((cat), ::arun::log::Level::kWarn,  __VA_ARGS__)
#define ARUN_LOG_ERROR(cat, ...) ::arun::log::Write((cat), ::arun::log::Level::kError, __VA_ARGS__)
