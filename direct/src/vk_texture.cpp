// Host build of the emulator layer's texture substitution (see vk_texture.h).
// The wiring mirrors runtime/vulkan/android_vulkan_layer.cpp; "next" is the
// host loader's entry point instead of the next layer's.

#include "vk_texture.h"

#include "log.h"
#include "props.h"

#include "texture/astc_substitution.h"

#include <windows.h>

#include <cstdarg>
#include <cstring>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>

// --- android/log.h and sys/system_properties.h for the shared code -------------------------

int __android_log_print(int priority, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const auto level = priority >= ANDROID_LOG_ERROR  ? axrb::LogLevel::Error
                       : priority == ANDROID_LOG_WARN ? axrb::LogLevel::Warn
                       : priority == ANDROID_LOG_INFO ? axrb::LogLevel::Info
                                                      : axrb::LogLevel::Debug;
    axrb::log_writev(level, tag, format, args);
    va_end(args);
    return 0;
}

int __system_property_get(const char* name, char* value) {
    const std::string v = axrb::property_get(name);
    const size_t n = std::min<size_t>(v.size(), PROP_VALUE_MAX - 1);
    std::memcpy(value, v.data(), n);
    value[n] = 0;
    return static_cast<int>(n);
}

namespace axrb::vktex {
namespace {

using texture::AstcSubstitution;
using texture::DeviceCalls;

Lookup g_lookup = nullptr;

void* key(const void* handle) { return handle ? *reinterpret_cast<void* const*>(handle) : nullptr; }

struct Device {
    VkDevice handle = VK_NULL_HANDLE;
    std::shared_ptr<const DeviceCalls> calls;
};

std::shared_mutex g_lock;
std::map<void*, Device> g_devices;

std::shared_ptr<const DeviceCalls> calls_for(const void* handle) {
    std::shared_lock lock(g_lock);
    auto it = g_devices.find(key(handle));
    return it == g_devices.end() ? nullptr : it->second.calls;
}

// Loader entry points (trampolines, valid for every device), resolved once per
// name: used when a device has no substitution, and for physical devices.
std::shared_mutex g_resolved_lock;
std::map<std::string, PFN_vkVoidFunction> g_resolved;

template <typename F>
F host(const char* name) {
    {
        std::shared_lock lock(g_resolved_lock);
        auto it = g_resolved.find(name);
        if (it != g_resolved.end()) return reinterpret_cast<F>(it->second);
    }
    PFN_vkVoidFunction fn = g_lookup(VK_NULL_HANDLE, name);
    if (fn) {
        std::unique_lock lock(g_resolved_lock);
        g_resolved[name] = fn;
    }
    return reinterpret_cast<F>(fn);
}

template <typename F>
F global(const char* name) {
    return host<F>(name);
}

template <typename T>
T a_(uint64_t v) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<uintptr_t>(v));
    else return static_cast<T>(v);
}

uint64_t r_(VkResult r) { return static_cast<uint64_t>(static_cast<int64_t>(r)); }

AstcSubstitution& images() { return AstcSubstitution::instance(); }

// Like vkSetDeviceLoaderData: dispatchable objects created inside the
// substitution share the device's loader dispatch pointer.
VKAPI_ATTR VkResult VKAPI_CALL set_loader_data(VkDevice device, void* object) {
    *reinterpret_cast<void**>(object) = *reinterpret_cast<void**>(device);
    return VK_SUCCESS;
}

bool target_format(VkFormat format, VkFormat* target) {
    int bw = 0, bh = 0;
    bool srgb = false;
    texture::Codec codec = texture::Codec::None;
    return texture::compressed_format(format, bw, bh, srgb, codec, *target);
}

// --- physical-device queries --------------------------------------------------------------

// Linear compressed images are CPU staging buffers backed by optimal images.
// Advertise the operations implemented by that backing image, rather than the
// desktop driver's (usually empty) native linear compressed-image capabilities.
constexpr VkFormatFeatureFlags kLinearFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags kLinearUsage = VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

uint64_t GetPhysicalDeviceFeatures(const uint64_t* a) {
    auto pd = a_<VkPhysicalDevice>(a[0]);
    auto* out = a_<VkPhysicalDeviceFeatures*>(a[1]);
    global<PFN_vkGetPhysicalDeviceFeatures>("vkGetPhysicalDeviceFeatures")(pd, out);
    if (out->textureCompressionBC) out->textureCompressionASTC_LDR = out->textureCompressionETC2 = VK_TRUE;
    return 0;
}

