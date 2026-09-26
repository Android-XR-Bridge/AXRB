/* Windows half of Quest Bridge.

   Presents a stereo frame to the active PC OpenXR runtime and reads the
   head and grip poses that the Quest-side runtime will be sent as QbPose.
   The color in the headset is a stand-in until a QbFrame arrives. */

#include "bridge.h"
#include "link.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_frame_limit = 0;

static void die(const char* what, XrResult r) {
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    std::snprintf(name, sizeof(name), "%d", (int)r);
    std::fprintf(stderr, "qb-host: %s failed (%s)\n", what, name);
    std::exit(1);
}

static void check(XrResult r, const char* what) {
    if (XR_FAILED(r)) die(what, r);
}

template <size_t N>
static void set_name(char (&dst)[N], const char* src) {
    strncpy_s(dst, src, _TRUNCATE);
}

static XrPath make_path(XrInstance instance, const char* text) {
    XrPath path = XR_NULL_PATH;
    check(xrStringToPath(instance, text, &path), text);
    return path;
}

static bool has_extension(const char* name) {
    uint32_t count = 0;
    check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
          "xrEnumerateInstanceExtensionProperties");
    std::vector<XrExtensionProperties> ext(count);
    for (auto& e : ext) e.type = XR_TYPE_EXTENSION_PROPERTIES;
    check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, ext.data()),
          "xrEnumerateInstanceExtensionProperties");
    for (uint32_t i = 0; i < count; ++i) {
        if (std::strcmp(ext[i].extensionName, name) == 0) return true;
    }
    return false;
}

struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<ID3D11Texture2D*> textures;
    std::vector<ID3D11RenderTargetView*> rtvs;
};

struct App {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    XrActionSet actions = XR_NULL_HANDLE;
    XrAction grip = XR_NULL_HANDLE;
    XrSpace grip_space[2] = {};
    XrPath hand_path[2] = {};
    Eye eye[2];
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    Link link;
    bool session_running = false;
    bool exit_requested = false;
    int presented = 0;
    int copies = 0;
    int misses = 0;
    double wait_total = 0;
    double wait_worst = 0;
    double stale_total = 0;
    double stale_worst = 0;
    int used_drawn = 0;

    void destroy() {
        link.stop();
        for (Eye& e : eye) {
            for (auto* rtv : e.rtvs)
                if (rtv) rtv->Release();
            if (e.swapchain) xrDestroySwapchain(e.swapchain);
        }
        for (XrSpace space : grip_space)
            if (space) xrDestroySpace(space);
        if (local) xrDestroySpace(local);
        if (actions) xrDestroyActionSet(actions);
        if (session) xrDestroySession(session);
        if (context) context->Release();
        if (device) device->Release();
        if (instance) xrDestroyInstance(instance);
    }
};

static void create_instance(App& app) {
    if (!has_extension(XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
        std::fprintf(stderr, "qb-host: the active OpenXR runtime has no D3D11 enable extension\n");
        std::exit(1);
    }
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    set_name(info.applicationInfo.applicationName, "Quest Bridge");
    info.applicationInfo.applicationVersion = 1;
    set_name(info.applicationInfo.engineName, "quest-bridge");
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = enabled;
    check(xrCreateInstance(&info, &app.instance), "xrCreateInstance");

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    check(xrGetInstanceProperties(app.instance, &props), "xrGetInstanceProperties");
    std::printf("qb-host: runtime %s\n", props.runtimeName);
    std::fflush(stdout);
}

static void create_system(App& app) {
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult result = xrGetSystem(app.instance, &info, &app.system);
    if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE || result == XR_ERROR_RUNTIME_UNAVAILABLE) {
        std::fprintf(stderr,
                     "qb-host: no headset session. Wake the headset with Meta Link or SteamVR, then run again.\n");
        std::fflush(stdout);
        std::exit(2);
    }
    check(result, "xrGetSystem");
    XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
    check(xrGetSystemProperties(app.instance, app.system, &props), "xrGetSystemProperties");
    std::printf("qb-host: system %s\n", props.systemName);
}

