// Standalone GPU regression: exercise the production shader on synthetic images
// without a headset or game. Same translation unit exposes the private renderer.
#include "xr_layers.cpp"
#include <iostream>

template <class T> T api(const char *name) {
    static auto module = LoadLibraryW(L"vulkan-1.dll");
    auto fn = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!fn)
        throw std::runtime_error(name);
    return fn;
}
#define V(name) api<PFN_vk##name>("vk" #name)
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
uint32_t memoryType(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p{};
    vk().GetPhysicalDeviceMemoryProperties(physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("test memory type unavailable");
}
Image makeImage(Session &s, VkImageUsageFlags usage) {
    Image image;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {8, 8, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = usage;
    check(vk().CreateImage(s.device(), &ci, nullptr, &image.image));
    VkMemoryRequirements need{};
    vk().GetImageMemoryRequirements(s.device(), image.image, &need);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = need.size;
    ai.memoryTypeIndex =
        memoryType(s.binding.physicalDevice, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vk().AllocateMemory(s.device(), &ai, nullptr, &image.memory));
    check(vk().BindImageMemory(s.device(), image.image, image.memory, 0));
    image.views = s.imageViews(image.image, VK_FORMAT_R8G8B8A8_UNORM, 1);
    return image;
}
// A narrow fake OpenXR runtime backed by real Vulkan images. It deliberately
// offers only sRGB, reproducing SteamVR's format restriction. All layer
// translation, compute dispatches and linear-to-sRGB blits remain production code.
Session *fakeSession{};
struct FakeChain {
    Image image;
    bool acquired{}, waited{};
};
std::map<XrSwapchain, FakeChain> fakeChains;
uint64_t nextChain = 10;
unsigned submittedLayers{};
bool timeoutNextWait{};
XrResult XRAPI_PTR fakeFormats(XrSession, uint32_t capacity, uint32_t *count, int64_t *formats) {
    *count = 1;
    if (capacity)
        formats[0] = VK_FORMAT_R8G8B8A8_SRGB;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeCreate(XrSession, const XrSwapchainCreateInfo *info, XrSwapchain *out) {
    require(info->format == VK_FORMAT_R8G8B8A8_SRGB, "runtime sRGB format selected");
    require(!(info->usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT), "no storage usage on sRGB");
    FakeChain chain;
    fakeSession->allocateImage(chain.image, VK_FORMAT_R8G8B8A8_SRGB, info->width, info->height,
                               info->arraySize,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    *out = reinterpret_cast<XrSwapchain>(nextChain++);
    fakeChains[*out] = std::move(chain);
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeImages(XrSwapchain chain, uint32_t capacity, uint32_t *count,
                              XrSwapchainImageBaseHeader *out) {
    *count = 1;
    if (capacity)
        reinterpret_cast<XrSwapchainImageVulkanKHR *>(out)->image = fakeChains.at(chain).image.image;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeAcquire(XrSwapchain chain, const XrSwapchainImageAcquireInfo *, uint32_t *index) {
    auto &c = fakeChains.at(chain);
    require(!c.acquired, "acquire order");
    c.acquired = true;
    c.waited = false;
    *index = 0;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeWait(XrSwapchain chain, const XrSwapchainImageWaitInfo *) {
    auto &c = fakeChains.at(chain);
    require(c.acquired && !c.waited, "wait order");
    if (timeoutNextWait) {
        timeoutNextWait = false;
        return XR_TIMEOUT_EXPIRED;
    }
    c.waited = true;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeRelease(XrSwapchain chain, const XrSwapchainImageReleaseInfo *) {
    auto &c = fakeChains.at(chain);
    require(c.acquired && c.waited, "release order");
    c.acquired = c.waited = false;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeDestroy(XrSwapchain chain) {
    fakeSession->destroy(fakeChains.at(chain).image);
    fakeChains.erase(chain);
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeLocate(XrSession, const XrViewLocateInfo *, XrViewState *state, uint32_t capacity,
                              uint32_t *count, XrView *views) {
    require(capacity >= 2, "stereo view capacity");
    *count = 2;
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    for (int i = 0; i < 2; ++i) {
        views[i].pose.orientation = {0, 0, 0, 1};
        views[i].fov = {-.785398f, .785398f, .785398f, -.785398f};
    }
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeEnd(XrSession, const XrFrameEndInfo *frame) {
    submittedLayers = frame->layerCount;
    if (frame->layerCount) {
        require(frame->layerCount == 3, "layer order/count preserved");
        require(frame->layers[0]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION &&
                    frame->layers[1]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION &&
                    frame->layers[2]->type == XR_TYPE_COMPOSITION_LAYER_QUAD,
                "panorama translated, quad retained");
        for (uint32_t i = 0; i < 3; ++i)
            require(!frame->layers[i]->next, "consumed extension chains removed");
        auto *projection = reinterpret_cast<const XrCompositionLayerProjection *>(frame->layers[1]);
        require(projection->viewCount == 2 && projection->views[1].subImage.imageArrayIndex == 1,
                "stereo array selection");
    }
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fakeProc(XrInstance, const char *name, PFN_xrVoidFunction *fn) {
#define FAKE(name_, fn_)                                                                                     \
    if (std::strcmp(name, "xr" #name_) == 0) {                                                               \
        *fn = reinterpret_cast<PFN_xrVoidFunction>(fn_);                                                     \
        return XR_SUCCESS;                                                                                   \
    }
    FAKE(EnumerateSwapchainFormats, fakeFormats);
    FAKE(CreateSwapchain, fakeCreate);
    FAKE(EnumerateSwapchainImages, fakeImages);
    FAKE(AcquireSwapchainImage, fakeAcquire);
    FAKE(WaitSwapchainImage, fakeWait);
    FAKE(ReleaseSwapchainImage, fakeRelease);
    FAKE(DestroySwapchain, fakeDestroy);
    FAKE(LocateViews, fakeLocate);
    FAKE(EndFrame, fakeEnd);
#undef FAKE
    *fn = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
void gpuTests(Session &s) {
    s.init();
    s.initPipeline();
    auto source = makeImage(s, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    auto target = makeImage(s, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 256;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(V(CreateBuffer)(s.device(), &bi, nullptr, &buffer));
    VkMemoryRequirements need{};
    V(GetBufferMemoryRequirements)(s.device(), buffer, &need);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = need.size;
    ai.memoryTypeIndex =
        memoryType(s.binding.physicalDevice, need.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vk().AllocateMemory(s.device(), &ai, nullptr, &memory));
    check(V(BindBufferMemory)(s.device(), buffer, memory, 0));
    void *mapped{};
    check(V(MapMemory)(s.device(), memory, 0, 256, 0, &mapped));
    auto *pixels = static_cast<uint8_t *>(mapped);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            auto i = 4 * (y * 8 + x);
            pixels[i] = uint8_t(x * 30);
            pixels[i + 1] = uint8_t(y * 30);
            pixels[i + 2] = 80;
            pixels[i + 3] = 128;
        }
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {8, 8, 1};
    s.begin();
    s.barrier(source.image, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    V(CmdCopyBufferToImage)(s.cmd, buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    s.barrier(source.image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    s.submit();
    VkDescriptorImageInfo images[2] = {{s.sampler, source.views[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                       {VK_NULL_HANDLE, target.views[0], VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = s.descriptor;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType =
            i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vk().UpdateDescriptorSets(s.device(), 2, writes, 0, nullptr);
    bool initialized = false;
    auto dispatch = [&](const Params &p) {
        s.begin();
        s.barrier(target.image, 1,
                  initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_GENERAL);
        vk().CmdBindPipeline(s.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline);
        vk().CmdBindDescriptorSets(s.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipelineLayout, 0, 1,
                                   &s.descriptor, 0, nullptr);
        vk().CmdPushConstants(s.cmd, s.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p), &p);
        vk().CmdDispatch(s.cmd, 1, 1, 1);
        s.barrier(target.image, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        V(CmdCopyImageToBuffer)(s.cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                                &region);
        s.submit();
        initialized = true;
    };
    Params p;
    p.anglesFlags[3] = 2;
    dispatch(p);
    require(pixels[0] == 0 && pixels[1] == 0 && pixels[4 * 63] == 210 && pixels[4 * 63 + 1] == 210 &&
                pixels[3] == 128,
            "planar copy/alpha");
    p.anglesFlags[3] = 3;
    dispatch(p);
    require(pixels[1] == 210 && pixels[4 * 63 + 1] == 0, "vertical flip");
    p.anglesFlags[3] = 0;
    p.colorScale = {.5f, 1, 1, 1};
    p.colorBias = {.1f, 0, 0, 0};
    dispatch(p);
    require(pixels[3] == 255 && pixels[0] >= 25 && pixels[0] <= 26 && pixels[4 * 63] >= 130 &&
                pixels[4 * 63] <= 131,
            "color transform and opaque alpha");
    p = Params{};
    p.rect = {.5f, 0, .5f, 1};
    p.anglesFlags[3] = 2;
    dispatch(p);
    require(pixels[0] == 120 && pixels[4 * 7] == 210, "crop clamps inside subimage");
    p = Params{};
    p.originRadius[3] = 0;
    p.tangents = {-1, 1, 1, -1};
    p.anglesFlags = {6.2831853f, 1.5707963f, -1.5707963f, 2};
    dispatch(p);
    require(pixels[0] < pixels[4 * 7] && pixels[1] < pixels[4 * 56 + 1] && pixels[3] == 128,
            "panorama ray orientation");
    p.anglesFlags[0] = .2f;
    dispatch(p);
    require(pixels[3] == 0 && pixels[4 * 7 + 3] == 0, "panorama angular bounds transparent");
    p.anglesFlags[3] = 6;
    dispatch(p);
    for (int i = 3; i < 256; i += 4)
        require(pixels[i] == 0, "eye visibility");
    auto session = reinterpret_cast<XrSession>(1);
    auto chain = reinterpret_cast<XrSwapchain>(2);
    auto composed = std::make_unique<Session>();
    composed->binding = s.binding;
    composed->session = session;
    composed->width = composed->height = 8;
    fakeSession = composed.get();
    sessions[session] = std::move(composed);
    Source src;
    src.session = session;
    src.info.width = src.info.height = 8;
    src.info.arraySize = 1;
    src.valid = true;
    src.mirror = std::move(source);
    source = {};
    sources[chain] = std::move(src);
    xr_layers_instance(fakeProc, reinterpret_cast<XrInstance>(1), {});
    XrCompositionLayerColorScaleBiasKHR neutral{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    neutral.colorScale = {1, 1, 1, 1};
    XrCompositionLayerImageLayoutFB flip{XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB};
    flip.next = &neutral;
    flip.flags = XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB;
    XrCompositionLayerProjection original{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    original.next = &neutral;
    XrCompositionLayerEquirect2KHR eq{XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
    eq.next = &flip;
    eq.pose.orientation.w = 1;
    eq.centralHorizontalAngle = 6.2831853f;
    eq.upperVerticalAngle = 1.5707963f;
    eq.lowerVerticalAngle = -1.5707963f;
    eq.subImage = {chain, {{0, 0}, {8, 8}}, 0};
    eq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.next = &flip;
    quad.subImage = eq.subImage;
    quad.layerFlags = eq.layerFlags;
    const XrCompositionLayerBaseHeader *layers[] = {
        reinterpret_cast<XrCompositionLayerBaseHeader *>(&original),
        reinterpret_cast<XrCompositionLayerBaseHeader *>(&eq),
        reinterpret_cast<XrCompositionLayerBaseHeader *>(&quad)};
    XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};
    frame.layerCount = 3;
    frame.layers = layers;
    require(xr_layers_end(session, &frame) == XR_SUCCESS, "translated frame submission");
    require(xr_layers_end(session, &frame) == XR_SUCCESS, "repeated image acquire/render/release");
    timeoutNextWait = true;
    require(xr_layers_end(session, &frame) == XR_ERROR_RUNTIME_FAILURE && submittedLayers == 0,
            "timed-out image wait closes frame");
    require(xr_layers_end(session, &frame) == XR_SUCCESS,
            "retry waits the acquired image without reacquiring");
    require(eq.next == &flip && flip.next == &neutral && quad.subImage.swapchain == chain,
            "guest structures unchanged");
    // Read the real sRGB destination of the transformed quad: row 7's linear
    // green 210 maps to ~234 sRGB, and alpha stays 128.
    auto &output = fakeChains.at(sessions[session]->targets.at(2).swapchain).image;
    s.begin();
    s.barrier(output.image, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    V(CmdCopyImageToBuffer)(s.cmd, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    s.barrier(output.image, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    s.submit();
    require(pixels[1] >= 233 && pixels[1] <= 235 && pixels[3] == 128, "runtime sRGB output conversion");
    XrBaseInStructure unknown{XR_TYPE_UNKNOWN};
    eq.next = &unknown;
    require(xr_layers_end(session, &frame) == XR_ERROR_RUNTIME_FAILURE && submittedLayers == 0,
            "failed conversion closes frame and returns error");
    xr_layers_destroy("xrDestroySession", uint64_t(session));
    require(fakeChains.empty(), "compositor resources destroyed");
    uint32_t first = 1, second = 2;
    uint64_t tracking[3] = {uint64_t(chain), 0, uint64_t(&first)};
    xr_layers_note("xrAcquireSwapchainImage", tracking, XR_SUCCESS);
    tracking[2] = uint64_t(&second);
    xr_layers_note("xrAcquireSwapchainImage", tracking, XR_SUCCESS);
    xr_layers_note("xrWaitSwapchainImage", tracking, XR_TIMEOUT_EXPIRED);
    require(!sources[chain].waited, "guest timeout is not a successful image wait");
    xr_layers_note("xrWaitSwapchainImage", tracking, XR_SUCCESS);
    require(sources[chain].waited && sources[chain].acquired.front() == first,
            "guest images use FIFO acquire order");
    xr_layers_note("xrReleaseSwapchainImage", tracking, XR_SUCCESS);
    require(!sources[chain].waited && sources[chain].acquired.front() == second,
            "release advances acquired images");
    sources.erase(chain);
    V(UnmapMemory)(s.device(), memory);
    V(DestroyBuffer)(s.device(), buffer, nullptr);
    vk().FreeMemory(s.device(), memory, nullptr);
    s.destroy(source);
    s.destroy(target);
}
int main() {
    VkInstance vi{};
    VkDevice device{};
    try {
        VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        check(V(CreateInstance)(&ii, nullptr, &vi));
        uint32_t count{};
        check(V(EnumeratePhysicalDevices)(vi, &count, nullptr));
        require(count > 0, "no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count);
        check(V(EnumeratePhysicalDevices)(vi, &count, devices.data()));
        uint32_t queueCount{};
        V(GetPhysicalDeviceQueueFamilyProperties)(devices[0], &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        V(GetPhysicalDeviceQueueFamilyProperties)(devices[0], &queueCount, queues.data());
        uint32_t family = 0;
        while (family < queueCount && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
            ++family;
        require(family < queueCount, "no compute queue");
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        check(V(CreateDevice)(devices[0], &di, nullptr, &device));
        {
            Session s;
            s.binding = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
            s.binding.instance = vi;
            s.binding.physicalDevice = devices[0];
            s.binding.device = device;
            s.binding.queueFamilyIndex = family;
            gpuTests(s);
        }
        V(DestroyDevice)(device, nullptr);
        V(DestroyInstance)(vi, nullptr);
        std::puts("GPU compositor shader and frame translation checks passed");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "compositor test: %s\n", e.what());
        return 1;
    }
}
