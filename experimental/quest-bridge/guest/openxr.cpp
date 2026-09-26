/* OpenXR for the guest, by passing each call to the host's runtime.

   The same trick as vulkan.cpp. The guest's libopenxr_loader.so is this
   file: every xr* function it asks for is a thunk that calls the function of
   the same name through the host's openxr_loader.dll, which reaches whatever
   runtime the PC has (SteamVR, typically). Guest memory is identity mapped and
   OpenXR's structures have the same layout on both sides, so they go across
   untouched, pNext chains and all. And because the guest's Vulkan is the
   host's Vulkan, a Vulkan-backed session works as it is: the VkInstance and
   VkDevice the guest binds are the host's own, and the swapchain images the
   runtime hands back are VkImages the guest can render into directly.

   What needs care is only what is Android about the guest:
     - xrInitializeLoaderKHR (the loader wants the Java VM) is answered here;
     - XrInstanceCreateInfoAndroidKHR and other Android structures are taken
       out of the instance's pNext chain, and extensions the host lacks are
       left out of the request;
     - the runtime reports itself as Oculus's, since that is the device the
       guest believes it is on and the runtime it will accept. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <windows.h>
#include <tlhelp32.h>

#include "android.h"
#include "xr_table.h"

#include <openxr/openxr.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

void xr_capture_note(const std::string& name, const uint64_t* a, int64_t result);
void xr_capture_before_end();

namespace {

using HostFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t);

std::once_flag g_loaded;
HMODULE g_library = nullptr;
PFN_xrGetInstanceProcAddr g_get_proc = nullptr;
std::mutex g_lock;
std::unordered_map<std::string, const XrSignature*> g_signatures;
std::unordered_map<std::string, PFN_xrVoidFunction> g_resolved;
XrInstance g_instance = XR_NULL_HANDLE;
std::atomic<int64_t> g_xr_period_ns{0};
std::atomic<int64_t> g_xr_display_ns{0};
std::atomic<int64_t> g_wait_ns{0}, g_begin_ns{0}, g_end_ns{0};
std::atomic<uint64_t> g_input_active{0}, g_input_pressed{0}, g_input_located{0}, g_input_lost{0};
/* QB_XR_INPUT: what each space handle is (a reference space type, or an
   action space), and the last positions seen, for finding a head and hands
   measured against different origins. */
std::mutex g_pose_lock;
std::unordered_map<uint64_t, std::string> g_space_kind;
std::string g_last_head, g_last_hands[2];
/* Names for paths and actions, and each action's strongest reading in the
   current five seconds. */
std::unordered_map<uint64_t, std::string> g_path_names, g_action_names;
std::unordered_map<uint64_t, float> g_action_peak;
int g_hand_turn = 0;

/* Extensions that only mean something on Android, which the guest may ask
   for and the host never has. */
/* Extensions the host has that are kept from the guest. SteamVR offers
   XR_EXT_hand_tracking and answers it with a hand skeleton made up from the
   controllers; a Quest holding controllers reports no tracked hands, and a
   game that sees hand data may drive its hands from that skeleton instead
   of the controller poses. QB_XR_HANDS=1 lets it through. */
bool hidden_from_guest(const std::string& extension) {
    static const bool hands = QB_ENV("QB_XR_HANDS") != nullptr;
    if (!hands && (extension == "XR_EXT_hand_tracking" || extension == "XR_EXT_hand_joints_motion_range" ||
                   extension == "XR_EXT_hand_interaction" || extension == "XR_EXT_hand_tracking_data_source"))
        return true;
    return false;
}

const std::set<std::string> kAndroidOnly = {
    "XR_KHR_android_create_instance", "XR_KHR_loader_init", "XR_KHR_loader_init_android",
    "XR_KHR_android_thread_settings", "XR_KHR_android_surface_swapchain",
};

/* Meta's foveation extensions, which a Quest always has and a PC runtime
   does not. OVRPlugin sets up a fixed-foveation profile on every eye layer
   when the game asks for foveated rendering, and a missing extension fails
   the whole layer, so Unity never gets eye textures. Foveation only saves
   GPU time, so these are offered and answered here as doing nothing: the
   profile is accepted and the swapchain renders at full resolution. */
const std::vector<std::pair<const char*, uint32_t>> kEmulated = {
    {"XR_FB_foveation", 1},
    {"XR_FB_foveation_configuration", 1},
    {"XR_FB_swapchain_update_state", 3},
    {"XR_FB_swapchain_update_state_vulkan", 1},
};

bool is_emulated(const std::string& extension) {
    for (const auto& entry : kEmulated)
        if (extension == entry.first) return true;
    return false;
}

/* Answered here rather than passed on. */
const std::set<std::string> kOurs = {
    "xrGetInstanceProcAddr", "xrInitializeLoaderKHR", "xrEnumerateInstanceExtensionProperties",
    "xrEnumerateApiLayerProperties", "xrCreateInstance", "xrSetAndroidApplicationThreadKHR",
    "xrGetInstanceProperties",
    /* the emulated extensions' functions */
    "xrCreateFoveationProfileFB", "xrDestroyFoveationProfileFB", "xrUpdateSwapchainFB", "xrGetSwapchainStateFB",
    /* vulkan_enable2, made through vulkan_enable */
    "xrCreateVulkanInstanceKHR", "xrCreateVulkanDeviceKHR", "xrGetVulkanGraphicsDevice2KHR",
};

/* XrSwapchainCreateInfoFoveationFB, which only means something with the
   foveation extension the host does not have. */
