// libvulkan.so for AXRB Direct: the Vulkan loader, forwarding to the host's GPU.
//
// Every command is a thunk that packs its arguments into 64-bit slots and traps
// to the host ("vk.call" with the command's index), which unpacks them into the
// host driver's function (direct/src/vk_host.cpp). Structures are not copied:
// guest memory is the host's, and guest and host Vulkan structures have the same
// layout. The thunks are generated (direct/tools/gen_vulkan.py).
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <string.h>

#include <type_traits>

#include "../axrb_guest.h"

extern "C" uint64_t axrb_vk_call(uint32_t id, const uint64_t* args);
// Whether the host has `id` for the given instance (kInstance) or device
// (kDevice) handle; global commands are always present.
extern "C" uint64_t axrb_vk_query(uint32_t id, uint64_t handle, uint32_t level);
AXRB_HOST_FUNCTION(axrb_vk_call, "vk.call");
AXRB_HOST_FUNCTION(axrb_vk_query, "vk.query");

namespace {

enum Level : uint32_t { kGlobal, kInstance, kDevice };

struct VkCommand {
    const char* name;
    PFN_vkVoidFunction fn;
    Level level;
};

template <typename T>
inline uint64_t u64(T v) {
    if constexpr (std::is_same_v<T, float>) {
        uint32_t bits;
        memcpy(&bits, &v, sizeof bits);
        return bits;
    } else if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(v);
    } else {
        return static_cast<uint64_t>(v);
    }
}

template <typename R>
inline R from_u64(uint64_t v) {
    if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<R>(static_cast<uintptr_t>(v));
    } else if constexpr (std::is_enum_v<R>) {
        return static_cast<R>(static_cast<std::underlying_type_t<R>>(v));
    } else {
        return static_cast<R>(v);
    }
}

}  // namespace

#include "vk_thunks.inc"

namespace {

const VkCommand* find_command(const char* name, uint32_t* id) {
    if (name == nullptr) return nullptr;
    for (uint32_t i = 0; i < sizeof kCommands / sizeof kCommands[0]; ++i) {
        if (strcmp(kCommands[i].name, name) == 0) {
            *id = i;
            return &kCommands[i];
        }
    }
    return nullptr;
}

}  // namespace

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName);

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (pName == nullptr) return nullptr;
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    uint32_t id;
    const VkCommand* c = find_command(pName, &id);
    if (c == nullptr) return nullptr;
    if (c->level == kGlobal) return c->fn;
    if (instance == VK_NULL_HANDLE) return nullptr;
    return axrb_vk_query(id, u64(instance), kInstance) ? c->fn : nullptr;
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (pName == nullptr || device == VK_NULL_HANDLE) return nullptr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    uint32_t id;
    const VkCommand* c = find_command(pName, &id);
    if (c == nullptr || c->level != kDevice) return nullptr;
    return axrb_vk_query(id, u64(device), kDevice) ? c->fn : nullptr;
}
