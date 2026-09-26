/* QB_XR_CAPTURE=directory: saves what the guest submits to the headset.

   Just before each xrEndFrame (the first QB_XR_CAPTURE_FRAMES of them, 8 by
   default), every swapchain's most recently acquired image is copied back from
   the GPU and written as <dir>/frameNNN_scK_WxH.rgba, raw 8-bit RGBA rows, top
   first. tools/xr_frames.py turns those into PNGs. It is a debugging aid for
   seeing whether the guest renders anything sensible when no one is wearing
   the headset.

   The guest's Vulkan handles are the host's own (vulkan.cpp passes them
   through), so the copy uses the device and queue the guest gave the session.
   There are no Vulkan headers in this tree; the few structures needed are
   declared here with their 64-bit layouts. */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "qb_env.h"
#include <windows.h>

#include <openxr/openxr.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

typedef uint64_t VkHandle; /* every handle is 8 bytes on x64, dispatchable or not */

struct VkCommandPoolCreateInfo {
    int32_t sType;
    const void* pNext;
    uint32_t flags;
    uint32_t queueFamilyIndex;
};
struct VkCommandBufferAllocateInfo {
    int32_t sType;
    const void* pNext;
    VkHandle commandPool;
    int32_t level;
    uint32_t commandBufferCount;
};
struct VkCommandBufferBeginInfo {
    int32_t sType;
    const void* pNext;
    uint32_t flags;
    const void* pInheritanceInfo;
};
struct VkBufferCreateInfo {
    int32_t sType;
    const void* pNext;
    uint32_t flags;
    uint64_t size;
    uint32_t usage;
    int32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
};
struct VkMemoryRequirements {
    uint64_t size;
    uint64_t alignment;
    uint32_t memoryTypeBits;
};
struct VkMemoryAllocateInfo {
    int32_t sType;
    const void* pNext;
    uint64_t allocationSize;
    uint32_t memoryTypeIndex;
};
struct VkMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
};
struct VkMemoryHeap {
    uint64_t size;
    uint32_t flags;
};
struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};
struct VkImageSubresourceRange {
    uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount;
};
struct VkImageMemoryBarrier {
    int32_t sType;
    const void* pNext;
    uint32_t srcAccessMask, dstAccessMask;
    int32_t oldLayout, newLayout;
    uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex;
    VkHandle image;
    VkImageSubresourceRange subresourceRange;
};
struct VkBufferImageCopy {
    uint64_t bufferOffset;
    uint32_t bufferRowLength, bufferImageHeight;
    uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount;
    int32_t x, y, z;
    uint32_t width, height, depth;
};
struct VkSubmitInfo {
    int32_t sType;
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const VkHandle* pWaitSemaphores;
    const uint32_t* pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkHandle* pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkHandle* pSignalSemaphores;
};

/* XR_KHR_vulkan_enable structures, laid out as openxr_platform.h has them. */
struct GraphicsBindingVulkan {
    XrStructureType type;
    const void* next;
    VkHandle instance, physicalDevice, device;
    uint32_t queueFamilyIndex, queueIndex;
};
struct SwapchainImageVulkan {
    XrStructureType type;
    void* next;
    VkHandle image;
};

struct Swapchain {
    uint32_t width = 0, height = 0;
    int64_t format = 0;
    std::vector<VkHandle> images;
    int acquired = -1;
};

std::mutex g_lock;
std::string g_dir;
int g_frames_left = 0;
int g_frame = 0;
GraphicsBindingVulkan g_binding{};
std::unordered_map<uint64_t, Swapchain> g_swapchains;

bool enabled() {
    static const bool on = [] {
        const char* dir = QB_ENV("QB_XR_CAPTURE");
        if (!dir) return false;
        g_dir = dir;
        CreateDirectoryA(dir, nullptr);
        const char* frames = QB_ENV("QB_XR_CAPTURE_FRAMES");
        g_frames_left = frames ? std::atoi(frames) : 8;
        return true;
    }();
    return on;
}

template <typename T>
T vk(const char* name) {
    static HMODULE library = LoadLibraryA("vulkan-1.dll");
    return library ? reinterpret_cast<T>(GetProcAddress(library, name)) : nullptr;
}

/* Copies one image (layer 0, mip 0) into host memory. The image was released
   by the guest, so it is in COLOR_ATTACHMENT_OPTIMAL, which is where it is
   left again. */
