// Host half of libandroid.so (direct/guest/proxy/libandroid.c): native windows,
// assets and the configuration. Objects handed to the guest (ANativeWindow*,
// AAsset*, AAssetDir*, AConfiguration*) are host objects the guest only passes
// back; buffers it reads (AAsset_getBuffer, file names) are host memory, which
// guest code can address directly (the address spaces are the same).

#include "ndk.h"

#include "cpu.h"
#include "jni.h"
#include "kernel.h"
#include "linux.h"
#include "log.h"
#include "memory.h"

#include "arun/zip_reader.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace axrb {
using namespace linux_abi;

namespace {

NdkConfig g_config;

// --- native windows ----------------------------------------------------------------------

std::mutex g_windows_lock;
std::unordered_set<NativeWindow*> g_windows;
std::unordered_map<uint64_t, std::unique_ptr<NativeWindow>> g_art_windows;

void window_release(NativeWindow* w) {
    if (w->refs.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
    {
        std::lock_guard<std::mutex> held(g_windows_lock);
        g_windows.erase(w);
    }
    jni::release(w->surface);
    delete w;
}

// --- assets --------------------------------------------------------------------------------

struct Asset {
    static constexpr uint32_t kMagic = 0x54535341;  // "ASST"
    uint32_t magic = kMagic;
    const arun::pkg::ZipEntry* entry = nullptr;
    std::vector<uint8_t> inflated;  // compressed entries, extracted at open
    const uint8_t* data = nullptr;
    uint64_t size = 0;
    uint64_t pos = 0;
};

struct AssetDir {
    std::vector<std::string> names;
    size_t next = 0;
};

std::string asset_name(uint64_t guest_name) {
    std::string name;
    if (!guest_name || !guest_string(guest_name, name, 4096)) return {};
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    while (name.size() > 2 && name.compare(0, 2, "./") == 0) name.erase(0, 2);
    return name;
}

Asset* asset_of(uint64_t address) {
    auto* a = reinterpret_cast<Asset*>(untag(address));
    return a && a->magic == Asset::kMagic ? a : nullptr;
}

int64_t asset_seek(Asset* a, int64_t offset, int whence) {
    int64_t base = whence == 0 ? 0 : whence == 1 ? static_cast<int64_t>(a->pos) : static_cast<int64_t>(a->size);
    int64_t to = base + offset;
    if (to < 0 || to > static_cast<int64_t>(a->size)) return -1;
    a->pos = static_cast<uint64_t>(to);
    return to;
}

int open_apk_fd(GuestThread& thread) {
    int64_t fd = do_syscall(thread, 56, static_cast<uint64_t>(static_cast<int64_t>(AT_FDCWD_)),
                            reinterpret_cast<uint64_t>(g_config.guest_apk.c_str()), O_RDONLY_ | O_CLOEXEC_, 0, 0, 0);
    return fd < 0 ? -1 : static_cast<int>(fd);
}

struct Configuration {
    int density = 480;
};

void def(const char* name, HostFunction fn) { register_named_function(std::string("android.") + name, std::move(fn)); }

}  // namespace

NativeWindow* native_window(uint64_t address) {
    auto* w = reinterpret_cast<NativeWindow*>(untag(address));
    std::lock_guard<std::mutex> held(g_windows_lock);
    auto art = g_art_windows.find(untag(address));
    if (art != g_art_windows.end()) return art->second.get();
    return g_windows.count(w) ? w : nullptr;
}

void native_window_size(NativeWindow* w, int32_t* width, int32_t* height) {
    if (!w->surface && w->width > 0 && w->height > 0) {
        *width = w->width;
        *height = w->height;
        return;
    }
    uint64_t raw = 0;
    int32_t sw = 0, sh = 0;
    if (jni::object_field(w->surface, "mWidth", "I", &raw)) sw = static_cast<int32_t>(raw);
    if (jni::object_field(w->surface, "mHeight", "I", &raw)) sh = static_cast<int32_t>(raw);
    if (sw <= 0 || sh <= 0) {
        sw = g_config.width;
        sh = g_config.height;
    }
    *width = w->width > 0 ? w->width : sw;
    *height = w->height > 0 ? w->height : sh;
}

void art_windows_init() {
    register_named_function("art.window_register", [](HostCall& c) {
        if (!c.x(0) || c.x(1) == 0 || c.x(2) == 0 || c.x(1) > 16384 || c.x(2) > 16384) return c.ret(-EINVAL_);
        std::lock_guard<std::mutex> held(g_windows_lock);
        auto& window = g_art_windows[untag(c.x(0))];
        if (!window) window = std::make_unique<NativeWindow>();
        window->handle = untag(c.x(0));
        window->width = static_cast<int32_t>(c.x(1));
        window->height = static_cast<int32_t>(c.x(2));
        LOGI("art", "AOSP native window %llx (%dx%d)", window->handle, window->width, window->height);
        c.ret(0);
    });
}

void ndk_init(const NdkConfig& config) {
    g_config = config;

    // ANativeWindow
    def("ANativeWindow_fromSurface", [](HostCall& c) {
        const uint64_t surface = c.x(1);
        uint64_t handle = 0;
        if (!surface || !jni::object_field(surface, "mNativeObject", "J", &handle) || handle == 0) return c.ret(0);
        {
            std::lock_guard<std::mutex> held(g_windows_lock);
            for (NativeWindow* w : g_windows) {
                if (w->handle == handle) {
                    w->refs.fetch_add(1);
                    return c.ret(reinterpret_cast<uint64_t>(w));
                }
            }
        }
        auto* w = new NativeWindow;
        w->surface = jni::retain(surface);
        w->handle = handle;
        {
            std::lock_guard<std::mutex> held(g_windows_lock);
            g_windows.insert(w);
        }
        int32_t width, height;
        native_window_size(w, &width, &height);
        LOGI("ndk", "ANativeWindow %p for surface %llu (%dx%d)", static_cast<void*>(w),
             static_cast<unsigned long long>(handle), width, height);
        c.ret(reinterpret_cast<uint64_t>(w));
    });
    def("ANativeWindow_toSurface", [](HostCall& c) { c.ret(0); });
    def("ANativeWindow_acquire", [](HostCall& c) {
        if (NativeWindow* w = native_window(c.x(0))) w->refs.fetch_add(1);
    });
    def("ANativeWindow_release", [](HostCall& c) {
        if (NativeWindow* w = native_window(c.x(0))) window_release(w);
    });
    def("ANativeWindow_getWidth", [](HostCall& c) {
        NativeWindow* w = native_window(c.x(0));
        if (!w) return c.ret(static_cast<uint64_t>(-EINVAL_));
        int32_t width, height;
        native_window_size(w, &width, &height);
        c.ret(static_cast<uint64_t>(width));
    });
    def("ANativeWindow_getHeight", [](HostCall& c) {
        NativeWindow* w = native_window(c.x(0));
        if (!w) return c.ret(static_cast<uint64_t>(-EINVAL_));
        int32_t width, height;
        native_window_size(w, &width, &height);
        c.ret(static_cast<uint64_t>(height));
    });
    def("ANativeWindow_getFormat", [](HostCall& c) {
        NativeWindow* w = native_window(c.x(0));
        c.ret(w ? static_cast<uint64_t>(w->format) : static_cast<uint64_t>(-EINVAL_));
    });
    def("ANativeWindow_setBuffersGeometry", [](HostCall& c) {
        NativeWindow* w = native_window(c.x(0));
        if (!w) return c.ret(static_cast<uint64_t>(-EINVAL_));
        w->width = static_cast<int32_t>(c.x(1));
        w->height = static_cast<int32_t>(c.x(2));
        if (static_cast<int32_t>(c.x(3)) != 0) w->format = static_cast<int32_t>(c.x(3));
        c.ret(0);
    });
    for (const char* name : {"ANativeWindow_setBuffersTransform", "ANativeWindow_setBuffersDataSpace",
                             "ANativeWindow_setFrameRate", "ANativeWindow_setFrameRateWithChangeStrategy",
                             "ANativeWindow_tryAllocateBuffers"}) {
        def(name, [](HostCall& c) { c.ret(0); });
    }
    def("ANativeWindow_getBuffersDataSpace", [](HostCall& c) { c.ret(0); });
    // Software rendering into the window is not supported (no CPU-visible surface).
    def("ANativeWindow_lock", [](HostCall& c) { c.ret(static_cast<uint64_t>(-EINVAL_)); });
    def("ANativeWindow_unlockAndPost", [](HostCall& c) { c.ret(static_cast<uint64_t>(-EINVAL_)); });

    // AAssetManager
    static int asset_manager_token = 0;
    def("AAssetManager_fromJava", [](HostCall& c) { c.ret(reinterpret_cast<uint64_t>(&asset_manager_token)); });
    def("AAssetManager_open", [](HostCall& c) {
        std::string name = asset_name(c.x(1));
        if (name.empty() || !g_config.apk) return c.ret(0);
        const arun::pkg::ZipEntry* e = g_config.apk->Find("assets/" + name);
        if (!e || e->is_directory) return c.ret(0);
        auto a = std::make_unique<Asset>();
        a->entry = e;
        a->size = e->uncompressed_size;
        if (e->method == arun::pkg::CompressionMethod::kStore) {
            auto raw = g_config.apk->RawData(*e);
            if (!raw.ok()) return c.ret(0);
            a->data = raw->data();
        } else {
            auto bytes = g_config.apk->Extract(*e);
            if (!bytes.ok()) {
                LOGW("ndk", "asset %s: %s", name.c_str(), bytes.status().ToString().c_str());
                return c.ret(0);
            }
            a->inflated = std::move(*bytes);
            a->data = a->inflated.data();
        }
        c.ret(reinterpret_cast<uint64_t>(a.release()));
    });
    def("AAssetManager_openDir", [](HostCall& c) {
        std::string dir = asset_name(c.x(1));
        while (!dir.empty() && dir.back() == '/') dir.pop_back();
        std::string prefix = "assets/" + (dir.empty() ? std::string() : dir + "/");
        auto d = std::make_unique<AssetDir>();
        std::set<std::string> names;
        if (g_config.apk) {
            for (const auto& e : g_config.apk->entries()) {
                if (e.is_directory || e.name.size() <= prefix.size() || e.name.compare(0, prefix.size(), prefix) != 0)
                    continue;
                std::string rest = e.name.substr(prefix.size());
                if (rest.find('/') == std::string::npos) names.insert(rest);
            }
        }
        d->names.assign(names.begin(), names.end());
        c.ret(reinterpret_cast<uint64_t>(d.release()));
    });
    def("AAssetDir_getNextFileName", [](HostCall& c) {
        auto* d = reinterpret_cast<AssetDir*>(untag(c.x(0)));
        if (!d || d->next >= d->names.size()) return c.ret(0);
        c.ret(reinterpret_cast<uint64_t>(d->names[d->next++].c_str()));
    });
    def("AAssetDir_rewind", [](HostCall& c) {
        if (auto* d = reinterpret_cast<AssetDir*>(untag(c.x(0)))) d->next = 0;
    });
    def("AAssetDir_close", [](HostCall& c) { delete reinterpret_cast<AssetDir*>(untag(c.x(0))); });

    // AAsset
    def("AAsset_read", [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        if (!a) return c.ret(static_cast<uint64_t>(-1));
        uint64_t n = std::min<uint64_t>(c.x(2), a->size - a->pos);
        if (n && !guest_write(c.x(1), a->data + a->pos, n)) return c.ret(static_cast<uint64_t>(-1));
        a->pos += n;
        c.ret(n);
    });
    auto seek = [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        c.ret(a ? static_cast<uint64_t>(asset_seek(a, static_cast<int64_t>(c.x(1)), static_cast<int>(c.x(2))))
                : static_cast<uint64_t>(-1));
    };
    def("AAsset_seek", seek);
    def("AAsset_seek64", seek);
    def("AAsset_close", [](HostCall& c) {
        if (Asset* a = asset_of(c.x(0))) {
            a->magic = 0;
            delete a;
        }
    });
    def("AAsset_getBuffer", [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        c.ret(a ? reinterpret_cast<uint64_t>(a->data) : 0);
    });
    auto length = [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        c.ret(a ? a->size : static_cast<uint64_t>(-1));
    };
    def("AAsset_getLength", length);
    def("AAsset_getLength64", length);
    auto remaining = [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        c.ret(a ? a->size - a->pos : static_cast<uint64_t>(-1));
    };
    def("AAsset_getRemainingLength", remaining);
    def("AAsset_getRemainingLength64", remaining);
    auto open_fd = [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        if (!a || !a->inflated.empty() || a->entry->method != arun::pkg::CompressionMethod::kStore)
            return c.ret(static_cast<uint64_t>(-1));
        int fd = open_apk_fd(c.thread());
        if (fd < 0) return c.ret(static_cast<uint64_t>(-1));
        int64_t start = static_cast<int64_t>(a->entry->data_offset), len = static_cast<int64_t>(a->size);
        guest_write(c.x(1), &start, 8);
        guest_write(c.x(2), &len, 8);
        c.ret(static_cast<uint64_t>(fd));
    };
    def("AAsset_openFileDescriptor", open_fd);
    def("AAsset_openFileDescriptor64", open_fd);
    def("AAsset_isAllocated", [](HostCall& c) {
        Asset* a = asset_of(c.x(0));
        c.ret(a && !a->inflated.empty() ? 1 : 0);
    });

    // AConfiguration
    def("AConfiguration_new", [](HostCall& c) { c.ret(reinterpret_cast<uint64_t>(new Configuration)); });
    def("AConfiguration_delete", [](HostCall& c) { delete reinterpret_cast<Configuration*>(untag(c.x(0))); });
    def("AConfiguration_fromAssetManager", [](HostCall&) {});
    def("AConfiguration_getDensity", [](HostCall& c) { c.ret(static_cast<uint64_t>(g_config.density_dpi)); });
    def("AConfiguration_getOrientation", [](HostCall& c) {
        c.ret(g_config.width >= g_config.height ? 2 : 1);  // ACONFIGURATION_ORIENTATION_LAND / PORT
    });
    def("AConfiguration_getScreenWidthDp", [](HostCall& c) {
        c.ret(static_cast<uint64_t>(g_config.width * 160 / g_config.density_dpi));
    });
    def("AConfiguration_getScreenHeightDp", [](HostCall& c) {
        c.ret(static_cast<uint64_t>(g_config.height * 160 / g_config.density_dpi));
    });
    def("AConfiguration_getSmallestScreenWidthDp", [](HostCall& c) {
        c.ret(static_cast<uint64_t>(std::min(g_config.width, g_config.height) * 160 / g_config.density_dpi));
    });
    def("AConfiguration_getScreenSize", [](HostCall& c) { c.ret(3); });  // LARGE
    def("AConfiguration_getSdkVersion", [](HostCall& c) { c.ret(static_cast<uint64_t>(g_config.sdk)); });
    def("AConfiguration_getLanguage", [](HostCall& c) { guest_write(c.x(1), "en", 2); });
    def("AConfiguration_getCountry", [](HostCall& c) { guest_write(c.x(1), "US", 2); });
    def("AConfiguration_getTouchscreen", [](HostCall& c) { c.ret(3); });  // FINGER
    def("AConfiguration_getKeyboard", [](HostCall& c) { c.ret(1); });     // NOKEYS
    def("AConfiguration_getNavigation", [](HostCall& c) { c.ret(1); });   // NONAV
    def("AConfiguration_getUiModeType", [](HostCall& c) { c.ret(7); });   // VR_HEADSET
    def("AConfiguration_getUiModeNight", [](HostCall& c) { c.ret(1); });  // NO
    def("AConfiguration_getLayoutDirection", [](HostCall& c) { c.ret(0); });
}

}  // namespace axrb
