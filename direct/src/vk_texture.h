#pragma once

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace axrb::vktex {

// Mobile texture formats on a desktop GPU: ASTC, ETC2 and EAC images are
// stored in native BC formats and their uploads transcoded on the way
// (runtime/vulkan/texture, the same code as the emulator's guest layer). The
// guest is told these formats are supported, as they are on a Quest.

using Override = uint64_t (*)(const uint64_t* args);
using Lookup = PFN_vkVoidFunction (*)(VkInstance instance, const char* name);

// lookup resolves host commands (instance-level, through the system loader).
void init(Lookup lookup);
bool enabled();

// Overrides for the commands the substitution needs to see.
std::vector<std::pair<const char*, Override>> overrides();

// vkCreateDevice: adjusts the enabled features (BC instead of ASTC/ETC2) for the
// duration of the call, then registers the created device.
struct DeviceFeatures {
    VkPhysicalDeviceFeatures plain{};
    VkPhysicalDeviceFeatures2* features2 = nullptr;
    VkPhysicalDeviceFeatures saved2{};
    bool active = false;
};
void prepare_device(VkInstance instance, VkPhysicalDevice physical, VkDeviceCreateInfo& info, DeviceFeatures& state);
void finish_device(VkInstance instance, VkPhysicalDevice physical, const VkDeviceCreateInfo& info, DeviceFeatures& state,
                   VkResult result, VkDevice device);

}  // namespace axrb::vktex