uint64_t features2(const uint64_t* a, const char* name) {
    auto pd = a_<VkPhysicalDevice>(a[0]);
    auto* out = a_<VkPhysicalDeviceFeatures2*>(a[1]);
    auto next = global<PFN_vkGetPhysicalDeviceFeatures2>(name);
    if (!next) next = global<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2");
    next(pd, out);
    if (out->features.textureCompressionBC)
        out->features.textureCompressionASTC_LDR = out->features.textureCompressionETC2 = VK_TRUE;
    return 0;
}
uint64_t GetPhysicalDeviceFeatures2(const uint64_t* a) { return features2(a, "vkGetPhysicalDeviceFeatures2"); }
uint64_t GetPhysicalDeviceFeatures2KHR(const uint64_t* a) { return features2(a, "vkGetPhysicalDeviceFeatures2KHR"); }

uint64_t GetPhysicalDeviceFormatProperties(const uint64_t* a) {
    auto pd = a_<VkPhysicalDevice>(a[0]);
    auto format = static_cast<VkFormat>(a[1]);
    VkFormat target;
    const bool substituted = target_format(format, &target);
    if (substituted) format = target;
    global<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties")(
        pd, format, a_<VkFormatProperties*>(a[2]));
    if (substituted) {
        auto* out = a_<VkFormatProperties*>(a[2]);
        out->linearTilingFeatures = out->optimalTilingFeatures & kLinearFeatures;
        out->bufferFeatures = 0;
    }
    return 0;
}

uint64_t format_properties2(const uint64_t* a, const char* name) {
    auto pd = a_<VkPhysicalDevice>(a[0]);
    auto format = static_cast<VkFormat>(a[1]);
    VkFormat target;
    const bool substituted = target_format(format, &target);
    if (substituted) format = target;
    auto next = global<PFN_vkGetPhysicalDeviceFormatProperties2>(name);
    if (!next) next = global<PFN_vkGetPhysicalDeviceFormatProperties2>("vkGetPhysicalDeviceFormatProperties2");
    next(pd, format, a_<VkFormatProperties2*>(a[2]));
    if (substituted) {
        auto* out = a_<VkFormatProperties2*>(a[2]);
        out->formatProperties.linearTilingFeatures = out->formatProperties.optimalTilingFeatures & kLinearFeatures;
        out->formatProperties.bufferFeatures = 0;
        for (auto* chain = static_cast<VkBaseOutStructure*>(out->pNext); chain; chain = chain->pNext)
            if (chain->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
                auto* props = reinterpret_cast<VkFormatProperties3*>(chain);
                props->linearTilingFeatures = props->optimalTilingFeatures & kLinearFeatures;
                props->bufferFeatures = 0;
            }
    }
    return 0;
}
uint64_t GetPhysicalDeviceFormatProperties2(const uint64_t* a) {
    return format_properties2(a, "vkGetPhysicalDeviceFormatProperties2");
}
uint64_t GetPhysicalDeviceFormatProperties2KHR(const uint64_t* a) {
    return format_properties2(a, "vkGetPhysicalDeviceFormatProperties2KHR");
}

uint64_t GetPhysicalDeviceImageFormatProperties(const uint64_t* a) {
    auto format = static_cast<VkFormat>(a[1]);
    auto tiling = static_cast<VkImageTiling>(a[3]);
    auto usage = static_cast<VkImageUsageFlags>(a[4]);
    VkFormat target;
    if (target_format(format, &target)) {
        if (a[2] != VK_IMAGE_TYPE_2D && a[2] != VK_IMAGE_TYPE_3D)
            return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
        if (tiling == VK_IMAGE_TILING_LINEAR) {
            if (a[2] != VK_IMAGE_TYPE_2D || (usage & ~kLinearUsage)) return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
            tiling = VK_IMAGE_TILING_OPTIMAL;
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        } else if (tiling != VK_IMAGE_TILING_OPTIMAL) return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
        format = target;
    }
    return r_(global<PFN_vkGetPhysicalDeviceImageFormatProperties>("vkGetPhysicalDeviceImageFormatProperties")(
        a_<VkPhysicalDevice>(a[0]), format, static_cast<VkImageType>(a[2]), tiling,
        usage, static_cast<VkImageCreateFlags>(a[5]),
        a_<VkImageFormatProperties*>(a[6])));
}

