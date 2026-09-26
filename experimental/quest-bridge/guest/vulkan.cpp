/* Vulkan for the guest, by passing each call to the host's own driver.

   The guest's libvulkan.so is this file. Every vk* symbol it asks for is a
   thunk, and the thunk calls the function of the same name in vulkan-1.dll.
   Nothing is translated in between, because nothing needs to be: guest
   memory is identity mapped, so a pointer the guest built is a pointer the
   host can follow, and Vulkan's structures have the same layout on arm64
   Android and x64 Windows (fixed-width integers and eight-byte pointers,
   never a long). Handles are the host's own, which the guest treats as
   opaque anyway.

   What does need care is what differs between the two platforms rather than
   the two processors:
     - allocation callbacks are guest functions the host cannot call, so the
       allocator argument is always replaced with null;
     - Android's surface extension becomes a Win32 surface on a real window,
       so what the game presents appears on the desktop;
     - extension lists are filtered to what the host driver has, since a
       mobile driver and a desktop one never agree on the details. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include "vk_queue.h"
#include <windows.h>

#include "android.h"
#include "vk_table.h"
#include "astc.h"
#include <algorithm>
#include <chrono>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using HostFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
using GetProc = void* (*)(uint64_t, const char*);

std::once_flag g_loaded;
HMODULE g_library = nullptr;
GetProc g_instance_proc = nullptr;
GetProc g_device_proc = nullptr;
std::mutex g_lock;
std::unordered_map<std::string, const VkSignature*> g_signatures;
std::unordered_map<std::string, void*> g_resolved;
uint64_t g_instance = 0;
/* Bumped whenever resolved functions are thrown away (a new instance), so
   the per-import fast path below follows. */
std::atomic<uint64_t> g_vk_generation{1};

/* Structure types used below, from vulkan_core.h and vulkan_win32.h. */
const uint32_t kWin32SurfaceCreateInfo = 1000009000;

/* The window a guest surface becomes. Made on its own thread, which keeps
   pumping its messages for as long as the process lives. */
HWND g_window = nullptr;
const int kWindowWidth = 1280;
const int kWindowHeight = 720;

void load() {
    std::call_once(g_loaded, [] {
        g_library = LoadLibraryA("vulkan-1.dll");
        if (!g_library) {
            std::fprintf(stderr, "vulkan: the host has no vulkan-1.dll\n");
            return;
        }
        g_instance_proc = reinterpret_cast<GetProc>(GetProcAddress(g_library, "vkGetInstanceProcAddr"));
        g_device_proc = reinterpret_cast<GetProc>(GetProcAddress(g_library, "vkGetDeviceProcAddr"));
        for (const VkSignature& entry : kVkSignatures) g_signatures[entry.name] = &entry;
    });
}

void* host_function(const std::string& name) {
    std::lock_guard<std::mutex> held(g_lock);
    auto found = g_resolved.find(name);
    if (found != g_resolved.end()) return found->second;
    void* fn = g_instance_proc ? g_instance_proc(g_instance, name.c_str()) : nullptr;
    if (!fn && g_library) fn = reinterpret_cast<void*>(GetProcAddress(g_library, name.c_str()));
    if (fn) g_resolved[name] = fn;
    return fn;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CLOSE) return 0; /* the guest owns its lifetime, not the close box */
    return DefWindowProcA(window, message, w, l);
}