static void create_actions(App& app) {
    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    set_name(set_info.actionSetName, "gameplay");
    set_name(set_info.localizedActionSetName, "Gameplay");
    check(xrCreateActionSet(app.instance, &set_info, &app.actions), "xrCreateActionSet");

    app.hand_path[0] = make_path(app.instance, "/user/hand/left");
    app.hand_path[1] = make_path(app.instance, "/user/hand/right");

    XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
    action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
    set_name(action_info.actionName, "grip_pose");
    set_name(action_info.localizedActionName, "Grip Pose");
    action_info.countSubactionPaths = 2;
    action_info.subactionPaths = app.hand_path;
    check(xrCreateAction(app.actions, &action_info, &app.grip), "xrCreateAction");

    XrPath left_grip = make_path(app.instance, "/user/hand/left/input/grip/pose");
    XrPath right_grip = make_path(app.instance, "/user/hand/right/input/grip/pose");
    XrActionSuggestedBinding bindings[] = {
        {app.grip, left_grip},
        {app.grip, right_grip},
    };
    const char* profiles[] = {
        "/interaction_profiles/oculus/touch_controller",
        "/interaction_profiles/khr/simple_controller",
        "/interaction_profiles/valve/index_controller",
        "/interaction_profiles/htc/vive_controller",
        "/interaction_profiles/microsoft/motion_controller",
    };
    for (const char* profile : profiles) {
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = make_path(app.instance, profile);
        suggested.countSuggestedBindings = 2;
        suggested.suggestedBindings = bindings;
        XrResult result = xrSuggestInteractionProfileBindings(app.instance, &suggested);
        if (XR_FAILED(result))
            std::printf("qb-host: skipped profile %s (%d)\n", profile, (int)result);
    }
}

static void create_device(App& app) {
    PFN_xrGetD3D11GraphicsRequirementsKHR get_reqs = nullptr;
    check(xrGetInstanceProcAddr(app.instance, "xrGetD3D11GraphicsRequirementsKHR",
                                reinterpret_cast<PFN_xrVoidFunction*>(&get_reqs)),
          "xrGetInstanceProcAddr(D3D11)");
    XrGraphicsRequirementsD3D11KHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    check(get_reqs(app.instance, app.system, &reqs), "xrGetD3D11GraphicsRequirementsKHR");

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        std::fprintf(stderr, "qb-host: CreateDXGIFactory1 failed\n");
        std::exit(1);
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* candidate = nullptr;
        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (std::memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) == 0) {
            adapter = candidate;
            break;
        }
        candidate->Release();
    }
    factory->Release();
    if (!adapter) {
        std::fprintf(stderr, "qb-host: OpenXR asked for an adapter this process cannot see\n");
        std::exit(1);
    }

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1,
                                   D3D11_SDK_VERSION, &app.device, nullptr, &app.context);
    adapter->Release();
    if (FAILED(hr)) {
        std::fprintf(stderr, "qb-host: D3D11CreateDevice failed (0x%08lx)\n", (unsigned long)hr);
        std::exit(1);
    }
}

static int64_t pick_format(XrSession session) {
    uint32_t count = 0;
    check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(count);
    check(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "xrEnumerateSwapchainFormats");
    for (int64_t format : formats) {
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) return format;
    }
    if (formats.empty()) {
        std::fprintf(stderr, "qb-host: runtime exposed no swapchain format\n");
        std::exit(1);
    }
    return formats[0];
}

