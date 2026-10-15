// Host side of the guest's libvulkan.so (direct/guest/proxy/libvulkan.cpp): the
// guest's Vulkan commands run on the host GPU through the system Vulkan loader.
//
// Guest and host share memory and structure layouts, so a command is the host
// driver's function called with the guest's arguments. What differs is the
// platform, and that is translated here:
//   - Android surfaces (ANativeWindow) become Win32 surfaces on a desktop window,
//     and VK_KHR_android_surface is offered in place of VK_KHR_win32_surface;
//   - Windows-only extensions are hidden, guest layers are not loaded;
//   - guest allocation callbacks and debug callbacks (guest code) are dropped.

#include "vk_host.h"

#include "cpu.h"
#include "log.h"
#include "ndk.h"
#include "window.h"

#define VK_NO_PROTOTYPES 1
#define VK_USE_PLATFORM_WIN32_KHR 1
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include "vk_texture.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace axrb {
namespace {

enum Level : uint32_t { kGlobal, kInstance, kDevice };

using Invoke = uint64_t (*)(void* fn, const uint64_t* args);

struct VkCommand {
    const char* name;
    Level level;
    Invoke invoke;
};

template <typename T>
T arg(uint64_t v) {
    if constexpr (std::is_same_v<T, const VkAllocationCallbacks*>) {
        return nullptr;  // guest code: the host driver allocates for itself
    } else if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(static_cast<uintptr_t>(v));
    } else if constexpr (std::is_same_v<T, float>) {
        float f;
        const uint32_t bits = static_cast<uint32_t>(v);
        std::memcpy(&f, &bits, sizeof f);
        return f;
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(static_cast<std::underlying_type_t<T>>(v));
    } else {
        return static_cast<T>(v);
    }
}

template <typename R>
uint64_t result(R r) {
    if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<uintptr_t>(r);
    } else {
        return static_cast<uint64_t>(static_cast<int64_t>(r));
    }
}

template <typename F>
struct Invoker;

template <typename R, typename... A>
struct Invoker<R(VKAPI_PTR*)(A...)> {
    static uint64_t call(void* fn, const uint64_t* a) { return call(fn, a, std::index_sequence_for<A...>{}); }

    template <size_t... I>
    static uint64_t call(void* fn, [[maybe_unused]] const uint64_t* a, std::index_sequence<I...>) {
        auto f = reinterpret_cast<R(VKAPI_PTR*)(A...)>(fn);
        if constexpr (std::is_void_v<R>) {
            f(arg<A>(a[I])...);
            return 0;
        } else {
            return result(f(arg<A>(a[I])...));
        }
    }
};

#include "vk_entries.inc"

constexpr uint32_t kCount = sizeof kCommands / sizeof kCommands[0];

using Override = uint64_t (*)(const uint64_t* args);

std::once_flag g_load_once;
PFN_vkGetInstanceProcAddr g_gipa = nullptr;
std::atomic<void*> g_fns[kCount];
Override g_overrides[kCount];
// Live instances, oldest first. Commands are resolved through any of them: the
// loader's trampolines dispatch on the handle, whichever instance they came from.
std::mutex g_instances_lock;
std::vector<VkInstance> g_instances;
std::atomic<bool> g_android_surface{false};
std::atomic<uint64_t> g_fake_handles{0x0a0b0000};

bool load_loader() {
    std::call_once(g_load_once, [] {
        HMODULE m = LoadLibraryW(L"vulkan-1.dll");
        if (!m) {
            LOGE("vulkan", "vulkan-1.dll is not installed: the guest has no GPU");
            return;
        }
        g_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(m, "vkGetInstanceProcAddr"));
        LOGI("vulkan", "host Vulkan loader ready (%u guest commands)", kCount);
    });
    return g_gipa != nullptr;
}

template <typename F>
F host_proc(VkInstance instance, const char* name) {
    return load_loader() ? reinterpret_cast<F>(g_gipa(instance, name)) : nullptr;
}

VkInstance any_instance() {
    std::lock_guard<std::mutex> lock(g_instances_lock);
    return g_instances.empty() ? VK_NULL_HANDLE : g_instances.front();
}