bool read_image(VkHandle image, uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
    auto get_queue = vk<void (*)(VkHandle, uint32_t, uint32_t, VkHandle*)>("vkGetDeviceQueue");
    auto create_pool = vk<int32_t (*)(VkHandle, const VkCommandPoolCreateInfo*, const void*, VkHandle*)>("vkCreateCommandPool");
    auto destroy_pool = vk<void (*)(VkHandle, VkHandle, const void*)>("vkDestroyCommandPool");
    auto allocate_cmd = vk<int32_t (*)(VkHandle, const VkCommandBufferAllocateInfo*, VkHandle*)>("vkAllocateCommandBuffers");
    auto begin = vk<int32_t (*)(VkHandle, const VkCommandBufferBeginInfo*)>("vkBeginCommandBuffer");
    auto end = vk<int32_t (*)(VkHandle)>("vkEndCommandBuffer");
    auto barrier = vk<void (*)(VkHandle, uint32_t, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*,
                               uint32_t, const VkImageMemoryBarrier*)>("vkCmdPipelineBarrier");
    auto copy = vk<void (*)(VkHandle, VkHandle, int32_t, VkHandle, uint32_t, const VkBufferImageCopy*)>(
        "vkCmdCopyImageToBuffer");
    auto submit = vk<int32_t (*)(VkHandle, uint32_t, const VkSubmitInfo*, VkHandle)>("vkQueueSubmit");
    auto wait = vk<int32_t (*)(VkHandle)>("vkQueueWaitIdle");
    auto create_buffer = vk<int32_t (*)(VkHandle, const VkBufferCreateInfo*, const void*, VkHandle*)>("vkCreateBuffer");
    auto destroy_buffer = vk<void (*)(VkHandle, VkHandle, const void*)>("vkDestroyBuffer");
    auto requirements = vk<void (*)(VkHandle, VkHandle, VkMemoryRequirements*)>("vkGetBufferMemoryRequirements");
    auto properties = vk<void (*)(VkHandle, VkPhysicalDeviceMemoryProperties*)>("vkGetPhysicalDeviceMemoryProperties");
    auto allocate = vk<int32_t (*)(VkHandle, const VkMemoryAllocateInfo*, const void*, VkHandle*)>("vkAllocateMemory");
    auto free_memory = vk<void (*)(VkHandle, VkHandle, const void*)>("vkFreeMemory");
    auto bind = vk<int32_t (*)(VkHandle, VkHandle, VkHandle, uint64_t)>("vkBindBufferMemory");
    auto map = vk<int32_t (*)(VkHandle, VkHandle, uint64_t, uint64_t, uint32_t, void**)>("vkMapMemory");
    if (!get_queue || !create_pool || !copy || !map) return false;

    VkHandle device = g_binding.device;
    uint64_t bytes = (uint64_t)width * height * 4;

    VkBufferCreateInfo buffer_info{12, nullptr, 0, bytes, 0x2 /* TRANSFER_DST */, 0, 0, nullptr};
    VkHandle buffer = 0;
    if (create_buffer(device, &buffer_info, nullptr, &buffer) != 0) return false;
    VkMemoryRequirements need{};
    requirements(device, buffer, &need);
    VkPhysicalDeviceMemoryProperties memory{};
    properties(g_binding.physicalDevice, &memory);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((need.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & 0x6) == 0x6) { /* visible+coherent */
            type = i;
            break;
        }
    VkHandle backing = 0;
    VkMemoryAllocateInfo allocate_info{5, nullptr, need.size, type};
    if (type == UINT32_MAX || allocate(device, &allocate_info, nullptr, &backing) != 0) {
        destroy_buffer(device, buffer, nullptr);
        return false;
    }
    bind(device, buffer, backing, 0);

    VkHandle queue = 0;
    get_queue(device, g_binding.queueFamilyIndex, g_binding.queueIndex, &queue);
    VkCommandPoolCreateInfo pool_info{39, nullptr, 0x1 /* TRANSIENT */, g_binding.queueFamilyIndex};
    VkHandle pool = 0;
    create_pool(device, &pool_info, nullptr, &pool);
    VkCommandBufferAllocateInfo cmd_info{40, nullptr, pool, 0, 1};
    VkHandle cmd = 0;
    allocate_cmd(device, &cmd_info, &cmd);
    VkCommandBufferBeginInfo begin_info{42, nullptr, 0x1 /* ONE_TIME_SUBMIT */, nullptr};
    begin(cmd, &begin_info);
    VkImageMemoryBarrier to_src{45, nullptr, 0x100, 0x800, 2 /* COLOR_ATTACHMENT */, 6 /* TRANSFER_SRC */,
                                ~0u, ~0u, image, {1, 0, 1, 0, 1}};
    barrier(cmd, 0x400, 0x1000, 0, 0, nullptr, 0, nullptr, 1, &to_src);
    VkBufferImageCopy region{0, 0, 0, 1, 0, 0, 1, 0, 0, 0, width, height, 1};
    copy(cmd, image, 6, buffer, 1, &region);
    VkImageMemoryBarrier back{45, nullptr, 0x800, 0x100, 6, 2, ~0u, ~0u, image, {1, 0, 1, 0, 1}};
    barrier(cmd, 0x1000, 0x400, 0, 0, nullptr, 0, nullptr, 1, &back);
    end(cmd);
    VkSubmitInfo submit_info{4, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    bool ok = submit(queue, 1, &submit_info, 0) == 0 && wait(queue) == 0;

    void* mapped = nullptr;
    if (ok && map(device, backing, 0, bytes, 0, &mapped) == 0 && mapped) {
        out.assign(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + bytes);
    } else {
        ok = false;
    }
    destroy_pool(device, pool, nullptr);
    destroy_buffer(device, buffer, nullptr);
    free_memory(device, backing, nullptr);
    return ok;
}

}  // namespace

