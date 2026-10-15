#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r); return 1; } } while (0)
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); return 1; } } while (0)

int main(int argc, char** argv) {
    const int khr = argc > 1 && !strcmp(argv[1], "khr");
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = khr ? VK_API_VERSION_1_1 : VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 64;
    VkPhysicalDevice devices[64];
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    REQUIRE(count);
    VkPhysicalDevice physical = devices[0];
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, &formatProperties);
    const VkFormatFeatureFlags linearFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    REQUIRE((formatProperties.linearTilingFeatures & linearFeatures) == linearFeatures);
    VkFormatProperties2 formatProperties2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
    vkGetPhysicalDeviceFormatProperties2(physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, &formatProperties2);
    REQUIRE(formatProperties2.formatProperties.linearTilingFeatures == formatProperties.linearTilingFeatures);
    VkImageFormatProperties linearProperties;
    CHECK(vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        0, &linearProperties));
    VkPhysicalDeviceImageFormatInfo2 linearQuery = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK, .type = VK_IMAGE_TYPE_2D, .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    VkImageFormatProperties2 linearProperties2 = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
    CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &linearQuery, &linearProperties2));
    REQUIRE(linearProperties.maxExtent.width == linearProperties2.imageFormatProperties.maxExtent.width);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, NULL);
    VkQueueFamilyProperties families[64];
    REQUIRE(familyCount && familyCount <= 64);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families);
    uint32_t family = 0;
    while (family < familyCount && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    REQUIRE(family < familyCount);
    float priority = 1;
    VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkPhysicalDeviceSynchronization2Features sync = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
    VkPhysicalDeviceMaintenance4Features maintenance = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES, .pNext = &sync };
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &maintenance };
    vkGetPhysicalDeviceFeatures2(physical, &features);
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue };
    const char* extensions[] = { VK_KHR_MAINTENANCE_4_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME };
    if (khr) { dci.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]); dci.ppEnabledExtensionNames = extensions; }
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    PFN_vkGetDeviceImageMemoryRequirements query = (PFN_vkGetDeviceImageMemoryRequirements)vkGetDeviceProcAddr(device,
        khr ? "vkGetDeviceImageMemoryRequirementsKHR" : "vkGetDeviceImageMemoryRequirements");
    PFN_vkBindImageMemory2 bind = (PFN_vkBindImageMemory2)vkGetDeviceProcAddr(device,
        khr ? "vkBindImageMemory2KHR" : "vkBindImageMemory2");
    REQUIRE(query);
    for (int kind = 0; kind < 4; ++kind) {
        VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = kind == 2 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK, .extent = {16, 16, kind == 2 ? 4 : 1},
            .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = kind == 0 ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = kind == 0 ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED };
        VkFormat formats[] = { VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_4x4_SRGB_BLOCK };
        if (kind == 3) ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        VkImageFormatListCreateInfo list = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
            .viewFormatCount = 2, .pViewFormats = formats };
        if (kind == 1) { ci.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT; ci.pNext = &list; }
        VkMemoryRequirements2 predicted = { .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        VkDeviceImageMemoryRequirements input = { .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS, .pCreateInfo = &ci };
        if (maintenance.maintenance4) {
            query(device, &input, &predicted);
            REQUIRE(predicted.memoryRequirements.size && predicted.memoryRequirements.memoryTypeBits);
        }
        VkImage image;
        CHECK(vkCreateImage(device, &ci, NULL, &image));
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, image, &requirements);
        REQUIRE(requirements.size && requirements.memoryTypeBits);
        if (maintenance.maintenance4) REQUIRE(predicted.memoryRequirements.size == requirements.size);
        uint32_t type = 0;
        while (type < memory.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) ||
                (kind == 0 && !(memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))) ++type;
        REQUIRE(type < memory.memoryTypeCount);
        VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size, .memoryTypeIndex = type };
        VkDeviceMemory backing;
        CHECK(vkAllocateMemory(device, &allocation, NULL, &backing));
        VkBindImageMemoryInfo binding = { .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, .image = image, .memory = backing };
        REQUIRE(bind);
        CHECK(bind(device, 1, &binding));
        if (kind == 0) {
            VkImageSubresource subresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT };
            VkSubresourceLayout layout;
            vkGetImageSubresourceLayout(device, image, &subresource, &layout);
            REQUIRE(layout.rowPitch == 64 && layout.size == 256 && layout.offset + layout.size <= requirements.size);
            void* mapped;
            CHECK(vkMapMemory(device, backing, 0, VK_WHOLE_SIZE, 0, &mapped));
            memset((char*)mapped + layout.offset, 0, layout.size);
            vkUnmapMemory(device, backing);
        }
        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
            .viewType = kind == 2 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D,
            .format = kind == 1 ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : ci.format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
        VkImageView view;
        CHECK(vkCreateImageView(device, &vci, NULL, &view));
        vkDestroyImageView(device, view, NULL);
        vkDestroyImage(device, image, NULL);
        vkFreeMemory(device, backing, NULL);
    }
    REQUIRE(sync.synchronization2);
    PFN_vkCmdPipelineBarrier2 barrier = (PFN_vkCmdPipelineBarrier2)vkGetDeviceProcAddr(device,
        khr ? "vkCmdPipelineBarrier2KHR" : "vkCmdPipelineBarrier2");
    PFN_vkQueueSubmit2 submit = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(device,
        khr ? "vkQueueSubmit2KHR" : "vkQueueSubmit2");
    REQUIRE(barrier && submit);
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(command, &begin));
    VkDependencyInfo dependency = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    barrier(command, &dependency);
    CHECK(vkEndCommandBuffer(command));
    VkQueue q;
    vkGetDeviceQueue(device, family, 0, &q);
    VkCommandBufferSubmitInfo commandInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = command };
    VkSubmitInfo2 submission = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &commandInfo };
    CHECK(submit(q, 1, &submission, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(q));
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    puts("VULKAN_IMAGES_SELFTEST_PASS");
    return 0;
}