void* resolve(uint32_t id) {
    void* fn = g_fns[id].load(std::memory_order_acquire);
    if (fn) return fn;
    const VkInstance instance = kCommands[id].level == kGlobal ? VK_NULL_HANDLE : any_instance();
    if (kCommands[id].level != kGlobal && instance == VK_NULL_HANDLE) return nullptr;
    fn = host_proc<void*>(instance, kCommands[id].name);
    if (fn) g_fns[id].store(fn, std::memory_order_release);
    return fn;
}

template <typename F>
F command(const char* name) {
    for (uint32_t i = 0; i < kCount; ++i) {
        if (std::strcmp(kCommands[i].name, name) == 0) return reinterpret_cast<F>(resolve(i));
    }
    return host_proc<F>(any_instance(), name);
}

// Unlinks structures from a pNext chain for the duration of a call (the chain is
// guest memory, so it is relinked afterwards).
class ChainFilter {
public:
    ChainFilter(const void** head, std::initializer_list<VkStructureType> drop) {
        const void** link = head;
        while (*link) {
            auto* s = static_cast<const VkBaseInStructure*>(*link);
            if (std::find(drop.begin(), drop.end(), s->sType) != drop.end()) {
                saved_.emplace_back(link, *link);
                *link = s->pNext;
            } else {
                link = reinterpret_cast<const void**>(const_cast<const VkBaseInStructure**>(&s->pNext));
            }
        }
    }
    ~ChainFilter() {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) *it->first = it->second;
    }

private:
    std::vector<std::pair<const void**, const void*>> saved_;
};

