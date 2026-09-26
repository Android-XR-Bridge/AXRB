#pragma once

#include "bridge.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

struct QbMsgBuf {
    uint32_t type;
    uint32_t size;
    uint8_t body[508];
};

inline bool qb_write(HANDLE pipe, uint32_t type, const void* body, uint32_t bytes) {
    uint8_t buf[512];
    if (4u + bytes > sizeof(buf)) return false;
    std::memcpy(buf, &type, 4);
    if (bytes) std::memcpy(buf + 4, body, bytes);
    DWORD wrote = 0;
    return WriteFile(pipe, buf, 4u + bytes, &wrote, nullptr) && wrote == 4u + bytes;
}

inline bool qb_read(HANDLE pipe, QbMsgBuf* msg) {
    uint8_t buf[512];
    DWORD got = 0;
    if (!ReadFile(pipe, buf, sizeof(buf), &got, nullptr) || got < 4) return false;
    std::memcpy(&msg->type, buf, 4);
    msg->size = got - 4;
    if (msg->size > sizeof(msg->body)) return false;
    if (msg->size) std::memcpy(msg->body, buf + 4, msg->size);
    return true;
}

template <typename T>
inline bool qb_body(const QbMsgBuf& msg, T* out) {
    if (msg.size < sizeof(T)) return false;
    std::memcpy(out, msg.body, sizeof(T));
    return true;
}