HWND make_window() {
    static std::once_flag made;
    std::call_once(made, [] {
        std::mutex ready_lock;
        std::condition_variable ready;
        bool done = false;
        std::thread([&] {
            WNDCLASSA kind{};
            kind.lpfnWndProc = window_proc;
            kind.hInstance = GetModuleHandleA(nullptr);
            kind.lpszClassName = "QuestBridgeGuestWindow";
            kind.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassA(&kind);
            RECT rect{0, 0, kWindowWidth, kWindowHeight};
            AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
            HWND window = CreateWindowExA(0, kind.lpszClassName, "Quest Bridge - guest display",
                                          WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                          rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                          kind.hInstance, nullptr);
            {
                std::lock_guard<std::mutex> held(ready_lock);
                g_window = window;
                done = true;
            }
            ready.notify_all();
            MSG message;
            while (GetMessageA(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
        }).detach();
        std::unique_lock<std::mutex> held(ready_lock);
        ready.wait(held, [&] { return done; });
    });
    return g_window;
}

/* The host's extensions, by name, for an instance (physical == 0) or for a
   physical device. */
std::set<std::string> host_extensions(uint64_t physical) {
    std::set<std::string> names;
    void* fn = host_function(physical ? "vkEnumerateDeviceExtensionProperties"
                                      : "vkEnumerateInstanceExtensionProperties");
    if (!fn) return names;
    uint32_t count = 0;
    std::vector<uint8_t> properties;
    if (physical) {
        auto enumerate = reinterpret_cast<int32_t (*)(uint64_t, const char*, uint32_t*, void*)>(fn);
        enumerate(physical, nullptr, &count, nullptr);
        properties.resize((size_t)count * 260);
        enumerate(physical, nullptr, &count, properties.data());
    } else {
        auto enumerate = reinterpret_cast<int32_t (*)(const char*, uint32_t*, void*)>(fn);
        enumerate(nullptr, &count, nullptr);
        properties.resize((size_t)count * 260);
        enumerate(nullptr, &count, properties.data());
    }
    for (uint32_t i = 0; i < count; ++i) names.insert(reinterpret_cast<const char*>(properties.data() + i * 260));
    return names;
}

/* Rewrites an extension list: the names the host has are kept, the Android
   surface becomes the Win32 one, and the rest are dropped and reported. The
   strings stay the guest's own; only the array of pointers is new. */
std::vector<const char*> filter_extensions(const char* const* wanted, uint32_t count,
                                           const std::set<std::string>& host, const char* what) {
    std::vector<const char*> kept;
    for (uint32_t i = 0; i < count; ++i) {
        std::string name = wanted[i];
        if (name == "VK_KHR_android_surface") {
            kept.push_back("VK_KHR_win32_surface");
            continue;
        }
        if (host.count(name)) kept.push_back(wanted[i]);
        else std::printf("vulkan: %s extension %s is not on this host, left out\n", what, name.c_str());
    }
    return kept;
}

}  // namespace

/* The plain pass-through functions, per import: after the first call the
   host function and its argument layout are remembered, so a vkCmd* call is
   a table read and the call itself rather than a walk through names. The
   functions vulkan_call treats specially, and the few that take floats, go
   the ordinary way. */
namespace {
struct VkFast {
    std::atomic<uint64_t> generation{0};
    void* fn = nullptr;
    const VkSignature* sig = nullptr;
};
VkFast g_vk_fast[1 << 16];

bool vk_special(const std::string& name) {
    if (name.compare(0, 7, "vkQueue") == 0 || name == "vkDeviceWaitIdle") return true;
    static const char* special[] = {"vkCreateAndroidSurfaceKHR", "vkCreateDevice", "vkCreateInstance",
                                    "vkEnumerateDeviceLayerProperties", "vkEnumerateInstanceExtensionProperties",
                                    "vkEnumerateInstanceLayerProperties", "vkGetDeviceProcAddr",
                                    "vkGetInstanceProcAddr", "vkGetPhysicalDeviceImageFormatProperties",
                                    "vkEnumerateDeviceExtensionProperties", "vkDestroyInstance",
                                    "vkDestroySurfaceKHR", "vkCreateSwapchainKHR",
                                    /* ASTC emulation, below */
                                    "vkGetPhysicalDeviceFeatures", "vkGetPhysicalDeviceFeatures2",
                                    "vkGetPhysicalDeviceFeatures2KHR", "vkGetPhysicalDeviceFormatProperties",
                                    "vkGetPhysicalDeviceFormatProperties2", "vkGetPhysicalDeviceFormatProperties2KHR",
                                    "vkGetPhysicalDeviceImageFormatProperties2",
                                    "vkGetPhysicalDeviceImageFormatProperties2KHR", "vkCreateImage", "vkDestroyImage",
                                    "vkCreateImageView", "vkBindBufferMemory", "vkBindBufferMemory2",
                                    "vkBindBufferMemory2KHR", "vkMapMemory", "vkUnmapMemory", "vkFreeMemory",
                                    "vkAllocateCommandBuffers", "vkBeginCommandBuffer", "vkResetCommandBuffer",
                                    "vkFreeCommandBuffers", "vkResetCommandPool", "vkDestroyCommandPool",
                                    "vkCmdCopyBufferToImage", "vkCmdCopyBufferToImage2",
                                    "vkCmdCopyBufferToImage2KHR"};
    for (const char* one : special)
        if (name == one) return true;
    return false;
}
}  // namespace

/* ASTC on a GPU without it. Quest games ship nearly every texture as ASTC,
   which no desktop GPU samples; told the truth, Unity decompresses each one
   on the CPU, in emulated code, and logs an error with a symbolized stack
   for every CopyTexture it cannot do, every frame. So the device claims
   ASTC: an ASTC image is made as RGBA8 with the same extent, each upload of
   blocks into one is decoded on the host (astc.cpp) into a staging buffer of
   ours, and the copy is recorded from that instead. Views and format
   queries say RGBA8 where the guest said ASTC. Copies between two such
   images need nothing, since both sides are RGBA8 and extents are texels.
   QB_NO_ASTC turns all of it off. */
namespace {
const uint32_t kRgba8Unorm = 37, kRgba8Srgb = 43;
struct AstcImage {
    int block_w, block_h;
};
struct Staging {
    uint64_t device, buffer, memory;
};
std::mutex g_astc_lock;
std::unordered_map<uint64_t, AstcImage> g_astc_images;              /* image -> blocks */
std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> g_buffer_memory; /* buffer -> memory, offset */
std::unordered_map<uint64_t, uint8_t*> g_memory_base;                 /* memory -> host pointer of offset 0 */
std::unordered_map<uint64_t, uint64_t> g_memory_device;
std::unordered_map<uint64_t, uint64_t> g_command_device;              /* command buffer -> device */
std::unordered_map<uint64_t, std::vector<uint64_t>> g_pool_commands;  /* pool -> its command buffers */
std::unordered_map<uint64_t, std::vector<Staging>> g_staging;         /* command buffer -> uploads it reads */
std::unordered_map<uint64_t, uint64_t> g_device_physical;
std::atomic<uint64_t> g_astc_uploads{0}, g_astc_bytes{0};

bool astc_enabled() {
    static const bool on = QB_ENV("QB_NO_ASTC") == nullptr;
    return on;
}
uint32_t substitute(uint32_t format) {
    int bw, bh;
    bool srgb;
    if (!astc_enabled() || !astc_block_size(format, &bw, &bh, &srgb)) return format;
    return srgb ? kRgba8Srgb : kRgba8Unorm;
}
template <typename F> F host_as(const char* name) { return reinterpret_cast<F>(host_function(name)); }

void release_staging(uint64_t command) {
    auto found = g_staging.find(command);
    if (found == g_staging.end()) return;
    auto destroy_buffer = host_as<void (*)(uint64_t, uint64_t, const void*)>("vkDestroyBuffer");
    auto free_memory = host_as<void (*)(uint64_t, uint64_t, const void*)>("vkFreeMemory");
    for (const Staging& one : found->second) {
        destroy_buffer(one.device, one.buffer, nullptr);
        free_memory(one.device, one.memory, nullptr);
    }
    g_staging.erase(found);
}

/* A host-visible buffer of that many bytes, mapped, for one upload. */
uint8_t* make_staging(uint64_t device, uint64_t size, Staging& out) {
    auto physical = g_device_physical.find(device);
    if (physical == g_device_physical.end()) return nullptr;
    uint8_t info[56] = {};
    uint32_t type = 12; /* VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO */
    uint32_t usage = 1; /* TRANSFER_SRC */
    std::memcpy(info, &type, 4);
    std::memcpy(info + 24, &size, 8);
    std::memcpy(info + 32, &usage, 4);
    if (host_as<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>("vkCreateBuffer")(device, info, nullptr,
                                                                                            &out.buffer) != 0)
        return nullptr;
    uint64_t requirements[3] = {};
    host_as<void (*)(uint64_t, uint64_t, void*)>("vkGetBufferMemoryRequirements")(device, out.buffer, requirements);
    uint8_t properties[520] = {};
    host_as<void (*)(uint64_t, void*)>("vkGetPhysicalDeviceMemoryProperties")(physical->second, properties);
    uint32_t type_count = 0, chosen = UINT32_MAX;
    std::memcpy(&type_count, properties, 4);
    for (uint32_t i = 0; i < type_count && i < 32; ++i) {
        uint32_t flags = 0;
        std::memcpy(&flags, properties + 4 + i * 8, 4);
        if ((requirements[2] >> i) & 1 && (flags & 6) == 6) { /* HOST_VISIBLE | HOST_COHERENT */
            chosen = i;
            break;
        }
    }
    auto destroy_buffer = host_as<void (*)(uint64_t, uint64_t, const void*)>("vkDestroyBuffer");
    if (chosen == UINT32_MAX) {
        destroy_buffer(device, out.buffer, nullptr);
        return nullptr;
    }
    uint8_t allocate[32] = {};
    uint32_t allocate_type = 5; /* VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO */
    std::memcpy(allocate, &allocate_type, 4);
    std::memcpy(allocate + 16, &requirements[0], 8);
    std::memcpy(allocate + 24, &chosen, 4);
    if (host_as<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>("vkAllocateMemory")(device, allocate,
                                                                                              nullptr, &out.memory) != 0) {
        destroy_buffer(device, out.buffer, nullptr);
        return nullptr;
    }
    host_as<int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)>("vkBindBufferMemory")(device, out.buffer, out.memory, 0);
    void* mapped = nullptr;
    host_as<int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, void**)>("vkMapMemory")(
        device, out.memory, 0, ~0ull, 0, &mapped);
    out.device = device;
    return static_cast<uint8_t*>(mapped);
}