static void create_swapchains(App& app) {
    XrViewConfigurationView views[2]{};
    for (auto& view : views) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    uint32_t count = 0;
    check(xrEnumerateViewConfigurationViews(app.instance, app.system,
                                            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views),
          "xrEnumerateViewConfigurationViews");
    if (count != 2) {
        std::fprintf(stderr, "qb-host: expected 2 stereo views, runtime reported %u\n", count);
        std::exit(1);
    }

    const int64_t format = pick_format(app.session);
    for (int i = 0; i < 2; ++i) {
        Eye& eye = app.eye[i];
        eye.width = (int32_t)views[i].recommendedImageRectWidth;
        eye.height = (int32_t)views[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format = format;
        info.sampleCount = views[i].recommendedSwapchainSampleCount;
        info.width = (uint32_t)eye.width;
        info.height = (uint32_t)eye.height;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        check(xrCreateSwapchain(app.session, &info, &eye.swapchain), "xrCreateSwapchain");

        uint32_t image_count = 0;
        check(xrEnumerateSwapchainImages(eye.swapchain, 0, &image_count, nullptr), "xrEnumerateSwapchainImages");
        std::vector<XrSwapchainImageD3D11KHR> images(image_count);
        for (auto& image : images) image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        check(xrEnumerateSwapchainImages(eye.swapchain, image_count, &image_count,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
              "xrEnumerateSwapchainImages");
        eye.textures.resize(image_count);
        eye.rtvs.resize(image_count);
        for (uint32_t n = 0; n < image_count; ++n) {
            eye.textures[n] = images[n].texture;
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = (DXGI_FORMAT)format;
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            HRESULT hr = app.device->CreateRenderTargetView(images[n].texture, &desc, &eye.rtvs[n]);
            if (FAILED(hr)) {
                std::fprintf(stderr, "qb-host: CreateRenderTargetView failed (0x%08lx)\n", (unsigned long)hr);
                std::exit(1);
            }
        }
        std::printf("qb-host: eye %d %dx%d samples %u\n", i, eye.width, eye.height,
                    views[i].recommendedSwapchainSampleCount);
    }
    if (app.eye[0].width == app.eye[1].width && app.eye[0].height == app.eye[1].height &&
        views[0].recommendedSwapchainSampleCount == 1 && views[1].recommendedSwapchainSampleCount == 1) {
        app.link.start(app.device, (uint32_t)app.eye[0].width, (uint32_t)app.eye[0].height, (DXGI_FORMAT)format);
    }
}

static void create_session(App& app) {
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = app.device;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = app.system;
    check(xrCreateSession(app.instance, &info, &app.session), "xrCreateSession");

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &app.actions;
    check(xrAttachSessionActionSets(app.session, &attach), "xrAttachSessionActionSets");

    XrPosef identity{};
    identity.orientation.w = 1.f;
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        space.action = app.grip;
        space.subactionPath = app.hand_path[i];
        space.poseInActionSpace = identity;
        check(xrCreateActionSpace(app.session, &space, &app.grip_space[i]), "xrCreateActionSpace");
    }

    XrReferenceSpaceCreateInfo local{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    local.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    local.poseInReferenceSpace = identity;
    check(xrCreateReferenceSpace(app.session, &local, &app.local), "xrCreateReferenceSpace");

    uint32_t blend_count = 0;
    check(xrEnumerateEnvironmentBlendModes(app.instance, app.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                            &blend_count, nullptr),
          "xrEnumerateEnvironmentBlendModes");
    std::vector<XrEnvironmentBlendMode> blends(blend_count);
    check(xrEnumerateEnvironmentBlendModes(app.instance, app.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            blend_count, &blend_count, blends.data()),
          "xrEnumerateEnvironmentBlendModes");
    app.blend = blends.empty() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : blends[0];
    for (XrEnvironmentBlendMode mode : blends) {
        if (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) app.blend = mode;
    }

    create_swapchains(app);
}

static void fill_eye(App& app, int eye_index, float seconds, bool shared) {
    Eye& eye = app.eye[eye_index];
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    check(xrAcquireSwapchainImage(eye.swapchain, &acquire, &index), "xrAcquireSwapchainImage");
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    check(xrWaitSwapchainImage(eye.swapchain, &wait), "xrWaitSwapchainImage");

    if (shared) {
        app.link.copy_eye(app.context, eye_index, eye.textures[index]);
        /* What the client actually handed over, so a frozen picture can be told
           from a picture that is being copied and is simply not changing. */
        static int looked = 0;
        ++looked;
        static bool dumped = false;
        if (looked > 40 && eye_index == 0 && !dumped) {
            dumped = true;
            /* One whole eye, written out so the picture itself can be looked at. */
            D3D11_TEXTURE2D_DESC src{};
            app.link.eye[0]->GetDesc(&src);
            D3D11_TEXTURE2D_DESC sd = src;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.MiscFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* whole = nullptr;
            if (SUCCEEDED(app.device->CreateTexture2D(&sd, nullptr, &whole))) {
                app.context->CopyResource(whole, app.link.eye[0]);
                D3D11_MAPPED_SUBRESOURCE read{};
                if (SUCCEEDED(app.context->Map(whole, 0, D3D11_MAP_READ, 0, &read))) {
                    FILE* out = nullptr;
                    fopen_s(&out, "eye0.raw", "wb");
                    if (out) {
                        uint32_t header[3] = {src.Width, src.Height, read.RowPitch};
                        std::fwrite(header, sizeof(header), 1, out);
                        std::fwrite(read.pData, (size_t)read.RowPitch * src.Height, 1, out);
                        std::fclose(out);
                        std::printf("qb-host: wrote eye0.raw %ux%u\n", src.Width, src.Height);
                        std::fflush(stdout);
                    }
                    app.context->Unmap(whole, 0);
                }
                whole->Release();
            }
        }
        if (looked < 6 || looked % 120 == 0) {
            D3D11_TEXTURE2D_DESC sd{};
            D3D11_TEXTURE2D_DESC src{};
            app.link.eye[eye_index]->GetDesc(&src);
            sd.Width = 2;
            sd.Height = 2;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.Format = src.Format;
            sd.SampleDesc.Count = 1;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* staging = nullptr;
            if (SUCCEEDED(app.device->CreateTexture2D(&sd, nullptr, &staging))) {
                D3D11_BOX box{};
                box.left = src.Width / 2;
                box.right = box.left + 2;
                box.top = src.Height / 2;
                box.bottom = box.top + 2;
                box.back = 1;
                app.context->CopySubresourceRegion(staging, 0, 0, 0, 0, app.link.eye[eye_index], 0, &box);
                D3D11_MAPPED_SUBRESOURCE read{};
                if (SUCCEEDED(app.context->Map(staging, 0, D3D11_MAP_READ, 0, &read))) {
                    const uint8_t* p = static_cast<const uint8_t*>(read.pData);
                    std::printf("qb-host: shared eye %d centre %d %d %d %d\n", eye_index, p[0], p[1], p[2], p[3]);
                    std::fflush(stdout);
                    app.context->Unmap(staging, 0);
                }
                staging->Release();
            }
        }
    } else if (app.link.copy_held(app.context, eye_index, eye.textures[index])) {
        /* Keep the last grid instead of flashing the standby color. */
    } else {
        float t = seconds;
        float color[4] = {
            0.10f + 0.15f * std::sinf(t),
            0.35f + 0.20f * std::sinf(t + 2.f) + (eye_index ? 0.f : 0.15f),
            0.55f + 0.20f * std::sinf(t + 4.f) + (eye_index ? 0.15f : 0.f),
            1.f,
        };
        app.context->ClearRenderTargetView(eye.rtvs[index], color);
    }

    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(eye.swapchain, &release), "xrReleaseSwapchainImage");
}

static QbXform xform_from(const XrPosef& pose) {
    QbXform x{};
    x.px = pose.position.x;
    x.py = pose.position.y;
    x.pz = pose.position.z;
    x.qx = pose.orientation.x;
    x.qy = pose.orientation.y;
    x.qz = pose.orientation.z;
    x.qw = pose.orientation.w;
    return x;
}

static void read_pose(App& app, XrTime time, QbPose* out) {
    *out = {};
    out->display_time_ns = (int64_t)time;

    XrActiveActionSet active{app.actions, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    check(xrSyncActions(app.session, &sync), "xrSyncActions");

    for (int i = 0; i < 2; ++i) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = app.grip;
        get.subactionPath = app.hand_path[i];
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        check(xrGetActionStatePose(app.session, &get, &state), "xrGetActionStatePose");
        if (!state.isActive) continue;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        check(xrLocateSpace(app.grip_space[i], app.local, time, &loc), "xrLocateSpace");
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0) continue;
        out->hand[i] = xform_from(loc.pose);
        out->hand_active |= 1u << i;
    }
}