uint64_t image_format_properties2(const uint64_t* a, const char* name) {
    auto info = *a_<const VkPhysicalDeviceImageFormatInfo2*>(a[1]);
    VkFormat target;
    if (target_format(info.format, &target)) {
        if (info.type != VK_IMAGE_TYPE_2D && info.type != VK_IMAGE_TYPE_3D)
            return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
        if (info.tiling == VK_IMAGE_TILING_LINEAR) {
            if (info.type != VK_IMAGE_TYPE_2D || (info.usage & ~kLinearUsage)) return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        } else if (info.tiling != VK_IMAGE_TILING_OPTIMAL) return r_(VK_ERROR_FORMAT_NOT_SUPPORTED);
        info.format = target;
    }
    auto next = global<PFN_vkGetPhysicalDeviceImageFormatProperties2>(name);
    if (!next) next = global<PFN_vkGetPhysicalDeviceImageFormatProperties2>("vkGetPhysicalDeviceImageFormatProperties2");
    return r_(next(a_<VkPhysicalDevice>(a[0]), &info, a_<VkImageFormatProperties2*>(a[2])));
}
uint64_t GetPhysicalDeviceImageFormatProperties2(const uint64_t* a) {
    return image_format_properties2(a, "vkGetPhysicalDeviceImageFormatProperties2");
}
uint64_t GetPhysicalDeviceImageFormatProperties2KHR(const uint64_t* a) {
    return image_format_properties2(a, "vkGetPhysicalDeviceImageFormatProperties2KHR");
}

// --- device commands ----------------------------------------------------------------------

#define NEXT(field, name) (c ? c->field : host<decltype(DeviceCalls::field)>(name))

#include "vk_linear.inc"

uint64_t DestroyDevice(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    if (!device) return 0;
    if (calls_for(device)) {
        std::vector<VkImage> linearImages;
        {
            std::lock_guard lock(g_linear_lock);
            for (const auto& [image, linear] : g_linear)
                if (linear->device == device) linearImages.push_back(image);
        }
        for (VkImage image : linearImages) {
            images().destroy_image(device, image, nullptr);
            destroy_linear(device, image);
        }
        images().remove_device(device);
        std::unique_lock lock(g_lock);
        g_devices.erase(key(device));
    }
    host<PFN_vkDestroyDevice>("vkDestroyDevice")(device, nullptr);
    return 0;
}

uint64_t CreateImage(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkImageCreateInfo*>(a[1]);
    auto* out = a_<VkImage*>(a[3]);
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next; next = next->pNext)
        TRACE("vkimage", "image extension sType=%u", next->sType);
    TRACE("vkimage", "create image format=%u type=%u extent=%ux%ux%u mips=%u layers=%u samples=%u flags=0x%x usage=0x%x next=%p",
         info->format, info->imageType, info->extent.width, info->extent.height, info->extent.depth,
         info->mipLevels, info->arrayLayers, info->samples, info->flags, info->usage, info->pNext);
    if (!calls_for(device)) return r_(host<PFN_vkCreateImage>("vkCreateImage")(device, info, nullptr, out));
    VkFormat substituted;
    if (info->tiling == VK_IMAGE_TILING_LINEAR && target_format(info->format, &substituted))
        return r_(create_linear(device, info, out));
    const VkResult result = images().create_image(device, info, nullptr, out);
    TRACE("vkimage", "created image=%p result=%d usage=0x%x", result == VK_SUCCESS ? *out : VK_NULL_HANDLE, result, info->usage);
    if (result != VK_SUCCESS) LOGW("vulkan", "image creation failed %d format=%u tiling=%u", result, info->format, info->tiling);
    return r_(result);
}

uint64_t DestroyImage(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto image = a_<VkImage>(a[1]);
    if (!calls_for(device)) host<PFN_vkDestroyImage>("vkDestroyImage")(device, image, nullptr);
    else images().destroy_image(device, image, nullptr);
    destroy_linear(device, image);
    return 0;
}

uint64_t CreateImageView(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkImageViewCreateInfo*>(a[1]);
    auto* out = a_<VkImageView*>(a[3]);
    if (!calls_for(device)) return r_(host<PFN_vkCreateImageView>("vkCreateImageView")(device, info, nullptr, out));
    return r_(images().create_image_view(device, info, nullptr, out));
}