/* Where the guest's buffer lives in host memory, mapping it if nobody has. */
uint8_t* buffer_bytes(uint64_t buffer) {
    auto bound = g_buffer_memory.find(buffer);
    if (bound == g_buffer_memory.end()) return nullptr;
    uint64_t memory = bound->second.first;
    auto base = g_memory_base.find(memory);
    if (base == g_memory_base.end()) {
        auto device = g_memory_device.find(memory);
        if (device == g_memory_device.end()) return nullptr;
        void* mapped = nullptr;
        if (host_as<int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, void**)>("vkMapMemory")(
                device->second, memory, 0, ~0ull, 0, &mapped) != 0)
            return nullptr;
        base = g_memory_base.emplace(memory, static_cast<uint8_t*>(mapped)).first;
    }
    return base->second + bound->second.second;
}

/* Rewrites one upload into an ASTC image: regions are 56-byte
   VkBufferImageCopy bodies, stride apart. Returns the new source buffer and
   fills the rewritten regions, or 0 to pass the call through untouched. */
uint64_t decode_upload(uint64_t command, uint64_t source, const AstcImage& image, const uint8_t* regions,
                       uint32_t count, size_t stride, std::vector<uint8_t>& rewritten) {
    auto device = g_command_device.find(command);
    uint8_t* from = buffer_bytes(source);
    if (device == g_command_device.end() || !from) {
        std::printf("vulkan: astc upload with no readable source, left as it is\n");
        return 0;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = regions + i * stride;
        uint32_t w, h, d, layers;
        std::memcpy(&w, r + 44, 4);
        std::memcpy(&h, r + 48, 4);
        std::memcpy(&d, r + 52, 4);
        std::memcpy(&layers, r + 28, 4);
        total += (uint64_t)w * h * std::max(1u, d) * std::max(1u, layers) * 4;
    }
    Staging staging{};
    uint8_t* to = make_staging(device->second, std::max<uint64_t>(total, 16), staging);
    if (!to) {
        std::printf("vulkan: astc upload: no staging buffer, left as it is\n");
        return 0;
    }
    rewritten.assign(regions, regions + count * stride);
    uint64_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* r = rewritten.data() + i * stride;
        uint64_t offset;
        uint32_t row_length, image_height, w, h, d, layers;
        std::memcpy(&offset, r, 8);
        std::memcpy(&row_length, r + 8, 4);
        std::memcpy(&image_height, r + 12, 4);
        std::memcpy(&layers, r + 28, 4);
        std::memcpy(&w, r + 44, 4);
        std::memcpy(&h, r + 48, 4);
        std::memcpy(&d, r + 52, 4);
        d = std::max(1u, d);
        layers = std::max(1u, layers);
        if (!row_length) row_length = w;
        if (!image_height) image_height = h;
        size_t row_blocks = (row_length + image.block_w - 1) / image.block_w;
        size_t slice_blocks = row_blocks * ((image_height + image.block_h - 1) / image.block_h);
        uint64_t slice_bytes = (uint64_t)w * h * 4;
        for (uint32_t layer = 0; layer < layers; ++layer)
            astc_decode(from + offset + (uint64_t)layer * d * slice_blocks * 16, image.block_w, image.block_h, (int)w,
                        (int)h, (int)d, row_blocks, slice_blocks, to + at + (uint64_t)layer * d * slice_bytes, (size_t)w * 4,
                        slice_bytes);
        uint32_t zero = 0;
        std::memcpy(r, &at, 8);
        std::memcpy(r + 8, &zero, 4);
        std::memcpy(r + 12, &zero, 4);
        at += slice_bytes * d * layers;
    }
    g_staging[command].push_back(staging);
    ++g_astc_uploads;
    g_astc_bytes += total;
    uint64_t uploads = g_astc_uploads.load();
    if (uploads == 1 || uploads % 1000 == 0) {
        std::printf("vulkan: astc: %llu uploads decoded on the host, %.1f MB of RGBA8\n",
                    (unsigned long long)uploads, g_astc_bytes.load() / 1048576.0);
        std::fflush(stdout);
    }
    return staging.buffer;
}