static void render_frame(App& app) {
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    check(xrWaitFrame(app.session, &wait, &frame), "xrWaitFrame");
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    check(xrBeginFrame(app.session, &begin), "xrBeginFrame");

    XrCompositionLayerProjectionView projection_views[2]{};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    uint32_t layer_count = 0;

    if (frame.shouldRender) {
        QbPose pose{};
        read_pose(app, frame.predictedDisplayTime, &pose);

        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame.predictedDisplayTime;
        locate.space = app.local;
        XrViewState view_state{XR_TYPE_VIEW_STATE};
        XrView views[2]{};
        views[0].type = XR_TYPE_VIEW;
        views[1].type = XR_TYPE_VIEW;
        uint32_t view_count = 0;
        check(xrLocateViews(app.session, &locate, &view_state, 2, &view_count, views), "xrLocateViews");

        if (view_count == 2 && (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            pose.head = xform_from(views[0].pose);
            QbViews view_info{};
            for (int i = 0; i < 2; ++i) {
                pose.view[i] = xform_from(views[i].pose);
                view_info.fov_left[i] = views[i].fov.angleLeft;
                view_info.fov_right[i] = views[i].fov.angleRight;
                view_info.fov_up[i] = views[i].fov.angleUp;
                view_info.fov_down[i] = views[i].fov.angleDown;
            }
            app.link.publish(pose, view_info);
            float seconds = (float)(frame.predictedDisplayTime * 1e-9);
            LARGE_INTEGER t0{}, t1{}, freq{};
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t0);
            bool shared = app.link.begin_copy(app.context);
            QueryPerformanceCounter(&t1);
            double wait_ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
            app.wait_total += wait_ms;
            if (wait_ms > app.wait_worst) app.wait_worst = wait_ms;
            if (shared) ++app.copies;
            else ++app.misses;
            /* When the picture came from the client, it was drawn for the pose
               the client had, not the one we just read. Submitting that older
               pose is what lets the compositor warp it onto the head where it
               is now, instead of leaving the world swimming behind. */
            QbFrame drawn = app.link.last_frame();
            {
                /* How far the head moved since the client drew this picture.
                   If this is zero, the compositor has nothing to warp. */
                const XrQuaternionf& now = views[0].pose.orientation;
                float dot = now.x * drawn.view[0].qx + now.y * drawn.view[0].qy + now.z * drawn.view[0].qz +
                            now.w * drawn.view[0].qw;
                if (dot < 0) dot = -dot;
                if (dot > 1.f) dot = 1.f;
                float degrees = 2.f * std::acos(dot) * 57.2957795f;
                if (degrees > app.stale_worst) app.stale_worst = degrees;
                app.stale_total += degrees;
            }
            if (app.presented % 207 == 0)
                std::printf("qb-host: held %d, frame message says %ux%u, eye 0 quat %.3f %.3f %.3f %.3f, now %.3f %.3f %.3f"
                            " %.3f\n",
                            (int)app.link.have_held, drawn.width, drawn.height, drawn.view[0].qx, drawn.view[0].qy, drawn.view[0].qz,
                            drawn.view[0].qw, views[0].pose.orientation.x, views[0].pose.orientation.y,
                            views[0].pose.orientation.z, views[0].pose.orientation.w);
            bool drawn_pose = drawn.width != 0 && (drawn.view[0].qw != 0.f || drawn.view[0].qx != 0.f ||
                                                   drawn.view[0].qy != 0.f || drawn.view[0].qz != 0.f);
            for (int i = 0; i < 2; ++i) {
                fill_eye(app, i, seconds, shared);
                projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projection_views[i].pose = views[i].pose;
                projection_views[i].fov = views[i].fov;
                if (app.link.have_held && drawn_pose) {
                    if (i == 0) ++app.used_drawn;
                    projection_views[i].pose.position = {drawn.view[i].px, drawn.view[i].py, drawn.view[i].pz};
                    projection_views[i].pose.orientation = {drawn.view[i].qx, drawn.view[i].qy, drawn.view[i].qz,
                                                           drawn.view[i].qw};
                    projection_views[i].fov = {drawn.fov[i][0], drawn.fov[i][1], drawn.fov[i][2], drawn.fov[i][3]};
                }
                projection_views[i].subImage.swapchain = app.eye[i].swapchain;
                projection_views[i].subImage.imageRect.extent = {app.eye[i].width, app.eye[i].height};
            }
            if (shared) app.link.end_copy(app.context);
            else app.context->Flush();
            layer.space = app.local;
            layer.viewCount = 2;
            layer.views = projection_views;
            layer_count = 1;
            app.presented += 1;
            if (app.presented == 1 || app.presented % 207 == 0) {
                std::printf(
                    "qb-host: frame %d %s head %.2f %.2f %.2f hands %u, copies %d misses %d, lock wait avg %.2fms"
                    " worst %.2fms, drawn pose used %d, head moved avg %.1fdeg worst %.1fdeg\n",
                    app.presented, shared ? "shared" : "color", pose.head.px, pose.head.py, pose.head.pz,
                    pose.hand_active, app.copies, app.misses, app.wait_total / app.presented, app.wait_worst,
                    app.used_drawn, app.stale_total / app.presented, app.stale_worst);
                app.wait_worst = 0;
                app.stale_worst = 0;
                std::fflush(stdout);
            }
        }
    }

    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = frame.predictedDisplayTime;
    end.environmentBlendMode = app.blend;
    end.layerCount = layer_count;
    end.layers = layer_count ? layers : nullptr;
    check(xrEndFrame(app.session, &end), "xrEndFrame");
}