const int kSwapchainCreateInfoFoveation = XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB;

/* Which runtime the guest reaches: SteamVR when it is running (Steam Link,
   or any SteamVR headset), otherwise the machine's active runtime, which is
   Oculus's while the Meta Link app is open. QB_XR_RUNTIME=steamvr|oculus
   forces one; XR_RUNTIME_JSON, if already set, wins over everything. Same rule
   as qb-host, so the two always land on the same runtime. */
void choose_runtime() {
    if (GetEnvironmentVariableA("XR_RUNTIME_JSON", nullptr, 0) > 0) return;
    const char* steamvr = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json";
    const char* forced = QB_ENV("QB_XR_RUNTIME");
    std::string want = forced ? forced : "";
    if (want == "active") return; // AXRB follows the user's registered runtime.
    if (want == "oculus") return; /* the active runtime, which the Link app registers */
    bool use_steamvr = want == "steamvr";
    if (!use_steamvr && GetFileAttributesA(steamvr) != INVALID_FILE_ATTRIBUTES) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (Process32First(snap, &entry)) {
                do {
                    if (_stricmp(entry.szExeFile, "vrserver.exe") == 0) use_steamvr = true;
                } while (!use_steamvr && Process32Next(snap, &entry));
            }
            CloseHandle(snap);
        }
    }
    if (!use_steamvr) return;
    SetEnvironmentVariableA("XR_RUNTIME_JSON", steamvr);
    _putenv_s("XR_RUNTIME_JSON", steamvr);
    std::printf("openxr: the guest will use SteamVR\n");
}

void load() {
    std::call_once(g_loaded, [] {
        choose_runtime();
        g_library = LoadLibraryA("openxr_loader.dll");
        if (!g_library) {
            std::fprintf(stderr, "openxr: the host has no openxr_loader.dll\n");
            return;
        }
        g_get_proc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(g_library, "xrGetInstanceProcAddr"));
        for (const XrSignature& entry : kXrSignatures) g_signatures[entry.name] = &entry;
    });
}

/* The functions that exist before any instance does are always resolved
   against no instance; the rest against the current one. */
bool is_global(const std::string& name) {
    return name == "xrEnumerateInstanceExtensionProperties" || name == "xrEnumerateApiLayerProperties" ||
           name == "xrCreateInstance";
}

PFN_xrVoidFunction host_function(const std::string& name) {
    std::lock_guard<std::mutex> held(g_lock);
    auto found = g_resolved.find(name);
    if (found != g_resolved.end()) return found->second;
    PFN_xrVoidFunction fn = nullptr;
    XrInstance instance = is_global(name) ? XR_NULL_HANDLE : g_instance;
    if (g_get_proc && XR_SUCCEEDED(g_get_proc(instance, name.c_str(), &fn)) && fn) g_resolved[name] = fn;
    return fn;
}

std::vector<XrExtensionProperties> host_extensions() {
    std::vector<XrExtensionProperties> list;
    auto enumerate =
        reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(host_function("xrEnumerateInstanceExtensionProperties"));
    if (!enumerate) return list;
    uint32_t count = 0;
    if (XR_FAILED(enumerate(nullptr, 0, &count, nullptr))) return list;
    list.resize(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    enumerate(nullptr, count, &count, list.data());
    list.resize(count);
    return list;
}

}  // namespace