template <typename T>
VkResult fill(const std::vector<T>& items, uint32_t* count, T* out) {
    if (out == nullptr) {
        *count = static_cast<uint32_t>(items.size());
        return VK_SUCCESS;
    }
    const uint32_t n = std::min<uint32_t>(*count, static_cast<uint32_t>(items.size()));
    std::memcpy(out, items.data(), n * sizeof(T));
    *count = n;
    return n < items.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

bool windows_only(const char* name) {
    return std::strstr(name, "win32") != nullptr || std::strstr(name, "WIN32") != nullptr ||
           std::strcmp(name, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME) == 0 ||
           std::strcmp(name, "VK_NV_acquire_winrt_display") == 0 ||
           std::strcmp(name, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
}

std::vector<VkExtensionProperties> host_instance_extensions() {
    std::vector<VkExtensionProperties> out;
    auto enumerate = host_proc<PFN_vkEnumerateInstanceExtensionProperties>(VK_NULL_HANDLE,
                                                                           "vkEnumerateInstanceExtensionProperties");
    uint32_t n = 0;
    if (!enumerate || enumerate(nullptr, &n, nullptr) != VK_SUCCESS) return out;
    out.resize(n);
    enumerate(nullptr, &n, out.data());
    out.resize(n);
    return out;
}

std::vector<VkExtensionProperties> host_device_extensions(VkPhysicalDevice pd) {
    std::vector<VkExtensionProperties> out;
    auto enumerate = command<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    if (!enumerate || enumerate(pd, nullptr, &n, nullptr) != VK_SUCCESS) return out;
    out.resize(n);
    enumerate(pd, nullptr, &n, out.data());
    out.resize(n);
    return out;
}

bool has_extension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

// --- overrides ------------------------------------------------------------------------

uint64_t EnumerateInstanceExtensionProperties(const uint64_t* a) {
    auto* layer = arg<const char*>(a[0]);
    auto* count = arg<uint32_t*>(a[1]);
    auto* props = arg<VkExtensionProperties*>(a[2]);
    if (layer != nullptr) return result(VK_ERROR_LAYER_NOT_PRESENT);
    std::vector<VkExtensionProperties> visible;
    for (const auto& e : host_instance_extensions()) {
        if (std::strcmp(e.extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0) {
            VkExtensionProperties android{};
            std::strcpy(android.extensionName, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
            android.specVersion = VK_KHR_ANDROID_SURFACE_SPEC_VERSION;
            visible.push_back(android);
        } else if (!windows_only(e.extensionName)) {
            visible.push_back(e);
        }
    }
    return result(fill(visible, count, props));
}

uint64_t EnumerateLayerProperties(const uint64_t* a, int count_index) {
    *arg<uint32_t*>(a[count_index]) = 0;
    return result(VK_SUCCESS);
}

uint64_t EnumerateInstanceLayerProperties(const uint64_t* a) { return EnumerateLayerProperties(a, 0); }
uint64_t EnumerateDeviceLayerProperties(const uint64_t* a) { return EnumerateLayerProperties(a, 1); }

uint64_t CreateInstance(const uint64_t* a) {
    auto* info = arg<const VkInstanceCreateInfo*>(a[0]);
    auto* out = arg<VkInstance*>(a[2]);
    auto create = host_proc<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create) return result(VK_ERROR_INITIALIZATION_FAILED);
    const auto available = host_instance_extensions();
    VkInstanceCreateInfo ci = *info;
    std::vector<const char*> extensions;
    bool android_surface = false;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        const char* name = info->ppEnabledExtensionNames[i];
        if (std::strcmp(name, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME) == 0) {
            android_surface = true;
            name = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        }
        if (!has_extension(available, name)) {
            LOGW("vulkan", "instance extension %s is not available on the host GPU: not enabled", name);
            continue;
        }
        extensions.push_back(name);
    }
    for (uint32_t i = 0; i < info->enabledLayerCount; ++i) {
        LOGI("vulkan", "guest layer %s is not loaded", info->ppEnabledLayerNames[i]);
    }
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = 0;
    ci.ppEnabledLayerNames = nullptr;
    ChainFilter filter(&ci.pNext, {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                                   VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT});
    const VkApplicationInfo* app = info->pApplicationInfo;
    const VkResult r = create(&ci, nullptr, out);
    LOGI("vulkan", "vkCreateInstance(%s, api %u.%u, %u extensions) = %d", app && app->pApplicationName ? app->pApplicationName : "?",
         app ? VK_API_VERSION_MAJOR(app->apiVersion) : 1, app ? VK_API_VERSION_MINOR(app->apiVersion) : 0,
         ci.enabledExtensionCount, r);
    if (r == VK_SUCCESS) {
        {
            std::lock_guard<std::mutex> lock(g_instances_lock);
            g_instances.push_back(*out);
        }
        if (android_surface) g_android_surface.store(true);
    }
    return result(r);
}

uint64_t DestroyInstance(const uint64_t* a) {
    auto instance = arg<VkInstance>(a[0]);
    if (instance == VK_NULL_HANDLE) return 0;
    auto destroy = command<PFN_vkDestroyInstance>("vkDestroyInstance");
    if (destroy) destroy(instance, nullptr);
    std::lock_guard<std::mutex> lock(g_instances_lock);
    g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), instance), g_instances.end());
    return 0;
}

uint64_t EnumerateDeviceExtensionProperties(const uint64_t* a) {
    auto pd = arg<VkPhysicalDevice>(a[0]);
    auto* layer = arg<const char*>(a[1]);
    auto* count = arg<uint32_t*>(a[2]);
    auto* props = arg<VkExtensionProperties*>(a[3]);
    if (layer != nullptr) return result(VK_ERROR_LAYER_NOT_PRESENT);
    std::vector<VkExtensionProperties> visible;
    for (const auto& e : host_device_extensions(pd)) {
        if (!windows_only(e.extensionName)) visible.push_back(e);
    }
    return result(fill(visible, count, props));
}

