/* The Android side of a native application: pipes, the looper it waits on,
   and the activity object its entry point is handed.

   This is what lets the NDK's own android_native_app_glue run unchanged. The
   glue makes a pipe, starts a thread, waits on a looper for commands, and
   expects the Java side to send them as the application starts, gains a
   window and gains focus. Here the commands come from this side instead. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "android.h"
#include "qb_env.h"
#include <algorithm>
#include <string>
#include <vector>

#include <chrono>
#include <cstring>

namespace {

/* The looper returns the identifier a descriptor was added with. */
const int kPollWake = -1;
const int kPollTimeout = -3;

}  // namespace

int GuestLibc::make_pipe() {
    std::lock_guard<std::recursive_mutex> held(lock);
    int read_end = next_fd++;
    int write_end = next_fd++;
    auto pipe = std::make_shared<GuestPipe>();
    fds[read_end] = pipe;
    fds[write_end] = pipe;
    pipe_writers.insert(write_end);
    return read_end;
}

void GuestLibc::link_socket_pair(int a, int b) {
    std::lock_guard<std::recursive_mutex> held(lock);
    fds[a]->peer = fds[b];
    fds[b]->peer = fds[a];
}

bool GuestLibc::activity_call(const std::string& name, GuestCpu& cpu) {
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    auto write32 = [&](uint64_t va, uint32_t value) {
        uint8_t* p = guest_ptr(mem, va, 4);
        if (p) std::memcpy(p, &value, 4);
    };

    /* AAssetManager: files packed in the APK's assets/, read from the loose
       copy in QB_ROOT/apk/assets (extract it once from base.apk). An asset
       and a directory listing are host objects; guest memory is identity
       mapped, so the pointer to one is its handle and getBuffer's answer is
       directly readable by the guest. */
    if (name.compare(0, 6, "AAsset") == 0) {
        struct Asset {
            std::vector<uint8_t> data;
            size_t pos = 0;
        };
        struct AssetDir {
            std::vector<std::string> names;
            size_t next = 0;
        };
        const char* root_env = QB_ENV("QB_ROOT");
        const std::string assets = std::string(root_env ? root_env : ".") + "/apk/assets/";
        auto text = [&](uint64_t va) -> std::string {
            const char* s = reinterpret_cast<const char*>(guest_ptr(mem, va, 1));
            return s ? s : "";
        };
        if (name == "AAssetManager_fromJava") {
            ret(0xA55E70000ull); /* one manager: any non-null value */
            return true;
        }
        if (name == "AAssetManager_open") {
            std::string file = text(arg(1));
            FILE* in = std::fopen((assets + file).c_str(), "rb");
            if (QB_ENV("QB_TRACE_ASSETS")) std::printf("asset: open %s -> %s\n", file.c_str(), in ? "found" : "missing");
            if (!in) {
                ret(0);
                return true;
            }
            auto* asset = new Asset;
            std::fseek(in, 0, SEEK_END);
            asset->data.resize((size_t)std::ftell(in));
            std::fseek(in, 0, SEEK_SET);
            if (!asset->data.empty()) std::fread(asset->data.data(), 1, asset->data.size(), in);
            std::fclose(in);
            ret(reinterpret_cast<uint64_t>(asset));
            return true;
        }
        if (name == "AAssetManager_openDir") {
            auto* dir = new AssetDir;
            std::string sub = text(arg(1));
            WIN32_FIND_DATAA found{};
            HANDLE search = FindFirstFileA((assets + sub + (sub.empty() ? "*" : "/*")).c_str(), &found);
            if (search != INVALID_HANDLE_VALUE) {
                do {
                    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) dir->names.push_back(found.cFileName);
                } while (FindNextFileA(search, &found));
                FindClose(search);
            }
            ret(reinterpret_cast<uint64_t>(dir));
            return true;
        }
        if (name == "AAssetDir_getNextFileName") {
            auto* dir = reinterpret_cast<AssetDir*>(arg(0));
            ret(dir && dir->next < dir->names.size() ? reinterpret_cast<uint64_t>(dir->names[dir->next++].c_str()) : 0);
            return true;
        }
        if (name == "AAssetDir_rewind") {
            if (auto* dir = reinterpret_cast<AssetDir*>(arg(0))) dir->next = 0;
            return true;
        }
        if (name == "AAssetDir_close") {
            delete reinterpret_cast<AssetDir*>(arg(0));
            return true;
        }
        auto* asset = reinterpret_cast<Asset*>(arg(0));
        if (!asset) {
            ret(0);
            return true;
        }
        if (name == "AAsset_close") {
            delete asset;
            return true;
        }
        if (name == "AAsset_getLength" || name == "AAsset_getLength64") {
            ret(asset->data.size());
            return true;
        }
        if (name == "AAsset_getRemainingLength" || name == "AAsset_getRemainingLength64") {
            ret(asset->data.size() - asset->pos);
            return true;
        }
        if (name == "AAsset_getBuffer") {
            ret(reinterpret_cast<uint64_t>(asset->data.data()));
            return true;
        }
        if (name == "AAsset_isAllocated") {
            ret(0);
            return true;
        }
        if (name == "AAsset_read") {
            size_t count = std::min<size_t>((size_t)arg(2), asset->data.size() - asset->pos);
            uint8_t* out = guest_ptr(mem, arg(1), count ? count : 1);
            if (out && count) std::memcpy(out, asset->data.data() + asset->pos, count);
            asset->pos += count;
            ret(count);
            return true;
        }
        if (name == "AAsset_seek" || name == "AAsset_seek64") {
            int64_t offset = (int64_t)arg(1);
            int whence = (int)arg(2);
            int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)asset->pos : (int64_t)asset->data.size();
            int64_t to = base + offset;
            if (to < 0 || to > (int64_t)asset->data.size()) {
                ret((uint64_t)-1);
                return true;
            }
            asset->pos = (size_t)to;
            ret((uint64_t)to);
            return true;
        }
        if (name == "AAsset_openFileDescriptor" || name == "AAsset_openFileDescriptor64") {
            ret((uint64_t)-1); /* not a plain file in the APK here: callers fall back to read */
            return true;
        }
    }
    if (name == "pipe" || name == "pipe2") {
        int read_end = make_pipe();
        write32(arg(0), (uint32_t)read_end);
        write32(arg(0) + 4, (uint32_t)(read_end + 1));
        ret(0);
        return true;
    }
    if (name == "write") {
        int fd = (int)arg(0);
        uint8_t* from = guest_ptr(mem, arg(1), arg(2));
        std::shared_ptr<GuestPipe> pipe;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            auto found = fds.find(fd);
            if (found != fds.end()) pipe = found->second;
        }
        if (pipe) {
            if (auto peer = pipe->peer.lock()) pipe = peer; /* a socketpair end writes to the other */
        }
        if (!pipe || !from) {
            ret((uint64_t)-1);
            return true;
        }
        if (pipe->event) {
            uint64_t add = 0;
            std::memcpy(&add, from, 8);
            {
                std::lock_guard<std::mutex> held(pipe->lock);
                pipe->counter += add;
            }
            pipe->ready.notify_all();
            wake_loopers();
            ret(8);
            return true;
        }
        {
            std::lock_guard<std::mutex> held(pipe->lock);
            for (uint64_t i = 0; i < arg(2); ++i) pipe->bytes.push_back(from[i]);
        }
        pipe->ready.notify_all();
        wake_loopers();
        ret(arg(2));
        return true;
    }
    if (name == "read") {
        int fd = (int)arg(0);
        std::shared_ptr<GuestPipe> pipe;
        {
            std::lock_guard<std::recursive_mutex> held(lock);
            auto found = fds.find(fd);
            if (found != fds.end()) pipe = found->second;
        }
        if (!pipe) {
            ret((uint64_t)-1);
            return true;
        }
        std::unique_lock<std::mutex> held(pipe->lock);
        if (pipe->event) {
            if (!pipe->counter && pipe->nonblocking) {
                held.unlock();
                set_errno(cpu, 11); /* EAGAIN */
                ret((uint64_t)-1);
                return true;
            }
            pipe->ready.wait(held, [&] { return pipe->counter != 0; });
            uint64_t value = pipe->semaphore ? 1 : pipe->counter;
            pipe->counter -= value;
            uint8_t* to = guest_ptr(mem, arg(1), 8);
            if (to) std::memcpy(to, &value, 8);
            ret(8);
            return true;
        }
        if (pipe->bytes.empty() && pipe->nonblocking) {
            held.unlock();
            set_errno(cpu, 11); /* EAGAIN */
            ret((uint64_t)-1);
            return true;
        }
        pipe->ready.wait(held, [&] { return !pipe->bytes.empty(); });
        uint64_t count = std::min<uint64_t>(arg(2), pipe->bytes.size());
        uint8_t* to = guest_ptr(mem, arg(1), count);
        if (to)
            for (uint64_t i = 0; i < count; ++i) to[i] = pipe->bytes[(size_t)i];
        pipe->bytes.erase(pipe->bytes.begin(), pipe->bytes.begin() + (size_t)count);
        ret(count);
        return true;
    }
    if (name == "close") {
        ret(0);
        return true;
    }

    /* The looper. A descriptor is watched, and polling gives back the
       identifier it was registered with once it has something to read. */
    if (name == "ALooper_prepare") {
        std::lock_guard<std::recursive_mutex> held(lock);
        uint64_t looper = 0x100 + loopers.size();
        loopers[cpu.tpidr] = looper;
        ret(looper);
        return true;
    }
    if (name == "ALooper_forThread") {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = loopers.find(cpu.tpidr);
        /* The UI thread always has one: Java's main Looper exists before any
           application code runs there. */
        if (found == loopers.end() && cpu.tpidr == image->cpu.tpidr)
            found = loopers.emplace(cpu.tpidr, 0x100 + loopers.size()).first;
        ret(found == loopers.end() ? 0 : found->second);
        return true;
    }
    if (name == "ALooper_acquire" || name == "ALooper_release") {
        ret(0);
        return true;
    }
    if (name == "ALooper_addFd") {
        std::lock_guard<std::recursive_mutex> held(lock);
        watched.push_back({(uint64_t)arg(0), (int)arg(1), (int)arg(2), arg(4), arg(5)});
        ret(1);
        return true;
    }
    if (name == "ALooper_removeFd") {
        std::lock_guard<std::recursive_mutex> held(lock);
        for (size_t i = 0; i < watched.size(); ++i)
            if (watched[i].fd == (int)arg(1)) {
                watched.erase(watched.begin() + i);
                break;
            }
        ret(1);
        return true;
    }
    if (name == "ALooper_wake") {
        wake_loopers();
        ret(0);
        return true;
    }
    if (name == "ALooper_pollOnce" || name == "ALooper_pollAll") {
        int64_t timeout = (int64_t)(int32_t)arg(0);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout < 0 ? 0 : timeout);
        for (;;) {
            /* Whichever watched descriptor has something waiting. */
            {
                std::lock_guard<std::recursive_mutex> held(lock);
                for (const Watched& entry : watched) {
                    auto found = fds.find(entry.fd);
                    if (found == fds.end()) continue;
                    std::lock_guard<std::mutex> inner(found->second->lock);
                    if (found->second->bytes.empty()) continue;
                    if (arg(1)) write32(arg(1), (uint32_t)entry.fd);
                    if (arg(2)) write32(arg(2), (uint32_t)entry.events);
                    if (arg(3)) {
                        uint64_t data = entry.data;
                        uint8_t* p = guest_ptr(mem, arg(3), 8);
                        if (p) std::memcpy(p, &data, 8);
                    }
                    ret((uint64_t)(int64_t)entry.ident);
                    return true;
                }
            }
            if (woken.exchange(false)) {
                ret((uint64_t)(int64_t)kPollWake);
                return true;
            }
            if (timeout == 0 || (timeout > 0 && std::chrono::steady_clock::now() >= deadline)) {
                ret((uint64_t)(int64_t)kPollTimeout);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /* Configuration and input, which the glue asks for and a headset
       application then ignores. */
    if (name == "AConfiguration_new") {
        ret(guest_alloc(64));
        return true;
    }
    if (name == "ANativeActivity_finish") {
        if (const char* event_name = QB_ENV("QB_STOP_EVENT")) {
            HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
            if (event) { SetEvent(event); CloseHandle(event); }
        }
        ret(0);
        return true;
    }
    if (name == "AConfiguration_fromAssetManager" || name == "AConfiguration_delete" ||
        name == "AInputQueue_attachLooper" || name == "AInputQueue_detachLooper" ||
        name == "AInputQueue_finishEvent" ||
        name == "ANativeActivity_setWindowFlags" || name == "ANativeActivity_setWindowFormat" || name == "AConfiguration_getOrientation") {
        ret(0);
        return true;
    }
    if (name == "AConfiguration_getDensity") {
        ret(320);
        return true;
    }
    if (name.compare(0, 20, "AConfiguration_get") == 0 || name.compare(0, 18, "AConfiguration_get") == 0) {
        /* The rest of the configuration is whatever a headset reports, which
           is nothing an application here looks at. */
        ret(0);
        return true;
    }
    if (name == "AInputQueue_getEvent" || name == "AInputQueue_preDispatchEvent") {
        ret((uint64_t)-1); /* nothing waiting */
        return true;
    }
    /* Sensors: a manager with nothing in it. The headset's pose comes from
       OpenXR, not from these. */
    if (name == "ASensorManager_getInstance" || name == "ASensorManager_getInstanceForPackage") {
        ret(0x5e50000);
        return true;
    }
    if (name == "ASensorManager_getSensorList") {
        uint8_t* out = guest_ptr(mem, arg(1), 8);
        if (out) std::memset(out, 0, 8);
        ret(0);
        return true;
    }
    if (name == "ASensorManager_getDefaultSensor" || name == "ASensorManager_getDefaultSensorEx") {
        ret(0);
        return true;
    }
    if (name == "ASensorManager_createEventQueue") {
        ret(0x5e51000);
        return true;
    }
    if (name.compare(0, 16, "ASensorEventQueue") == 0 || name == "ASensorManager_destroyEventQueue") {
        ret(name == "ASensorEventQueue_getEvents" ? 0 : 0);
        return true;
    }
    if (name == "ANativeWindow_fromSurface") {
        ret(0x5730000); /* the one window there is */
        return true;
    }
    /* The same size as the host window a guest surface turns into, since a
       Vulkan swapchain has to match its window exactly. */
    if (name == "ANativeWindow_getWidth") {
        ret(1280);
        return true;
    }
    if (name == "ANativeWindow_getHeight") {
        ret(720);
        return true;
    }
    if (name == "ANativeWindow_setBuffersGeometry" || name == "ANativeWindow_acquire" ||
        name == "ANativeWindow_release") {
        ret(0);
        return true;
    }
    if (name == "__android_log_print" || name == "__android_log_write" || name == "__android_log_vprint") {
        /* The arguments sit in the registers after the format, so the line has
           to be built the same way a formatted print is. A log that prints its
           format string instead of its values is no use for diagnosing a game. */
        const char* tag = reinterpret_cast<const char*>(guest_ptr(mem, arg(1), 1));
        const char* format = reinterpret_cast<const char*>(guest_ptr(mem, arg(2), 1));
        /* write has no arguments at all, and vprint carries them in a va_list. */
        std::string line = name == "__android_log_write" ? std::string(format ? format : "")
                           : name == "__android_log_vprint" ? format_from_valist(format, arg(3))
                                                            : format_from_guest(format, cpu, 3, 0);
        std::printf("guest[%s]: %s\n", tag ? tag : "?", line.c_str());
        std::fflush(stdout);
        ret(0);
        return true;
    }
    return false;
}

void GuestLibc::wake_loopers() { woken = true; }

uint64_t GuestLibc::make_activity() {
    /* The activity the entry point is handed, and the callbacks block it
       fills in. Both live in guest memory because the guest writes to them. */
    uint64_t callbacks = guest_alloc(16 * 8);
    uint8_t* zeroed = guest_ptr(image->mem, callbacks, 16 * 8);
    if (zeroed) std::memset(zeroed, 0, 16 * 8);

    uint64_t activity = guest_alloc(80);
    uint8_t* fields = guest_ptr(image->mem, activity, 80);
    if (!fields) return 0;
    std::memset(fields, 0, 80);
    uint64_t internal = guest_string("/data/data/com.questbridge.guest/files");
    uint64_t external = guest_string("/sdcard/Android/data/com.questbridge.guest/files");
    uint64_t obb = guest_string("/sdcard/Android/obb/com.questbridge.guest");
    uint64_t clazz = handle_for("object", "activity");
    uint32_t sdk = 29;
    std::memcpy(fields + 0, &callbacks, 8);
    std::memcpy(fields + 8, &vm_va, 8);
    std::memcpy(fields + 16, &env_va, 8);
    std::memcpy(fields + 24, &clazz, 8);
    std::memcpy(fields + 32, &internal, 8);
    std::memcpy(fields + 40, &external, 8);
    std::memcpy(fields + 48, &sdk, 4);
    /* instance at 56 is the application's own, assetManager at 64 stays null */
    std::memcpy(fields + 72, &obb, 8);
    activity_va = activity;
    callbacks_va = callbacks;
    return activity;
}

void GuestLibc::lifecycle(int which, uint64_t argument) {
    /* The callbacks block is in the order native_activity.h declares it. */
    static const int kOnStart = 0, kOnResume = 1, kOnSaveInstanceState = 2, kOnPause = 3, kOnStop = 4,
                     kOnDestroy = 5, kOnWindowFocusChanged = 6, kOnNativeWindowCreated = 7;
    (void)kOnSaveInstanceState;
    (void)kOnPause;
    (void)kOnStop;
    (void)kOnDestroy;
    (void)kOnStart;
    (void)kOnResume;
    (void)kOnWindowFocusChanged;
    (void)kOnNativeWindowCreated;
    uint64_t function = 0;
    uint8_t* slot = guest_ptr(image->mem, callbacks_va + (uint64_t)which * 8, 8);
    if (slot) std::memcpy(&function, slot, 8);
    if (!function) return;
    call_guest(function, activity_va, argument);
}

bool GuestLibc::pump_looper(GuestCpu& cpu) {
    uint64_t mine = 0;
    std::vector<Watched> ready;
    {
        std::lock_guard<std::recursive_mutex> held(lock);
        auto found = loopers.find(cpu.tpidr);
        if (found == loopers.end()) return false;
        mine = found->second;
        for (const Watched& entry : watched) {
            if (entry.looper != mine || !entry.callback) continue;
            auto pipe = fds.find(entry.fd);
            if (pipe == fds.end()) continue;
            std::lock_guard<std::mutex> inner(pipe->second->lock);
            bool readable = pipe->second->event ? pipe->second->counter != 0 : !pipe->second->bytes.empty();
            if (readable) ready.push_back(entry);
        }
    }
    for (const Watched& entry : ready) {
        /* int callback(int fd, int events, void* data): 0 unregisters. */
        uint64_t keep = call_guest_args(entry.callback, {(uint64_t)entry.fd, (uint64_t)entry.events, entry.data});
        if ((uint32_t)keep == 0) {
            std::lock_guard<std::recursive_mutex> held(lock);
            for (size_t i = 0; i < watched.size(); ++i)
                if (watched[i].fd == entry.fd && watched[i].callback == entry.callback) {
                    watched.erase(watched.begin() + i);
                    break;
                }
        }
    }
    return !ready.empty();
}
