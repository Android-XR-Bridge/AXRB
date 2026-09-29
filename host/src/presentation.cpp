#include "openxr_session.h"
#include "splash.h"

namespace axrb::host::detail {
#if defined(_WIN32)
namespace {
bool same_pose(const XrPosef& a, const XrPosef& b)
{
    return a.position.x == b.position.x && a.position.y == b.position.y &&
        a.position.z == b.position.z && a.orientation.x == b.orientation.x &&
        a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z &&
        a.orientation.w == b.orientation.w;
}

bool same_view(const XrView& a, const XrView& b)
{
    return same_pose(a.pose, b.pose) &&
        a.fov.angleLeft == b.fov.angleLeft &&
        a.fov.angleRight == b.fov.angleRight &&
        a.fov.angleUp == b.fov.angleUp &&
        a.fov.angleDown == b.fov.angleDown;
}
}
#endif


#if defined(_WIN32)
bool OpenXrSession::create_d3d11_device()
{
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    const XrResult result = getD3D11GraphicsRequirements_(instance_, systemId_, &requirements);
    if (result != XR_SUCCESS) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: xrGetD3D11GraphicsRequirementsKHR failed: %s (%d)\n",
            xr_result_name(result),
            result);
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "AXRB OpenXR: CreateDXGIFactory1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory.get()->EnumAdapters1(i, adapter.put());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter.get()->GetDesc1(&desc))) {
            continue;
        }

        if (desc.AdapterLuid.HighPart == requirements.adapterLuid.HighPart &&
            desc.AdapterLuid.LowPart == requirements.adapterLuid.LowPart) {
            selectedAdapter = std::move(adapter);
            break;
        }
    }

    if (selectedAdapter.get() == nullptr) {
        std::fprintf(stderr, "AXRB OpenXR: failed to find D3D11 adapter requested by OpenXR runtime\n");
        return false;
    }

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        selectedAdapter.get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        featureLevels,
        sizeof(featureLevels) / sizeof(featureLevels[0]),
        D3D11_SDK_VERSION,
        d3dDevice_.put(),
        &createdFeatureLevel,
        d3dContext_.put());
    if (FAILED(hr)) {
        std::fprintf(stderr, "AXRB OpenXR: D3D11CreateDevice failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }

    // SteamVR also uses the binding device's immediate context during
    // xrEndFrame. Protect those accesses as well as our own copy commands.
    ComPtr<ID3D11Multithread> multithread;
    if (FAILED(d3dContext_.get()->QueryInterface(__uuidof(ID3D11Multithread),
            reinterpret_cast<void**>(multithread.put())))) return false;
    multithread.get()->SetMultithreadProtected(TRUE);

    if (createdFeatureLevel < requirements.minFeatureLevel) {
        std::fprintf(stderr, "AXRB OpenXR: D3D11 feature level is below runtime requirement\n");
        return false;
    }

    // Reception has its own immediate context: SteamVR can hold its
    // binding context while pacing xrEndFrame without delaying Android.
    hr = D3D11CreateDevice(selectedAdapter.get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
        sizeof(featureLevels) / sizeof(featureLevels[0]), D3D11_SDK_VERSION,
        receiveDevice_.put(), nullptr, receiveContext_.put());
    if (FAILED(hr)) return false;

    std::fprintf(stderr, "AXRB OpenXR: D3D11 graphics binding ready\n");
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::create_projection_swapchain()
{
    XrResult result;
    // Extent/format are negotiated before receive/pose threads start. Growing
    // the layer array must not rewrite these concurrently observed values.
    if (projectionFormat_ == 0) {
        uint32_t viewCount = 0;
        if (enumerateViewConfigurationViews_(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0, &viewCount, nullptr) != XR_SUCCESS || viewCount != 2) return false;
        std::array<XrViewConfigurationView, 2> configViews{{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        if (enumerateViewConfigurationViews_(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                2, &viewCount, configViews.data()) != XR_SUCCESS) return false;
        projectionWidth_ = projectionHeight_ = 0;
        // The stereo transport uses equal-size array slices. Accommodate both
        // recommendations if the runtime recommends asymmetric view sizes.
        for (const auto& view : configViews) {
            projectionWidth_ = (std::max)(projectionWidth_, view.recommendedImageRectWidth);
            projectionHeight_ = (std::max)(projectionHeight_, view.recommendedImageRectHeight);
        }
        // Local benchmark override: retain the requested physical-eye minimum
        // without changing AXRB's default for ordinary launches.
        if (const char* extent = std::getenv("AXRB_MIN_EYE_EXTENT")) {
            unsigned width = 0, height = 0;
            char trailing = 0;
            if (std::sscanf(extent, "%ux%u%c", &width, &height, &trailing) != 2 ||
                !axrb::protocol::valid_render_extent(width, height)) return false;
            for (const auto& view : configViews) {
                if (width > view.maxImageRectWidth || height > view.maxImageRectHeight) return false;
            }
            projectionWidth_ = (std::max)(projectionWidth_, width);
            projectionHeight_ = (std::max)(projectionHeight_, height);
        }
        if (!axrb::protocol::valid_render_extent(projectionWidth_, projectionHeight_)) {
            std::fprintf(stderr, "AXRB OpenXR: unsupported recommended eye extent %ux%u\n", projectionWidth_, projectionHeight_);
            return false;
        }
        std::fprintf(stderr, "AXRB OpenXR: runtime recommended stereo extent %ux%u\n", projectionWidth_, projectionHeight_);
        uint32_t formatCount = 0;
        result = enumerateSwapchainFormats_(session_, 0, &formatCount, nullptr);
        if (result != XR_SUCCESS || formatCount == 0) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::vector<int64_t> formats(formatCount);
        result = enumerateSwapchainFormats_(session_, formatCount, &formatCount, formats.data());
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats(list) failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::fprintf(stderr, "AXRB OpenXR: supported swapchain formats:");
        for (int64_t format : formats) {
            std::fprintf(stderr, " %lld", static_cast<long long>(format));
        }
        std::fprintf(stderr, "\n");

        int64_t selectedFormat = formats[0];
        constexpr int64_t preferredFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        };
        for (int64_t preferred : preferredFormats) {
            for (int64_t format : formats) {
                if (format == preferred) {
                    selectedFormat = format;
                    break;
                }
            }
            if (selectedFormat == preferred) {
                break;
            }
        }
        projectionFormat_ = selectedFormat;
    }

    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = projectionFormat_;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = projectionWidth_;
    swapchainInfo.height = projectionHeight_;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = projectionArraySize_;
    swapchainInfo.mipCount = 1;

    result = createSwapchain_(session_, &swapchainInfo, &projectionSwapchain_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateSwapchain failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    uint32_t imageCount = 0;
    result = enumerateSwapchainImages_(projectionSwapchain_, 0, &imageCount, nullptr);
    if (result != XR_SUCCESS || imageCount == 0) {
        std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    projectionImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    splashUploaded_.assign(imageCount, false);
    uploadedAndroidSequenceByImage_.assign(imageCount, UINT64_MAX);
    uploadedGpuSessionByImage_.assign(imageCount, 0);
    uploadedCompositionByImage_.resize(imageCount);
    uploadedMixedTimes_.assign(imageCount, 0);
    uploadedExtentByImage_.resize(imageCount, {static_cast<int32_t>(projectionWidth_), static_cast<int32_t>(projectionHeight_)});
    uploadedOverflowWorldFromView_.resize(imageCount);
    uploadedOverflowWorldFromViewValid_.assign(imageCount, false);
    uploadedOverflowViews_.resize(imageCount);
    uploadedOverflowViewsValid_.assign(imageCount, false);
    result = enumerateSwapchainImages_(
        projectionSwapchain_,
        imageCount,
        &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(projectionImages_.data()));
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages(list) failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    std::fprintf(
        stderr,
        "AXRB OpenXR: projection swapchain ready %ux%u images=%u\n",
        projectionWidth_,
        projectionHeight_,
        imageCount);
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::update_projection_layers(XrTime displayTime, XrDuration displayPeriod,
                                           NativeLayerSubmission& submission)
{
    submission.hasGameProjection = false;
    static axrb::protocol::PerfStats stats("host-projection");
    axrb::protocol::PerfScope scope(stats);
    submission.layers.clear();
    std::unique_lock handoffLock(frameHandoffMutex_, std::defer_lock);
    if (!concurrentGpuFrames_) handoffLock.lock();
    // Pin one coherent frame through swapchain growth, acquisition and copy.
    HostImageSnapshot frame;
    {
        static axrb::protocol::PerfStats snapshotStats("host-frame-snapshot");
        axrb::protocol::PerfScope snapshotScope(snapshotStats);
        if (imageFrame_) frame = imageFrame_->snapshot();
    }
    if (handoffLock.owns_lock()) handoffLock.unlock();
    if (frame.header.version == axrb::protocol::kEmptyImageFrameVersion) {
        trim_composition_resources(0, false);
        return false;
    }

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = acquireSwapchainImage_(projectionSwapchain_, &acquireInfo, &imageIndex);
    if (result != XR_SUCCESS || imageIndex >= projectionImages_.size()) return false;
    struct ReleaseImage {
        PFN_xrReleaseSwapchainImage release;
        XrSwapchain swapchain;
        ~ReleaseImage() {
            XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            release(swapchain, &info);
        }
    } releaseImage{releaseSwapchainImage_, projectionSwapchain_};

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    static axrb::protocol::PerfStats swapchainWaitStats("host-swapchain-wait");
    {
        axrb::protocol::PerfScope waitScope(swapchainWaitStats);
        result = waitSwapchainImage_(projectionSwapchain_, &waitInfo);
    }
    if (result != XR_SUCCESS) return false;

    // Never hold the receive/publication lock while waiting for the receiver.
    // This opt-in experiment budgets against both predicted display lead and
    // the current frame period, reserving time for upload/submission. These
    // are best-effort waits, not a hard deadline guarantee on Windows.
    // Missing time conversion, invalid prediction or late frames skip the wait.
    const XrTime hostNow = windows_xr_time();
    if (hostNow) {
        const int64_t lead = displayTime - hostNow;
        static axrb::protocol::PerfStats leadStats("host-display-lead");
        leadStats.record(static_cast<double>(lead) / 1000000.0);
        const auto budget = fresh_wait_budget_ns(freshFrameWaitUs_, lead, displayPeriod);
        if (budget > 0 && imageFrame_ && submittedGameFrames_.initialized) {
            if (imageFrame_->needs_newer(submittedGameFrames_.lastSequence)) {
                static axrb::protocol::PerfStats freshWaitStats("host-fresh-image-wait");
                axrb::protocol::PerfScope waitScope(freshWaitStats);
                imageFrame_->wait_for_newer(submittedGameFrames_.lastSequence, std::chrono::nanoseconds(budget));
            }
        }
    }
    if (!concurrentGpuFrames_) handoffLock.lock();
    // A newer complete frame may have arrived while SteamVR held the swapchain.
    // Replace texture and pose metadata together.
    if (imageFrame_) {
        auto newest = imageFrame_->snapshot();
        if (newest.pixels) frame = std::move(newest);
    }
    if (frame.header.version == axrb::protocol::kEmptyImageFrameVersion) {
        trim_composition_resources(0, false);
        return false;
    }
    if (frame.pixels) {
        static axrb::protocol::PerfStats ageStats("host-selected-frame-age");
        ageStats.record(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frame.receivedAt).count());
    }

    // Auxiliary slices retain each later part's real image extent. Keep the
    // image acquired until every native descriptor and sphere fallback that
    // samples it has been built.
    struct ReleasePanels {
        OpenXrSession* owner;
        ~ReleasePanels() {
            if (owner->panelAcquired_) {
                XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                owner->releaseSwapchainImage_(owner->panelSwapchain_, &info);
                owner->panelAcquired_ = false;
            }
        }
    } releasePanels{this};
    const bool mixedBatch = frame.gpu && axrb::protocol::mixed_gpu_version(frame.header.version);
    const bool precompose = mixedBatch && should_precompose(*frame.gpu);
    if (mixedBatch && !precompose && frame.gpu->count > 1 && !acquire_panel_swapchain(frame))
        return false;

    XrPosef overflowWorldFromView{};
    const XrPosef* overflowWorldFromViewPtr = nullptr;
    if (precompose) {
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = displayTime;
        locate.space = localSpace_;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        for (auto& view : overflowViews_) view = {XR_TYPE_VIEW};
        constexpr XrViewStateFlags requiredViewFlags =
            XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if (locateViews_(session_, &locate, &state,
                static_cast<uint32_t>(overflowViews_.size()), &count,
                overflowViews_.data()) != XR_SUCCESS ||
            count != static_cast<uint32_t>(overflowViews_.size()) ||
            (state.viewStateFlags & requiredViewFlags) != requiredViewFlags)
            return false;

        bool hasViewSpace = false;
        for (uint32_t i = 0; i < frame.gpu->count; ++i)
            hasViewSpace |= frame.gpu->parts[i].projection.is_view_space();
        if (hasViewSpace) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (locateSpace_(viewSpace_, localSpace_, displayTime, &location) != XR_SUCCESS ||
                (location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT |
                    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) !=
                    (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
                return false;
            overflowWorldFromView = location.pose;
            overflowWorldFromViewPtr = &overflowWorldFromView;
        }
    }

    bool uploaded;
    {
        static axrb::protocol::PerfStats copyStats("host-texture-upload");
        axrb::protocol::PerfScope copyScope(copyStats);
        uploaded = fill_projection_texture(
            projectionImages_[imageIndex].texture, imageIndex, frame, precompose, overflowWorldFromViewPtr);
    }
    if (handoffLock.owns_lock()) handoffLock.unlock();

    if (uploaded) {
        static axrb::protocol::PerfStats mirrorStats("host-preview");
        axrb::protocol::PerfScope mirrorScope(mirrorStats);
        const auto extent = uploadedExtentByImage_[imageIndex];
        const auto sequence = uploadedAndroidSequenceByImage_[imageIndex];
        mirror_.present(d3dContext_.get(), projectionImages_[imageIndex].texture,
            extent.width, extent.height, static_cast<DXGI_FORMAT>(projectionFormat_), sequence);
        if (frame.gpu && (mixedBatch || frame.projection.view_count == 2))
            debug_capture_frame(d3dContext_.get(), projectionImages_[imageIndex].texture,
                extent.width, extent.height, sequence);
    }

    const uint32_t splashSize = (std::min)(512u, (std::min)(projectionWidth_, projectionHeight_));
    if (!uploaded && !debugGraphicsTest_) {
        mirror_.present(d3dContext_.get(), projectionImages_[imageIndex].texture,
            splashSize, splashSize, static_cast<DXGI_FORMAT>(projectionFormat_), UINT64_MAX);
        submission.quads.resize(1);
        auto& splash = submission.quads[0];
        splash = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        splash.space = viewSpace_;
        splash.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        splash.pose.orientation.w = 1.0f;
        splash.pose.position.z = -2.0f;
        splash.size = {0.8f, 0.8f};
        splash.subImage.swapchain = projectionSwapchain_;
        splash.subImage.imageRect.extent = {
            static_cast<int32_t>(splashSize), static_cast<int32_t>(splashSize)};
        submission.layers.push_back(
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&splash));
        trim_composition_resources(0, false);
        return true;
    }
    if (!uploaded) return false;

    const auto& parts = uploadedCompositionByImage_[imageIndex];
    uint32_t projectionCount = 0, quadCount = 0, sphereCount = 0;
    bool usesPanels = false;
    bool needsLocatedViews = false;
    for (const auto& part : parts) {
        usesPanels |= part.panel;
        if (part.projection.is_equirect()) {
            ++sphereCount;
        } else if (const uint32_t count = part.projection.quad_count()) {
            quadCount += count;
        } else {
            ++projectionCount;
            needsLocatedViews |= part.projection.view_count != 2;
        }
    }
    submission.projectionViews.resize(projectionCount);
    submission.projections.resize(projectionCount);
    submission.quads.resize(quadCount);
    submission.spheres.resize(sphereCount);
    submission.layers.reserve(projectionCount + quadCount + sphereCount + 1);

    std::array<XrView, 2> locatedViews{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    if (needsLocatedViews) {
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = displayTime;
        locateInfo.space = localSpace_;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        result = locateViews_(session_, &locateInfo, &viewState,
            static_cast<uint32_t>(locatedViews.size()), &viewCount, locatedViews.data());
        if (result != XR_SUCCESS || viewCount < 2) return false;
    }

    uint32_t projectionIndex = 0, quadIndex = 0, sphereIndex = 0, fallbackSphereCount = 0;
    for (const auto& part : parts) {
        const auto& metadata = part.projection;
        const XrSwapchain swapchain = part.panel ? panelSwapchain_ : projectionSwapchain_;
        auto* image = part.panel
            ? panelImages_[panelImageIndex_].texture
            : projectionImages_[imageIndex].texture;
        if (metadata.is_equirect()) {
            const auto& source = metadata.equirect;
            auto& sphere = submission.spheres[sphereIndex++];
            sphere = {XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
            sphere.space = localSpace_;
            sphere.layerFlags = source.layer_flags;
            sphere.eyeVisibility = static_cast<XrEyeVisibility>(source.eye_visibility);
            sphere.pose.position = {source.pose.x, source.pose.y, source.pose.z};
            sphere.pose.orientation = {source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
            sphere.radius = source.radius;
            sphere.centralHorizontalAngle = source.horizontal_angle;
            sphere.upperVerticalAngle = source.upper_angle;
            sphere.lowerVerticalAngle = source.lower_angle;
            sphere.subImage.swapchain = swapchain;
            sphere.subImage.imageArrayIndex = part.imageArrayIndex;
            sphere.subImage.imageRect.extent = part.extent;
            if (equirectEnabled_) {
                submission.layers.push_back(
                    reinterpret_cast<const XrCompositionLayerBaseHeader*>(&sphere));
            } else {
                const uint32_t fallbackIndex = fallbackSphereCount++;
                if (!render_equirect(fallbackIndex, displayTime, image, sphere, source)) return false;
                submission.layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                    &equirectTargets_[fallbackIndex].layer));
            }
            continue;
        }

        const uint32_t packedQuads = metadata.quad_count();
        if (packedQuads) {
            for (uint32_t q = 0; q < packedQuads; ++q) {
                const auto& source = metadata.quads[q];
                auto& quad = submission.quads[quadIndex++];
                quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                quad.space = localSpace_;
                quad.layerFlags = source.layer_flags;
                quad.eyeVisibility = static_cast<XrEyeVisibility>(source.eye_visibility);
                quad.pose.position = {source.pose.x, source.pose.y, source.pose.z};
                quad.pose.orientation = {source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
                quad.size = {source.width, source.height};
                quad.subImage.swapchain = swapchain;
                quad.subImage.imageArrayIndex = part.imageArrayIndex + q;
                quad.subImage.imageRect.extent = part.extent;
                submission.layers.push_back(
                    reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
            }
            continue;
        }

        auto& projectionViews = submission.projectionViews[projectionIndex];
        auto& projectionLayer = submission.projections[projectionIndex++];
        for (uint32_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
            auto& view = projectionViews[eyeIndex];
            view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            if (metadata.view_count == 2) {
                const auto& source = metadata.views[eyeIndex];
                view.pose.position = {source.pose.x, source.pose.y, source.pose.z};
                view.pose.orientation = {
                    source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
                view.fov = {
                    source.angle_left, source.angle_right, source.angle_up, source.angle_down};
                view.subImage.imageRect.extent = part.extent;
            } else {
                view.pose = locatedViews[eyeIndex].pose;
                view.fov = {-kAppProjectionHalfFovRadians, kAppProjectionHalfFovRadians,
                    kAppProjectionHalfFovRadians, -kAppProjectionHalfFovRadians};
                view.subImage.imageRect.extent = {
                    static_cast<int32_t>(projectionWidth_), static_cast<int32_t>(projectionHeight_)};
            }
            view.subImage.swapchain = swapchain;
            view.subImage.imageArrayIndex = part.imageArrayIndex + eyeIndex;
        }
        projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projectionLayer.space = metadata.is_view_space() ? viewSpace_ : localSpace_;
        projectionLayer.layerFlags =
            metadata.layer_flags & axrb::protocol::kCompositionLayerFlagsMask;
        projectionLayer.viewCount = 2;
        projectionLayer.views = projectionViews.data();
        submission.layers.push_back(
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer));
    }

    trim_composition_resources(fallbackSphereCount, usesPanels);
    if (projectionCount && !reportedProjectionSubmit_) {
        std::fprintf(stderr, "AXRB OpenXR: submitting projection layer to runtime\n");
        reportedProjectionSubmit_ = true;
    }
    if (projectionCount) {
        submission.hasGameProjection = true;
        submission.gameSequence = uploadedAndroidSequenceByImage_[imageIndex];
        const auto extent = uploadedExtentByImage_[imageIndex];
        submission.gameWidth = extent.width;
        submission.gameHeight = extent.height;
    }
    return !submission.layers.empty();
}
#endif

#if defined(_WIN32)
bool OpenXrSession::fill_projection_texture(ID3D11Texture2D* texture, uint32_t imageIndex,
                                            const HostImageSnapshot& frame, bool precompose,
                                            const XrPosef* overflowWorldFromView)
{
    if (texture == nullptr || d3dContext_.get() == nullptr) {
        return false;
    }
    if (upload_android_frame(texture, imageIndex, frame, precompose, overflowWorldFromView)) {
        splashUploaded_[imageIndex] = false;
        return true;
    }
    // Never associate fallback pixels with stale game poses or layer metadata.
    uploadedCompositionByImage_[imageIndex].clear();
    uploadedAndroidSequenceByImage_[imageIndex] = UINT64_MAX;
    uploadedGpuSessionByImage_[imageIndex] = 0;
    uploadedOverflowWorldFromViewValid_[imageIndex] = false;
    uploadedOverflowViewsValid_[imageIndex] = false;
    if (!debugGraphicsTest_) {
        if (!splashUploaded_[imageIndex]) {
            const uint32_t size = (std::min)(512u, (std::min)(projectionWidth_, projectionHeight_));
            if (splashPixels_.empty()) {
                const bool bgra = projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM || projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                const bool linear = projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM || projectionFormat_ == DXGI_FORMAT_R8G8B8A8_UNORM;
                splashPixels_ = load_splash_pixels(size, bgra, linear);
            }
            D3D11_BOX box{0, 0, 0, size, size, 1};
            d3dContext_.get()->UpdateSubresource(texture, 0, &box, splashPixels_.data(), size * 4, size * size * 4);
            splashUploaded_[imageIndex] = true;
        }
        return false;
    }

    const uint32_t stride = projectionWidth_ * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * projectionHeight_);
    const uint32_t tick = projectionFrameCounter_++;
    for (uint32_t y = 0; y < projectionHeight_; ++y) {
        for (uint32_t x = 0; x < projectionWidth_; ++x) {
            const size_t offset = static_cast<size_t>(y) * stride + x * 4;
            pixels[offset + 0] = static_cast<uint8_t>((x + tick * 3) & 0xff);
            pixels[offset + 1] = static_cast<uint8_t>((y + tick * 2) & 0xff);
            pixels[offset + 2] = static_cast<uint8_t>((x / 8 + y / 8 + tick) & 0xff);
            pixels[offset + 3] = 255;
        }
    }

    for (uint32_t layer = 0; layer < 2; ++layer) {
        D3D11_BOX box{};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = projectionWidth_;
        box.bottom = projectionHeight_;
        box.back = 1;
        d3dContext_.get()->UpdateSubresource(texture, layer, &box, pixels.data(), stride, stride * projectionHeight_);
    }
    // Procedural test pixels still need a render-camera descriptor, just like
    // an untagged legacy frame. Reuse the ordinary native submission path.
    auto& composition = uploadedCompositionByImage_[imageIndex];
    composition.resize(1);
    composition[0] = {};
    composition[0].extent = {
        static_cast<int32_t>(projectionWidth_), static_cast<int32_t>(projectionHeight_)};
    uploadedExtentByImage_[imageIndex] = composition[0].extent;
    splashUploaded_[imageIndex] = false;
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::upload_android_frame(ID3D11Texture2D* texture, uint32_t imageIndex,
                                         const HostImageSnapshot& frame, bool precompose,
                                         const XrPosef* overflowWorldFromView)
{
    if (imageFrame_ == nullptr) {
        return false;
    }
    if (imageIndex >= uploadedAndroidSequenceByImage_.size()) {
        return false;
    }

    const auto& header = frame.header;
    const auto& projection = frame.projection;
    const auto& pixels = frame.pixels;
    if (header.width == 0 || header.height == 0 || header.layers == 0 || pixels == nullptr || pixels->empty()) {
        return false;
    }
    if (axrb::protocol::mixed_gpu_version(header.version)) {
        if (!frame.gpu) return false;
        const uint32_t activeMixedCount = frame.gpu->count;
        if (activeMixedCount < 2 ||
            activeMixedCount > axrb::protocol::kMaxWireCompositionLayers ||
            (!precompose && !panelAcquired_)) return false;

        // The target optics are display-time state even for all-LOCAL batches.
        // VIEW sources additionally depend on VIEW's pose in host world space.
        const bool needsTransform = precompose && overflowWorldFromView != nullptr;
        const bool viewTransformChanged = needsTransform &&
            (!uploadedOverflowWorldFromViewValid_[imageIndex] ||
             !same_pose(uploadedOverflowWorldFromView_[imageIndex], *overflowWorldFromView));
        const bool targetViewsChanged = precompose &&
            (!uploadedOverflowViewsValid_[imageIndex] ||
             !same_view(uploadedOverflowViews_[imageIndex][0], overflowViews_[0]) ||
             !same_view(uploadedOverflowViews_[imageIndex][1], overflowViews_[1]));
        if (uploadedAndroidSequenceByImage_[imageIndex] != header.sequence ||
            uploadedMixedTimes_[imageIndex] != header.monotonic_time_ns ||
            (!precompose && panelSequences_[panelImageIndex_] != header.sequence) ||
            viewTransformChanged || targetViewsChanged) {
            bool queued = true;
            uint32_t panelSlice = 0;
            if (!precompose) {
                for (uint32_t i = 0; i < activeMixedCount; ++i) {
                    auto& part = frame.gpu->parts[i];
                    const uint32_t sliceCount = part.projection.view_count == 2 ? 2u : 1u;
                    const bool panel = i != 0;
                    const uint32_t imageArrayIndex = panel ? panelSlice : 0;
                    auto* destination = panel
                        ? panelImages_[panelImageIndex_].texture
                        : texture;
                    if (!part.receiver.enqueue_copy_to(
                            d3dContext_.get(), destination, imageArrayIndex, sliceCount)) {
                        queued = false;
                    }
                    if (panel) panelSlice += sliceCount;
                    if (!queued) break;
                }
            }
            if (queued && precompose)
                queued = compose_overflow(texture, frame, overflowWorldFromView);
            // One completion covers every eye and layer. Keep the frame lease
            // and handoff lock until it finishes, even after a partial enqueue.
            const bool completed = frameCopyCompletion_.wait(d3dDevice_.get(), d3dContext_.get());
            if (!queued || !completed) {
                if (!precompose) panelSequences_[panelImageIndex_] = UINT64_MAX;
                gpuFrames_.retire(frame.gpu);
                return false;
            }
            static bool reportedBatch = false;
            if (!reportedBatch) {
                std::fprintf(stderr, "AXRB GPU: mixed frame copies use one completion fence\n");
                reportedBatch = true;
            }

            // Publish descriptors only after every texture slice is complete.
            auto& nextParts = uploadedCompositionByImage_[imageIndex];
            if (precompose) {
                UploadedCompositionPart output{};
                output.projection.view_count = 2;
                const auto& first = frame.gpu->parts[0];
                const bool firstProjection = first.projection.view_count == 2;
                output.projection.layer_flags = firstProjection
                    ? first.projection.layer_flags &
                        ~(uint32_t(XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) |
                          axrb::protocol::kProjectionViewSpaceBit)
                    : uint32_t(XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    const auto& view = overflowViews_[eye];
                    auto& target = output.projection.views[eye];
                    target.pose = to_protocol_pose(view.pose);
                    target.angle_left = view.fov.angleLeft;
                    target.angle_right = view.fov.angleRight;
                    target.angle_up = view.fov.angleUp;
                    target.angle_down = view.fov.angleDown;
                }
                output.extent = {
                    static_cast<int32_t>(projectionWidth_),
                    static_cast<int32_t>(projectionHeight_)};
                nextParts.assign(1, output);
            } else {
                nextParts.resize(activeMixedCount);
                panelSlice = 0;
                for (uint32_t i = 0; i < activeMixedCount; ++i) {
                    const auto& part = frame.gpu->parts[i];
                    nextParts[i] = {
                        part.projection,
                        {static_cast<int32_t>(part.header.width),
                         static_cast<int32_t>(part.header.height)},
                        i ? panelSlice : 0,
                        i != 0,
                    };
                    if (i) panelSlice += part.projection.view_count == 2 ? 2u : 1u;
                }
            }
            if (needsTransform) {
                uploadedOverflowWorldFromView_[imageIndex] = *overflowWorldFromView;
                uploadedOverflowWorldFromViewValid_[imageIndex] = true;
            } else {
                uploadedOverflowWorldFromViewValid_[imageIndex] = false;
            }
            if (precompose) {
                uploadedOverflowViews_[imageIndex] = overflowViews_;
                uploadedOverflowViewsValid_[imageIndex] = true;
            } else {
                uploadedOverflowViewsValid_[imageIndex] = false;
            }
            uploadedExtentByImage_[imageIndex] =
                uploadedCompositionByImage_[imageIndex][0].extent;
            uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
            uploadedGpuSessionByImage_[imageIndex] = 0;
            uploadedMixedTimes_[imageIndex] = header.monotonic_time_ns;
            if (!precompose) panelSequences_[panelImageIndex_] = header.sequence;
        }
        return true;
    }
    uploadedOverflowWorldFromViewValid_[imageIndex] = false;
    uploadedOverflowViewsValid_[imageIndex] = false;
    if ((header.version == axrb::protocol::kWindowsGpuFrameVersion || header.version == axrb::protocol::kQuadGpuFrameVersion || axrb::protocol::equirect_gpu_version(header.version))) {
        if (pixels->size() != sizeof(axrb::protocol::WindowsGpuFrame)) return false;
        axrb::protocol::WindowsGpuFrame gpu{};
        std::memcpy(&gpu, pixels->data(), sizeof(gpu));
        if (uploadedGpuSessionByImage_[imageIndex] == gpu.session &&
            uploadedAndroidSequenceByImage_[imageIndex] == header.sequence &&
            uploadedExtentByImage_[imageIndex].width == static_cast<int32_t>(header.width) &&
            uploadedExtentByImage_[imageIndex].height == static_cast<int32_t>(header.height)) {

            return true;
        }
        if (!frame.gpu || frame.gpu->count != 1) return false;
        if (!frame.gpu->parts[0].receiver.copy_to(d3dContext_.get(), texture)) { gpuFrames_.retire(frame.gpu); return false; }
        uploadedGpuSessionByImage_[imageIndex] = gpu.session;
        uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
        uploadedExtentByImage_[imageIndex] = {
            static_cast<int32_t>(header.width), static_cast<int32_t>(header.height)};
        uploadedCompositionByImage_[imageIndex].assign(1, {
            projection, uploadedExtentByImage_[imageIndex], 0, false});

        if (!reportedGpuImage_) { std::fprintf(stderr, "AXRB GPU: shared Windows textures active; no pixel TCP transfer\n"); reportedGpuImage_ = true; }
        return true;
    }
    if (header.sequence == uploadedAndroidSequenceByImage_[imageIndex]) {

        return true;
    }
    uploadedGpuSessionByImage_[imageIndex] = 0;
    if (projection.view_count == 2 && (header.width > projectionWidth_ || header.height > projectionHeight_)) {
        return false; // Cropping here would change the angular scale.
    }

    const uint32_t copyWidth = header.width < projectionWidth_ ? header.width : projectionWidth_;
    const uint32_t copyHeight = header.height < projectionHeight_ ? header.height : projectionHeight_;
    const uint32_t sourceStride = header.width * header.bytes_per_pixel;
    const bool needsBgra =
        projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
        projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    std::vector<uint8_t> uploadBuffer(static_cast<size_t>(header.width) * header.height * header.layers * 4);
    for (uint32_t layer = 0; layer < header.layers; ++layer) {
        const uint64_t layerOffset = static_cast<uint64_t>(layer) * header.width * header.height * header.bytes_per_pixel;
        for (uint32_t y = 0; y < header.height; ++y) {
            const uint32_t sourceY = header.height - 1 - y;
            const uint64_t sourceRow = layerOffset + static_cast<uint64_t>(sourceY) * sourceStride;
            const uint64_t destRow =
                (static_cast<uint64_t>(layer) * header.width * header.height + static_cast<uint64_t>(y) * header.width) * 4;
            for (uint32_t x = 0; x < header.width; ++x) {
                const uint64_t source = sourceRow + static_cast<uint64_t>(x) * header.bytes_per_pixel;
                const uint64_t dest = destRow + static_cast<uint64_t>(x) * 4;
                if (needsBgra) {
                    uploadBuffer[dest + 0] = (*pixels)[source + 2];
                    uploadBuffer[dest + 1] = (*pixels)[source + 1];
                    uploadBuffer[dest + 2] = (*pixels)[source + 0];
                    uploadBuffer[dest + 3] = (*pixels)[source + 3];
                } else {
                    uploadBuffer[dest + 0] = (*pixels)[source + 0];
                    uploadBuffer[dest + 1] = (*pixels)[source + 1];
                    uploadBuffer[dest + 2] = (*pixels)[source + 2];
                    uploadBuffer[dest + 3] = (*pixels)[source + 3];
                }
            }
        }
    }

    const uint8_t* uploadPixels = uploadBuffer.data();
    const uint32_t uploadStride = header.width * 4;

    const uint32_t sourceLayers = header.layers;
    for (uint32_t targetLayer = 0; targetLayer < 2; ++targetLayer) {
        const uint32_t sourceLayer = sourceLayers > 1 ? targetLayer % sourceLayers : 0;
        const uint64_t sourceOffset =
            static_cast<uint64_t>(sourceLayer) * header.width * header.height * 4;
        if (sourceOffset >= uploadBuffer.size()) {
            continue;
        }

        D3D11_BOX box{};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = copyWidth;
        box.bottom = copyHeight;
        box.back = 1;
        d3dContext_.get()->UpdateSubresource(
            texture,
            targetLayer,
            &box,
            uploadPixels + sourceOffset,
            uploadStride,
            uploadStride * header.height);
    }

    if (!reportedAndroidImageSubmit_) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: submitting Android image frames to SteamVR (%ux%u layers=%u)\n",
            header.width,
            header.height,
            header.layers);
        reportedAndroidImageSubmit_ = true;
    }
    uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
    uploadedExtentByImage_[imageIndex] = {
        static_cast<int32_t>(copyWidth), static_cast<int32_t>(copyHeight)};
    uploadedCompositionByImage_[imageIndex].assign(1, {
        projection, uploadedExtentByImage_[imageIndex], 0, false});
    if (projection.view_count == 2 && !reportedStereoProjection_) {
        const auto& l = projection.views[0];
        const auto& r = projection.views[1];
        const float dx = r.pose.x-l.pose.x, dy = r.pose.y-l.pose.y, dz = r.pose.z-l.pose.z;
        std::fprintf(stderr, "AXRB OpenXR: stereo render metadata active; camera separation=%.1fmm left FOV=(%.3f %.3f %.3f %.3f)\n",
                     std::sqrt(dx*dx+dy*dy+dz*dz)*1000, l.angle_left, l.angle_right, l.angle_up, l.angle_down);
        reportedStereoProjection_ = true;
    }
    return true;
}
#endif

} // namespace axrb::host::detail