/* Called after a passed-through OpenXR call returns, with its arguments. */
void xr_capture_note(const std::string& name, const uint64_t* a, int64_t result) {
    if (!enabled() || result < 0) return;
    std::lock_guard<std::mutex> held(g_lock);
    if (name == "xrCreateSession") {
        const XrSessionCreateInfo* info = reinterpret_cast<const XrSessionCreateInfo*>(a[1]);
        for (const XrBaseInStructure* at = reinterpret_cast<const XrBaseInStructure*>(info ? info->next : nullptr); at;
             at = at->next)
            if (at->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR)
                g_binding = *reinterpret_cast<const GraphicsBindingVulkan*>(at);
    } else if (name == "xrCreateSwapchain") {
        const XrSwapchainCreateInfo* info = reinterpret_cast<const XrSwapchainCreateInfo*>(a[1]);
        Swapchain& chain = g_swapchains[*reinterpret_cast<const uint64_t*>(a[2])];
        chain.width = info->width;
        chain.height = info->height;
        chain.format = info->format;
    } else if (name == "xrEnumerateSwapchainImages") {
        uint32_t capacity = (uint32_t)a[1];
        const uint32_t* count = reinterpret_cast<const uint32_t*>(a[2]);
        const SwapchainImageVulkan* images = reinterpret_cast<const SwapchainImageVulkan*>(a[3]);
        if (!capacity || !count || !images || images[0].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) return;
        Swapchain& chain = g_swapchains[a[0]];
        chain.images.clear();
        for (uint32_t i = 0; i < *count; ++i) chain.images.push_back(images[i].image);
    } else if (name == "xrAcquireSwapchainImage") {
        const uint32_t* index = reinterpret_cast<const uint32_t*>(a[2]);
        if (index) g_swapchains[a[0]].acquired = (int)*index;
    } else if (name == "xrDestroySwapchain") {
        g_swapchains.erase(a[0]);
    }
}

/* Called just before xrEndFrame goes to the runtime. */
void xr_capture_before_end() {
    if (!enabled()) return;
    std::lock_guard<std::mutex> held(g_lock);
    if (g_frames_left <= 0 || !g_binding.device) return;
    --g_frames_left;
    int k = 0;
    for (auto& entry : g_swapchains) {
        Swapchain& chain = entry.second;
        if (chain.acquired < 0 || chain.acquired >= (int)chain.images.size()) continue;
        std::vector<uint8_t> pixels;
        bool ok = read_image(chain.images[(size_t)chain.acquired], chain.width, chain.height, pixels);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/frame%03d_sc%d_%ux%u_f%lld.rgba", g_dir.c_str(), g_frame, k,
                      chain.width, chain.height, (long long)chain.format);
        if (ok) {
            if (FILE* out = std::fopen(path, "wb")) {
                std::fwrite(pixels.data(), 1, pixels.size(), out);
                std::fclose(out);
            }
        }
        std::printf("capture: frame %d swapchain %d (%ux%u) %s\n", g_frame, k, chain.width, chain.height,
                    ok ? path : "could not be read back");
        ++k;
    }
    ++g_frame;
}
