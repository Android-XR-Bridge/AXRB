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

protocol::Pose relative_to_view(const protocol::Pose& worldPose, const XrPosef& worldFromView)
{
    const XrQuaternionf viewFromWorld{
        -worldFromView.orientation.x,
        -worldFromView.orientation.y,
        -worldFromView.orientation.z,
        worldFromView.orientation.w,
    };
    const XrVector3f delta{
        worldPose.x - worldFromView.position.x,
        worldPose.y - worldFromView.position.y,
        worldPose.z - worldFromView.position.z,
    };
    const XrVector3f position = rotate(viewFromWorld, delta);
    const XrQuaternionf orientation = multiply(
        viewFromWorld, {worldPose.qx, worldPose.qy, worldPose.qz, worldPose.qw});
    return {
        position.x, position.y, position.z,
        orientation.x, orientation.y, orientation.z, orientation.w,
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

bool convert_space(protocol::Pose& pose, bool sourceIsView, bool targetIsView,
                   const XrPosef* worldFromView)
{
    if (sourceIsView == targetIsView) return true;
    if (!worldFromView) return false;
    pose = targetIsView
        ? relative_to_view(pose, *worldFromView)
        : relative_to_world(pose, *worldFromView);
    return true;
}

XrView projection_target_view(const protocol::ImageProjectionView& source)
{
    XrView view{XR_TYPE_VIEW};
    view.pose.position = {source.pose.x, source.pose.y, source.pose.z};
    view.pose.orientation = {source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
    view.fov = {source.angle_left, source.angle_right, source.angle_up, source.angle_down};
    return view;
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

bool OpenXrSession::compose_overflow(
    ID3D11Texture2D* target, const HostImageSnapshot& frame, const XrPosef* worldFromView) {
    if (!frame.gpu || frame.gpu->count == 0) return false;
    const auto& first = frame.gpu->parts[0];
    const bool firstIsProjection = first.projection.view_count == 2;
    const bool targetIsView = firstIsProjection && first.projection.is_view_space();
    const UINT width = firstIsProjection ? first.header.width : projectionWidth_;
    const UINT height = firstIsProjection ? first.header.height : projectionHeight_;
    const auto format = static_cast<DXGI_FORMAT>(projectionFormat_);
    std::array<XrView, 2> targetViews{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        targetViews[eye] = firstIsProjection
            ? projection_target_view(first.projection.views[eye])
            : overflowViews_[eye];
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
        if (metadata.is_equirect()) {
            if (!convert_space(metadata.equirect.pose, false, targetIsView, worldFromView))
                return false;
        } else if (metadata.view_count != 2) {
            if (!convert_space(metadata.quads[0].pose, false, targetIsView, worldFromView))
                return false;
        }
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& targetView = targetViews[eye];
            bool rendered = false;
            if (metadata.view_count == 2) {
                auto sourceView = metadata.views[eye];
                if (!convert_space(sourceView.pose, metadata.is_view_space(), targetIsView, worldFromView))
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
        std::fprintf(stderr, "AXRB compositor: %u ordered dynamic layers composed into stereo; native limit %u\n",
                     frame.gpu->count, nativeLayerLimit_);
        reported = true;
    }
    return true;
}
}
#endif