/* Clears textureCompressionASTC_LDR (VkPhysicalDeviceFeatures + 84) where the
   guest asks the host for it, and puts it back after. */
struct FeatureMask {
    std::vector<uint32_t*> cleared;
    void clear(uint8_t* features) {
        uint32_t* flag = reinterpret_cast<uint32_t*>(features + 84);
        if (*flag) {
            *flag = 0;
            cleared.push_back(flag);
        }
    }
    ~FeatureMask() {
        for (uint32_t* flag : cleared) *flag = 1;
    }
};

bool astc_call(const std::string& name, GuestCpu& cpu) {
    if (!astc_enabled()) return false;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    if (name == "vkGetPhysicalDeviceFeatures" || name == "vkGetPhysicalDeviceFeatures2" ||
        name == "vkGetPhysicalDeviceFeatures2KHR") {
        host_as<void (*)(uint64_t, uint64_t)>(name.c_str())(arg(0), arg(1));
        uint8_t* features = reinterpret_cast<uint8_t*>(arg(1));
        if (features) {
            uint32_t yes = 1;
            std::memcpy(features + (name == "vkGetPhysicalDeviceFeatures" ? 84 : 16 + 84), &yes, 4);
        }
        return true;
    }
    if (name == "vkGetPhysicalDeviceFormatProperties" || name == "vkGetPhysicalDeviceFormatProperties2" ||
        name == "vkGetPhysicalDeviceFormatProperties2KHR") {
        host_as<void (*)(uint64_t, uint32_t, uint64_t)>(name.c_str())(arg(0), substitute((uint32_t)arg(1)), arg(2));
        return true;
    }
    if (name == "vkGetPhysicalDeviceImageFormatProperties2" || name == "vkGetPhysicalDeviceImageFormatProperties2KHR") {
        uint8_t* info = reinterpret_cast<uint8_t*>(arg(1));
        uint32_t format = 0;
        std::memcpy(&format, info + 16, 4);
        uint32_t used = substitute(format);
        std::memcpy(info + 16, &used, 4);
        int32_t result = host_as<int32_t (*)(uint64_t, uint64_t, uint64_t)>(name.c_str())(arg(0), arg(1), arg(2));
        std::memcpy(info + 16, &format, 4);
        ret((uint64_t)(int64_t)result);
        return true;
    }
    if (name == "vkCreateImage") {
        uint8_t* info = reinterpret_cast<uint8_t*>(arg(1));
        uint32_t format = 0, flags = 0;
        std::memcpy(&format, info + 24, 4);
        std::memcpy(&flags, info + 16, 4);
        AstcImage image{};
        bool srgb = false;
        bool astc = astc_block_size(format, &image.block_w, &image.block_h, &srgb);
        if (astc) {
            uint32_t used = srgb ? kRgba8Srgb : kRgba8Unorm, used_flags = flags & ~0x80u; /* no BLOCK_TEXEL_VIEW */
            std::memcpy(info + 24, &used, 4);
            std::memcpy(info + 16, &used_flags, 4);
        }
        int32_t result = host_as<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>("vkCreateImage")(
            arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3)));
        if (astc) {
            std::memcpy(info + 24, &format, 4);
            std::memcpy(info + 16, &flags, 4);
            if (result == 0) {
                std::lock_guard<std::mutex> held(g_astc_lock);
                g_astc_images[*reinterpret_cast<uint64_t*>(arg(3))] = image;
            }
        }
        ret((uint64_t)(int64_t)result);
        return true;
    }
    if (name == "vkDestroyImage") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            g_astc_images.erase(arg(1));
        }
        host_as<void (*)(uint64_t, uint64_t, const void*)>("vkDestroyImage")(arg(0), arg(1), nullptr);
        return true;
    }
    if (name == "vkCreateImageView") {
        uint8_t* info = reinterpret_cast<uint8_t*>(arg(1));
        uint32_t format = 0;
        std::memcpy(&format, info + 36, 4);
        uint32_t used = substitute(format);
        std::memcpy(info + 36, &used, 4);
        int32_t result = host_as<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>("vkCreateImageView")(
            arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3)));
        std::memcpy(info + 36, &format, 4);
        ret((uint64_t)(int64_t)result);
        return true;
    }
    if (name == "vkBindBufferMemory") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            g_buffer_memory[arg(1)] = {arg(2), arg(3)};
            g_memory_device[arg(2)] = arg(0);
        }
        ret((uint64_t)(int64_t)host_as<int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)>("vkBindBufferMemory")(
            arg(0), arg(1), arg(2), arg(3)));
        return true;
    }
    if (name == "vkBindBufferMemory2" || name == "vkBindBufferMemory2KHR") {
        const uint8_t* infos = reinterpret_cast<const uint8_t*>(arg(2));
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            for (uint32_t i = 0; i < (uint32_t)arg(1); ++i) {
                uint64_t buffer, memory, offset;
                std::memcpy(&buffer, infos + i * 40 + 16, 8);
                std::memcpy(&memory, infos + i * 40 + 24, 8);
                std::memcpy(&offset, infos + i * 40 + 32, 8);
                g_buffer_memory[buffer] = {memory, offset};
                g_memory_device[memory] = arg(0);
            }
        }
        ret((uint64_t)(int64_t)host_as<int32_t (*)(uint64_t, uint64_t, uint64_t)>(name.c_str())(arg(0), arg(1), arg(2)));
        return true;
    }
    if (name == "vkMapMemory") {
        /* (device, memory, offset, size, flags, ppData): six arguments. */
        int32_t result = host_as<int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, void**)>("vkMapMemory")(
            arg(0), arg(1), arg(2), arg(3), (uint32_t)arg(4), reinterpret_cast<void**>(arg(5)));
        if (result == 0) {
            std::lock_guard<std::mutex> held(g_astc_lock);
            g_memory_base[arg(1)] = *reinterpret_cast<uint8_t**>(arg(5)) - arg(2);
            g_memory_device[arg(1)] = arg(0);
        }
        ret((uint64_t)(int64_t)result);
        return true;
    }
    if (name == "vkUnmapMemory" || name == "vkFreeMemory") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            g_memory_base.erase(arg(1));
            if (name == "vkFreeMemory") g_memory_device.erase(arg(1));
        }
        if (name == "vkUnmapMemory") host_as<void (*)(uint64_t, uint64_t)>("vkUnmapMemory")(arg(0), arg(1));
        else host_as<void (*)(uint64_t, uint64_t, const void*)>("vkFreeMemory")(arg(0), arg(1), nullptr);
        return true;
    }
    if (name == "vkAllocateCommandBuffers") {
        int32_t result = host_as<int32_t (*)(uint64_t, uint64_t, uint64_t)>(name.c_str())(arg(0), arg(1), arg(2));
        if (result == 0) {
            const uint8_t* info = reinterpret_cast<const uint8_t*>(arg(1));
            uint64_t pool;
            uint32_t count;
            std::memcpy(&pool, info + 16, 8);
            std::memcpy(&count, info + 28, 4);
            std::lock_guard<std::mutex> held(g_astc_lock);
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t command = reinterpret_cast<uint64_t*>(arg(2))[i];
                g_command_device[command] = arg(0);
                g_pool_commands[pool].push_back(command);
            }
        }
        ret((uint64_t)(int64_t)result);
        return true;
    }
    /* Uploads recorded into a command buffer are done with once it is begun
       again, reset, or freed, since none of those is allowed while it runs. */
    if (name == "vkBeginCommandBuffer" || name == "vkResetCommandBuffer") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            release_staging(arg(0));
        }
        ret((uint64_t)(int64_t)host_as<int32_t (*)(uint64_t, uint64_t)>(name.c_str())(arg(0), arg(1)));
        return true;
    }
    if (name == "vkFreeCommandBuffers") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            auto& list = g_pool_commands[arg(1)];
            for (uint32_t i = 0; i < (uint32_t)arg(2); ++i) {
                uint64_t command = reinterpret_cast<const uint64_t*>(arg(3))[i];
                release_staging(command);
                g_command_device.erase(command);
                list.erase(std::remove(list.begin(), list.end(), command), list.end());
            }
        }
        host_as<void (*)(uint64_t, uint64_t, uint32_t, uint64_t)>(name.c_str())(arg(0), arg(1), (uint32_t)arg(2), arg(3));
        return true;
    }
    if (name == "vkResetCommandPool" || name == "vkDestroyCommandPool") {
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            for (uint64_t command : g_pool_commands[arg(1)]) {
                release_staging(command);
                if (name == "vkDestroyCommandPool") g_command_device.erase(command);
            }
            if (name == "vkDestroyCommandPool") g_pool_commands.erase(arg(1));
        }
        if (name == "vkResetCommandPool")
            ret((uint64_t)(int64_t)host_as<int32_t (*)(uint64_t, uint64_t, uint32_t)>(name.c_str())(arg(0), arg(1),
                                                                                                 (uint32_t)arg(2)));
        else host_as<void (*)(uint64_t, uint64_t, const void*)>(name.c_str())(arg(0), arg(1), nullptr);
        return true;
    }
    if (name == "vkCmdCopyBufferToImage") {
        /* QB_TRACE_UPLOADS: the time uploads take, every five seconds. */
        static const bool timing = QB_ENV("QB_TRACE_UPLOADS") != nullptr;
        struct Timer {
            bool on;
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            ~Timer() {
                if (!on) return;
                static std::mutex lock;
                static double ms = 0, worst = 0;
                static uint64_t calls = 0;
                static auto window = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                double one = std::chrono::duration<double, std::milli>(now - start).count();
                std::lock_guard<std::mutex> held(lock);
                ms += one;
                worst = std::max(worst, one);
                ++calls;
                if (now - window > std::chrono::seconds(5)) {
                    std::printf("vulkan: uploads: %llu calls, %.1f ms total, worst %.2f ms\n", (unsigned long long)calls,
                                ms, worst);
                    ms = worst = 0;
                    calls = 0;
                    window = now;
                }
            }
        } timer{timing};
        std::vector<uint8_t> rewritten;
        uint64_t source = arg(1);
        const void* regions = reinterpret_cast<const void*>(arg(5));
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            auto image = g_astc_images.find(arg(2));
            if (image != g_astc_images.end()) {
                uint64_t decoded = decode_upload(arg(0), arg(1), image->second,
                                                 reinterpret_cast<const uint8_t*>(arg(5)), (uint32_t)arg(4), 56, rewritten);
                if (decoded) {
                    source = decoded;
                    regions = rewritten.data();
                }
            }
        }
        host_as<void (*)(uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, const void*)>(name.c_str())(
            arg(0), source, arg(2), (uint32_t)arg(3), (uint32_t)arg(4), regions);
        return true;
    }
    if (name == "vkCmdCopyBufferToImage2" || name == "vkCmdCopyBufferToImage2KHR") {
        /* VkCopyBufferToImageInfo2: srcBuffer 16, dstImage 24, regionCount
           36, pRegions 40; each VkBufferImageCopy2 is sType and pNext, then
           the same 56 bytes. */
        uint8_t info[48];
        std::memcpy(info, reinterpret_cast<const void*>(arg(1)), 48);
        uint64_t source, target, regions;
        uint32_t count;
        std::memcpy(&source, info + 16, 8);
        std::memcpy(&target, info + 24, 8);
        std::memcpy(&count, info + 36, 4);
        std::memcpy(&regions, info + 40, 8);
        std::vector<uint8_t> rewritten;
        {
            std::lock_guard<std::mutex> held(g_astc_lock);
            auto image = g_astc_images.find(target);
            if (image != g_astc_images.end()) {
                uint64_t decoded = decode_upload(arg(0), source, image->second,
                                                 reinterpret_cast<const uint8_t*>(regions) + 16, count, 72, rewritten);
                if (decoded) {
                    /* rewritten holds bodies at 72-byte strides starting 16 in;
                       put the headers back in front. */
                    std::vector<uint8_t> whole(reinterpret_cast<const uint8_t*>(regions),
                                               reinterpret_cast<const uint8_t*>(regions) + count * 72);
                    for (uint32_t i = 0; i < count; ++i) std::memcpy(whole.data() + i * 72 + 16, rewritten.data() + i * 72, 56);
                    rewritten.swap(whole);
                    uint64_t pointer = reinterpret_cast<uint64_t>(rewritten.data());
                    std::memcpy(info + 16, &decoded, 8);
                    std::memcpy(info + 40, &pointer, 8);
                }
            }
        }
        host_as<void (*)(uint64_t, const void*)>(name.c_str())(arg(0), info);
        return true;
    }
    return false;
}
}  // namespace

