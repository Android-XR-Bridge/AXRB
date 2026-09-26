/* Vulkan through the bridge, the way an engine reaches it: dlopen the
   loader, take vkGetInstanceProcAddr, and ask for everything else by name.
   Then real work on the host GPU: fill a buffer with a command and read the
   result back through mapped memory, which only works if a pointer the host
   driver returns is one the guest can use. */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

void qb_check(const char* label, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

static PFN_vkGetInstanceProcAddr gipa;

int qb_guest_main(void) {
    void* loader = dlopen("libvulkan.so", RTLD_NOW);
    CHECK("dlopen libvulkan", loader != 0, 1);
    gipa = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
    CHECK("vkGetInstanceProcAddr", gipa != 0, 1);
    if (!gipa) return 1;

    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)gipa(0, "vkCreateInstance");
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "vktest";
    app.apiVersion = VK_API_VERSION_1_1;
    const char* extensions[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = extensions;
    VkInstance instance = 0;
    CHECK("vkCreateInstance", create_instance(&instance_info, 0, &instance), VK_SUCCESS);

#define GET(type, name) PFN_##name name = (PFN_##name)gipa(instance, #name)
    GET(x, vkEnumeratePhysicalDevices);
    GET(x, vkGetPhysicalDeviceProperties);
    GET(x, vkGetPhysicalDeviceQueueFamilyProperties);
    GET(x, vkGetPhysicalDeviceMemoryProperties);
    GET(x, vkCreateDevice);
    GET(x, vkGetDeviceProcAddr);
    CHECK("instance functions", vkEnumeratePhysicalDevices && vkCreateDevice && vkGetDeviceProcAddr, 1);

    uint32_t count = 1;
    VkPhysicalDevice physical = 0;
    VkResult enumerated = vkEnumeratePhysicalDevices(instance, &count, &physical);
    CHECK("a physical device", enumerated == VK_SUCCESS || enumerated == VK_INCOMPLETE, 1);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    CHECK("it has a name", properties.deviceName[0] != 0, 1);

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, 0);
    VkQueueFamilyProperties family_list[16];
    if (families > 16) families = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, family_list);
    uint32_t graphics = 0;
    for (uint32_t i = 0; i < families; ++i)
        if (family_list[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics = i;
            break;
        }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = graphics;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    VkDevice device = 0;
    CHECK("vkCreateDevice", vkCreateDevice(physical, &device_info, 0, &device), VK_SUCCESS);

#define DGET(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(device, #name)
    DGET(vkGetDeviceQueue);
    DGET(vkCreateBuffer);
    DGET(vkGetBufferMemoryRequirements);
    DGET(vkAllocateMemory);
    DGET(vkBindBufferMemory);
    DGET(vkMapMemory);
    DGET(vkCreateCommandPool);
    DGET(vkAllocateCommandBuffers);
    DGET(vkBeginCommandBuffer);
    DGET(vkCmdFillBuffer);
    DGET(vkEndCommandBuffer);
    DGET(vkQueueSubmit);
    DGET(vkQueueWaitIdle);
    DGET(vkCmdSetLineWidth);
    DGET(vkDestroyDevice);
    CHECK("device functions", vkCmdFillBuffer && vkQueueSubmit && vkMapMemory, 1);

    VkQueue queue = 0;
    vkGetDeviceQueue(device, graphics, 0, &queue);

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = 4096;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer = 0;
    CHECK("vkCreateBuffer", vkCreateBuffer(device, &buffer_info, 0, &buffer), VK_SUCCESS);
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(device, buffer, &needs);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = 0;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((needs.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & want) == want) {
            type = i;
            break;
        }
    }
    VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = needs.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory backing = 0;
    CHECK("vkAllocateMemory", vkAllocateMemory(device, &allocate, 0, &backing), VK_SUCCESS);
    CHECK("vkBindBufferMemory", vkBindBufferMemory(device, buffer, backing, 0), VK_SUCCESS);
    volatile uint32_t* mapped = 0;
    CHECK("vkMapMemory", vkMapMemory(device, backing, 0, 4096, 0, (void**)&mapped), VK_SUCCESS);
    mapped[0] = 0x11111111u;
    mapped[1023] = 0x22222222u;

    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = graphics;
    VkCommandPool pool = 0;
    CHECK("vkCreateCommandPool", vkCreateCommandPool(device, &pool_info, 0, &pool), VK_SUCCESS);
    VkCommandBufferAllocateInfo command_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VkCommandBuffer commands = 0;
    CHECK("vkAllocateCommandBuffers", vkAllocateCommandBuffers(device, &command_info, &commands), VK_SUCCESS);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commands, &begin);
    /* The GPU writes the whole buffer; the CPU's values have to be replaced. */
    vkCmdFillBuffer(commands, buffer, 0, 4096, 0xc0ffee42u);
    vkEndCommandBuffer(commands);
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    CHECK("vkQueueSubmit", vkQueueSubmit(queue, 1, &submit, 0), VK_SUCCESS);
    CHECK("vkQueueWaitIdle", vkQueueWaitIdle(queue), VK_SUCCESS);
    CHECK("gpu wrote the first word", mapped[0], 0xc0ffee42u);
    CHECK("gpu wrote the last word", mapped[1023], 0xc0ffee42u);

    /* A proc lookup for something no host has comes back empty. */
    CHECK("unknown function", gipa(instance, "vkNoSuchFunctionQB") == 0, 1);
    vkDestroyDevice(device, 0);
    return 0;
}