uint64_t device_image_memory_requirements(const uint64_t* a, const char* name) {
    const auto* info = a_<const VkDeviceImageMemoryRequirements*>(a[1]);
    VkFormat target;
    if (!target_format(info->pCreateInfo->format, &target)) {
        host<PFN_vkGetDeviceImageMemoryRequirements>(name)(
            a_<VkDevice>(a[0]), info, a_<VkMemoryRequirements2*>(a[2]));
        return 0;
    }
    // Maintenance4 queries must use the same substitution and linear staging
    // requirements as normal image creation, never the unsupported ASTC ICD path.
    VkImage image{};
    const uint64_t create[] = {a[0], reinterpret_cast<uint64_t>(info->pCreateInfo), 0, reinterpret_cast<uint64_t>(&image)};
    if (static_cast<int64_t>(CreateImage(create)) != VK_SUCCESS) {
        a_<VkMemoryRequirements2*>(a[2])->memoryRequirements = {};
        return 0;
    }
    VkImageMemoryRequirementsInfo2 query{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
    query.image = image;
    const uint64_t requirements[] = {a[0], reinterpret_cast<uint64_t>(&query), a[2]};
    GetImageMemoryRequirements2(requirements);
    const uint64_t destroy[] = {a[0], reinterpret_cast<uint64_t>(image), 0};
    DestroyImage(destroy);
    return 0;
}
uint64_t GetDeviceImageMemoryRequirements(const uint64_t* a) {
    return device_image_memory_requirements(a, "vkGetDeviceImageMemoryRequirements");
}
uint64_t GetDeviceImageMemoryRequirementsKHR(const uint64_t* a) {
    return device_image_memory_requirements(a, "vkGetDeviceImageMemoryRequirementsKHR");
}

uint64_t CreateBuffer(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkBufferCreateInfo*>(a[1]);
    auto* out = a_<VkBuffer*>(a[3]);
    auto c = calls_for(device);
    const VkResult r = NEXT(createBuffer, "vkCreateBuffer")(device, info, nullptr, out);
    if (r == VK_SUCCESS && c) images().created_buffer(*out, info->size);
    return r_(r);
}

uint64_t DestroyBuffer(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto buffer = a_<VkBuffer>(a[1]);
    auto c = calls_for(device);
    if (c) images().destroyed_buffer(buffer);
    NEXT(destroyBuffer, "vkDestroyBuffer")(device, buffer, nullptr);
    return 0;
}

uint64_t BindBufferMemory(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto buffer = a_<VkBuffer>(a[1]);
    auto memory = a_<VkDeviceMemory>(a[2]);
    auto c = calls_for(device);
    const VkResult r = NEXT(bindBufferMemory, "vkBindBufferMemory")(device, buffer, memory, a[3]);
    if (r == VK_SUCCESS && c) images().bound_buffer(buffer, memory, a[3]);
    return r_(r);
}

uint64_t bind_buffer_memory2(const uint64_t* a, const char* name) {
    auto device = a_<VkDevice>(a[0]);
    const auto count = static_cast<uint32_t>(a[1]);
    auto* infos = a_<const VkBindBufferMemoryInfo*>(a[2]);
    auto c = calls_for(device);
    auto next = host<PFN_vkBindBufferMemory2>(name);
    const VkResult r = next(device, count, infos);
    if (r == VK_SUCCESS && c)
        for (uint32_t i = 0; i < count; ++i) images().bound_buffer(infos[i].buffer, infos[i].memory, infos[i].memoryOffset);
    return r_(r);
}
uint64_t BindBufferMemory2(const uint64_t* a) { return bind_buffer_memory2(a, "vkBindBufferMemory2"); }
uint64_t BindBufferMemory2KHR(const uint64_t* a) { return bind_buffer_memory2(a, "vkBindBufferMemory2KHR"); }

uint64_t AllocateMemory(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkMemoryAllocateInfo*>(a[1]);
    auto* out = a_<VkDeviceMemory*>(a[3]);
    auto c = calls_for(device);
    const VkResult r = NEXT(allocateMemory, "vkAllocateMemory")(device, info, nullptr, out);
    if (r == VK_SUCCESS && c) images().allocated_memory(device, info, *out);
    return r_(r);
}

uint64_t FreeMemory(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto memory = a_<VkDeviceMemory>(a[1]);
    auto c = calls_for(device);
    if (c) images().free_memory(memory);
    NEXT(freeMemory, "vkFreeMemory")(device, memory, nullptr);
    return 0;
}

uint64_t MapMemory(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto memory = a_<VkDeviceMemory>(a[1]);
    auto** out = a_<void**>(a[5]);
    auto c = calls_for(device);
    const VkResult r = NEXT(mapMemory, "vkMapMemory")(device, memory, a[2], a[3], static_cast<VkMemoryMapFlags>(a[4]), out);
    if (r == VK_SUCCESS && c) images().mapped(memory, a[2], *out);
    return r_(r);
}

uint64_t UnmapMemory(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto memory = a_<VkDeviceMemory>(a[1]);
    auto c = calls_for(device);
    if (c) images().unmapped(memory);
    NEXT(unmapMemory, "vkUnmapMemory")(device, memory);
    return 0;
}

uint64_t CreateCommandPool(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkCommandPoolCreateInfo*>(a[1]);
    auto* out = a_<VkCommandPool*>(a[3]);
    const VkResult r = host<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, info, nullptr, out);
    if (r == VK_SUCCESS && calls_for(device)) images().created_pool(*out, info->queueFamilyIndex);
    return r_(r);
}

