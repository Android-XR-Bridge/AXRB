#pragma once
#include <cstdlib>

/* QB_* switches are fixed for the life of the process, and std::getenv scans
   the whole environment block each time: on a host-call path that showed up
   as most of the main thread's time. QB_ENV looks the name up once per call
   site and keeps the answer. */
#define QB_ENV(name) ([]() -> const char* { static const char* const value = std::getenv(name); return value; }())
