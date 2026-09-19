#include "openxr_session.h"
#if defined(_WIN32)
namespace axrb::host::detail {
namespace {
XrQuaternionf multiply(const XrQuaternionf& a, const XrQuaternionf& b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

XrVector3f rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const float dot = u.x * v.x + u.y * v.y + u.z * v.z;
    const float crossX = u.y * v.z - u.z * v.y;
    const float crossY = u.z * v.x - u.x * v.z;
    const float crossZ = u.x * v.y - u.y * v.x;
    const float scale = q.w * q.w - (u.x * u.x + u.y * u.y + u.z * u.z);
    return {
        2.0f * dot * u.x + scale * v.x + 2.0f * q.w * crossX,
        2.0f * dot * u.y + scale * v.y + 2.0f * q.w * crossY,
        2.0f * dot * u.z + scale * v.z + 2.0f * q.w * crossZ,
    };
}

protocol::Pose relative_to_world(const protocol::Pose& viewPose, const XrPosef& worldFromView)
{
    const XrVector3f position = rotate(
        worldFromView.orientation, {viewPose.x, viewPose.y, viewPose.z});
    const XrQuaternionf orientation = multiply(
        worldFromView.orientation, {viewPose.qx, viewPose.qy, viewPose.qz, viewPose.qw});
    return {
        worldFromView.position.x + position.x,
        worldFromView.position.y + position.y,
        worldFromView.position.z + position.z,
        orientation.x, orientation.y, orientation.z, orientation.w,
    };
}

bool convert_view_to_world(protocol::Pose& pose, bool sourceIsView,
                           const XrPosef* worldFromView)
{
    if (!sourceIsView) return true;
    if (!worldFromView) return false;
    pose = relative_to_world(pose, *worldFromView);
    return true;
}

}

void OpenXrSession::trim_composition_resources(uint32_t spheres, bool panels) {
    while (equirectTargets_.size() > spheres) {
        auto& target = equirectTargets_.back();
        if (target.swapchain && destroySwapchain_(target.swapchain) != XR_SUCCESS) break;
        equirectTargets_.pop_back();
    }
    if (!panels && panelSwapchain_ && !panelAcquired_ && destroySwapchain_(panelSwapchain_) == XR_SUCCESS) {
        panelSwapchain_ = XR_NULL_HANDLE; panelImages_.clear(); panelSequences_.clear();
        panelWidth_ = panelHeight_ = panelLayers_ = 0;
    }
}

bool OpenXrSession::should_precompose(const GpuFrameBatch& frame) const
{
    if (frame.count > nativeLayerLimit_) return true;
    if (!precomposeProjectionLayers_) return false;
    // Optionally collapse projection stacks so runtimes that cannot order
    // multiple projection layers receive one stereo result.
    bool foundProjection = false;
    for (uint32_t i = 0; i < frame.count; ++i) {
        const auto& metadata = frame.parts[i].projection;
        if (metadata.is_equirect() || metadata.quad_count()) continue;
        if (foundProjection) return true;
        foundProjection = true;
    }
    return false;
}

bool OpenXrSession::compose_overflow(
    ID3D11Texture2D* target, const HostImageSnapshot& frame, const XrPosef* worldFromView) {
    if (!frame.gpu || frame.gpu->count == 0) return false;
    const UINT width = projectionWidth_;
    const UINT height = projectionHeight_;
    const auto format = static_cast<DXGI_FORMAT>(projectionFormat_);
    const auto& targetViews = overflowViews_;
    for (uint32_t eye = 0; eye < 2; ++eye) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{}; desc.Format = format;
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice = eye; desc.Texture2DArray.ArraySize = 1;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
        if (FAILED(d3dDevice_.get()->CreateRenderTargetView(target, &desc, &output))) return false;
        const float transparent[4]{};
        d3dContext_.get()->ClearRenderTargetView(output.Get(), transparent);
    }
    for (uint32_t i = 0; i < frame.gpu->count; ++i) {
        auto& part = frame.gpu->parts[i];
        auto* source = part.receiver.render_texture(d3dContext_.get());
        if (!source) return false;
        const XrExtent2Di extent{
            static_cast<int32_t>(part.header.width), static_cast<int32_t>(part.header.height)};
        auto metadata = part.projection;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& targetView = targetViews[eye];
            bool rendered = false;
            if (metadata.view_count == 2) {
                auto sourceView = metadata.views[eye];
                if (!convert_view_to_world(
                        sourceView.pose, metadata.is_view_space(), worldFromView))
                    return false;
                rendered = quadRenderer_.render_projection(
                    d3dDevice_.get(), d3dContext_.get(), source, eye, extent, format,
                    sourceView, metadata.layer_flags, targetView, target, eye, width, height);
            } else if (metadata.is_equirect()) {
                rendered = equirectRenderer_.render(
                    d3dDevice_.get(), d3dContext_.get(), source, 0, extent, format,
                    metadata.equirect, targetView, target, eye, width, height, true);
            } else {
                rendered = quadRenderer_.render(
                    d3dDevice_.get(), d3dContext_.get(), source, 0, extent, format,
                    metadata.quads[0], targetView, target, eye, width, height);
            }
            if (!rendered) return false;
        }
    }
    static bool reported = false;
    if (!reported) {
        std::fprintf(stderr, "AXRB compositor: %u ordered dynamic layers composed into stereo by policy; native limit %u\n",
                     frame.gpu->count, nativeLayerLimit_);
        reported = true;
    }
    return true;
}
}
#endif
