#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace berberis {
inline void StringAppendV(std::string* out, const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int count = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (count < 0) return;
    std::vector<char> buffer(size_t(count) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    out->append(buffer.data(), size_t(count));
}
inline void StringAppendF(std::string* out, const char* format, ...) {
    va_list args; va_start(args, format); StringAppendV(out, format, args); va_end(args);
}
inline std::string StringPrintf(const char* format, ...) {
    std::string out;
    va_list args; va_start(args, format); StringAppendV(&out, format, args); va_end(args);
    return out;
}
}
