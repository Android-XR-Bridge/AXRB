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
    const bool scene = frame.projection.view_count == 2;
    const UINT width = scene ? frame.header.width : projectionWidth_;
    const UINT height = scene ? frame.header.height : projectionHeight_;
    const auto format = static_cast<DXGI_FORMAT>(projectionFormat_);
    // Normalize an unpremultiplied scene before blending premultiplied overlays.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> base;
    if (scene && (frame.projection.layer_flags & 6u) == 6u) {
        D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
        desc.MiscFlags = 0; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d3dDevice_.get()->CreateTexture2D(&desc, nullptr, &base))) return false;
        d3dContext_.get()->CopyResource(base.Get(), target);
    }
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& view = overflowViews_[eye];
        if (!scene || base) {
            D3D11_RENDER_TARGET_VIEW_DESC desc{}; desc.Format = format;
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.FirstArraySlice = eye; desc.Texture2DArray.ArraySize = 1;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
            if (FAILED(d3dDevice_.get()->CreateRenderTargetView(target, &desc, &output))) return false;
            const float transparent[4]{}; d3dContext_.get()->ClearRenderTargetView(output.Get(), transparent);
        }
        if (base) {
            const float l = std::tan(view.fov.angleLeft), r = std::tan(view.fov.angleRight);
            const float u = std::tan(view.fov.angleUp), d = std::tan(view.fov.angleDown);
            protocol::ImageQuad quad{}; quad.pose = to_protocol_pose(view.pose);
            const auto& q = view.pose.orientation;
            const float x = (l+r)*.5f, y = (u+d)*.5f, z = -1;
            const float tx = 2*(q.y*z-q.z*y), ty = 2*(q.z*x-q.x*z), tz = 2*(q.x*y-q.y*x);
            quad.pose.x += x+q.w*tx+q.y*tz-q.z*ty;
            quad.pose.y += y+q.w*ty+q.z*tx-q.x*tz;
            quad.pose.z += z+q.w*tz+q.x*ty-q.y*tx;
            quad.width = r-l; quad.height = u-d; quad.layer_flags = 6;
            if (!quadRenderer_.render(d3dDevice_.get(), d3dContext_.get(), base.Get(), eye,
                {static_cast<int32_t>(width),static_cast<int32_t>(height)}, format, quad, view, target, eye, width, height)) return false;
        }
        for (uint32_t i = scene ? 1u : 0u; i < frame.gpu->count; ++i) {
            auto& part = frame.gpu->parts[i];
            auto* source = part.receiver.render_texture(d3dContext_.get());
            if (!source) return false;
            const uint32_t slice = 0;
            XrExtent2Di extent{static_cast<int32_t>(part.header.width),static_cast<int32_t>(part.header.height)};
            // Overlay metadata is world-space. A VIEW projection's pixels and
            // eye cameras are not, so render a private metadata copy in VIEW.
            auto metadata = part.projection;
            if (worldFromView) {
                if (metadata.is_equirect())
                    metadata.equirect.pose = relative_to_view(metadata.equirect.pose, *worldFromView);
                else
                    metadata.quads[0].pose = relative_to_view(metadata.quads[0].pose, *worldFromView);
            }
            bool rendered = metadata.is_equirect()
                ? equirectRenderer_.render(d3dDevice_.get(), d3dContext_.get(), source,
                    slice, extent, format, metadata.equirect, view, target, eye, width, height, true)
                : quadRenderer_.render(d3dDevice_.get(), d3dContext_.get(), source,
                    slice, extent, format, metadata.quads[0], view, target, eye, width, height);
            if (!rendered) return false;
        }
    }
    static bool reported = false;
    if (!reported) {
        std::fprintf(stderr, "AXRB compositor: %u dynamic layers composed into stereo; native limit %u\n", frame.gpu->count, nativeLayerLimit_);
        reported = true;
    }
    return true;
}
}
#endif
