// Times CPU writes into each host-visible Vulkan memory type of the guest
// driver, using the store pattern of Unity's skinning output (16-byte stores
// that advance 12 or 16 bytes), plus a read pass. Compares with plain RAM.
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <immintrin.h>

static double write_pass(void* base, size_t bytes, int repeats) {
    auto start = std::chrono::steady_clock::now();
    const __m128 v = _mm_set1_ps(1.5f);
    for (int r = 0; r < repeats; ++r) {
        char* p = static_cast<char*>(base);
        char* end = p + bytes - 64;
        while (p < end) {
            _mm_storeu_ps(reinterpret_cast<float*>(p), v); p += 12;
            _mm_storeu_ps(reinterpret_cast<float*>(p), v); p += 12;
            _mm_storeu_ps(reinterpret_cast<float*>(p), v); p += 16;
        }
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
           (double(bytes) / 40 * repeats);
}

static double read_pass(void* base, size_t bytes) {
    auto start = std::chrono::steady_clock::now();
    volatile float sink = 0;
    __m128 acc = _mm_setzero_ps();
    for (char* p = static_cast<char*>(base); p + 16 <= static_cast<char*>(base) + bytes; p += 16)
        acc = _mm_add_ps(acc, _mm_loadu_ps(reinterpret_cast<float*>(p)));
    sink = _mm_cvtss_f32(acc);
    (void)sink;
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (double(bytes) / 40);
}

int main() {
    const size_t bytes = 4 << 20;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) { std::puts("instance failed"); return 1; }
    uint32_t count = 1;
    VkPhysicalDevice physical;
    vkEnumeratePhysicalDevices(instance, &count, &physical);
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkDevice device;
    if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) { std::puts("device failed"); return 1; }
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    std::vector<char> ram(bytes);
    std::printf("ram: write %.2f ns/vertex, read %.2f ns/vertex\n", write_pass(ram.data(), bytes, 20), read_pass(ram.data(), bytes));
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const auto flags = props.memoryTypes[i].propertyFlags;
        if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = bytes; bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VkBuffer buffer; vkCreateBuffer(device, &bci, nullptr, &buffer);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, buffer, &req);
        if (!(req.memoryTypeBits & (1u << i))) { std::printf("type %u flags=%x not allowed for vertex buffers\n", i, flags); continue; }
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size; mai.memoryTypeIndex = i;
        VkDeviceMemory memory;
        if (vkAllocateMemory(device, &mai, nullptr, &memory) != VK_SUCCESS) { std::printf("type %u alloc failed\n", i); continue; }
        vkBindBufferMemory(device, buffer, memory, 0);
        void* mapped = nullptr;
        vkMapMemory(device, memory, 0, bytes, 0, &mapped);
        write_pass(mapped, bytes, 1);
        const double w = write_pass(mapped, bytes, 5);
        const double r = read_pass(mapped, bytes);
        std::printf("type %u flags=%x heap=%u: write %.2f ns/vertex, read %.2f ns/vertex\n", i, flags,
                    props.memoryTypes[i].heapIndex, w, r);
        vkUnmapMemory(device, memory);
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
