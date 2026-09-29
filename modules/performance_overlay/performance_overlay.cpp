#include "openxr_session.h"
#include "performance_hud_bitmap.h"
#if defined(_WIN32)
namespace axrb::host::detail {
bool OpenXrSession::update_fps_hud(XrCompositionLayerQuad& layer,uint32_t existingLayers) {
    constexpr uint32_t width=axrb::host::performanceHudWidth,height=axrb::host::performanceHudHeight;
    if(!fpsHudInitialized_){
        fpsHudInitialized_=true;
        wchar_t name[128]{}; auto length=GetEnvironmentVariableW(L"AXRB_FPS_HUD_EVENT",name,128);
        if(length&&length<128)fpsHudEvent_=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,name);
        guestPerformance_.start();
    }
    if(!fpsHudEvent_)return false;
    if(mirror_.consume_hud_toggle()) {
        if(WaitForSingleObject(fpsHudEvent_,0)==WAIT_OBJECT_0)ResetEvent(fpsHudEvent_);else SetEvent(fpsHudEvent_);
        fpsHudUpdated_={};
    }
    const bool visible=WaitForSingleObject(fpsHudEvent_,0)==WAIT_OBJECT_0;
    const auto now=std::chrono::steady_clock::now();
    if(now-fpsHudUpdated_>=std::chrono::milliseconds(250)) {
        fpsHudUpdated_=now;
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        const auto stats=imageFrame_?imageFrame_->performance.snapshot(ns):axrb::host::PerformanceSummary{};
        if(now-fpsHudLogged_>=std::chrono::seconds(1)) {
            fpsHudLogged_=now;
            std::fprintf(stderr,"AXRB TruePerf: steady_ns=%lld fresh_fps=%.1f received_fps=%.1f host_fps=%.1f repeat_fps=%.1f avg_ms=%.3f worst_ms=%.3f low1_fps=%.3f low1_ready=%d fresh_age_ms=%.1f fresh_total=%llu received_total=%llu host_total=%llu repeat_total=%llu width=%u height=%u target_hz=%.3f hud=%d\n",
                static_cast<long long>(ns),stats.fresh.fps,stats.received.fps,stats.host.fps,stats.repeats.fps,
                stats.fresh.averageMs,stats.fresh.worstMs,stats.fresh.low1Fps,stats.fresh.low1Ready,stats.fresh.ageMs,
                static_cast<unsigned long long>(stats.fresh.total),static_cast<unsigned long long>(stats.received.total),
                static_cast<unsigned long long>(stats.host.total),static_cast<unsigned long long>(stats.repeats.total),
                stats.width,stats.height,stats.targetHz,visible);
        }
        if(visible) {
            const auto guest=guestPerformance_.snapshot();
            auto bgra=axrb::host::performance_hud_bitmap(stats,guest.first,guest.second);
            mirror_.performance_hud(bgra,true);
            if(!bgra.empty()) {
                fpsHudPixels_=std::move(bgra);
                if(projectionFormat_==DXGI_FORMAT_R8G8B8A8_UNORM || projectionFormat_==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                    for(size_t i=0;i<fpsHudPixels_.size();i+=4)std::swap(fpsHudPixels_[i],fpsHudPixels_[i+2]);
                ++fpsHudGeneration_;
            }
        } else mirror_.performance_hud({},false);
    }
    if(!visible||fpsHudFailed_||fpsHudPixels_.empty())return false;
    if(fpsHudSwapchain_==XR_NULL_HANDLE){
        PFN_xrGetSystemProperties properties=nullptr; XrSystemProperties system{XR_TYPE_SYSTEM_PROPERTIES};
        if(!load_func("xrGetSystemProperties",&properties)||properties(instance_,systemId_,&system)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        fpsHudMaxLayers_=system.graphicsProperties.maxLayerCount;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=projectionFormat_;info.sampleCount=1;info.width=width;info.height=height;
        info.faceCount=info.arraySize=info.mipCount=1;
        if(createSwapchain_(session_,&info,&fpsHudSwapchain_)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        uint32_t count=0;
        if(enumerateSwapchainImages_(fpsHudSwapchain_,0,&count,nullptr)!=XR_SUCCESS||!count){fpsHudFailed_=true;return false;}
        fpsHudImages_.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});fpsHudUploaded_.assign(count,0);
        if(enumerateSwapchainImages_(fpsHudSwapchain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(fpsHudImages_.data()))!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        std::fprintf(stderr,"AXRB TruePerf HUD: unique successful game-projection submissions; 1s FPS, 5s gaps; NOT compositor scanout. F1 in mirror toggles.\n");
    }
    if(existingLayers>=fpsHudMaxLayers_)return false;
    uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(acquireSwapchainImage_(fpsHudSwapchain_,&acquire,&index)!=XR_SUCCESS)return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(waitSwapchainImage_(fpsHudSwapchain_,&wait)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
    if(index>=fpsHudImages_.size()){fpsHudFailed_=true;return false;}
    if(fpsHudUploaded_[index]!=fpsHudGeneration_){
        d3dContext_.get()->UpdateSubresource(fpsHudImages_[index].texture,0,nullptr,fpsHudPixels_.data(),width*4,width*height*4);
        fpsHudUploaded_[index]=fpsHudGeneration_;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(releaseSwapchainImage_(fpsHudSwapchain_,&release)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
    layer={XR_TYPE_COMPOSITION_LAYER_QUAD};layer.space=viewSpace_;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    layer.pose.orientation.w=1;layer.pose.position={0.0f,0.32f,-1.6f};layer.size={0.80f,0.27f};
    layer.subImage.swapchain=fpsHudSwapchain_;layer.subImage.imageRect.extent={width,height};
    return true;
}
}
#endif
