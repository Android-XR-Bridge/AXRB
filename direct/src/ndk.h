#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace arun::pkg {
class ZipReader;
}

namespace axrb {

// Host side of the NDK proxy libraries under direct/guest/proxy.
struct NdkConfig {
    std::shared_ptr<arun::pkg::ZipReader> apk;
    std::string guest_apk;
    int width = 1920, height = 1080, density_dpi = 480;
    int sdk = 36;
};
void ndk_init(const NdkConfig& config);
// Register real ART/AOSP ANativeWindow objects with the Windows graphics bridge.
void art_windows_init();

// An ANativeWindow* handed to guest code: a host object, opaque to the guest.
struct NativeWindow {
    static constexpr uint32_t kMagic = 0x4e57414e;  // "NAWN"
    uint32_t magic = kMagic;
    std::atomic<int> refs{1};
    uint64_t surface = 0;  // JNI global reference to the android.view.Surface
    uint64_t handle = 0;   // Surface.mNativeObject
    int32_t format = 1;    // AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
    int32_t width = 0, height = 0;  // setBuffersGeometry overrides (0 = the surface's size)
};
// nullptr unless `address` is a live ANativeWindow* from ANativeWindow_fromSurface.
NativeWindow* native_window(uint64_t address);
// The window's current size (the surface's, or the buffers-geometry override).
void native_window_size(NativeWindow* window, int32_t* width, int32_t* height);

}  // namespace axrb