uint64_t CreateDevice(const uint64_t* a) {
    auto pd = arg<VkPhysicalDevice>(a[0]);
    auto* info = arg<const VkDeviceCreateInfo*>(a[1]);
    auto* out = arg<VkDevice*>(a[3]);
    auto create = command<PFN_vkCreateDevice>("vkCreateDevice");
    if (!create) return result(VK_ERROR_INITIALIZATION_FAILED);
    const auto available = host_device_extensions(pd);
    VkDeviceCreateInfo ci = *info;
    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        const char* name = info->ppEnabledExtensionNames[i];
        if (!has_extension(available, name)) {
            LOGW("vulkan", "device extension %s is not available on the host GPU: not enabled", name);
            continue;
        }
        extensions.push_back(name);
        LOGI("vulkan", "device extension %s", name);
    }
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = 0;
    ci.ppEnabledLayerNames = nullptr;
    const VkInstance instance = any_instance();
    vktex::DeviceFeatures features;
    vktex::prepare_device(instance, pd, ci, features);
    const VkResult r = create(pd, &ci, nullptr, out);
    vktex::finish_device(instance, pd, ci, features, r, r == VK_SUCCESS ? *out : VK_NULL_HANDLE);
    LOGI("vulkan", "vkCreateDevice(%u queue families, %u extensions) = %d", ci.queueCreateInfoCount,
         ci.enabledExtensionCount, r);
    return result(r);
}

uint64_t CreateAndroidSurfaceKHR(const uint64_t* a) {
    auto instance = arg<VkInstance>(a[0]);
    auto* info = arg<const VkAndroidSurfaceCreateInfoKHR*>(a[1]);
    auto* out = arg<VkSurfaceKHR*>(a[3]);
    const uint64_t address = reinterpret_cast<uintptr_t>(info->window);
    NativeWindow* window = native_window(address);
    if (window == nullptr) {
        LOGE("vulkan", "vkCreateAndroidSurfaceKHR: %llx is not an ANativeWindow", static_cast<unsigned long long>(address));
        return result(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    }
    int32_t width = 0, height = 0;
    native_window_size(window, &width, &height);
    HWND hwnd = host_window_for(address, width, height);
    auto create = host_proc<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR");
    if (!hwnd || !create) return result(VK_ERROR_INITIALIZATION_FAILED);
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd;
    const VkResult r = create(instance, &ci, nullptr, out);
    LOGI("vulkan", "android surface %dx%d -> win32 surface: %d", width, height, r);
    return result(r);
}

uint64_t CreateFakeHandle(const uint64_t* a, int out_index) {
    *arg<uint64_t*>(a[out_index]) = g_fake_handles.fetch_add(1);
    return result(VK_SUCCESS);
}

uint64_t CreateDebugUtilsMessengerEXT(const uint64_t* a) { return CreateFakeHandle(a, 3); }
uint64_t CreateDebugReportCallbackEXT(const uint64_t* a) { return CreateFakeHandle(a, 3); }
uint64_t Nothing(const uint64_t*) { return 0; }
uint64_t ExtensionNotPresent(const uint64_t*) { return result(VK_ERROR_EXTENSION_NOT_PRESENT); }

void install_overrides() {
    const std::pair<const char*, Override> table[] = {
        {"vkEnumerateInstanceExtensionProperties", EnumerateInstanceExtensionProperties},
        {"vkEnumerateInstanceLayerProperties", EnumerateInstanceLayerProperties},
        {"vkEnumerateDeviceLayerProperties", EnumerateDeviceLayerProperties},
        {"vkCreateInstance", CreateInstance},
        {"vkDestroyInstance", DestroyInstance},
        {"vkEnumerateDeviceExtensionProperties", EnumerateDeviceExtensionProperties},
        {"vkCreateDevice", CreateDevice},
        {"vkCreateAndroidSurfaceKHR", CreateAndroidSurfaceKHR},
        {"vkCreateDebugUtilsMessengerEXT", CreateDebugUtilsMessengerEXT},
        {"vkDestroyDebugUtilsMessengerEXT", Nothing},
        {"vkSubmitDebugUtilsMessageEXT", Nothing},
        {"vkCreateDebugReportCallbackEXT", CreateDebugReportCallbackEXT},
        {"vkDestroyDebugReportCallbackEXT", Nothing},
        {"vkDebugReportMessageEXT", Nothing},
        {"vkGetAndroidHardwareBufferPropertiesANDROID", ExtensionNotPresent},
        {"vkGetMemoryAndroidHardwareBufferANDROID", ExtensionNotPresent},
    };
    auto install = [](const char* name, Override fn) {
        for (uint32_t i = 0; i < kCount; ++i) {
            if (std::strcmp(kCommands[i].name, name) == 0) {
                g_overrides[i] = fn;
                return true;
            }
        }
        return false;
    };
    for (const auto& [name, fn] : table) {
        if (!install(name, fn)) fatal("vulkan: override for unknown command %s", name);
    }
    // Texture substitution (vk_texture.h); commands newer than the guest
    // headers are simply never called.
    for (const auto& [name, fn] : vktex::overrides()) install(name, fn);
}

PFN_vkVoidFunction texture_lookup(VkInstance instance, const char* name) {
    if (instance) return host_proc<PFN_vkVoidFunction>(instance, name);
    return command<PFN_vkVoidFunction>(name);
}

bool supported(uint32_t id, uint64_t handle, uint32_t level) {
    const char* name = kCommands[id].name;
    if (std::strcmp(name, "vkCreateAndroidSurfaceKHR") == 0) return g_android_surface.load();
    if (std::strstr(name, "AndroidHardwareBuffer") != nullptr) return false;
    if (g_overrides[id] && level == kInstance) return true;
    if (level == kInstance) {
        return host_proc<void*>(reinterpret_cast<VkInstance>(handle), name) != nullptr;
    }
    auto gdpa = host_proc<PFN_vkGetDeviceProcAddr>(any_instance(), "vkGetDeviceProcAddr");
    return gdpa && gdpa(reinterpret_cast<VkDevice>(handle), name) != nullptr;
}

}  // namespace