uint64_t AllocateCommandBuffers(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto* info = a_<const VkCommandBufferAllocateInfo*>(a[1]);
    auto* out = a_<VkCommandBuffer*>(a[2]);
    // Through the loader's trampoline, which sets up the new handles' dispatch.
    const VkResult r = host<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, info, out);
    if (r == VK_SUCCESS && calls_for(device))
        images().allocated_command_buffers(device, info->commandPool, info->commandBufferCount, out,
                                           info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    return r_(r);
}

uint64_t FreeCommandBuffers(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto pool = a_<VkCommandPool>(a[1]);
    const auto count = static_cast<uint32_t>(a[2]);
    auto* buffers = a_<const VkCommandBuffer*>(a[3]);
    if (calls_for(device)) images().freed_command_buffers(count, buffers);
    host<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(device, pool, count, buffers);
    return 0;
}

uint64_t ResetCommandPool(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto pool = a_<VkCommandPool>(a[1]);
    auto c = calls_for(device);
    if (c) images().reset_pool(pool, false);
    return r_(NEXT(resetCommandPool, "vkResetCommandPool")(device, pool, static_cast<VkCommandPoolResetFlags>(a[2])));
}

uint64_t DestroyCommandPool(const uint64_t* a) {
    auto device = a_<VkDevice>(a[0]);
    auto pool = a_<VkCommandPool>(a[1]);
    auto c = calls_for(device);
    if (c) images().reset_pool(pool, true);
    NEXT(destroyCommandPool, "vkDestroyCommandPool")(device, pool, nullptr);
    return 0;
}

uint64_t BeginCommandBuffer(const uint64_t* a) {
    auto command = a_<VkCommandBuffer>(a[0]);
    auto* info = a_<const VkCommandBufferBeginInfo*>(a[1]);
    auto c = calls_for(command);
    if (c) images().reset_command_buffer(command);
    const VkResult r = NEXT(beginCommandBuffer, "vkBeginCommandBuffer")(command, info);
    if (r == VK_SUCCESS && c) images().began(command, info->flags);
    return r_(r);
}

uint64_t ResetCommandBuffer(const uint64_t* a) {
    auto command = a_<VkCommandBuffer>(a[0]);
    auto c = calls_for(command);
    if (c) images().reset_command_buffer(command);
    return r_(NEXT(resetCommandBuffer, "vkResetCommandBuffer")(command, static_cast<VkCommandBufferResetFlags>(a[1])));
}

uint64_t CmdCopyBufferToImage(const uint64_t* a) {
    auto command = a_<VkCommandBuffer>(a[0]);
    auto source = a_<VkBuffer>(a[1]);
    auto target = a_<VkImage>(a[2]);
    auto layout = static_cast<VkImageLayout>(a[3]);
    const auto count = static_cast<uint32_t>(a[4]);
    auto* regions = a_<const VkBufferImageCopy*>(a[5]);
    auto c = calls_for(command);
    if (c && images().copy_buffer_to_image(command, c->device, source, target, layout, count, regions)) return 0;
    NEXT(copyBufferToImage, "vkCmdCopyBufferToImage")(command, source, target, layout, count, regions);
    return 0;
}

uint64_t copy_buffer_to_image2(const uint64_t* a, const char* name) {
    auto command = a_<VkCommandBuffer>(a[0]);
    auto* info = a_<const VkCopyBufferToImageInfo2*>(a[1]);
    auto c = calls_for(command);
    if (c) {
        std::vector<VkBufferImageCopy> regions(info->regionCount);
        for (uint32_t i = 0; i < info->regionCount; ++i) {
            const auto& r = info->pRegions[i];
            regions[i] = {r.bufferOffset, r.bufferRowLength, r.bufferImageHeight, r.imageSubresource, r.imageOffset, r.imageExtent};
        }
        if (images().copy_buffer_to_image(command, c->device, info->srcBuffer, info->dstImage, info->dstImageLayout,
                                          info->regionCount, regions.data()))
            return 0;
    }
    auto next = host<PFN_vkCmdCopyBufferToImage2>(name);
    next(command, info);
    return 0;
}
uint64_t CmdCopyBufferToImage2(const uint64_t* a) { return copy_buffer_to_image2(a, "vkCmdCopyBufferToImage2"); }
uint64_t CmdCopyBufferToImage2KHR(const uint64_t* a) { return copy_buffer_to_image2(a, "vkCmdCopyBufferToImage2KHR"); }

