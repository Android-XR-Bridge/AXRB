#pragma once
// Host build of shared Android-side code: android/log.h onto the runner's log.

enum { ANDROID_LOG_VERBOSE = 2, ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL };

int __android_log_print(int priority, const char* tag, const char* format, ...);
