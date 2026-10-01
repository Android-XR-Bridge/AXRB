#pragma once

#include "menu_shortcut.h"
#include "perf_stats.h"
#include "mirror_window.h"
#include "gpu_frame_batch.h"
#include "frame_pool.h"
#include "fps_counter.h"
#if defined(AXRB_ENABLE_PERFORMANCE_OVERLAY)
#include "fresh_frame_stats.h"
#include "guest_performance_reader.h"
#endif
#include <atomic>
#include "debug_frame_capture.h"
#include "windows_gpu_frame.h"
#include "frame_delivery_counter.h"
#include "precise_frame_wait.h"
#include "frame_history.h"

#include "gpu_transport.h"
#include "image_transport.h"
#include "transport_tcp.h"
#include "video_transport.h"

#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <memory>
#include <mutex>
#include <string_view>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#define XR_USE_TIMESPEC
#else
#define XR_USE_GRAPHICS_API_D3D11
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "equirect_renderer.h"
#include "quad_renderer.h"

#if defined(_WIN32)
#else
#include <dlfcn.h>
#include <time.h>
#endif

namespace axrb::host::detail {

constexpr float kAppProjectionHalfFovRadians = 0.95f;

const char* xr_result_name(XrResult result);
axrb::protocol::Pose to_protocol_pose(const XrPosef& pose);
uint64_t monotonic_time_ns();

#if defined(_WIN32)
template <typename T>
class ComPtr {
public:
    ~ComPtr()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
        }
    }

    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept
        : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }
    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other) {
            if (ptr_ != nullptr) {
                ptr_->Release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const { return ptr_; }
    T** put()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
        return &ptr_;
    }

private:
    T* ptr_ = nullptr;
};
#endif

class OpenXrLoader {
public:
    ~OpenXrLoader()
    {
#if defined(_WIN32)
        if (library_ != nullptr) {
            FreeLibrary(library_);
        }
#else
        if (library_ != nullptr) {
            dlclose(library_);
        }
#endif
    }