uint64_t CmdExecuteCommands(const uint64_t* a) {
    auto command = a_<VkCommandBuffer>(a[0]);
    const auto count = static_cast<uint32_t>(a[1]);
    auto* secondaries = a_<const VkCommandBuffer*>(a[2]);
    auto c = calls_for(command);
    if (c) images().executed(command, count, secondaries);
    NEXT(executeCommands, "vkCmdExecuteCommands")(command, count, secondaries);
    return 0;
}

uint64_t QueueSubmit(const uint64_t* a) {
    auto queue = a_<VkQueue>(a[0]);
    const auto count = static_cast<uint32_t>(a[1]);
    auto* submits = a_<const VkSubmitInfo*>(a[2]);
    auto fence = a_<VkFence>(a[3]);
    auto c = calls_for(queue);
    auto next = NEXT(queueSubmit, "vkQueueSubmit");
    if (!c) return r_(next(queue, count, submits, fence));
    for (uint32_t i = 0; i < count; ++i) images().before_submit(submits[i].commandBufferCount, submits[i].pCommandBuffers);
    if (!c->computeFamilies) return r_(next(queue, count, submits, fence));
    std::vector<VkSubmitInfo> modified;
    std::vector<std::vector<VkCommandBuffer>> lists(count);
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < submits[i].commandBufferCount; ++j) {
            images().gpu_commands(submits[i].pCommandBuffers[j], lists[i]);
            lists[i].push_back(submits[i].pCommandBuffers[j]);
        }
        if (lists[i].size() != submits[i].commandBufferCount) {
            if (modified.empty()) modified.assign(submits, submits + count);
            modified[i].commandBufferCount = static_cast<uint32_t>(lists[i].size());
            modified[i].pCommandBuffers = lists[i].data();
        }
    }
    return r_(next(queue, count, modified.empty() ? submits : modified.data(), fence));
}

uint64_t queue_submit2(const uint64_t* a, const char* name) {
    auto queue = a_<VkQueue>(a[0]);
    const auto count = static_cast<uint32_t>(a[1]);
    auto* submits = a_<const VkSubmitInfo2*>(a[2]);
    auto fence = a_<VkFence>(a[3]);
    auto c = calls_for(queue);
    auto next = host<PFN_vkQueueSubmit2>(name);
    if (!c) return r_(next(queue, count, submits, fence));
    for (uint32_t i = 0; i < count; ++i) {
        std::vector<VkCommandBuffer> buffers;
        for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
            buffers.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
        images().before_submit(static_cast<uint32_t>(buffers.size()), buffers.data());
    }
    if (!c->computeFamilies) return r_(next(queue, count, submits, fence));
    std::vector<VkSubmitInfo2> modified;
    std::vector<std::vector<VkCommandBufferSubmitInfo>> lists(count);
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j) {
            const auto& original = submits[i].pCommandBufferInfos[j];
            std::vector<VkCommandBuffer> extra;
            images().gpu_commands(original.commandBuffer, extra);
            for (auto command : extra) {
                VkCommandBufferSubmitInfo item{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
                item.commandBuffer = command;
                item.deviceMask = original.deviceMask;
                lists[i].push_back(item);
            }
            lists[i].push_back(original);
        }
        if (lists[i].size() != submits[i].commandBufferInfoCount) {
            if (modified.empty()) modified.assign(submits, submits + count);
            modified[i].commandBufferInfoCount = static_cast<uint32_t>(lists[i].size());
            modified[i].pCommandBufferInfos = lists[i].data();
        }
    }
    return r_(next(queue, count, modified.empty() ? submits : modified.data(), fence));
}
uint64_t QueueSubmit2(const uint64_t* a) { return queue_submit2(a, "vkQueueSubmit2"); }
uint64_t QueueSubmit2KHR(const uint64_t* a) { return queue_submit2(a, "vkQueueSubmit2KHR"); }

#undef NEXT

}  // namespace

void init(Lookup lookup) { g_lookup = lookup; }

