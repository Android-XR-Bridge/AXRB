// Runs tools/astc_bc3_spirv.h (the GPU ASTC -> BC3 transcoder) on a desktop Vulkan GPU and compares it with the
// CPU path (astc_decode.h + texture_transcode.h encode_bc3):
//   decode-only mode must match astc::decode_block exactly;
//   BC3 output must decode to (nearly) the same pixels as the CPU encoder's output.
// Random legal blocks for every footprint, over a region whose size is not a multiple of the tile or footprint.
// Build (Windows): cl /std:c++17 /O2 /EHsc /I <vulkan headers> tests\astc_gpu_check.cpp
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "../../runtime/vulkan/texture/astc_bc3_spirv.h"
#include "reference_astc_decode.h"
#include "../../runtime/vulkan/texture/etc2_to_bc.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { std::printf("%s failed: %d\n", #x, r_); std::exit(1); } } while (0)

struct Gpu {
    PFN_vkGetInstanceProcAddr gipa;
    VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue; uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memory;
    template <class T> T instanceFn(const char* n) { return reinterpret_cast<T>(gipa(instance, n)); }
    PFN_vkGetDeviceProcAddr gdpa;
    template <class T> T fn(const char* n) { return reinterpret_cast<T>(gdpa(device, n)); }
};