bool GuestLibc::openxr_call(const std::string& name, GuestCpu& cpu) {
    load();
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](int64_t value) { cpu.x[0] = (uint64_t)value; };
    const bool trace = QB_ENV("QB_TRACE_XR") != nullptr;
    if (!g_get_proc) {
        ret(XR_ERROR_RUNTIME_UNAVAILABLE);
        return true;
    }
    if (trace && name != "xrGetInstanceProcAddr" && kOurs.count(name))
        std::printf("openxr: %s (answered here, x0 %llx x1 %llx)\n", name.c_str(), (unsigned long long)arg(0),
                    (unsigned long long)arg(1));

    if (name == "xrGetInstanceProcAddr") {
        const char* wanted = reinterpret_cast<const char*>(arg(1));
        uint64_t* out = reinterpret_cast<uint64_t*>(arg(2));
        if (!wanted || !out) {
            ret(XR_ERROR_VALIDATION_FAILURE);
            return true;
        }
        std::string want = wanted;
        PFN_xrVoidFunction host = nullptr;
        bool ours = kOurs.count(want) != 0;
        if (!ours && g_get_proc) g_get_proc(arg(0) ? (XrInstance)arg(0) : g_instance, wanted, &host);
        *out = (ours || host) ? image->linker.thunk_for(want) : 0;
        if (trace) std::printf("openxr: xrGetInstanceProcAddr(%s) -> %s, into %llx\n", wanted,
                               (ours || host) ? "yes" : "no", (unsigned long long)arg(2));
        ret((ours || host) ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED);
        return true;
    }
    if (name == "xrInitializeLoaderKHR" || name == "xrSetAndroidApplicationThreadKHR") {
        ret(XR_SUCCESS);
        return true;
    }
    if (name == "xrEnumerateApiLayerProperties") {
        uint32_t* count = reinterpret_cast<uint32_t*>(arg(1));
        if (count) *count = 0;
        ret(XR_SUCCESS);
        return true;
    }
    if (name == "xrEnumerateInstanceExtensionProperties") {
        /* The host's, plus the Android ones the guest's own loader would
           have offered, which are answered here. */
        std::vector<XrExtensionProperties> list = arg(0) ? std::vector<XrExtensionProperties>() : host_extensions();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const XrExtensionProperties& e) { return hidden_from_guest(e.extensionName); }),
                   list.end());
        if (!arg(0)) {
            for (const char* android : {"XR_KHR_android_create_instance", "XR_KHR_loader_init",
                                        "XR_KHR_loader_init_android", "XR_KHR_android_thread_settings"}) {
                XrExtensionProperties entry{XR_TYPE_EXTENSION_PROPERTIES};
                std::snprintf(entry.extensionName, sizeof(entry.extensionName), "%s", android);
                entry.extensionVersion = 1;
                list.push_back(entry);
            }
            for (const auto& emulated : kEmulated) {
                bool host_has = false;
                for (const XrExtensionProperties& e : list) host_has = host_has || emulated.first == std::string(e.extensionName);
                if (host_has) continue;
                XrExtensionProperties entry{XR_TYPE_EXTENSION_PROPERTIES};
                std::snprintf(entry.extensionName, sizeof(entry.extensionName), "%s", emulated.first);
                entry.extensionVersion = emulated.second;
                list.push_back(entry);
            }
        }
        uint32_t capacity = (uint32_t)arg(1);
        uint32_t* count = reinterpret_cast<uint32_t*>(arg(2));
        XrExtensionProperties* out = reinterpret_cast<XrExtensionProperties*>(arg(3));
        if (!count) {
            ret(XR_ERROR_VALIDATION_FAILURE);
            return true;
        }
        *count = (uint32_t)list.size();
        if (!capacity) {
            ret(XR_SUCCESS);
            return true;
        }
        if (capacity < list.size()) {
            ret(XR_ERROR_SIZE_INSUFFICIENT);
            return true;
        }
        for (size_t i = 0; i < list.size(); ++i) {
            void* next = out[i].next;
            out[i] = list[i];
            out[i].next = next;
        }
        ret(XR_SUCCESS);
        return true;
    }
    if (name == "xrCreateInstance") {
        XrInstanceCreateInfo info = *reinterpret_cast<const XrInstanceCreateInfo*>(arg(0));
        info.next = nullptr; /* XrInstanceCreateInfoAndroidKHR and friends */
        info.enabledApiLayerCount = 0;
        info.enabledApiLayerNames = nullptr;
        std::set<std::string> host;
        for (const XrExtensionProperties& e : host_extensions()) host.insert(e.extensionName);
        std::vector<const char*> kept;
        for (uint32_t i = 0; i < info.enabledExtensionCount; ++i) {
            const char* extension = info.enabledExtensionNames[i];
            if (host.count(extension) && !hidden_from_guest(extension)) kept.push_back(extension);
            else if (is_emulated(extension)) std::printf("openxr: instance extension %s is emulated here\n", extension);
            else if (!kAndroidOnly.count(extension))
                std::printf("openxr: instance extension %s is not on this host, left out\n", extension);
        }
        /* vulkan_enable2 is answered here through vulkan_enable's queries
           (see xrCreateVulkanInstanceKHR below), so a guest asking for the
           first gets the second enabled alongside it. */
        bool wants_enable2 = false, has_enable1 = false;
        for (const char* one : kept) {
            wants_enable2 = wants_enable2 || std::string(one) == "XR_KHR_vulkan_enable2";
            has_enable1 = has_enable1 || std::string(one) == "XR_KHR_vulkan_enable";
        }
        if (wants_enable2 && !has_enable1 && host.count("XR_KHR_vulkan_enable")) kept.push_back("XR_KHR_vulkan_enable");
        info.enabledExtensionCount = (uint32_t)kept.size();
        info.enabledExtensionNames = kept.data();
        /* The Quest's runtime accepts an empty application name, which
           OVRPlugin's pre-initialisation sends; a PC runtime rejects it with
           XR_ERROR_NAME_INVALID. Name it after the package. */
        if (!info.applicationInfo.applicationName[0])
            std::snprintf(info.applicationInfo.applicationName, sizeof(info.applicationInfo.applicationName), "%s",
                          property("ro.product.name").empty() ? "QuestBridgeGuest" : guest_package());
        if (!info.applicationInfo.engineName[0])
            std::snprintf(info.applicationInfo.engineName, sizeof(info.applicationInfo.engineName), "%s", "Unity");
        /* A runtime may not offer the newest API version the guest was built
           against; ask for 1.0, which every runtime takes. */
        if (XR_VERSION_MAJOR(info.applicationInfo.apiVersion) == 1 &&
            XR_VERSION_MINOR(info.applicationInfo.apiVersion) > 0)
            info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
        auto create = reinterpret_cast<PFN_xrCreateInstance>(host_function("xrCreateInstance"));
        XrInstance* out = reinterpret_cast<XrInstance*>(arg(1));
        XrResult result = create ? create(&info, out) : XR_ERROR_RUNTIME_UNAVAILABLE;
        if (XR_SUCCEEDED(result)) {
            std::lock_guard<std::mutex> held(g_lock);
            g_instance = *out;
            g_resolved.clear();
        }
        std::printf("openxr: xrCreateInstance with %zu extensions -> %d\n", kept.size(), (int)result);
        for (const char* one : kept) std::printf("openxr:   %s\n", one);
        ret(result);
        return true;
    }
    /* XR_KHR_vulkan_enable2: the runtime would create the Vulkan instance and
       device itself, through the application's vkGetInstanceProcAddr, which
       is arm64 code it cannot call. So they are made here instead: the
       runtime's required extensions (vulkan_enable's queries) are added to
       the application's, and the object is created through the guest's own
       Vulkan (vulkan.cpp), whose handles are the host's. */
    if (name == "xrCreateVulkanInstanceKHR" || name == "xrCreateVulkanDeviceKHR") {
        bool device = name == "xrCreateVulkanDeviceKHR";
        struct CreateInfo {
            uint32_t type;
            uint32_t pad;
            uint64_t next, system, flags, get_proc;
            uint64_t second;  /* instance: vulkanCreateInfo; device: vulkanPhysicalDevice */
            uint64_t third;   /* instance: vulkanAllocator; device: vulkanCreateInfo */
            uint64_t fourth;  /* device: vulkanAllocator */
        };
        const CreateInfo* xr_info = reinterpret_cast<const CreateInfo*>(arg(1));
        uint64_t out_handle = arg(2);
        int32_t* vk_result = reinterpret_cast<int32_t*>(arg(3));
        if (!xr_info) {
            ret(XR_ERROR_VALIDATION_FAILURE);
            return true;
        }
        /* The runtime's required extensions, space separated. */
        using GetExtensions = XrResult (*)(XrInstance, XrSystemId, uint32_t, uint32_t*, char*);
        auto get = reinterpret_cast<GetExtensions>(
            host_function(device ? "xrGetVulkanDeviceExtensionsKHR" : "xrGetVulkanInstanceExtensionsKHR"));
        std::string required;
        uint32_t size = 0;
        if (get && XR_SUCCEEDED(get((XrInstance)arg(0), (XrSystemId)xr_info->system, 0, &size, nullptr)) && size) {
            required.resize(size);
            get((XrInstance)arg(0), (XrSystemId)xr_info->system, size, &size, required.data());
            required.resize(std::strlen(required.c_str()));
        }
        /* VkInstanceCreateInfo and VkDeviceCreateInfo both keep their
           extension count and names at the end: instance at +48/+56, device
           at +48/+56 as well (after queue and layer fields). */
        const uint8_t* vk_info = reinterpret_cast<const uint8_t*>(device ? xr_info->third : xr_info->second);
        if (!vk_info) {
            ret(XR_ERROR_VALIDATION_FAILURE);
            return true;
        }
        std::vector<uint8_t> copy(vk_info, vk_info + (device ? 72 : 64)); /* device adds pEnabledFeatures */
        uint32_t count = 0;
        uint64_t names = 0;
        std::memcpy(&count, vk_info + 48, 4);
        std::memcpy(&names, vk_info + 56, 8);
        static std::vector<std::string> kept_strings[2]; /* live for the call and after */
        std::vector<std::string>& strings = kept_strings[device ? 1 : 0];
        strings.clear();
        for (uint32_t i = 0; i < count; ++i)
            strings.push_back(reinterpret_cast<const char* const*>(names)[i]);
        for (size_t start = 0; start < required.size();) {
            size_t end = required.find(' ', start);
            std::string one = required.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!one.empty() && std::find(strings.begin(), strings.end(), one) == strings.end()) strings.push_back(one);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        static std::vector<const char*> pointers[2];
        std::vector<const char*>& list = pointers[device ? 1 : 0];
        list.clear();
        for (const std::string& one : strings) list.push_back(one.c_str());
        uint32_t new_count = (uint32_t)list.size();
        uint64_t new_names = reinterpret_cast<uint64_t>(list.data());
        std::memcpy(copy.data() + 48, &new_count, 4);
        std::memcpy(copy.data() + 56, &new_names, 8);
        GuestCpu call{};
        if (device) {
            call.x[0] = xr_info->second; /* the physical device */
            call.x[1] = reinterpret_cast<uint64_t>(copy.data());
            call.x[2] = xr_info->fourth;
            call.x[3] = out_handle;
        } else {
            call.x[0] = reinterpret_cast<uint64_t>(copy.data());
            call.x[1] = xr_info->third;
            call.x[2] = out_handle;
        }
        call.tpidr = cpu.tpidr;
        vulkan_call(device ? "vkCreateDevice" : "vkCreateInstance", call);
        int32_t result = (int32_t)(uint32_t)call.x[0];
        if (vk_result) *vk_result = result;
        std::printf("openxr: %s: %u of the application's extensions + the runtime's -> vk %d\n", name.c_str(), count,
                    result);
        ret(result == 0 ? XR_SUCCESS : XR_ERROR_VALIDATION_FAILURE);
        return true;
    }
    if (name == "xrGetVulkanGraphicsDevice2KHR") {
        /* {type, next, systemId, vulkanInstance} -> vulkan_enable's form. */
        const uint64_t* info = reinterpret_cast<const uint64_t*>(arg(1));
        using GetDevice = XrResult (*)(XrInstance, XrSystemId, uint64_t, uint64_t*);
        auto get = reinterpret_cast<GetDevice>(host_function("xrGetVulkanGraphicsDeviceKHR"));
        XrResult result = info && get ? get((XrInstance)arg(0), (XrSystemId)info[2], info[3],
                                            reinterpret_cast<uint64_t*>(arg(2)))
                                      : XR_ERROR_FUNCTION_UNSUPPORTED;
        ret(result);
        return true;
    }
    if (name == "xrGetInstanceProperties") {
        auto get = reinterpret_cast<PFN_xrGetInstanceProperties>(host_function("xrGetInstanceProperties"));
        XrInstanceProperties* out = reinterpret_cast<XrInstanceProperties*>(arg(1));
        XrResult result = get ? get((XrInstance)arg(0), out) : XR_ERROR_RUNTIME_FAILURE;
        if (XR_SUCCEEDED(result) && out) {
            std::printf("openxr: the host runtime is %s; the guest is told Oculus\n", out->runtimeName);
            std::snprintf(out->runtimeName, sizeof(out->runtimeName), "%s", "Oculus");
        }
        ret(result);
        return true;
    }

    /* The emulated foveation extensions: a profile is a token, and updating
       a swapchain's foveation state changes nothing on the host. */
    if (name == "xrCreateFoveationProfileFB") {
        static std::atomic<uint64_t> next_profile{0x0f0ea7000001ull};
        uint64_t* out = reinterpret_cast<uint64_t*>(arg(2));
        if (out) *out = next_profile++;
        if (trace) std::printf("openxr: xrCreateFoveationProfileFB (emulated)\n");
        ret(XR_SUCCESS);
        return true;
    }
    if (name == "xrDestroyFoveationProfileFB" || name == "xrUpdateSwapchainFB" || name == "xrGetSwapchainStateFB") {
        if (trace) std::printf("openxr: %s (emulated)\n", name.c_str());
        ret(XR_SUCCESS);
        return true;
    }
    if (name == "xrCreateSwapchain") {
        /* Unlink the foveation struct for the call, then put it back: the
           chain is the guest's own memory. */
        XrBaseInStructure* info = reinterpret_cast<XrBaseInStructure*>(arg(1));
        std::vector<std::pair<XrBaseInStructure*, const XrBaseInStructure*>> unlinked;
        for (XrBaseInStructure* at = info; at && at->next;) {
            const XrBaseInStructure* next = at->next;
            if ((int)next->type == kSwapchainCreateInfoFoveation) {
                unlinked.push_back({at, next});
                at->next = next->next;
            } else {
                at = const_cast<XrBaseInStructure*>(next);
            }
        }
        auto create = reinterpret_cast<PFN_xrCreateSwapchain>(host_function(name));
        XrResult result = create ? create((XrSession)arg(0), reinterpret_cast<const XrSwapchainCreateInfo*>(info),
                                          reinterpret_cast<XrSwapchain*>(arg(2)))
                                 : XR_ERROR_FUNCTION_UNSUPPORTED;
        for (auto it = unlinked.rbegin(); it != unlinked.rend(); ++it) it->first->next = it->second;
        {
            uint64_t args[3] = {arg(0), arg(1), arg(2)};
            xr_capture_note(name, args, result);
        }
        if (trace) {
            const XrSwapchainCreateInfo* c = reinterpret_cast<const XrSwapchainCreateInfo*>(info);
            std::printf("openxr: xrCreateSwapchain %ux%u format %lld samples %u faces %u array %u mips %u usage %llx%s -> %d\n",
                        c->width, c->height, (long long)c->format, c->sampleCount, c->faceCount, c->arraySize,
                        c->mipCount, (unsigned long long)c->usageFlags,
                        unlinked.empty() ? "" : " (foveation struct left out)", (int)result);
        }
        ret(result);
        return true;
    }

    if (name == "xrDestroyInstance") {
        auto destroy = reinterpret_cast<PFN_xrDestroyInstance>(host_function(name));
        XrResult result = destroy ? destroy((XrInstance)arg(0)) : XR_ERROR_HANDLE_INVALID;
        std::lock_guard<std::mutex> held(g_lock);
        if ((XrInstance)arg(0) == g_instance) {
            /* Everything resolved against it went with it. */
            g_instance = XR_NULL_HANDLE;
            g_resolved.clear();
        }
        ret(result);
        return true;
    }

    if (name == "xrCreateReferenceSpace") {
        /* Meta's private reference spaces (XR_OCULUS_common_reference_spaces,
           types 1000043000 and 1000043001: the eye-level and floor-level
           tracking origins) do not exist on other runtimes. OVRPlugin uses
           them as its app space regardless, and with none it cannot locate
           the eyes. The standard LOCAL and STAGE spaces mean the same. */
        auto create = reinterpret_cast<PFN_xrCreateReferenceSpace>(host_function(name));
        XrReferenceSpaceCreateInfo info = *reinterpret_cast<const XrReferenceSpaceCreateInfo*>(arg(1));
        XrSpace* out = reinterpret_cast<XrSpace*>(arg(2));
        XrResult result = create ? create((XrSession)arg(0), &info, out) : XR_ERROR_FUNCTION_UNSUPPORTED;
        if (result == XR_ERROR_REFERENCE_SPACE_UNSUPPORTED &&
            ((int)info.referenceSpaceType == 1000043000 || (int)info.referenceSpaceType == 1000043001)) {
            int asked = (int)info.referenceSpaceType;
            info.referenceSpaceType = asked == 1000043001 ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
            result = create((XrSession)arg(0), &info, out);
            if (result == XR_ERROR_REFERENCE_SPACE_UNSUPPORTED) {
                info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
                result = create((XrSession)arg(0), &info, out);
            }
            std::printf("openxr: reference space %d stood in for by %d -> %d\n", asked, (int)info.referenceSpaceType,
                        (int)result);
        }
        if (XR_SUCCEEDED(result) && out) {
            std::lock_guard<std::mutex> held(g_pose_lock);
            g_space_kind[(uint64_t)*out] = "ref" + std::to_string((int)reinterpret_cast<const XrReferenceSpaceCreateInfo*>(arg(1))->referenceSpaceType) +
                                           "/used" + std::to_string((int)info.referenceSpaceType);
        }
        ret(result);
        return true;
    }

    /* Everything else goes through as it is. */
    auto signature = g_signatures.find(name);
    PFN_xrVoidFunction fn = host_function(name);
    if (signature == g_signatures.end() || !fn) {
        std::printf("openxr: %s is not available on this host\n", name.c_str());
        ret(XR_ERROR_FUNCTION_UNSUPPORTED);
        return true;
    }
    const XrSignature& sig = *signature->second;
    uint64_t a[12] = {};
    int integers = 0, floats = 0;
    uint64_t stack = cpu.sp;
    for (int i = 0; i < sig.params && i < 12; ++i) {
        if (sig.floats & (1u << i)) {
            a[i] = cpu.q[floats++].lo & 0xffffffffull;
        } else if (integers < 8) {
            a[i] = cpu.x[integers++];
        } else {
            a[i] = *reinterpret_cast<uint64_t*>(stack);
            stack += 8;
        }
    }
    if (trace) std::printf("openxr: %s\n", name.c_str());
    /* Who tore a swapchain down, since nothing about the call itself says. */
    if (trace && name == "xrDestroySwapchain") {
        std::printf("openxr:   from %s", guest_describe(cpu.x[30]).c_str());
        for (uint64_t at = cpu.sp, shown = 0; at < cpu.sp + 0x800 && shown < 12; at += 8) {
            uint64_t word = 0;
            std::memcpy(&word, reinterpret_cast<void*>(at), 8);
            if (word < 0x2000000000ull || word > 0x3000000000ull) continue;
            std::string where = guest_describe(word);
            if (where.find('+') == std::string::npos) continue;
            std::printf(" <- %s", where.c_str());
            ++shown;
        }
        std::printf("\n");
    }
    if (sig.floats) {
        /* The few that take a float, which x64 wants in an XMM register. */
        float value;
        int which = 0;
        while (!(sig.floats & (1u << which))) ++which;
        uint32_t bits = (uint32_t)a[which];
        std::memcpy(&value, &bits, 4);
        int64_t result = 0;
        if (which == 1) result = reinterpret_cast<XrResult (*)(uint64_t, float)>(fn)(a[0], value);
        else if (which == 3)
            result = reinterpret_cast<XrResult (*)(uint64_t, uint64_t, uint64_t, float)>(fn)(a[0], a[1], a[2], value);
        else {
            std::printf("openxr: %s takes a float in a shape not handled\n", name.c_str());
            result = XR_ERROR_FUNCTION_UNSUPPORTED;
        }
        ret(result);
        return true;
    }
    if (name == "xrWaitFrame" || name == "xrBeginFrame" || name == "xrEndFrame") {
        static std::atomic<int> counts[3];
        int which = name == "xrWaitFrame" ? 0 : name == "xrBeginFrame" ? 1 : 2;
        int n = ++counts[which];
        if (n == 1 || n == 10 || n == 100 || n == 1000 || n % 10000 == 0) { std::printf("xr: call %d of %s\n", n, name.c_str()); std::fflush(stdout); }
    }
    if (name == "xrEndFrame") {
        xr_capture_before_end();
        /* A frame-rate line every five seconds while frames are flowing. */
        static std::mutex fps_lock;
        static auto window_start = std::chrono::steady_clock::now();
        static int frames = 0;
        std::lock_guard<std::mutex> held(fps_lock);
        ++frames;
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - window_start).count();
        if (elapsed >= 5.0) {
            if (QB_ENV("QB_XR_INPUT")) {
                std::printf("xr: input: %llu active reads, %llu pressed, %llu located spaces, %llu controller locates lost\n",
                            (unsigned long long)g_input_active.exchange(0), (unsigned long long)g_input_pressed.exchange(0),
                            (unsigned long long)g_input_located.exchange(0), (unsigned long long)g_input_lost.exchange(0));
                std::lock_guard<std::mutex> held(g_pose_lock);
                std::printf("xr:   %s\nxr:   %s\nxr:   %s\n", g_last_head.c_str(), g_last_hands[0].c_str(),
                            g_last_hands[1].c_str());
                std::string peaks;
                for (auto& entry : g_action_peak) {
                    char one[96];
                    std::snprintf(one, sizeof(one), " %s=%.2f", g_action_names[entry.first].c_str(), entry.second);
                    peaks += one;
                }
                std::printf("xr:   peaks:%s\n", peaks.c_str());
                g_action_peak.clear();
            }
            int64_t period = g_xr_period_ns.load(std::memory_order_relaxed);
            std::printf("xr: %.1f frames a second (%d in %.1f s), runtime paces at %.1f Hz\n", frames / elapsed,
                        frames, elapsed, period > 0 ? 1e9 / (double)period : 0.0);
            std::printf("xr:   per frame: %.2f ms in xrWaitFrame, %.2f ms in xrBeginFrame, %.2f ms in xrEndFrame, %.2f ms total\n",
                        g_wait_ns.exchange(0) / 1e6 / frames, g_begin_ns.exchange(0) / 1e6 / frames,
                        g_end_ns.exchange(0) / 1e6 / frames, elapsed * 1000.0 / frames);
            std::fflush(stdout);
            frames = 0;
            window_start = std::chrono::steady_clock::now();
        }
    }
    auto call_start = std::chrono::steady_clock::now();
    uint64_t result = reinterpret_cast<HostFn>(fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                                  a[10], a[11]);
    if (name == "xrWaitFrame" || name == "xrEndFrame" || name == "xrBeginFrame") {
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - call_start).count();
        (name == "xrWaitFrame" ? g_wait_ns : name == "xrEndFrame" ? g_end_ns : g_begin_ns) += ns;
    }
    /* OVRPlugin locates its spaces at a time SteamVR calls invalid (the
       Quest runtime takes it), fails, and so re-announces the app space
       every frame, each with a logged stack. The latest predicted display
       time is a time the runtime always takes. */
    if (name == "xrLocateSpace" && (int32_t)(uint32_t)result == -30 /* XR_ERROR_TIME_INVALID */) {
        int64_t display = g_xr_display_ns.load(std::memory_order_relaxed);
        static std::atomic<bool> told{false};
        if (!told.exchange(true))
            std::printf("openxr: xrLocateSpace at time %lld is invalid to the runtime; using the frame's %lld\n",
                        (long long)a[2], (long long)display);
        if (display > 0)
            result = reinterpret_cast<HostFn>(fn)(a[0], a[1], (uint64_t)display, a[3], 0, 0, 0, 0, 0, 0, 0, 0);
    }
    /* QB_BENCH_POSE: every pose frozen at the first one located, so the
       game draws the same view every run and frame rates compare. For
       measuring only: the view does not follow the head. */
    static const bool bench_pose = QB_ENV("QB_BENCH_POSE") != nullptr;
    if (bench_pose && (int32_t)(uint32_t)result == 0) {
        static std::mutex frozen_lock;
        static std::map<std::pair<uint64_t, uint64_t>, std::vector<uint8_t>> frozen;
        std::lock_guard<std::mutex> held(frozen_lock);
        if (name == "xrLocateSpace" && a[3]) {
            /* XrSpaceLocation: flags at 16, pose (28 bytes) at 24. */
            uint8_t* location = reinterpret_cast<uint8_t*>(a[3]);
            uint64_t flags = 0;
            std::memcpy(&flags, location + 16, 8);
            auto& kept = frozen[{a[0], a[1]}];
            /* Frozen only once tracked (orientation and position valid): a
               frozen "not tracked" would keep the game waiting for tracking. */
            if (kept.empty()) {
                if ((flags & 3) == 3) kept.assign(location + 16, location + 52);
            } else {
                std::memcpy(location + 16, kept.data(), kept.size());
            }
        } else if (name == "xrLocateViews" && a[4] && a[5]) {
            /* XrView: pose (28 bytes) at 16 and fov at 44, 64 bytes each;
               XrViewState: flags at 16. */
            uint32_t count = *reinterpret_cast<uint32_t*>(a[4]);
            uint8_t* views = reinterpret_cast<uint8_t*>(a[5]);
            auto& kept = frozen[{~0ull, (uint64_t)count}];
            uint64_t state_flags = 0;
            if (a[3]) std::memcpy(&state_flags, reinterpret_cast<const uint8_t*>(a[3]) + 16, 8);
            if (kept.empty()) {
                if ((state_flags & 3) == 3) kept.assign(views, views + 64 * (size_t)count);
            } else
                for (uint32_t i = 0; i < count; ++i)
                    std::memcpy(views + 64 * i + 16, kept.data() + 64 * i + 16, 44);
        }
    }
    ret((int64_t)(int32_t)(uint32_t)result); /* every OpenXR function returns XrResult */
    /* QB_XR_INPUT: what input reads return, summed and shown with the
       frame-rate line: states the runtime calls active, buttons down,
       triggers pressed, and spaces with a valid position. */
    static const bool input_stats = QB_ENV("QB_XR_INPUT") != nullptr;
    if (input_stats && (int32_t)(uint32_t)result == 0) {
        std::lock_guard<std::mutex> held(g_pose_lock);
        if (name == "xrStringToPath" && a[1] && a[2]) {
            g_path_names[*reinterpret_cast<const uint64_t*>(a[2])] = reinterpret_cast<const char*>(a[1]);
        } else if (name == "xrCreateAction" && a[1] && a[2]) {
            const char* action_name = reinterpret_cast<const char*>(a[1]) + 16;
            g_action_names[*reinterpret_cast<const uint64_t*>(a[2])] = action_name;
        } else if (name == "xrSuggestInteractionProfileBindings" && a[1]) {
            const uint8_t* suggested = reinterpret_cast<const uint8_t*>(a[1]);
            uint64_t profile = 0, list = 0;
            uint32_t count = 0;
            std::memcpy(&profile, suggested + 16, 8);
            std::memcpy(&count, suggested + 24, 4);
            std::memcpy(&list, suggested + 32, 8);
            std::printf("xr: bindings for %s:\n", g_path_names[profile].c_str());
            for (uint32_t i = 0; i < count && list; ++i) {
                uint64_t action = 0, path = 0;
                std::memcpy(&action, reinterpret_cast<const uint8_t*>(list) + 16 * i, 8);
                std::memcpy(&path, reinterpret_cast<const uint8_t*>(list) + 16 * i + 8, 8);
                std::printf("xr:   %-24s <- %s\n", g_action_names[action].c_str(), g_path_names[path].c_str());
            }
        } else if (name == "xrGetCurrentInteractionProfile" && a[2]) {
            /* Each change of what a hand reports, as it happens. */
            uint64_t profile = 0;
            std::memcpy(&profile, reinterpret_cast<const uint8_t*>(a[2]) + 16, 8);
            static std::unordered_map<uint64_t, uint64_t> last;
            auto seen = last.find(a[1]);
            if (seen == last.end() || seen->second != profile) {
                last[a[1]] = profile;
                std::printf("xr: profile for %s is now %s\n", g_path_names[a[1]].c_str(),
                            profile ? g_path_names[profile].c_str() : "(none)");
            }
        } else if ((name == "xrGetActionStateBoolean" || name == "xrGetActionStateFloat") && a[1] && a[2]) {
            uint64_t action = 0;
            std::memcpy(&action, reinterpret_cast<const uint8_t*>(a[1]) + 16, 8);
            float value = 0;
            if (name == "xrGetActionStateBoolean") {
                uint32_t on = 0;
                std::memcpy(&on, reinterpret_cast<const uint8_t*>(a[2]) + 16, 4);
                value = on ? 1.0f : 0.0f;
            } else {
                std::memcpy(&value, reinterpret_cast<const uint8_t*>(a[2]) + 16, 4);
            }
            float& peak = g_action_peak[action];
            if (value > peak) peak = value;
        }
    }
    if (input_stats && (int32_t)(uint32_t)result == 0) {
        const uint8_t* out = reinterpret_cast<const uint8_t*>(a[2]);
        if (name == "xrGetActionStateBoolean" && out) {
            uint32_t current = 0, active = 0;
            std::memcpy(&current, out + 16, 4);
            std::memcpy(&active, out + 32, 4);
            g_input_active.fetch_add(active ? 1 : 0);
            g_input_pressed.fetch_add(current ? 1 : 0);
        } else if (name == "xrGetActionStateFloat" && out) {
            float current = 0;
            uint32_t active = 0;
            std::memcpy(&current, out + 16, 4);
            std::memcpy(&active, out + 32, 4);
            g_input_active.fetch_add(active ? 1 : 0);
            if (current > 0.1f) g_input_pressed.fetch_add(1);
        } else if (name == "xrLocateSpace") {
            const uint8_t* location = reinterpret_cast<const uint8_t*>(a[3]);
            uint64_t flags = 0;
            if (location) std::memcpy(&flags, location + 16, 8);
            if (!(flags & 0x2) && g_space_kind.find(a[0]) == g_space_kind.end()) g_input_lost.fetch_add(1);
            if (flags & 0x2) {
                g_input_located.fetch_add(1); /* XR_SPACE_LOCATION_POSITION_VALID_BIT */
                float position[3];
                std::memcpy(position, location + 40, 12);
                std::lock_guard<std::mutex> held(g_pose_lock);
                auto kind = [&](uint64_t space) {
                    auto found = g_space_kind.find(space);
                    return found == g_space_kind.end() ? std::string("action") : found->second;
                };
                char line[200];
                std::snprintf(line, sizeof(line), "%s in %s at (%.2f, %.2f, %.2f)", kind(a[0]).c_str(), kind(a[1]).c_str(),
                              position[0], position[1], position[2]);
                g_last_hands[(g_hand_turn++) & 1] = line;
            }
        } else if (name == "xrLocateViews") {
            const uint8_t* info = reinterpret_cast<const uint8_t*>(a[1]);
            const uint8_t* views = reinterpret_cast<const uint8_t*>(a[5]);
            if (info && views) {
                uint64_t space = 0;
                std::memcpy(&space, info + 24, 8);
                float position[3];
                std::memcpy(position, views + 32, 12);
                std::lock_guard<std::mutex> held(g_pose_lock);
                auto found = g_space_kind.find(space);
                char line[200];
                std::snprintf(line, sizeof(line), "left eye in %s at (%.2f, %.2f, %.2f)",
                              found == g_space_kind.end() ? "?" : found->second.c_str(), position[0], position[1],
                              position[2]);
                g_last_head = line;
            }
        }
    }
    /* The runtime's frame period, from xrWaitFrame's XrFrameState, shown
       with the frame-rate line: what the compositor is pacing to. */
    if (name == "xrWaitFrame" && a[2] && (int32_t)(uint32_t)result >= 0) {
        int64_t period = 0;
        std::memcpy(&period, reinterpret_cast<const uint8_t*>(a[2]) + 24, 8);
        g_xr_period_ns.store(period, std::memory_order_relaxed);
        int64_t display = 0;
        std::memcpy(&display, reinterpret_cast<const uint8_t*>(a[2]) + 16, 8); /* predictedDisplayTime */
        g_xr_display_ns.store(display, std::memory_order_relaxed);
    }
    xr_capture_note(name, a, (int64_t)(int32_t)(uint32_t)result);
    if (trace) std::printf("openxr:   -> %d\n", (int)(int32_t)(uint32_t)result);
    if (trace && name == "xrCreateReferenceSpace") {
        const XrReferenceSpaceCreateInfo* info = reinterpret_cast<const XrReferenceSpaceCreateInfo*>(a[1]);
        std::printf("openxr:   reference space type %d\n", (int)info->referenceSpaceType);
    }
    if (trace && name == "xrCreateSwapchain") {
        const XrSwapchainCreateInfo* info = reinterpret_cast<const XrSwapchainCreateInfo*>(a[1]);
        std::printf("openxr:   swapchain %ux%u format %lld samples %u faces %u array %u mips %u usage %llx\n",
                    info->width, info->height, (long long)info->format, info->sampleCount, info->faceCount,
                    info->arraySize, info->mipCount, (unsigned long long)info->usageFlags);
    }
    return true;
}