bool GuestLibc::vulkan_fast(int index, const std::string& name, GuestCpu& cpu) {
    if (index < 0 || index >= (1 << 16) || !g_library) return false;
    VkFast& slot = g_vk_fast[index];
    uint64_t generation = g_vk_generation.load(std::memory_order_acquire);
    if (slot.generation.load(std::memory_order_acquire) != generation) {
        if (vk_special(name)) return false;
        auto signature = g_signatures.find(name);
        void* fn = host_function(name);
        if (signature == g_signatures.end() || !fn || signature->second->floats) return false;
        slot.fn = fn;
        slot.sig = signature->second;
        slot.generation.store(generation, std::memory_order_release);
    }
    const VkSignature& sig = *slot.sig;
    uint64_t a[16] = {};
    int integers = 0;
    uint64_t stack = cpu.sp;
    for (int i = 0; i < sig.params && i < 16; ++i) {
        if (integers < 8) {
            a[i] = cpu.x[integers++];
        } else {
            a[i] = *reinterpret_cast<uint64_t*>(stack);
            stack += 8;
        }
    }
    if (sig.allocator >= 0) a[sig.allocator] = 0;
    uint64_t result = reinterpret_cast<HostFn>(slot.fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                                       a[10], a[11], a[12], a[13], a[14], a[15]);
    if (sig.returns) cpu.x[0] = result;
    return true;
}