bool vulkan_prepare_gpu_share() {
    wchar_t value[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"AXRB_GPU_SHARE", value, MAX_PATH) && std::wcscmp(value, L"0") == 0) return false;
    std::wstring dir;
    if (GetEnvironmentVariableW(L"AXRB_GPU_LAYER_DIR", value, MAX_PATH)) {
        dir = value;
    } else {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        dir = exe;
        dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\gpu";
    }
    if (GetFileAttributesW((dir + L"\\axrb_gpu_layer.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        LOGI("vulkan", "GPU-share layer not found in %ls: eye images use pixel transfer", dir.c_str());
        return false;
    }
    auto prepend = [](const wchar_t* name, const std::wstring& item, wchar_t separator) {
        wchar_t old[4096]{};
        std::wstring v = item;
        if (GetEnvironmentVariableW(name, old, 4096) && old[0]) v += separator + std::wstring(old);
        SetEnvironmentVariableW(name, v.c_str());
    };
    prepend(L"VK_LAYER_PATH", dir, L';');
    prepend(L"VK_INSTANCE_LAYERS", L"VK_LAYER_AXRB_gpu_share", L';');
    LOGI("vulkan", "GPU-share layer from %ls", dir.c_str());
    return true;
}

void vulkan_init() {
    vktex::init(texture_lookup);
    install_overrides();
    register_named_function("vk.call", [](HostCall& c) {
        const uint32_t id = static_cast<uint32_t>(c.x(0));
        if (id >= kCount) return c.ret(result(VK_ERROR_INITIALIZATION_FAILED));
        const auto* args = reinterpret_cast<const uint64_t*>(static_cast<uintptr_t>(c.x(1)));
        TRACE("vulkan", "%s", kCommands[id].name);
        if (g_overrides[id]) return c.ret(g_overrides[id](args));
        void* fn = resolve(id);
        if (!fn) {
            LOGE("vulkan", "%s is not available on the host", kCommands[id].name);
            return c.ret(result(VK_ERROR_EXTENSION_NOT_PRESENT));
        }
        c.ret(kCommands[id].invoke(fn, args));
    });
    register_named_function("vk.query", [](HostCall& c) {
        const uint32_t id = static_cast<uint32_t>(c.x(0));
        c.ret(id < kCount && supported(id, c.x(1), static_cast<uint32_t>(c.x(2))) ? 1 : 0);
    });
}

}  // namespace axrb