bool enabled() { return texture::astc_to_bc7_enabled(); }

std::vector<std::pair<const char*, Override>> overrides() {
    if (!enabled()) return {};
    return {
        {"vkGetPhysicalDeviceFeatures", GetPhysicalDeviceFeatures},
        {"vkGetPhysicalDeviceFeatures2", GetPhysicalDeviceFeatures2},
        {"vkGetPhysicalDeviceFeatures2KHR", GetPhysicalDeviceFeatures2KHR},
        {"vkGetPhysicalDeviceFormatProperties", GetPhysicalDeviceFormatProperties},
        {"vkGetPhysicalDeviceFormatProperties2", GetPhysicalDeviceFormatProperties2},
        {"vkGetPhysicalDeviceFormatProperties2KHR", GetPhysicalDeviceFormatProperties2KHR},
        {"vkGetPhysicalDeviceImageFormatProperties", GetPhysicalDeviceImageFormatProperties},
        {"vkGetPhysicalDeviceImageFormatProperties2", GetPhysicalDeviceImageFormatProperties2},
        {"vkGetPhysicalDeviceImageFormatProperties2KHR", GetPhysicalDeviceImageFormatProperties2KHR},
        {"vkDestroyDevice", DestroyDevice},
        {"vkCreateImage", CreateImage},
        {"vkDestroyImage", DestroyImage},
        {"vkCreateImageView", CreateImageView},
        {"vkGetImageMemoryRequirements", GetImageMemoryRequirements},
        {"vkGetImageMemoryRequirements2", GetImageMemoryRequirements2},
        {"vkGetImageMemoryRequirements2KHR", GetImageMemoryRequirements2KHR},
        {"vkGetDeviceImageMemoryRequirements", GetDeviceImageMemoryRequirements},
        {"vkGetDeviceImageMemoryRequirementsKHR", GetDeviceImageMemoryRequirementsKHR},
        {"vkBindImageMemory", BindImageMemory},
        {"vkBindImageMemory2", BindImageMemory2},
        {"vkBindImageMemory2KHR", BindImageMemory2KHR},
        {"vkGetImageSubresourceLayout", GetImageSubresourceLayout},
        {"vkCmdPipelineBarrier", CmdPipelineBarrier},
        {"vkCmdPipelineBarrier2", CmdPipelineBarrier2},
        {"vkCmdPipelineBarrier2KHR", CmdPipelineBarrier2KHR},
        {"vkCreateBuffer", CreateBuffer},
        {"vkDestroyBuffer", DestroyBuffer},
        {"vkBindBufferMemory", BindBufferMemory},
        {"vkBindBufferMemory2", BindBufferMemory2},
        {"vkBindBufferMemory2KHR", BindBufferMemory2KHR},
        {"vkAllocateMemory", AllocateMemory},
        {"vkFreeMemory", FreeMemory},
        {"vkMapMemory", MapMemory},
        {"vkUnmapMemory", UnmapMemory},
        {"vkCreateCommandPool", CreateCommandPool},
        {"vkAllocateCommandBuffers", AllocateCommandBuffers},
        {"vkFreeCommandBuffers", FreeCommandBuffers},
        {"vkResetCommandPool", ResetCommandPool},
        {"vkDestroyCommandPool", DestroyCommandPool},
        {"vkBeginCommandBuffer", BeginCommandBuffer},
        {"vkResetCommandBuffer", ResetCommandBuffer},
        {"vkCmdCopyBufferToImage", CmdCopyBufferToImage},
        {"vkCmdCopyBufferToImage2", CmdCopyBufferToImage2},
        {"vkCmdCopyBufferToImage2KHR", CmdCopyBufferToImage2KHR},
        {"vkCmdExecuteCommands", CmdExecuteCommands},
        {"vkQueueSubmit", QueueSubmit},
        {"vkQueueSubmit2", QueueSubmit2},
        {"vkQueueSubmit2KHR", QueueSubmit2KHR},
    };
}