bool GuestLibc::vulkan_call(const std::string& name, GuestCpu& cpu) {
    std::unique_lock<std::recursive_mutex> queueLock(guest_vulkan_queue_mutex(), std::defer_lock);
    if (name.compare(0, 7, "vkQueue") == 0 || name == "vkDeviceWaitIdle") queueLock.lock();
    load();
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    static const bool trace = QB_ENV("QB_TRACE_VK") != nullptr;
    if (!g_library) {
        ret((uint64_t)(int64_t)-9); /* VK_ERROR_INCOMPATIBLE_DRIVER */
        return true;
    }

    /* The two ways of asking for a function: the answer is always a thunk
       of ours with that name, and only if the host would answer too. */
    if (name == "vkGetInstanceProcAddr" || name == "vkGetDeviceProcAddr") {
        const char* wanted = reinterpret_cast<const char*>(arg(1));
        if (!wanted) {
            ret(0);
            return true;
        }
        std::string want = wanted;
        bool ours = want == "vkGetInstanceProcAddr" || want == "vkGetDeviceProcAddr" ||
                    want == "vkCreateAndroidSurfaceKHR" || want == "vkEnumerateInstanceExtensionProperties";
        void* host = nullptr;
        if (!ours) {
            if (name == "vkGetDeviceProcAddr" && g_device_proc) host = g_device_proc(arg(0), wanted);
            else if (g_instance_proc) host = g_instance_proc(arg(0) ? arg(0) : g_instance, wanted);
        }
        ret(ours || host ? image->linker.thunk_for(want) : 0);
        if (trace) std::printf("vulkan: %s(%s) -> %s\n", name.c_str(), wanted, (ours || host) ? "yes" : "no");
        return true;
    }

    /* The instance extensions the host has, plus Android's surface, which we
       make out of a Win32 one. */
    if (name == "vkEnumerateInstanceExtensionProperties") {
        std::set<std::string> host = host_extensions(0);
        if (arg(0)) host.clear(); /* a layer's extensions: there are no layers */
        else host.insert("VK_KHR_android_surface");
        uint32_t* count = reinterpret_cast<uint32_t*>(arg(1));
        uint8_t* out = reinterpret_cast<uint8_t*>(arg(2));
        if (!count) {
            ret((uint64_t)(int64_t)-3);
            return true;
        }
        if (!out) {
            *count = (uint32_t)host.size();
            ret(0);
            return true;
        }
        uint32_t written = 0;
        for (const std::string& extension : host) {
            if (written >= *count) break;
            uint8_t* entry = out + (size_t)written * 260;
            std::memset(entry, 0, 260);
            std::memcpy(entry, extension.c_str(), std::min<size_t>(extension.size(), 255));
            uint32_t version = 1;
            std::memcpy(entry + 256, &version, 4);
            ++written;
        }
        bool incomplete = written < host.size();
        *count = written;
        ret(incomplete ? 5 /* VK_INCOMPLETE */ : 0);
        return true;
    }
    if (name == "vkEnumerateInstanceLayerProperties" || name == "vkEnumerateDeviceLayerProperties") {
        uint32_t* count = reinterpret_cast<uint32_t*>(arg(name == "vkEnumerateInstanceLayerProperties" ? 0 : 1));
        if (count) *count = 0;
        ret(0);
        return true;
    }

    if (name == "vkCreateInstance") {
        /* VkInstanceCreateInfo: pNext 8, application 24, layers 32/40,
           extensions 48/56. A copy is made so the guest's is left alone. */
        uint8_t info[64];
        std::memcpy(info, reinterpret_cast<void*>(arg(0)), 64);
        uint32_t extension_count = 0;
        uint64_t extensions = 0;
        std::memcpy(&extension_count, info + 48, 4);
        std::memcpy(&extensions, info + 56, 8);
        std::vector<const char*> kept =
            filter_extensions(reinterpret_cast<const char* const*>(extensions), extension_count, host_extensions(0),
                              "instance");
        uint32_t none = 0;
        uint64_t zero = 0, list = reinterpret_cast<uint64_t>(kept.data());
        uint32_t kept_count = (uint32_t)kept.size();
        std::memcpy(info + 8, &zero, 8); /* debug callbacks and the like: guest code */
        std::memcpy(info + 32, &none, 4);
        std::memcpy(info + 40, &zero, 8);
        std::memcpy(info + 48, &kept_count, 4);
        std::memcpy(info + 56, &list, 8);
        auto create = reinterpret_cast<int32_t (*)(const void*, const void*, uint64_t*)>(host_function(name));
        uint64_t* out = reinterpret_cast<uint64_t*>(arg(2));
        int32_t result = create(info, nullptr, out);
        if (result == 0) {
            std::lock_guard<std::mutex> held(g_lock);
            g_instance = *out;
            g_resolved.clear(); /* now instance functions resolve for real */
            g_vk_generation.fetch_add(1, std::memory_order_release);
        }
        std::printf("vulkan: vkCreateInstance with %u of %u extensions -> %d\n", kept_count, extension_count, result);
        ret((uint64_t)(int64_t)result);
        return true;
    }

    if (name == "vkCreateDevice") {
        /* VkDeviceCreateInfo: pNext 8, layers 32/40, extensions 48/56. */
        uint8_t info[72];
        std::memcpy(info, reinterpret_cast<void*>(arg(1)), 72);
        uint32_t extension_count = 0;
        uint64_t extensions = 0;
        std::memcpy(&extension_count, info + 48, 4);
        std::memcpy(&extensions, info + 56, 8);
        std::vector<const char*> kept =
            filter_extensions(reinterpret_cast<const char* const*>(extensions), extension_count,
                              host_extensions(arg(0)), "device");
        uint32_t none = 0, kept_count = (uint32_t)kept.size();
        uint64_t zero = 0, list = reinterpret_cast<uint64_t>(kept.data());
        std::memcpy(info + 32, &none, 4);
        std::memcpy(info + 40, &zero, 8);
        std::memcpy(info + 48, &kept_count, 4);
        std::memcpy(info + 56, &list, 8);
        /* ASTC is claimed, not had: the host must not be asked for it. */
        FeatureMask mask;
        uint8_t features[220];
        uint64_t enabled = 0;
        std::memcpy(&enabled, info + 64, 8);
        if (enabled) {
            std::memcpy(features, reinterpret_cast<const void*>(enabled), sizeof(features));
            std::memset(features + 84, 0, 4);
            uint64_t ours = reinterpret_cast<uint64_t>(features);
            std::memcpy(info + 64, &ours, 8);
        }
        for (uint8_t* next = *reinterpret_cast<uint8_t**>(info + 8); next; next = *reinterpret_cast<uint8_t**>(next + 8))
            if (*reinterpret_cast<uint32_t*>(next) == 1000059000u) mask.clear(next + 16); /* VkPhysicalDeviceFeatures2 */
        auto create =
            reinterpret_cast<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>(host_function(name));
        int32_t result = create(arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3)));
        if (result == 0) {
            std::lock_guard<std::mutex> held(g_astc_lock);
            g_device_physical[*reinterpret_cast<uint64_t*>(arg(3))] = arg(0);
        }
        std::printf("vulkan: vkCreateDevice with %u of %u extensions -> %d\n", kept_count, extension_count, result);
        ret((uint64_t)(int64_t)result);
        return true;
    }

    if (name == "vkCreateAndroidSurfaceKHR") {
        /* A real window, and a surface on it. */
        HWND window = make_window();
        uint8_t info[40] = {};
        uint64_t instance_module = reinterpret_cast<uint64_t>(GetModuleHandleA(nullptr));
        uint64_t handle = reinterpret_cast<uint64_t>(window);
        std::memcpy(info, &kWin32SurfaceCreateInfo, 4);
        std::memcpy(info + 24, &instance_module, 8);
        std::memcpy(info + 32, &handle, 8);
        auto create = reinterpret_cast<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>(
            host_function("vkCreateWin32SurfaceKHR"));
        int32_t result = create ? create(arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3))) : -7;
        std::printf("vulkan: the guest's surface is a %dx%d window -> %d\n", kWindowWidth, kWindowHeight, result);
        ret((uint64_t)(int64_t)result);
        return true;
    }

    /* The guest's own window surface (its Android surface, a desktop window
       here) presents with FIFO, which on a 60 Hz monitor blocks every frame
       until the next refresh. On the headset that surface is not what paces
       the game: the XR runtime is. So it presents without waiting (mailbox),
       falling back to what the guest asked for if the driver refuses. */
    if (name == "vkCreateSwapchainKHR") {
        uint8_t* info = reinterpret_cast<uint8_t*>(arg(1));
        auto create = reinterpret_cast<int32_t (*)(uint64_t, const void*, const void*, uint64_t*)>(host_function(name));
        if (!create || !info) {
            ret((uint64_t)(int64_t)-7);
            return true;
        }
        uint32_t asked = 0;
        std::memcpy(&asked, info + 88, 4); /* VkSwapchainCreateInfoKHR.presentMode */
        uint32_t mailbox = 1;              /* VK_PRESENT_MODE_MAILBOX_KHR */
        std::memcpy(info + 88, &mailbox, 4);
        int32_t result = create(arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3)));
        if (result != 0) {
            std::memcpy(info + 88, &asked, 4);
            result = create(arg(0), info, nullptr, reinterpret_cast<uint64_t*>(arg(3)));
        } else {
            std::memcpy(info + 88, &asked, 4); /* the guest's structure as it was */
        }
        if (trace) std::printf("vulkan: vkCreateSwapchainKHR (mode %u asked) -> %d\n", asked, result);
        ret((uint64_t)(int64_t)result);
        return true;
    }

    if (astc_call(name, cpu)) return true;
    /* Everything else goes through as it is. */
    auto signature = g_signatures.find(name);
    void* fn = host_function(name);
    if (signature == g_signatures.end() || !fn) {
        std::printf("vulkan: %s is not available on this host\n", name.c_str());
        ret((uint64_t)(int64_t)-7); /* VK_ERROR_EXTENSION_NOT_PRESENT */
        return true;
    }
    const VkSignature& sig = *signature->second;
    uint64_t a[16] = {};
    int integers = 0, floats = 0;
    uint64_t stack = cpu.sp;
    for (int i = 0; i < sig.params && i < 16; ++i) {
        if (sig.floats & (1u << i)) {
            a[i] = cpu.q[floats++].lo & 0xffffffffull;
        } else if (integers < 8) {
            a[i] = cpu.x[integers++];
        } else {
            a[i] = *reinterpret_cast<uint64_t*>(stack);
            stack += 8;
        }
    }
    if (sig.allocator >= 0) a[sig.allocator] = 0;
    if (name == "vkGetPhysicalDeviceImageFormatProperties") a[1] = substitute((uint32_t)a[1]);
    if (trace) std::printf("vulkan: %s\n", name.c_str());

    if (sig.floats) {
        /* The few that take a float, which x64 wants in an XMM register at
           its own position. */
        auto f = [&](int i) {
            float value;
            uint32_t bits = (uint32_t)a[i];
            std::memcpy(&value, &bits, 4);
            return value;
        };
        if (sig.floats == 0x2 && sig.params == 2)
            reinterpret_cast<void (*)(uint64_t, float)>(fn)(a[0], f(1));
        else if (sig.floats == 0xe)
            reinterpret_cast<void (*)(uint64_t, float, float, float)>(fn)(a[0], f(1), f(2), f(3));
        else if (sig.floats == 0x6)
            reinterpret_cast<void (*)(uint64_t, float, float)>(fn)(a[0], f(1), f(2));
        else if (sig.floats == 0x4)
            reinterpret_cast<void (*)(uint64_t, uint64_t, float)>(fn)(a[0], a[1], f(2));
        else
            std::printf("vulkan: %s takes floats in a shape not handled\n", name.c_str());
        ret(0);
        return true;
    }

    uint64_t result = reinterpret_cast<HostFn>(fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                                  a[10], a[11], a[12], a[13], a[14], a[15]);
    if (sig.returns) ret(result);
    if (trace && sig.returns) std::printf("vulkan:   -> %d\n", (int)(int32_t)result);
    if ((int32_t)result == -11 && name == "vkGetPhysicalDeviceImageFormatProperties")
        std::printf("vulkan: format %llu type %llu tiling %llu usage %llx flags %llx not supported\n",
                    (unsigned long long)a[1], (unsigned long long)a[2], (unsigned long long)a[3],
                    (unsigned long long)a[4], (unsigned long long)a[5]);
    return true;
}