static void poll_events(App& app, bool* quit) {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(app.instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            std::printf("qb-host: session state %d\n", (int)changed->state);
            if (changed->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                check(xrBeginSession(app.session, &begin), "xrBeginSession");
                app.session_running = true;
            } else if (changed->state == XR_SESSION_STATE_STOPPING) {
                check(xrEndSession(app.session), "xrEndSession");
                app.session_running = false;
            } else if (changed->state == XR_SESSION_STATE_EXITING ||
                       changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                *quit = true;
            }
        }
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

static void run(App& app) {
    std::printf("qb-host: waiting for the headset session\n");
    bool quit = false;
    while (!quit) {
        poll_events(app, &quit);
        if (quit) break;
        if (!app.session_running) {
            Sleep(10);
            continue;
        }
        render_frame(app);
        if (g_frame_limit > 0 && app.presented >= g_frame_limit && !app.exit_requested) {
            check(xrRequestExitSession(app.session), "xrRequestExitSession");
            app.exit_requested = true;
        }
    }
    std::printf("qb-host: presented %d frames\n", app.presented);
}

static void prefer_steamvr() {
    if (GetEnvironmentVariableA("XR_RUNTIME_JSON", nullptr, 0) > 0) return;
    const char* json = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json";
    if (GetFileAttributesA(json) == INVALID_FILE_ATTRIBUTES) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32First(snap, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, "vrserver.exe") == 0) running = true;
        } while (!running && Process32Next(snap, &entry));
    }
    CloseHandle(snap);
    if (!running) return;
    SetEnvironmentVariableA("XR_RUNTIME_JSON", json);
    _putenv_s("XR_RUNTIME_JSON", json);
    std::printf("qb-host: using SteamVR\n");
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    /* The link thread paces itself with Sleep(1), which is 15.6ms at the
       default timer resolution and holds the whole bridge to 64Hz. */
    timeBeginPeriod(1);
    static_assert(sizeof(QbFrame) == 16 + 2 * sizeof(QbXform) + 32, "QbFrame carries a slot and a pose");
    static_assert(sizeof(QbPose) == 152, "QbPose layout");
    static_assert(sizeof(QbShare) == 32, "QbShare layout");
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            g_frame_limit = std::atoi(argv[++i]);
        }
    }
    prefer_steamvr();

    App app;
    create_instance(app);
    create_system(app);
    create_actions(app);
    create_device(app);
    create_session(app);
    run(app);
    app.destroy();
    return app.presented > 0 || g_frame_limit == 0 ? 0 : 1;
}