struct HostBuffer { VkBuffer buffer; VkDeviceMemory memory; uint32_t* data; };
HostBuffer makeBuffer(Gpu& g, VkDeviceSize size) {
    HostBuffer b{};
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size; info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    CHECK(g.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(g.device, &info, nullptr, &b.buffer));
    VkMemoryRequirements req; g.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(g.device, b.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = req.size;
    for (uint32_t t = 0; t < g.memory.memoryTypeCount; ++t) {
        const auto need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((req.memoryTypeBits & (1u << t)) && (g.memory.memoryTypes[t].propertyFlags & need) == need) { alloc.memoryTypeIndex = t; break; }
    }
    CHECK(g.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(g.device, &alloc, nullptr, &b.memory));
    CHECK(g.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(g.device, b.buffer, b.memory, 0));
    void* data; CHECK(g.fn<PFN_vkMapMemory>("vkMapMemory")(g.device, b.memory, 0, VK_WHOLE_SIZE, 0, &data));
    b.data = static_cast<uint32_t*>(data);
    return b;
}

struct Push { uint32_t sourceOffset, destinationOffset, rowBlocks, sliceBlocks, blocksX, blocksY, outBlocksX, outBlocksY, width, height, format; };

// BC3 block -> 16 RGBA8 texels (reference decoder, like the GPU's sampler).
void decodeBc3(const uint8_t* b, uint8_t out[16][4]) {
    int a[8] = {b[0], b[1]};
    if (a[0] > a[1]) for (int k = 2; k < 8; ++k) a[k] = ((8 - k) * a[0] + (k - 1) * a[1]) / 7;
    else { for (int k = 2; k < 6; ++k) a[k] = ((6 - k) * a[0] + (k - 1) * a[1]) / 5; a[6] = 0; a[7] = 255; }
    uint64_t abits = 0; for (int i = 0; i < 6; ++i) abits |= uint64_t(b[2 + i]) << (8 * i);
    const uint16_t c0 = b[8] | b[9] << 8, c1 = b[10] | b[11] << 8;
    auto unpack = [](uint16_t v, int* c) { c[0] = (v >> 11) << 3 | (v >> 13); c[1] = (v >> 5 & 63) << 2 | (v >> 9 & 3); c[2] = (v & 31) << 3 | (v >> 2 & 7); };
    int p[4][3]; unpack(c0, p[0]); unpack(c1, p[1]);
    for (int k = 0; k < 3; ++k) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
    const uint32_t cbits = b[12] | b[13] << 8 | b[14] << 16 | uint32_t(b[15]) << 24;
    for (int i = 0; i < 16; ++i) {
        const int ci = cbits >> (2 * i) & 3;
        for (int k = 0; k < 3; ++k) out[i][k] = static_cast<uint8_t>(p[ci][k]);
        out[i][3] = static_cast<uint8_t>(a[abits >> (3 * i) & 7]);
    }
}

int main() {
    HMODULE loader = LoadLibraryA("vulkan-1.dll");
    if (!loader) { std::printf("no vulkan-1.dll\n"); return 1; }
    Gpu g{};
    g.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    CHECK(reinterpret_cast<PFN_vkCreateInstance>(g.gipa(nullptr, "vkCreateInstance"))(&ici, nullptr, &g.instance));
    uint32_t count = 8; VkPhysicalDevice devices[8];
    CHECK(g.instanceFn<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(g.instance, &count, devices));
    g.physical = devices[0];
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p; g.instanceFn<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(devices[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { g.physical = devices[i]; std::printf("GPU: %s\n", p.deviceName); break; }
    }
    g.instanceFn<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(g.physical, &g.memory);
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    CHECK(g.instanceFn<PFN_vkCreateDevice>("vkCreateDevice")(g.physical, &dci, nullptr, &g.device));
    g.gdpa = g.instanceFn<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    g.fn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(g.device, 0, 0, &g.queue);

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(axrb::shaders::kAstcBc3); smci.pCode = axrb::shaders::kAstcBc3;
    VkShaderModule module; CHECK(g.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(g.device, &smci, nullptr, &module));
    VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
                                               {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dslci.bindingCount = 2; dslci.pBindings = bindings;
    VkDescriptorSetLayout setLayout; CHECK(g.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(g.device, &dslci, nullptr, &setLayout));
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &setLayout; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &range;
    VkPipelineLayout layout; CHECK(g.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(g.device, &plci, nullptr, &layout));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
    cpci.layout = layout;
    VkPipeline pipeline; CHECK(g.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));

    const int W = 250, H = 170;  // not a multiple of 16 or of any footprint
    const int footprints[][2] = {{4, 4}, {5, 5}, {6, 6}, {8, 5}, {8, 8}, {10, 10}, {12, 12}};
    std::mt19937 rng(3);
    HostBuffer src = makeBuffer(g, 1 << 20), dst = makeBuffer(g, 1 << 20);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    VkDescriptorPool pool; CHECK(g.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(g.device, &dpci, nullptr, &pool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &setLayout;
    VkDescriptorSet set; CHECK(g.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(g.device, &dsai, &set));
    VkDescriptorBufferInfo infos[2] = {{src.buffer, 0, VK_WHOLE_SIZE}, {dst.buffer, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[2] = {};
    for (int i = 0; i < 2; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstSet = set; writes[i].dstBinding = i;
        writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &infos[i];
    }
    g.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(g.device, 2, writes, 0, nullptr);
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmdPool; CHECK(g.fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(g.device, &cpi, nullptr, &cmdPool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cbai.commandPool = cmdPool; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd; CHECK(g.fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(g.device, &cbai, &cmd));

    auto run = [&](const Push& push, uint32_t groupsX, uint32_t groupsY) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(g.fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cmd, &begin));
        g.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        g.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        g.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        g.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(cmd, groupsX, groupsY, 1);
        CHECK(g.fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        CHECK(g.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(g.queue, 1, &submit, VK_NULL_HANDLE));
        CHECK(g.fn<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(g.queue));
    };

    bool ok = true;
    for (auto& f : footprints) for (int srgb = 0; srgb < 2; ++srgb) {
        const int bw = f[0], bh = f[1], nx = (W + bw - 1) / bw, ny = (H + bh - 1) / bh;
        const int rowBlocks = nx + 1;  // a padded source row, like bufferRowLength
        std::vector<uint8_t> blocks(size_t(rowBlocks) * ny * 16, 0);
        uint8_t texels[144][4];
        for (int by = 0; by < ny; ++by) for (int bx = 0; bx < nx; ++bx) {
            uint8_t* b = &blocks[(size_t(by) * rowBlocks + bx) * 16];
            do { for (int i = 0; i < 16; ++i) b[i] = static_cast<uint8_t>(rng()); }
            while (!refract::astc::decode_block(b, bw, bh, srgb, texels) && rng() % 8);  // mostly legal, some error blocks
            if (rng() % 16 == 0) {  // void extent (LDR)
                std::memset(b, 0xFF, 16); b[0] = 0xFC; b[1] = 0xFD;
                for (int i = 8; i < 16; ++i) b[i] = static_cast<uint8_t>(rng());
            }
        }
        // CPU reference image.
        std::vector<uint8_t> image(size_t(W) * H * 4);
        for (int by = 0; by < ny; ++by) for (int bx = 0; bx < nx; ++bx) {
            refract::astc::decode_block(&blocks[(size_t(by) * rowBlocks + bx) * 16], bw, bh, srgb, texels);
            for (int y = 0; y < bh && by * bh + y < H; ++y) for (int x = 0; x < bw && bx * bw + x < W; ++x)
                std::memcpy(&image[(size_t(by * bh + y) * W + bx * bw + x) * 4], texels[y * bw + x], 4);
        }
        std::memcpy(src.data + 4, blocks.data(), blocks.size());  // at a non-zero offset
        const uint32_t outX = (W + 3) / 4, outY = (H + 3) / 4;
        Push push{4, 8, uint32_t(rowBlocks), uint32_t(rowBlocks * ny), uint32_t(nx), uint32_t(ny), outX, outY, W, H,
                  uint32_t(bw | bh << 8 | srgb << 16 | 1 << 17)};
        run(push, (W + 15) / 16, (H + 15) / 16);
        int decodeDiff = 0;
        for (int i = 0; i < W * H; ++i) for (int c = 0; c < 4; ++c)
            decodeDiff = std::max(decodeDiff, std::abs(int(reinterpret_cast<uint8_t*>(dst.data + 8)[i * 4 + c]) - image[i * 4 + c]));
        push.format &= ~(1u << 17);
        run(push, (W + 15) / 16, (H + 15) / 16);
        const uint8_t* gpuBc = reinterpret_cast<uint8_t*>(dst.data + 8);
        // CPU encoder on the same pixels (edge texels repeated), then both decoded and compared.
        int identical = 0; double gpuErr = 0, cpuErr = 0; int maxDiff = 0;
        for (uint32_t by = 0; by < outY; ++by) for (uint32_t bx = 0; bx < outX; ++bx) {
            uint8_t px[16][4], cpuBc[16], a[16][4], b[16][4];
            for (int i = 0; i < 16; ++i) {
                const int x = std::min<int>(bx * 4 + (i & 3), W - 1), y = std::min<int>(by * 4 + (i >> 2), H - 1);
                std::memcpy(px[i], &image[(size_t(y) * W + x) * 4], 4);
            }
            axrb::texture::encode_bc3(px, cpuBc);
            const uint8_t* gb = gpuBc + (by * outX + bx) * 16;
            identical += !std::memcmp(gb, cpuBc, 16);
            decodeBc3(gb, a); decodeBc3(cpuBc, b);
            for (int i = 0; i < 16; ++i) for (int c = 0; c < 4; ++c) {
                gpuErr += std::pow(a[i][c] - px[i][c], 2); cpuErr += std::pow(b[i][c] - px[i][c], 2);
                maxDiff = std::max(maxDiff, std::abs(a[i][c] - b[i][c]));
            }
        }
        const double n = double(outX) * outY * 64;
        const double gpuPsnr = 10 * std::log10(255.0 * 255.0 / std::max(gpuErr / n, 1e-9));
        const double cpuPsnr = 10 * std::log10(255.0 * 255.0 / std::max(cpuErr / n, 1e-9));
        const bool pass = decodeDiff == 0 && gpuPsnr >= cpuPsnr - 0.1;
        ok &= pass;
        std::printf("%2dx%-2d %s: decode max diff %d; BC3 identical %.1f%%, PSNR gpu %.2f / cpu %.2f dB, max texel diff %d  %s\n",
                    bw, bh, srgb ? "srgb" : "unorm", decodeDiff, 100.0 * identical / (outX * outY), gpuPsnr, cpuPsnr, maxDiff, pass ? "ok" : "FAIL");
    }
    std::printf(ok ? "GPU ASTC check passed\n" : "GPU ASTC check FAILED\n");
    return ok ? 0 : 1;
}