void prepare_device(VkInstance instance, VkPhysicalDevice physical, VkDeviceCreateInfo& info, DeviceFeatures& state) {
    if (!enabled()) return;
    VkPhysicalDeviceFeatures supported{};
    reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(g_lookup(instance, "vkGetPhysicalDeviceFeatures"))(physical, &supported);
    if (!supported.textureCompressionBC) return;
    state.active = true;
    for (auto* node = static_cast<const VkBaseInStructure*>(info.pNext); node; node = node->pNext)
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            state.features2 = reinterpret_cast<VkPhysicalDeviceFeatures2*>(const_cast<VkBaseInStructure*>(node));
    auto adjust = [](VkPhysicalDeviceFeatures& f) {
        f.textureCompressionBC = VK_TRUE;
        f.textureCompressionASTC_LDR = VK_FALSE;
        f.textureCompressionETC2 = VK_FALSE;
    };
    if (state.features2) {
        // Guest memory: restored in finish_device.
        state.saved2 = state.features2->features;
        adjust(state.features2->features);
    } else {
        if (info.pEnabledFeatures) state.plain = *info.pEnabledFeatures;
        adjust(state.plain);
        info.pEnabledFeatures = &state.plain;
    }
}

void finish_device(VkInstance instance, VkPhysicalDevice physical, const VkDeviceCreateInfo&, DeviceFeatures& state,
                   VkResult result, VkDevice device) {
    if (state.features2) state.features2->features = state.saved2;
    if (!state.active || result != VK_SUCCESS) return;
    auto gipa = [&](const char* name) { return g_lookup(instance, name); };
    DeviceCalls calls;
    calls.device = device;
    calls.gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa("vkGetDeviceProcAddr"));
    calls.loader = set_loader_data;
    if (property_get("debug.axrb.astc_gpu") != "0") {
        auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            gipa("vkGetPhysicalDeviceQueueFamilyProperties"));
        uint32_t n = 0;
        query(physical, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        query(physical, &n, families.data());
        for (uint32_t i = 0; i < n && i < 32; ++i)
            if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) calls.computeFamilies |= 1u << i;
        VkPhysicalDeviceProperties props{};
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa("vkGetPhysicalDeviceProperties"))(physical, &props);
        calls.maxStorageRange = props.limits.maxStorageBufferRange;
    }
    reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa("vkGetPhysicalDeviceMemoryProperties"))(physical,
                                                                                                          &calls.memory);
    auto fn = [&](const char* name) { return calls.gdpa(device, name); };
#define AXRB_CALL(field, name) calls.field = reinterpret_cast<decltype(calls.field)>(fn(#name))
    AXRB_CALL(createImage, vkCreateImage);
    AXRB_CALL(destroyImage, vkDestroyImage);
    AXRB_CALL(createImageView, vkCreateImageView);
    AXRB_CALL(createBuffer, vkCreateBuffer);
    AXRB_CALL(destroyBuffer, vkDestroyBuffer);
    AXRB_CALL(bufferRequirements, vkGetBufferMemoryRequirements);
    AXRB_CALL(bindBufferMemory, vkBindBufferMemory);
    AXRB_CALL(bindBufferMemory2, vkBindBufferMemory2);
    AXRB_CALL(allocateMemory, vkAllocateMemory);
    AXRB_CALL(freeMemory, vkFreeMemory);
    AXRB_CALL(mapMemory, vkMapMemory);
    AXRB_CALL(unmapMemory, vkUnmapMemory);
    AXRB_CALL(allocateCommandBuffers, vkAllocateCommandBuffers);
    AXRB_CALL(freeCommandBuffers, vkFreeCommandBuffers);
    AXRB_CALL(resetCommandPool, vkResetCommandPool);
    AXRB_CALL(destroyCommandPool, vkDestroyCommandPool);
    AXRB_CALL(beginCommandBuffer, vkBeginCommandBuffer);
    AXRB_CALL(resetCommandBuffer, vkResetCommandBuffer);
    AXRB_CALL(copyBufferToImage, vkCmdCopyBufferToImage);
    AXRB_CALL(copyBufferToImage2, vkCmdCopyBufferToImage2);
    AXRB_CALL(copyImageToBuffer, vkCmdCopyImageToBuffer);
    AXRB_CALL(executeCommands, vkCmdExecuteCommands);
    AXRB_CALL(queueSubmit, vkQueueSubmit);
    AXRB_CALL(queueSubmit2, vkQueueSubmit2);
#undef AXRB_CALL
    if (!calls.bindBufferMemory2) calls.bindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(fn("vkBindBufferMemory2KHR"));
    if (!calls.copyBufferToImage2)
        calls.copyBufferToImage2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(fn("vkCmdCopyBufferToImage2KHR"));
    if (!calls.queueSubmit2) calls.queueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(fn("vkQueueSubmit2KHR"));
    auto shared = std::make_shared<const DeviceCalls>(calls);
    {
        std::unique_lock lock(g_lock);
        g_devices[key(device)] = {device, shared};
    }
    images().add_device(calls);
}

}  // namespace axrb::vktex
