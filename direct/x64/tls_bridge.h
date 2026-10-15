#pragma once
#include <cstdint>
#include <vector>
#include <windows.h>

namespace native_tls {
extern thread_local uint64_t gs_base;
// One batch per executable mapping: generate while writable, seal before use.
class Batch {
    uintptr_t near_address;
    std::vector<uint8_t*> pages;
    size_t used=65536;
public:
    explicit Batch(uintptr_t address):near_address(address) {}
    uint8_t* generate(const std::vector<uint8_t>& original,uintptr_t next);
    void seal();
};
}
