#include "openxr_session.h"
#if defined(_WIN32)
namespace axrb::host::detail {
bool OpenXrSession::acquire_panel_swapchain(const HostImageSnapshot &frame) {
    if (!frame.gpu || frame.gpu->count < 2)
        return false;
    uint32_t width = 0, height = 0, slices = 0;
    // Part zero remains in the projection swapchain for preview/capture. The
    // auxiliary swapchain stores every later part in app order, reserving two
    // adjacent slices for each projection and one for each mono layer.
    for (uint32_t i = 1; i < frame.gpu->count; ++i) {
        const auto& part = frame.gpu->parts[i];
        width = (std::max)(width, part.header.width);
        height = (std::max)(height, part.header.height);
        slices += part.projection.view_count == 2 ? 2u : 1u;
    }
    if (!panelSwapchain_ || width > panelWidth_ || height > panelHeight_ || slices != panelLayers_) {
        if (panelSwapchain_) {
            if (destroySwapchain_(panelSwapchain_) != XR_SUCCESS)
                return false;
            panelSwapchain_ = XR_NULL_HANDLE;
            panelImages_.clear();
        }
        panelWidth_ = width;
        panelHeight_ = height;
        panelLayers_ = slices;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.format = projectionFormat_;
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                          XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.width = panelWidth_;
        info.height = panelHeight_;
        info.arraySize = panelLayers_;
        info.sampleCount = info.faceCount = info.mipCount = 1;
        if (createSwapchain_(session_, &info, &panelSwapchain_) != XR_SUCCESS)
            return false;
        uint32_t images = 0;
        if (enumerateSwapchainImages_(panelSwapchain_, 0, &images, nullptr) != XR_SUCCESS || !images)
            return false;
        panelImages_.resize(images, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        panelSequences_.assign(images, UINT64_MAX);
        if (enumerateSwapchainImages_(panelSwapchain_, images, &images,
                                      reinterpret_cast<XrSwapchainImageBaseHeader *>(panelImages_.data())) !=
            XR_SUCCESS)
            return false;
        std::fprintf(stderr, "AXRB compositor: ordered layer storage %ux%u, %u slices (%llu MiB across %u images)\n",
                     panelWidth_, panelHeight_, panelLayers_,
                     static_cast<unsigned long long>(panelWidth_) * panelHeight_ * panelLayers_ * images * 4 /
                         (1024 * 1024),
                     images);
    }
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (acquireSwapchainImage_(panelSwapchain_, &acquire, &panelImageIndex_) != XR_SUCCESS)
        return false;
    // Mark ownership immediately so the caller's release guard also covers a
    // wait failure or an invalid runtime image index.
    panelAcquired_ = true;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (waitSwapchainImage_(panelSwapchain_, &wait) != XR_SUCCESS)
        return false;
    return panelImageIndex_ < panelImages_.size();
}
} // namespace axrb::host::detail
#endif
