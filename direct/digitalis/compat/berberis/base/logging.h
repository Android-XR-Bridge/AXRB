#pragma once
#include <cstdio>
#include <stdexcept>
#define LOG_ALWAYS_FATAL(...) do { char message[1024]; std::snprintf(message, sizeof(message), __VA_ARGS__); throw std::runtime_error(message); } while (0)
#define LOG_ALWAYS_FATAL_IF(condition, ...) do { if (condition) LOG_ALWAYS_FATAL(__VA_ARGS__); } while (0)
#define ALOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ALOGW(...) ALOGE(__VA_ARGS__)
#define ALOGI(...) ALOGE(__VA_ARGS__)
#define ALOGD(...) ((void)0)
#define ALOGV(...) ((void)0)
