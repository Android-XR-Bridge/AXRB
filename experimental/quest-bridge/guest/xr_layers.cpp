// Portable composition for runtimes without Quest's panorama/image transforms.
// No CPU readback: snapshot acquired color images, then reproject on the GPU.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_VULKAN
// Platform types must precede the OpenXR platform header.
#include <windows.h>
#include <vulkan/vulkan.h>

#include "xr_layers.h"
#include "vk_queue.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <openxr/openxr_platform.h>
#include <set>
#include <stdexcept>

namespace {
// clang-format off
#define VK_FUNCTIONS(X) \
    X(GetDeviceQueue) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdBlitImage) \
    X(QueueSubmit) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(DeviceWaitIdle) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
// clang-format on
struct Vulkan {
#define DECLARE(name) PFN_vk##name name{};
    VK_FUNCTIONS(DECLARE)
#undef DECLARE
    Vulkan() {
        auto module = LoadLibraryW(L"vulkan-1.dll");
#define LOAD(name)                                                                                           \
    name = reinterpret_cast<PFN_vk##name>(GetProcAddress(module, "vk" #name));                               \
    if (!name)                                                                                               \
        throw std::runtime_error("missing vk" #name);
        if (!module)
            throw std::runtime_error("Vulkan loader unavailable");
        VK_FUNCTIONS(LOAD)
#undef LOAD
    }
};
Vulkan &vk() {
    static Vulkan api;
    return api;
}
void check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(result));
}
void check(XrResult result) {
    if (XR_FAILED(result))
        throw std::runtime_error("OpenXR result " + std::to_string(result));
}
PFN_xrGetInstanceProcAddr getProc{};
XrInstance instance{};
std::set<std::string> enabled;
template <typename T> T xr(const char *name) {
    PFN_xrVoidFunction fn{};
    if (!getProc || XR_FAILED(getProc(instance, name, &fn)) || !fn)
        throw std::runtime_error(name);
    return reinterpret_cast<T>(fn);
}
#define XR(name) xr<PFN_xr##name>("xr" #name)
std::mutex mutex;
struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    std::vector<VkImageView> views;
};
struct Params {
    XrQuaternionf camera{0, 0, 0, 1}, inverseSphere{0, 0, 0, 1};
    std::array<float, 4> originRadius{0, 0, 0, -1}, tangents{}, anglesFlags{};
    std::array<float, 4> rect{0, 0, 1, 1}, colorScale{1, 1, 1, 1}, colorBias{};
};
static_assert(sizeof(Params) == 128);
struct Target {
    XrSwapchain swapchain{};
    uint32_t width{}, height{}, slices{};
    std::vector<XrSwapchainImageVulkanKHR> images;
    Image scratch;
    bool scratchInitialized{};
    uint32_t pendingIndex{};
    bool acquired{}, waited{};
    std::vector<bool> initialized;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
};
struct Session {
    XrSession session{};
    XrGraphicsBindingVulkanKHR binding{};
    VkQueue queue{};
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkFence fence{};
    VkDescriptorSetLayout descriptorLayout{};
    VkDescriptorPool descriptorPool{};
    VkDescriptorSet descriptor{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkSampler sampler{};
    uint32_t width{}, height{};
    std::map<uint32_t, Target> targets;
    bool failed{};
    VkDevice device() const { return binding.device; }
    void releaseViews(std::vector<VkImageView> &views) {
        for (auto view : views)
            vk().DestroyImageView(device(), view, nullptr);
        views.clear();
    }
    void destroy(Image &image) {
        releaseViews(image.views);
        if (image.image)
            vk().DestroyImage(device(), image.image, nullptr);
        if (image.memory)
            vk().FreeMemory(device(), image.memory, nullptr);
        image = {};
    }
    void destroy(Target &target) {
        destroy(target.scratch);
        if (target.swapchain) {
            try {
                XR(DestroySwapchain)(target.swapchain);
            } catch (...) {
            }
        }
        target = {};
    }
    ~Session() {
        if (!device())
            return;
        std::lock_guard<std::recursive_mutex> queueLock(guest_vulkan_queue_mutex());
        vk().DeviceWaitIdle(device());
        for (auto &[slot, target] : targets)
            destroy(target);
        if (pipeline)
            vk().DestroyPipeline(device(), pipeline, nullptr);
        if (pipelineLayout)
            vk().DestroyPipelineLayout(device(), pipelineLayout, nullptr);
        if (descriptorPool)
            vk().DestroyDescriptorPool(device(), descriptorPool, nullptr);
        if (descriptorLayout)
            vk().DestroyDescriptorSetLayout(device(), descriptorLayout, nullptr);
        if (sampler)
            vk().DestroySampler(device(), sampler, nullptr);
        if (fence)
            vk().DestroyFence(device(), fence, nullptr);
        if (pool)
            vk().DestroyCommandPool(device(), pool, nullptr);
    }
    std::vector<VkImageView> imageViews(VkImage image, VkFormat format, uint32_t slices) {
        std::vector<VkImageView> result;
        try {
            for (uint32_t slice = 0; slice < slices; ++slice) {
                VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                info.image = image;
                info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                info.format = format;
                info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, slice, 1};
                VkImageView view{};
                check(vk().CreateImageView(device(), &info, nullptr, &view));
                result.push_back(view);
            }
        } catch (...) {
            releaseViews(result);
            throw;
        }
        return result;
    }
    void allocateImage(Image &image, VkFormat format, uint32_t w, uint32_t h, uint32_t slices,
                       VkImageUsageFlags usage) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {w, h, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = slices;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        check(vk().CreateImage(device(), &ci, nullptr, &image.image));
        VkMemoryRequirements need{};
        vk().GetImageMemoryRequirements(device(), image.image, &need);
        VkPhysicalDeviceMemoryProperties properties{};
        vk().GetPhysicalDeviceMemoryProperties(binding.physicalDevice, &properties);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((need.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                type = i;
                break;
            }
        if (type == UINT32_MAX)
            throw std::runtime_error("no compositor GPU memory type");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = need.size;
        ai.memoryTypeIndex = type;
        check(vk().AllocateMemory(device(), &ai, nullptr, &image.memory));
        check(vk().BindImageMemory(device(), image.image, image.memory, 0));
        image.views = imageViews(image.image, format, slices);
    }
    void init() {
        if (pool)
            return;
        vk().GetDeviceQueue(device(), binding.queueFamilyIndex, binding.queueIndex, &queue);
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.queueFamilyIndex = binding.queueFamilyIndex;
        check(vk().CreateCommandPool(device(), &ci, nullptr, &pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        check(vk().AllocateCommandBuffers(device(), &ai, &cmd));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vk().CreateFence(device(), &fi, nullptr, &fence));
    }
    void begin() {
        if (failed)
            throw std::runtime_error("compositor device previously failed");
        init();
        check(vk().ResetCommandPool(device(), pool, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vk().BeginCommandBuffer(cmd, &bi));
    }
    void submit() {
        std::lock_guard<std::recursive_mutex> queueLock(guest_vulkan_queue_mutex());
        try {
            check(vk().EndCommandBuffer(cmd));
            check(vk().ResetFences(device(), 1, &fence));
            VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            info.commandBufferCount = 1;
            info.pCommandBuffers = &cmd;
            check(vk().QueueSubmit(queue, 1, &info, fence));
            check(vk().WaitForFences(device(), 1, &fence, VK_TRUE, 5'000'000'000ull));
        } catch (...) {
            failed = true;
            throw;
        }
    }
    void barrier(VkImage image, uint32_t slices, VkImageLayout before, VkImageLayout after) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = before;
        b.newLayout = after;
        b.srcAccessMask =
            before == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, slices};
        vk().CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void initPipeline() {
        if (pipeline)
            return;
        VkFormatProperties properties{};
        vk().GetPhysicalDeviceFormatProperties(binding.physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            throw std::runtime_error("RGBA8 storage images unavailable");
        wchar_t module[MAX_PATH]{};
        GetModuleFileNameW(nullptr, module, MAX_PATH);
        std::wstring file(module);
        file = file.substr(0, file.find_last_of(L"\\/") + 1) + L"xr_layers.spv";
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("xr_layers.spv missing");
        auto bytes = input.tellg();
        if (bytes <= 0 || bytes % 4)
            throw std::runtime_error("invalid compositor shader");
        std::vector<uint32_t> code(size_t(bytes) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char *>(code.data()), bytes);
        VkDescriptorSetLayoutBinding bindings[2] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 2;
        li.pBindings = bindings;
        check(vk().CreateDescriptorSetLayout(device(), &li, nullptr, &descriptorLayout));
        VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1;
        pi.poolSizeCount = 2;
        pi.pPoolSizes = sizes;
        check(vk().CreateDescriptorPool(device(), &pi, nullptr, &descriptorPool));
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descriptorLayout;
        check(vk().AllocateDescriptorSets(device(), &ai, &descriptor));
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Params)};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &descriptorLayout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &range;
        check(vk().CreatePipelineLayout(device(), &pli, nullptr, &pipelineLayout));
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = size_t(bytes);
        si.pCode = code.data();
        VkShaderModule shader{};
        check(vk().CreateShaderModule(device(), &si, nullptr, &shader));
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = shader;
        ci.stage.pName = "main";
        ci.layout = pipelineLayout;
        auto result = vk().CreateComputePipelines(device(), VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
        vk().DestroyShaderModule(device(), shader, nullptr);
        check(result);
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vk().CreateSampler(device(), &samplerInfo, nullptr, &sampler));
    }
    Target &target(uint32_t slot, uint32_t w, uint32_t h, uint32_t slices) {
        auto &t = targets[slot];
        if (t.swapchain && (t.width != w || t.height != h || t.slices != slices))
            destroy(t);
        if (t.swapchain)
            return t;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        // SteamVR may offer only sRGB color swapchains. Compute into a private
        // linear image, then blit (including Vulkan's sRGB conversion) to a
        // format explicitly offered by the runtime. No storage usage on sRGB.
        uint32_t formatsCount{};
        check(XR(EnumerateSwapchainFormats)(session, 0, &formatsCount, nullptr));
        std::vector<int64_t> formats(formatsCount);
        check(XR(EnumerateSwapchainFormats)(session, formatsCount, &formatsCount, formats.data()));
        for (auto candidate : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_FORMAT_B8G8R8A8_UNORM}) {
            VkFormatProperties properties{};
            vk().GetPhysicalDeviceFormatProperties(binding.physicalDevice, candidate, &properties);
            if (std::find(formats.begin(), formats.end(), candidate) != formats.end() &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
                info.format = candidate;
                break;
            }
        }
        if (!info.format)
            throw std::runtime_error("runtime has no supported compositor color format");
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.width = w;
        info.height = h;
        info.arraySize = slices;
        info.sampleCount = info.faceCount = info.mipCount = 1;
        try {
            check(XR(CreateSwapchain)(session, &info, &t.swapchain));
            t.width = w;
            t.height = h;
            t.slices = slices;
            uint32_t count{};
            check(XR(EnumerateSwapchainImages)(t.swapchain, 0, &count, nullptr));
            t.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            check(XR(EnumerateSwapchainImages)(
                t.swapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader *>(t.images.data())));
            t.initialized.resize(count);
            allocateImage(t.scratch, VK_FORMAT_R8G8B8A8_UNORM, w, h, slices,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            std::printf("openxr: GPU layer conversion slot %u, %ux%u x %u\n", slot, w, h, slices);
        } catch (...) {
            destroy(t);
            throw;
        }
        return t;
    }
};
struct Source {
    XrSession session{};
    XrSwapchainCreateInfo info{};
    std::vector<XrSwapchainImageVulkanKHR> images;
    std::deque<uint32_t> acquired;
    bool waited{}, valid{};
    Image mirror;
};
std::map<XrSession, std::unique_ptr<Session>> sessions;
std::map<XrSwapchain, Source> sources;

void snapshot(Session &s, Source &source) {
    const auto &info = source.info;
    if (!(info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) ||
        (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        return;
    if (!source.waited || source.acquired.empty() || source.acquired.front() >= source.images.size())
        throw std::runtime_error("swapchain image not waited");
    if (info.sampleCount != 1 || info.faceCount != 1 || !info.arraySize)
        throw std::runtime_error("unsupported compositor source dimensions");
    auto &mirror = source.mirror;
    if (!mirror.image) {
        s.allocateImage(mirror, VkFormat(info.format), info.width, info.height, info.arraySize,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    s.begin();
    auto image = source.images[source.acquired.front()].image;
    s.barrier(image, info.arraySize, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    s.barrier(mirror.image, info.arraySize,
              source.valid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, info.arraySize};
    copy.extent = {info.width, info.height, 1};
    vk().CmdCopyImage(s.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mirror.image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    s.barrier(image, info.arraySize, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    s.barrier(mirror.image, info.arraySize, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    s.submit();
    source.valid = true;
}

struct Transform {
    bool flip{}, color{}, unknown{};
    XrColor4f scale{1, 1, 1, 1}, bias{};
};
Transform transform(const void *chain) {
    Transform t;
    auto *next = static_cast<const XrBaseInStructure *>(chain);
    unsigned count = 0;
    for (; next && count < 32; next = next->next, ++count) {
        if (next->type == XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB) {
            auto flags = reinterpret_cast<const XrCompositionLayerImageLayoutFB *>(next)->flags;
            if (flags & ~XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB)
                throw std::runtime_error("unsupported image layout flag");
            t.flip = (flags & XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB) != 0;
        } else if (next->type == XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR) {
            auto *color = reinterpret_cast<const XrCompositionLayerColorScaleBiasKHR *>(next);
            t.scale = color->colorScale;
            t.bias = color->colorBias;
            t.color = t.scale.r != 1 || t.scale.g != 1 || t.scale.b != 1 || t.scale.a != 1 || t.bias.r != 0 ||
                      t.bias.g != 0 || t.bias.b != 0 || t.bias.a != 0;
        } else
            t.unknown = true;
    }
    if (next)
        throw std::runtime_error("cyclic or excessive layer extension chain");
    return t;
}
Params params(const XrSwapchainSubImage &sub, const Transform &t, XrCompositionLayerFlags flags) {
    auto found = sources.find(sub.swapchain);
    if (found == sources.end() || !found->second.valid) {
        static unsigned reports = 0;
        if (found != sources.end() && reports++ < 8) {
            const auto &s = found->second;
            std::fprintf(stderr,
                         "openxr: missing snapshot for %llx: %ux%u format %lld usage %llx images %zu\n",
                         (unsigned long long)sub.swapchain, s.info.width, s.info.height,
                         (long long)s.info.format, (unsigned long long)s.info.usageFlags, s.images.size());
        }
        throw std::runtime_error("layer has no released image snapshot");
    }
    const auto &source = found->second;
    const auto &r = sub.imageRect;
    if (sub.imageArrayIndex >= source.mirror.views.size() || r.offset.x < 0 || r.offset.y < 0 ||
        r.extent.width <= 0 || r.extent.height <= 0 ||
        uint64_t(r.offset.x) + r.extent.width > source.info.width ||
        uint64_t(r.offset.y) + r.extent.height > source.info.height)
        throw std::runtime_error("layer subimage outside swapchain");
    Params p;
    p.anglesFlags[3] =
        float((t.flip ? 1 : 0) | ((flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) ? 2 : 0));
    p.rect = {float(r.offset.x) / source.info.width, float(r.offset.y) / source.info.height,
              float(r.extent.width) / source.info.width, float(r.extent.height) / source.info.height};
    p.colorScale = {t.scale.r, t.scale.g, t.scale.b, t.scale.a};
    p.colorBias = {t.bias.r, t.bias.g, t.bias.b, t.bias.a};
    return p;
}
void render(Session &s, Target &t, const std::array<XrSwapchainSubImage, 2> &inputs,
            const std::array<Params, 2> &parameters) {
    s.initPipeline();
    if (!t.acquired) {
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        check(XR(AcquireSwapchainImage)(t.swapchain, &ai, &t.pendingIndex));
        t.acquired = true;
    }
    const auto index = t.pendingIndex;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = 5'000'000'000ll;
    if (!t.waited) {
        auto waited = XR(WaitSwapchainImage)(t.swapchain, &wi);
        if (waited != XR_SUCCESS)
            throw std::runtime_error("compositor image wait returned " + std::to_string(waited));
        t.waited = true;
    }
    try {
        if (index >= t.images.size())
            throw std::runtime_error("compositor acquire index out of bounds");
        // One descriptor set, updated only after the previous dispatch fence completed.
        for (uint32_t eye = 0; eye < t.slices; ++eye) {
            const auto &sub = inputs[eye];
            auto &source = sources.at(sub.swapchain);
            VkDescriptorImageInfo images[2] = {
                {s.sampler, source.mirror.views.at(sub.imageArrayIndex),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE, t.scratch.views[eye], VK_IMAGE_LAYOUT_GENERAL}};
            VkWriteDescriptorSet writes[2]{};
            for (uint32_t i = 0; i < 2; ++i) {
                writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[i].dstSet = s.descriptor;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType =
                    i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &images[i];
            }
            vk().UpdateDescriptorSets(s.device(), 2, writes, 0, nullptr);
            s.begin();
            if (eye == 0)
                s.barrier(t.scratch.image, t.slices,
                          t.scratchInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                               : VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_GENERAL);
            vk().CmdBindPipeline(s.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline);
            vk().CmdBindDescriptorSets(s.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipelineLayout, 0, 1,
                                       &s.descriptor, 0, nullptr);
            vk().CmdPushConstants(s.cmd, s.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Params),
                                  &parameters[eye]);
            vk().CmdDispatch(s.cmd, (t.width + 7) / 8, (t.height + 7) / 8, 1);
            if (eye + 1 == t.slices) {
                s.barrier(t.scratch.image, t.slices, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                s.barrier(t.images[index].image, t.slices,
                          t.initialized[index] ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                               : VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkImageBlit blit{};
                blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, t.slices};
                blit.srcOffsets[1] = blit.dstOffsets[1] = {int32_t(t.width), int32_t(t.height), 1};
                vk().CmdBlitImage(s.cmd, t.scratch.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  t.images[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                                  VK_FILTER_NEAREST);
                s.barrier(t.images[index].image, t.slices, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
            s.submit();
        }
        t.initialized[index] = true;
        t.scratchInitialized = true;
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        check(XR(ReleaseSwapchainImage)(t.swapchain, &ri));
        t.acquired = t.waited = false;
    } catch (...) {
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        try {
            if (XR(ReleaseSwapchainImage)(t.swapchain, &ri) == XR_SUCCESS)
                t.acquired = t.waited = false;
        } catch (...) {
        }
        throw;
    }
}

XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    XrVector3f t{2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * t.x + q.y * t.z - q.z * t.y, v.y + q.w * t.y + q.z * t.x - q.x * t.z,
            v.z + q.w * t.z + q.x * t.y - q.y * t.x};
}
} // namespace

void xr_layers_instance(PFN_xrGetInstanceProcAddr get, XrInstance current,
                        const std::vector<const char *> &extensions) {
    std::lock_guard<std::mutex> held(mutex);
    getProc = get;
    instance = current;
    enabled.clear();
    for (auto *name : extensions)
        enabled.insert(name);
}
void xr_layers_note(const std::string &name, const uint64_t *a, XrResult result) {
    if (XR_FAILED(result))
        return;
    std::lock_guard<std::mutex> held(mutex);
    try {
        if (name == "xrCreateSession") {
            auto *info = reinterpret_cast<const XrSessionCreateInfo *>(a[1]);
            for (auto *next = static_cast<const XrBaseInStructure *>(info->next); next; next = next->next)
                if (next->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
                    auto s = std::make_unique<Session>();
                    s->session = *reinterpret_cast<const XrSession *>(a[2]);
                    s->binding = *reinterpret_cast<const XrGraphicsBindingVulkanKHR *>(next);
                    std::array<XrViewConfigurationView, 2> views{
                        {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
                    uint32_t count{};
                    check(XR(EnumerateViewConfigurationViews)(instance, info->systemId,
                                                              XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                                              &count, views.data()));
                    s->width = views[0].recommendedImageRectWidth;
                    s->height = views[0].recommendedImageRectHeight;
                    sessions[s->session] = std::move(s);
                    break;
                }
        } else if (name == "xrCreateSwapchain") {
            Source source;
            source.session = reinterpret_cast<XrSession>(a[0]);
            source.info = *reinterpret_cast<const XrSwapchainCreateInfo *>(a[1]);
            sources[*reinterpret_cast<const XrSwapchain *>(a[2])] = std::move(source);
        } else if (name == "xrEnumerateSwapchainImages" && a[1] && a[3]) {
            auto *images = reinterpret_cast<const XrSwapchainImageVulkanKHR *>(a[3]);
            if (images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR)
                sources[reinterpret_cast<XrSwapchain>(a[0])].images.assign(
                    images, images + *reinterpret_cast<const uint32_t *>(a[2]));
        } else if (name == "xrAcquireSwapchainImage") {
            auto &source = sources[reinterpret_cast<XrSwapchain>(a[0])];
            source.acquired.push_back(*reinterpret_cast<const uint32_t *>(a[2]));
        } else if (name == "xrWaitSwapchainImage" && result == XR_SUCCESS)
            sources[reinterpret_cast<XrSwapchain>(a[0])].waited = true;
        else if (name == "xrReleaseSwapchainImage") {
            auto &source = sources[reinterpret_cast<XrSwapchain>(a[0])];
            source.waited = false;
            if (!source.acquired.empty())
                source.acquired.pop_front();
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "openxr: compositor tracking: %s\n", e.what());
    }
}
XrResult xr_layers_before_release(XrSwapchain swapchain) {
    std::lock_guard<std::mutex> held(mutex);
    try {
        auto found = sources.find(swapchain);
        if (found == sources.end())
            return XR_SUCCESS;
        auto session = sessions.find(found->second.session);
        if (session == sessions.end())
            return XR_SUCCESS;
        snapshot(*session->second, found->second);
        return XR_SUCCESS;
    } catch (const std::exception &e) {
        auto found = sources.find(swapchain);
        if (found != sources.end())
            found->second.valid = false;
        static unsigned reports = 0;
        if (reports++ < 8)
            std::fprintf(stderr, "openxr: compositor snapshot: %s\n", e.what());
        return XR_ERROR_RUNTIME_FAILURE;
    }
}
void xr_layers_destroy(const std::string &name, uint64_t handle) {
    std::lock_guard<std::mutex> held(mutex);
    {
        std::lock_guard<std::recursive_mutex> queueLock(guest_vulkan_queue_mutex());
        for (auto &[id, s] : sessions)
            if (s->failed)
                vk().DeviceWaitIdle(s->device());
    }
    for (auto it = sources.begin(); it != sources.end();) {
        bool remove = name == "xrDestroyInstance" ||
                      (name == "xrDestroySwapchain" && uint64_t(it->first) == handle) ||
                      (name == "xrDestroySession" && uint64_t(it->second.session) == handle);
        if (remove) {
            auto s = sessions.find(it->second.session);
            if (s != sessions.end())
                s->second->destroy(it->second.mirror);
            it = sources.erase(it);
        } else
            ++it;
    }
    if (name == "xrDestroyInstance")
        sessions.clear();
    else if (name == "xrDestroySession")
        sessions.erase(reinterpret_cast<XrSession>(handle));
}
XrResult xr_layers_end(XrSession session, const XrFrameEndInfo *frame) {
    std::lock_guard<std::mutex> held(mutex);
    try {
        if (!frame || frame->layerCount > 64 || (frame->layerCount && !frame->layers))
            return XR_ERROR_VALIDATION_FAILURE;
        std::vector<const XrCompositionLayerBaseHeader *> layers;
        std::vector<XrCompositionLayerProjection> projections(frame->layerCount);
        std::vector<XrCompositionLayerQuad> quads(frame->layerCount);
        for (uint32_t slot = 0; slot < frame->layerCount; ++slot) {
            auto *layer = frame->layers[slot];
            if (!layer)
                return XR_ERROR_LAYER_INVALID;
            const auto t = transform(layer->next);
            const bool panorama = layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR &&
                                  !enabled.count("XR_KHR_composition_layer_equirect2");
            const bool convert = panorama || t.flip || t.color;
            if (!convert) {
                if (t.unknown || !layer->next)
                    layers.push_back(layer);
                else if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                    projections[slot] = *reinterpret_cast<const XrCompositionLayerProjection *>(layer);
                    projections[slot].next = nullptr;
                    layers.push_back(
                        reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projections[slot]));
                } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                    quads[slot] = *reinterpret_cast<const XrCompositionLayerQuad *>(layer);
                    quads[slot].next = nullptr;
                    layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quads[slot]));
                } else
                    layers.push_back(layer);
                continue;
            }
            if (t.unknown)
                throw std::runtime_error("unknown extension on converted layer");
            auto found = sessions.find(session);
            if (found == sessions.end())
                throw std::runtime_error("layer conversion requires Vulkan session");
            auto &s = *found->second;
            if (layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) {
                const auto &eq = *reinterpret_cast<const XrCompositionLayerEquirect2KHR *>(layer);
                if (!std::isfinite(eq.radius) || eq.radius < 0 || eq.centralHorizontalAngle <= 0 ||
                    eq.upperVerticalAngle <= eq.lowerVerticalAngle)
                    return XR_ERROR_LAYER_INVALID;
                XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
                li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                li.displayTime = frame->displayTime;
                li.space = eq.space;
                XrViewState state{XR_TYPE_VIEW_STATE};
                std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
                uint32_t count{};
                check(XR(LocateViews)(session, &li, &state, 2, &count, views.data()));
                if (count != 2)
                    return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
                auto &target = s.target(slot, s.width, s.height, 2);
                std::array<XrSwapchainSubImage, 2> inputs{eq.subImage, eq.subImage};
                std::array<Params, 2> p;
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    p[eye] = params(eq.subImage, t, eq.layerFlags);
                    p[eye].camera = views[eye].pose.orientation;
                    auto q = eq.pose.orientation;
                    p[eye].inverseSphere = {-q.x, -q.y, -q.z, q.w};
                    auto c = views[eye].pose.position, origin = eq.pose.position;
                    auto position =
                        rotate(p[eye].inverseSphere, {c.x - origin.x, c.y - origin.y, c.z - origin.z});
                    p[eye].originRadius = {position.x, position.y, position.z, eq.radius};
                    auto f = views[eye].fov;
                    p[eye].tangents = {std::tan(f.angleLeft), std::tan(f.angleRight), std::tan(f.angleUp),
                                       std::tan(f.angleDown)};
                    p[eye].anglesFlags[0] = eq.centralHorizontalAngle;
                    p[eye].anglesFlags[1] = eq.upperVerticalAngle;
                    p[eye].anglesFlags[2] = eq.lowerVerticalAngle;
                    if ((eq.eyeVisibility == XR_EYE_VISIBILITY_LEFT && eye == 1) ||
                        (eq.eyeVisibility == XR_EYE_VISIBILITY_RIGHT && eye == 0))
                        p[eye].anglesFlags[3] = float(int(p[eye].anglesFlags[3]) | 4);
                    auto &view = target.projectionViews[eye];
                    view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                    view.pose = views[eye].pose;
                    view.fov = f;
                    view.subImage = {
                        target.swapchain, {{0, 0}, {int32_t(target.width), int32_t(target.height)}}, eye};
                }
                render(s, target, inputs, p);
                target.projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
                target.projection.space = eq.space;
                target.projection.layerFlags =
                    eq.layerFlags | XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                target.projection.viewCount = 2;
                target.projection.views = target.projectionViews.data();
                layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&target.projection));
            } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                const auto &quad = *reinterpret_cast<const XrCompositionLayerQuad *>(layer);
                auto p = params(quad.subImage, t, quad.layerFlags);
                auto &target = s.target(slot, quad.subImage.imageRect.extent.width,
                                        quad.subImage.imageRect.extent.height, 1);
                render(s, target, {quad.subImage, quad.subImage}, {p, p});
                target.quad = quad;
                target.quad.next = nullptr;
                target.quad.subImage = {target.swapchain, {{0, 0}, quad.subImage.imageRect.extent}, 0};
                layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&target.quad));
            } else
                throw std::runtime_error("image transform for this layer type is not implemented");
        }
        XrFrameEndInfo translated = *frame;
        translated.layerCount = uint32_t(layers.size());
        translated.layers = layers.data();
        return XR(EndFrame)(session, &translated);
    } catch (const std::exception &e) {
        static unsigned reports = 0;
        if (reports++ < 8)
            std::fprintf(stderr, "openxr: layer conversion failed: %s\n", e.what());
        // Close the frame even if conversion failed, without telling the guest
        // that its content was displayed. Otherwise the next WaitFrame can hang.
        if (frame) {
            XrFrameEndInfo empty = *frame;
            empty.layerCount = 0;
            empty.layers = nullptr;
            try {
                XR(EndFrame)(session, &empty);
            } catch (...) {
            }
        }
        return XR_ERROR_RUNTIME_FAILURE;
    }
}
