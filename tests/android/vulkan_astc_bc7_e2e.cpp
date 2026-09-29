// End-to-end check of ASTC textures through the AXRB Vulkan layer.
//
// Uploads an ASTC file (astcenc output) the way an engine does - persistently
// mapped staging buffer, buffer-to-image copy in two regions, submit - then
// reads every texel back through a compute shader (texelFetch) and compares
// it with astcenc's decode of the same file (uncompressed KTX). With the
// layer's BC7 substitution active the texture goes through ASTC -> BC7, so
// the comparison reports PSNR; without it gfxstream decodes the ASTC itself.
// "late" writes the staging bytes only after the copy is recorded, which the
// layer must notice at submit.
//   vulkan_astc_bc7_e2e <file.astc> <reference.ktx> [srgb] [late]
// Run with the layer enabled: setprop debug.vulkan.layers VK_LAYER_AXRB_runtime
// (userdebug image, layer in /data/local/debug/vulkan).
#include <vulkan/vulkan.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>
#include "astc_fetch_spirv.h" // fetch.comp: texelFetch of every texel, packUnorm4x8 into a buffer

#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { std::fprintf(stderr, "%s: %d at line %d\n", #call, r_, __LINE__); return 1; } } while (0)

namespace {
std::vector<uint8_t> read_file(const char* path) {
    std::vector<uint8_t> data;
    if (FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        data.resize(size_t(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        std::fclose(f);
    }
    return data;
}
bool read_ktx(const char* path, uint32_t* width, uint32_t* height, std::vector<uint32_t>* rgba) {
    const std::vector<uint8_t> d = read_file(path);
    if (d.size() < 68) return false;
    uint32_t h[13];
    std::memcpy(h, d.data() + 12, sizeof(h));
    const int components = h[3] == 0x1908 ? 4 : h[3] == 0x1907 ? 3 : 0;
    if (h[1] != 0x1401 || !components) return false;
    *width = h[6];
    *height = h[7];
    const size_t at = 64 + h[12] + 4, rowBytes = (size_t(*width) * components + 3) & ~size_t(3);
    rgba->resize(size_t(*width) * *height);
    for (uint32_t y = 0; y < *height; ++y)
        for (uint32_t x = 0; x < *width; ++x) {
            const uint8_t* p = d.data() + at + y * rowBytes + x * components;
            (*rgba)[size_t(y) * *width + x] = p[0] | p[1] << 8 | p[2] << 16 | uint32_t(components == 4 ? p[3] : 255) << 24;
        }
    return true;
}
double srgb_to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
VkFormat astc_format(int bw, int bh, bool srgb) {
    static const int dims[14][2] = {{4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
                                    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};
    for (int i = 0; i < 14; ++i)
        if (dims[i][0] == bw && dims[i][1] == bh) return VkFormat(157 + i * 2 + (srgb ? 1 : 0));
    return VK_FORMAT_UNDEFINED;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <file.astc> <reference.ktx> [srgb] [late]\n", argv[0]); return 2; }
    bool srgb = false, late = false;
    for (int i = 3; i < argc; ++i) {
        srgb |= !std::strcmp(argv[i], "srgb");
        late |= !std::strcmp(argv[i], "late");
    }
    const std::vector<uint8_t> file = read_file(argv[1]);
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> reference;
    if (file.size() < 16 || !read_ktx(argv[2], &width, &height, &reference)) return 3;
    const int bw = file[4], bh = file[5];
    const VkFormat format = astc_format(bw, bh, srgb);
    const uint32_t rowBlocks = (width + bw - 1) / bw;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t n = 1;
    VkPhysicalDevice physical;
    vkEnumeratePhysicalDevices(instance, &n, &physical);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    auto memoryType = [&](uint32_t bits, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        std::abort();
    };
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.data());
    uint32_t family = 0;
    while (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures features{};
    features.textureCompressionASTC_LDR = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qi;
    dci.pEnabledFeatures = &features;
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    // The texture, as a game creates it.
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {width, height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage image;
    CHECK(vkCreateImage(device, &ii, nullptr, &image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);
    std::printf("%s %ux%u ASTC %dx%d%s: image memory %.2f MiB\n", argv[1], width, height, bw, bh, srgb ? " sRGB" : "", req.size / 1048576.0);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imageMemory;
    CHECK(vkAllocateMemory(device, &mai, nullptr, &imageMemory));
    CHECK(vkBindImageMemory(device, image, imageMemory, 0));

    // Staging buffer, persistently mapped.
    const size_t bytes = file.size() - 16;
    auto makeBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory, void** mapped) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        if (vkCreateBuffer(device, &bi, nullptr, buffer)) return false;
        VkMemoryRequirements r;
        vkGetBufferMemoryRequirements(device, *buffer, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = r.size;
        ai.memoryTypeIndex = memoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        return !vkAllocateMemory(device, &ai, nullptr, memory) && !vkBindBufferMemory(device, *buffer, *memory, 0) &&
               !vkMapMemory(device, *memory, 0, VK_WHOLE_SIZE, 0, mapped);
    };
    VkBuffer staging, output;
    VkDeviceMemory stagingMemory, outputMemory;
    void *stagingMapped, *outputMapped;
    if (!makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMemory, &stagingMapped)) return 4;
    if (!makeBuffer(size_t(width) * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &output, &outputMemory, &outputMapped)) return 4;
    if (!late) std::memcpy(stagingMapped, file.data() + 16, bytes);
    else std::memset(stagingMapped, 0, bytes);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    // Two regions split on a row that is a multiple of the block height and of 4.
    const uint32_t unit = uint32_t(std::lcm(bh, 4));
    const uint32_t split = height > unit ? (height / 2) / unit * unit : height;
    VkBufferImageCopy regions[2]{};
    regions[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    regions[0].imageExtent = {width, split, 1};
    regions[1] = regions[0];
    regions[1].bufferOffset = VkDeviceSize(split / bh) * rowBlocks * 16;
    regions[1].imageOffset = {0, int32_t(split), 0};
    regions[1].imageExtent = {width, height - split, 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, split < height ? 2 : 1, regions);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    CHECK(vkEndCommandBuffer(cmd));
    if (late) std::memcpy(stagingMapped, file.data() + 16, bytes);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fci, nullptr, &fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, ~0ull));

    // Read every texel back.
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    CHECK(vkCreateImageView(device, &vci, nullptr, &view));
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkSampler sampler;
    CHECK(vkCreateSampler(device, &sci, nullptr, &sampler));
    VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 2;
    dlci.pBindings = bindings;
    VkDescriptorSetLayout setLayout;
    CHECK(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &setLayout));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push;
    VkPipelineLayout pipelineLayout;
    CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout));
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(kFetchSpirv);
    smci.pCode = kFetchSpirv;
    VkShaderModule module;
    CHECK(vkCreateShaderModule(device, &smci, nullptr, &module));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    cpci.layout = pipelineLayout;
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = sizes;
    VkDescriptorPool descriptorPool;
    CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout;
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(device, &dsai, &set));
    VkDescriptorImageInfo imageInfo{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo bufferInfo{output, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, nullptr, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    CHECK(vkResetCommandBuffer(cmd, 0));
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
    const uint32_t constants[3] = {width, height, 0};
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, constants);
    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    CHECK(vkEndCommandBuffer(cmd));
    CHECK(vkResetFences(device, 1, &fence));
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, ~0ull));

    // Compare, in linear space for sRGB (texelFetch converts).
    const uint32_t* got = static_cast<const uint32_t*>(outputMapped);
    double squared = 0;
    int worst = 0;
    size_t exact = 0, worstAt = 0, large = 0;
    for (size_t i = 0; i < reference.size(); ++i) {
        exact += got[i] == reference[i];
        for (int c = 0; c < 4; ++c) {
            double want = double((reference[i] >> (c * 8)) & 255);
            if (srgb && c < 3) want = std::round(srgb_to_linear(want / 255.0) * 255.0);
            const double d = double((got[i] >> (c * 8)) & 255) - want;
            squared += d * d;
            if (int(std::fabs(d)) > worst) { worst = int(std::fabs(d)); worstAt = i; }
            if (std::fabs(d) > 32) ++large;
        }
    }
    const double mse = squared / (double(reference.size()) * 4);
    const double psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99;
    std::printf("texels identical to the reference: %.2f%%, PSNR %.2f dB, worst channel difference %d at (%zu, %zu), "
                "%zu channel values off by more than 32\n",
                100.0 * double(exact) / double(reference.size()), psnr, worst, worstAt % width, worstAt / width, large);
    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return psnr >= 40 ? 0 : 1;
}