    bool load()
    {
#if defined(_WIN32)
        auto try_load = [&](const wchar_t* location, const char* source) {
            library_ = LoadLibraryW(location);
            const DWORD error = library_ ? ERROR_SUCCESS : GetLastError();
            const int size = WideCharToMultiByte(CP_UTF8, 0, location, -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(size > 0 ? size : 1, '\0');
            if (size > 0) WideCharToMultiByte(CP_UTF8, 0, location, -1, utf8.data(), size, nullptr, nullptr);
            std::fprintf(stderr, "AXRB OpenXR: %s loader \"%s\": %s (Windows error %lu)\n",
                         source, utf8.c_str(), library_ ? "loaded" : "failed", error);
        };
        const DWORD overrideSize = GetEnvironmentVariableW(L"AXRB_OPENXR_LOADER", nullptr, 0);
        if (overrideSize > 0) {
            std::wstring overridePath(overrideSize, L'\0');
            const DWORD size = GetEnvironmentVariableW(L"AXRB_OPENXR_LOADER", overridePath.data(), overrideSize);
            if (size > 0 && size < overrideSize) {
                try_load(overridePath.c_str(), "AXRB_OPENXR_LOADER");
            } else {
                std::fprintf(stderr, "AXRB OpenXR: could not read AXRB_OPENXR_LOADER (Windows error %lu)\n",
                             size >= overrideSize ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
            }
        }
        if (library_ == nullptr) {
            std::wstring executablePath(MAX_PATH, L'\0');
            for (;;) {
                const DWORD size = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
                if (size == 0) {
                    std::fprintf(stderr, "AXRB OpenXR: could not locate host executable (Windows error %lu)\n", GetLastError());
                    break;
                }
                if (size < executablePath.size()) {
                    executablePath.resize(size);
                    const auto separator = executablePath.find_last_of(L"\\/");
                    if (separator == std::wstring::npos) {
                        std::fprintf(stderr, "AXRB OpenXR: host executable path has no directory\n");
                        break;
                    }
                    executablePath.resize(separator + 1);
                    executablePath += L"openxr_loader.dll";
                    try_load(executablePath.c_str(), "adjacent");
                    break;
                }
                if (executablePath.size() >= 32768) {
                    std::fprintf(stderr, "AXRB OpenXR: host executable path exceeds Windows path limit\n");
                    break;
                }
                executablePath.resize(executablePath.size() > 16384 ? 32768 : executablePath.size() * 2);
            }
        }
        if (library_ == nullptr) {
            try_load(L"openxr_loader.dll", "ordinary DLL lookup");
        }
        if (library_ == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: no loader could be loaded; see attempted locations above\n");
            return false;
        }
        auto get_symbol = [&](const char* name) -> void* {
            return reinterpret_cast<void*>(GetProcAddress(library_, name));
        };
#else
        library_ = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
        if (library_ == nullptr) {
            library_ = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (library_ == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to load libopenxr_loader.so.1\n");
            return false;
        }
        auto get_symbol = [&](const char* name) -> void* {
            return dlsym(library_, name);
        };
#endif

        createInstance = reinterpret_cast<PFN_xrCreateInstance>(get_symbol("xrCreateInstance"));
        getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(get_symbol("xrGetInstanceProcAddr"));
        if (createInstance == nullptr || getInstanceProcAddr == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: loader is missing required exports\n");
            return false;
        }
        return true;
    }

    PFN_xrCreateInstance createInstance = nullptr;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;

private:
#if defined(_WIN32)
    HMODULE library_ = nullptr;
#else
    void* library_ = nullptr;
#endif
};

struct HostImageSnapshot {
    axrb::protocol::ImageFrameHeader header{};
    axrb::protocol::ImageProjection projection{};
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    std::shared_ptr<GpuFrameBatch> gpu;
    std::chrono::steady_clock::time_point receivedAt{};
};

struct HostImageFrame {
    HostImageFrame() {
        if (buffered_) std::fprintf(stderr, "AXRB timing experiment: two-image history enabled, stale limit=33ms\n");
    }
    bool buffered() const { return buffered_; }
    std::atomic<uint64_t> deliveredFrames{0};
#if defined(AXRB_ENABLE_PERFORMANCE_OVERLAY)
    axrb::host::PerformanceTelemetry performance;
#endif
    void store(const axrb::protocol::ImageFrameHeader& header, std::vector<uint8_t>&& pixels,
               const axrb::protocol::ImageProjection& projection = {}, std::shared_ptr<GpuFrameBatch> gpu = {})
    {
        static axrb::protocol::FrameIntervals stats("host-image-arrival");
        stats.record();
        HostImageSnapshot next{header, projection,
            std::make_shared<const std::vector<uint8_t>>(std::move(pixels)), std::move(gpu), std::chrono::steady_clock::now()};
#if defined(AXRB_ENABLE_PERFORMANCE_OVERLAY)
        performance.received(header.sequence, std::chrono::duration_cast<std::chrono::nanoseconds>(
            next.receivedAt.time_since_epoch()).count(),
            header.version != axrb::protocol::kEmptyImageFrameVersion && header.width && header.height,
            static_cast<bool>(next.gpu));
#endif
        std::vector<HostImageSnapshot> retired;
        if (buffered_) retired.reserve(2);
        { std::lock_guard lock(mutex);
            std::swap(latest, next);
            if (buffered_) {
                if (header.version == axrb::protocol::kEmptyImageFrameVersion) history_.clear(retired);
                else history_.push(latest, retired);
            }
        }
        updated.notify_one();
#if defined(_WIN32)
        preciseWait_.notify();
#endif
        const auto delivered = deliveredFrames.fetch_add(1, std::memory_order_relaxed) + 1;
        if (delivered % 300 == 0) {
            const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::fprintf(stderr, "AXRB Delivered: total=%llu steady_ns=%lld sequence=%llu width=%u height=%u layers=%u\n",
                static_cast<unsigned long long>(delivered), static_cast<long long>(nowNs),
                static_cast<unsigned long long>(header.sequence), header.width, header.height, header.layers);
            if (buffered_) {
                size_t pending; uint64_t overflow, expired;
                { std::lock_guard lock(mutex);
                    pending=history_.size(); overflow=history_.overflowDrops; expired=history_.expiredDrops;
                }
                std::fprintf(stderr, "AXRB FrameHistory: pending=%zu overflow_drops=%llu expired_drops=%llu\n",
                    pending, static_cast<unsigned long long>(overflow), static_cast<unsigned long long>(expired));
            }
        }
        // Release the previous slot outside the publication lock.
    }

    HostImageSnapshot snapshot() {
        std::vector<HostImageSnapshot> retired;
        if (buffered_) retired.reserve(2);
        std::lock_guard lock(mutex);
        if (buffered_) {
            history_.expire(std::chrono::steady_clock::now(), retired);
            if (const auto* pending = history_.front()) return *pending;
        }
        return latest;
    }
    void submitted(uint64_t sequence) {
        if (!buffered_) return;
        std::vector<HostImageSnapshot> retired;
        retired.reserve(2);
        std::lock_guard lock(mutex);
        history_.submitted(sequence, retired);
    }
    bool needs_newer(uint64_t sequence) {
        std::lock_guard lock(mutex);
        return latest.pixels && latest.header.sequence <= sequence;
    }
    void wait_for_newer(uint64_t sequence, std::chrono::nanoseconds timeout) {
#if defined(_WIN32)
        if (preciseWait_.enabled()) {
            preciseWait_.wait_for(timeout, [&] {
                std::lock_guard lock(mutex);
                return latest.header.sequence > sequence ||
                    latest.header.version == axrb::protocol::kEmptyImageFrameVersion;
            });
            return;
        }
#endif
        std::unique_lock lock(mutex);
        updated.wait_for(lock, timeout, [&] {
            return latest.header.sequence > sequence ||
                latest.header.version == axrb::protocol::kEmptyImageFrameVersion;
        });
    }
private:
    const bool buffered_ = [] {
        const char* value = std::getenv("AXRB_FRAME_HISTORY");
        return value && std::strcmp(value, "2") == 0;
    }();
    std::mutex mutex;
    std::condition_variable updated;
    HostImageSnapshot latest;
    FrameHistory<HostImageSnapshot> history_;
#if defined(_WIN32)
    PreciseFrameWait preciseWait_;
#endif
};

class OpenXrSession {
public:
    explicit OpenXrSession(HostImageFrame* imageFrame = nullptr)
        : imageFrame_(imageFrame)
    {
    }

    ~OpenXrSession();

    bool initialize(const std::string& gameName);

    bool open_mirror(const std::string& gameName);
    bool pump_mirror();

    axrb::protocol::PoseFrame make_frame(uint64_t sequence);

    axrb::protocol::PoseFrame latest_frame(uint64_t sequence);

    // Receive on the image thread, independently of the OpenXR compositor clock.
    // The same lock protects cross-device cache ownership and its render poses.
    bool receive_image(const axrb::protocol::ImageFrameHeader& header,
                       const axrb::protocol::ImageProjection& projection,
                       std::vector<uint8_t>&& pixels);

    bool drives_frame_loop() const;

private:
    void publish_pose(const axrb::protocol::PoseFrame& frame);

    template <typename T>
    bool load_func(const char* name, T* out)
    {
        PFN_xrVoidFunction function = nullptr;
        const XrResult result = loader_.getInstanceProcAddr(instance_, name, &function);
        if (result != XR_SUCCESS || function == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to load %s: %s (%d)\n", name, xr_result_name(result), result);
            return false;
        }
        *out = reinterpret_cast<T>(function);
        return true;
    }

    bool load_instance_functions();

    XrTime current_xr_time();

#if defined(_WIN32)
    bool create_d3d11_device();

    bool create_projection_swapchain();
    bool acquire_panel_swapchain(const HostImageSnapshot& frame);

    struct NativeLayerSubmission {
        bool hasGameProjection = false;
        uint64_t gameSequence = 0;
        uint32_t gameWidth = 0, gameHeight = 0;
        std::vector<std::array<XrCompositionLayerProjectionView, 2>> projectionViews;
        std::vector<XrCompositionLayerProjection> projections;
        std::vector<XrCompositionLayerQuad> quads;
        std::vector<XrCompositionLayerEquirect2KHR> spheres;
        std::vector<const XrCompositionLayerBaseHeader*> layers;
    };

    bool update_projection_layers(XrTime displayTime, XrDuration displayPeriod,
                                  NativeLayerSubmission& submission);

    bool render_equirect(uint32_t slot, XrTime time, ID3D11Texture2D* source,
        const XrCompositionLayerEquirect2KHR& layer, const axrb::protocol::ImageEquirect& metadata);
    bool update_fps_hud(XrCompositionLayerQuad& layer, uint32_t existingLayers);
    bool fill_projection_texture(ID3D11Texture2D* texture, uint32_t imageIndex,
        const HostImageSnapshot& frame, bool precompose,
        const XrPosef* overflowWorldFromView);

    bool upload_android_frame(ID3D11Texture2D* texture, uint32_t imageIndex,
        const HostImageSnapshot& frame, bool precompose,
        const XrPosef* overflowWorldFromView);
#endif

    bool create_reference_space(XrReferenceSpaceType type, XrSpace* space);

    bool string_to_path(const char* text, XrPath* path);

    enum InputAction { Trigger, Squeeze, Stick, Primary, Secondary, Menu, StickPress,
                       PrimaryContact, SecondaryContact, TriggerContact, StickContact, ThumbrestContact, InputCount };

    void initialize_hand_tracking();

    void locate_hand_joints(axrb::protocol::PoseFrame& frame, XrTime time, uint64_t sequence);

    void initialize_controller_actions();

    void suggest_pose_bindings(const char* interactionProfilePath, const char* poseInputSuffix);

    void locate_controller_spaces(axrb::protocol::PoseFrame& frame, XrTime locateTime, uint64_t sequence);

    void pump_events();

    void handle_session_state(XrSessionState state);

    OpenXrLoader loader_;
    HostImageFrame* imageFrame_ = nullptr;
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace appLocalSpace_ = XR_NULL_HANDLE;
    XrReferenceSpaceType trackingSpaceType_ = XR_REFERENCE_SPACE_TYPE_LOCAL;
    bool reportedLocalOrigin_ = false;
    bool localOriginInitialized_ = false;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction handPoseAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    bool equirectEnabled_ = false;
    bool handTrackingEnabled_ = false, handDataSourceEnabled_ = false;
    PFN_xrCreateHandTrackerEXT createHandTracker_ = nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints_ = nullptr;
    std::array<XrHandTrackerEXT, 2> handTrackers_{};
    std::array<XrAction, InputCount> inputActions_{};
    std::array<XrPath, 2> handSubactionPaths_{XR_NULL_PATH, XR_NULL_PATH};
    std::array<XrSpace, 2> handSpaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<XrSpace, 2> aimSpaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool sessionRunning_ = false;
    bool sessionVisible_ = false;
    bool useFrameLoop_ = true;
    bool controllerActionsReady_ = false;
    struct ControllerDiagnostics {
        uint64_t samples = 0, aimInactive = 0, gripInactive = 0, aimInvalid = 0, gripInvalid = 0;
    };
    std::array<ControllerDiagnostics, 2> controllerDiagnostics_{};
    uint64_t controllerSyncSamples_ = 0, controllerUnfocusedSamples_ = 0, controllerSyncFailures_ = 0;
    MenuShortcut menuShortcut_;
    bool reportedSyncFailure_ = false;
    axrb::protocol::PoseFrame latest_{};
    std::mutex frameMutex_;

    PFN_xrDestroyInstance destroyInstance_ = nullptr;
    PFN_xrGetSystem getSystem_ = nullptr;
    PFN_xrEnumerateViewConfigurationViews enumerateViewConfigurationViews_ = nullptr;
    PFN_xrCreateSession createSession_ = nullptr;
    PFN_xrDestroySession destroySession_ = nullptr;
    PFN_xrCreateReferenceSpace createReferenceSpace_ = nullptr;
    PFN_xrDestroySpace destroySpace_ = nullptr;
    PFN_xrPollEvent pollEvent_ = nullptr;
    PFN_xrBeginSession beginSession_ = nullptr;
    PFN_xrEndSession endSession_ = nullptr;
    PFN_xrWaitFrame waitFrame_ = nullptr;
    PFN_xrBeginFrame beginFrame_ = nullptr;
    PFN_xrEndFrame endFrame_ = nullptr;
    PFN_xrLocateSpace locateSpace_ = nullptr;
    PFN_xrLocateViews locateViews_ = nullptr;
    PFN_xrStringToPath stringToPath_ = nullptr;
    PFN_xrCreateActionSet createActionSet_ = nullptr;
    PFN_xrDestroyActionSet destroyActionSet_ = nullptr;
    PFN_xrCreateAction createAction_ = nullptr;
    PFN_xrDestroyAction destroyAction_ = nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings_ = nullptr;
    PFN_xrAttachSessionActionSets attachSessionActionSets_ = nullptr;
    PFN_xrCreateActionSpace createActionSpace_ = nullptr;
    PFN_xrSyncActions syncActions_ = nullptr;
    PFN_xrGetActionStatePose getActionStatePose_ = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean_ = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat_ = nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f_ = nullptr;
#if defined(_WIN32)
    PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats_ = nullptr;
    PFN_xrCreateSwapchain createSwapchain_ = nullptr;
    PFN_xrDestroySwapchain destroySwapchain_ = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages_ = nullptr;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage_ = nullptr;
    PFN_xrWaitSwapchainImage waitSwapchainImage_ = nullptr;
    PFN_xrReleaseSwapchainImage releaseSwapchainImage_ = nullptr;
    PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements_ = nullptr;
    // Overlapping receipt remains opt-in until the reported stereo regression
    // has been isolated on the headset, not just in synthetic GPU tests.
    const bool concurrentGpuFrames_ = [] {
        const char* value = std::getenv("AXRB_ASYNC_GPU_HANDOFF");
        return value && std::strcmp(value, "1") == 0;
    }();
    const bool precomposeProjectionLayers_ = [] {
        const char* value = std::getenv("AXRB_PRECOMPOSE_PROJECTION_LAYERS");
        return value && std::strcmp(value, "1") == 0;
    }();
    std::mutex frameHandoffMutex_;
    FramePool<GpuFrameBatch,4> gpuFrames_{imageFrame_ && imageFrame_->buffered() ? 4u : 3u};
    // Only the receiving thread assembles pending batches. Publication pins
    // the entire batch so its textures and projection metadata stay together.
    std::shared_ptr<GpuFrameBatch> pendingGpuFrame_;
    uint32_t pendingMixedCount_ = 0, pendingMixedIndex_ = 0;
    uint64_t pendingMixedSequence_ = 0;
    struct UploadedCompositionPart {
        axrb::protocol::ImageProjection projection{};
        XrExtent2Di extent{};
        uint32_t imageArrayIndex = 0;
        bool panel = false;
    };
    std::vector<std::vector<UploadedCompositionPart>> uploadedCompositionByImage_;
    std::vector<uint64_t> uploadedMixedTimes_;
    ComPtr<ID3D11Device> receiveDevice_;
    ComPtr<ID3D11DeviceContext> receiveContext_;
    bool reportedGpuImage_ = false;
    MirrorWindow mirror_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    GpuCompletion frameCopyCompletion_;
    GpuCompletion batchReceiveCompletion_;
    axrb::host::LayerColorRenderer receiveColorRenderer_;
    XrSwapchain projectionSwapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> projectionImages_;
    std::vector<uint64_t> uploadedAndroidSequenceByImage_;
    std::vector<uint64_t> uploadedGpuSessionByImage_;
    std::vector<XrExtent2Di> uploadedExtentByImage_;
    std::vector<XrPosef> uploadedOverflowWorldFromView_;
    std::vector<bool> uploadedOverflowWorldFromViewValid_;
    std::vector<std::array<XrView, 2>> uploadedOverflowViews_;
    std::vector<bool> uploadedOverflowViewsValid_;
    bool reportedStereoProjection_ = false;
    int64_t projectionFormat_ = 0;
    uint32_t projectionArraySize_ = 2;
    uint32_t projectionWidth_ = axrb::protocol::kTransportEyeDimension;
    uint32_t projectionHeight_ = axrb::protocol::kTransportEyeDimension;
    bool debugGraphicsTest_ = [] { const char* value = std::getenv("AXRB_DEBUG_GRAPHICS_TEST"); return value && std::strcmp(value, "1") == 0; }();
    std::vector<uint8_t> splashPixels_;
    std::vector<bool> splashUploaded_;
    uint32_t projectionFrameCounter_ = 0;
    XrSwapchain panelSwapchain_ = XR_NULL_HANDLE;
    uint32_t panelWidth_ = 0, panelHeight_ = 0, panelLayers_ = 0, panelImageIndex_ = 0;
    bool panelAcquired_ = false;
    std::vector<XrSwapchainImageD3D11KHR> panelImages_;
    std::vector<uint64_t> panelSequences_;
    axrb::host::EquirectRenderer equirectRenderer_;
    axrb::host::QuadRenderer quadRenderer_;
    uint32_t nativeLayerLimit_ = 1;
    NativeLayerSubmission nativeSubmission_;
    FrameDeliveryCounter submittedGameFrames_;
    bool performanceCounterTimeEnabled_ = false;
    PFN_xrConvertWin32PerformanceCounterToTimeKHR convertPerformanceCounterTime_ = nullptr;
    XrTime windows_xr_time();
    uint32_t freshFrameWaitUs_ = 0;
    std::array<XrView, 2> overflowViews_{};
    bool should_precompose(const GpuFrameBatch& frame) const;
    bool compose_overflow(ID3D11Texture2D*, const HostImageSnapshot&, const XrPosef* worldFromView);
    void trim_composition_resources(uint32_t spheres, bool panels);
    struct EquirectTarget {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
        std::array<XrCompositionLayerProjectionView, 2> views{};
        XrCompositionLayerProjection layer{};
    };
    std::deque<EquirectTarget> equirectTargets_;
    HANDLE fpsHudEvent_ = nullptr;
    bool fpsHudInitialized_ = false, fpsHudFailed_ = false;
    uint32_t fpsHudMaxLayers_ = 0;
    XrSwapchain fpsHudSwapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> fpsHudImages_;
    std::vector<uint64_t> fpsHudUploaded_;
    std::vector<uint8_t> fpsHudPixels_;
    uint64_t fpsHudGeneration_ = 1;
#if defined(AXRB_ENABLE_PERFORMANCE_OVERLAY)
    std::chrono::steady_clock::time_point fpsHudUpdated_{}, fpsHudLogged_{};
    axrb::host::GuestPerformanceReader guestPerformance_;
#endif
    axrb::host::FpsCounter fpsCounter_;

    bool reportedProjectionSubmit_ = false;
    bool reportedAndroidImageSubmit_ = false;
#endif
#if !defined(_WIN32)
    PFN_xrConvertTimespecTimeToTimeKHR convertTimespecTimeToTime_ = nullptr;
#endif
};

} // namespace axrb::host::detail
