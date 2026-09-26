/* OpenXR runtime a game binds to instead of Horizon.

   The Windows loader loads qb_runtime.dll. Swapchain images are the two
   textures qb-host shared over the pipe. The game renders into them between
   xrBeginFrame and xrEndFrame. qb-host copies them onto the PC headset. */

#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "bridge.h"
#include "wire.h"

#include <timeapi.h>

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <cstdio>
#include <cstring>
#include <vector>

struct Space {
    XrReferenceSpaceType type;
};

struct Swapchain {
    int eye = 0;
    bool acquired = false;
    ID3D11Texture2D* texture = nullptr;
};

struct Runtime {
    bool alive = false;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    QbShare share{};
    QbViews views{};
    QbPose pose{};
    bool have_pose = false;
    bool d3d11 = false;
    LUID luid{};
    bool have_luid = false;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIKeyedMutex* mutex[2] = {};
    int64_t last_display_ns = 0;
    ID3D11Texture2D* eye_tex[2] = {};
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool begun = false;
    bool in_frame = false;
    bool eyes_locked = false;
    bool exit_requested = false;
    std::vector<XrEventDataSessionStateChanged> events;
    Space spaces[8]{};
    int space_count = 0;
    Swapchain swaps[2]{};
    int swap_count = 0;
};

static Runtime g;

static void push_state(XrSessionState state) {
    g.state = state;
    XrEventDataSessionStateChanged ev{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    ev.session = reinterpret_cast<XrSession>(&g);
    ev.state = state;
    ev.time = g.pose.display_time_ns;
    g.events.push_back(ev);
    std::printf("qb-runtime: session %d\n", (int)state);
    std::fflush(stdout);
}

static bool same_instance(XrInstance instance) { return instance == reinterpret_cast<XrInstance>(&g); }

static bool connect_host() {
    std::printf("qb-runtime: connecting\n");
    std::fflush(stdout);
    for (int i = 0; i < 200; ++i) {
        HANDLE pipe = CreateFileA(QB_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
            g.pipe = pipe;
            return true;
        }
        Sleep(50);
    }
    return false;
}

static bool read_share() {
    for (;;) {
        QbMsgBuf msg{};
        if (!qb_read(g.pipe, &msg)) return false;
        if (msg.type == QB_MSG_SHARE && qb_body(msg, &g.share)) return g.share.width != 0;
    }
}

static bool discover_luid() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    bool found = false;
    for (UINT i = 0; !found; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        ID3D11Device* device = nullptr;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                        &device, nullptr, nullptr);
        adapter->Release();
        if (FAILED(hr)) continue;
        ID3D11Device1* device1 = nullptr;
        device->QueryInterface(IID_PPV_ARGS(&device1));
        ID3D11Texture2D* texture = nullptr;
        hr = device1->OpenSharedResource1((HANDLE)(uintptr_t)g.share.handle[0], IID_PPV_ARGS(&texture));
        device1->Release();
        if (SUCCEEDED(hr)) {
            IDXGIDevice* dxgi = nullptr;
            device->QueryInterface(IID_PPV_ARGS(&dxgi));
            IDXGIAdapter* owner = nullptr;
            dxgi->GetAdapter(&owner);
            DXGI_ADAPTER_DESC desc{};
            owner->GetDesc(&desc);
            g.luid = desc.AdapterLuid;
            g.have_luid = true;
            owner->Release();
            dxgi->Release();
            texture->Release();
            found = true;
        }
        device->Release();
    }
    factory->Release();
    return found;
}

static XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char*, uint32_t capacity, uint32_t* count,
                                                                  XrExtensionProperties* properties) {
    if (count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || properties == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    std::snprintf(properties[0].extensionName, XR_MAX_EXTENSION_NAME_SIZE, "%s", XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    properties[0].extensionVersion = XR_KHR_D3D11_enable_SPEC_VERSION;
    return XR_SUCCESS;
}

/* Sleep(1) is a 15.6ms sleep at the default timer resolution, which paces this
   whole bridge at 64Hz however fast everything else is. */
static void sharpen_timer() {
    static bool done = false;
    if (done) return;
    done = true;
    timeBeginPeriod(1);
}

static XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance) {
    if (info == nullptr || instance == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    sharpen_timer();
    if (g.alive) return XR_ERROR_LIMIT_REACHED;
    g = {};
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        if (std::strcmp(info->enabledExtensionNames[i], XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0) g.d3d11 = true;
    }
    if (!connect_host() || !read_share()) {
        std::fprintf(stderr, "qb-runtime: qb-host is not listening\n");
        if (g.pipe != INVALID_HANDLE_VALUE) CloseHandle(g.pipe);
        g = {};
        return XR_ERROR_RUNTIME_UNAVAILABLE;
    }
    g.alive = true;
    *instance = reinterpret_cast<XrInstance>(&g);
    std::printf("qb-runtime: host textures %ux%u\n", g.share.width, g.share.height);
    std::fflush(stdout);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
    if (!same_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    for (auto* mutex : g.mutex)
        if (mutex) mutex->Release();
    for (auto* texture : g.eye_tex)
        if (texture) texture->Release();
    if (g.context) g.context->Release();
    if (g.device) g.device->Release();
    if (g.pipe != INVALID_HANDLE_VALUE) CloseHandle(g.pipe);
    g = {};
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance instance, XrInstanceProperties* properties) {
    if (!same_instance(instance) || properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    std::snprintf(properties->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, "Quest Bridge");
    properties->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetSystem(XrInstance instance, const XrSystemGetInfo* info, XrSystemId* system) {
    if (!same_instance(instance) || info == nullptr || system == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    *system = 1;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetSystemProperties(XrInstance instance, XrSystemId, XrSystemProperties* properties) {
    if (!same_instance(instance) || properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    properties->systemId = 1;
    properties->vendorId = 0x2833;
    std::snprintf(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Meta Quest 3");
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxLayerCount = 16;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance instance, XrSystemId, uint32_t capacity,
                                                         uint32_t* count, XrViewConfigurationType* types) {
    if (!same_instance(instance) || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || types == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetViewConfigurationProperties(XrInstance instance, XrSystemId, XrViewConfigurationType,
                                                            XrViewConfigurationProperties* properties) {
    if (!same_instance(instance) || properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    properties->viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    properties->fovMutable = XR_FALSE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId, XrViewConfigurationType,
                                                             uint32_t capacity, uint32_t* count,
                                                             XrViewConfigurationView* views) {
    if (!same_instance(instance) || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 2 || views == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    for (int i = 0; i < 2; ++i) {
        views[i].recommendedImageRectWidth = g.share.width;
        views[i].maxImageRectWidth = g.share.width;
        views[i].recommendedImageRectHeight = g.share.height;
        views[i].maxImageRectHeight = g.share.height;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId, XrViewConfigurationType,
                                                            uint32_t capacity, uint32_t* count,
                                                            XrEnvironmentBlendMode* modes) {
    if (!same_instance(instance) || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || modes == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetD3D11GraphicsRequirementsKHR(XrInstance instance, XrSystemId,
                                                             XrGraphicsRequirementsD3D11KHR* requirements) {
    if (!same_instance(instance) || requirements == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (!g.d3d11) return XR_ERROR_FUNCTION_UNSUPPORTED;
    if (!g.have_luid && !discover_luid()) return XR_ERROR_RUNTIME_FAILURE;
    requirements->adapterLuid = g.luid;
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    if (!same_instance(instance) || info == nullptr || session == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    auto* binding = static_cast<const XrGraphicsBindingD3D11KHR*>(info->next);
    if (binding == nullptr || binding->type != XR_TYPE_GRAPHICS_BINDING_D3D11_KHR || binding->device == nullptr)
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    ID3D11Device1* device1 = nullptr;
    binding->device->QueryInterface(IID_PPV_ARGS(&device1));
    HRESULT hr = device1->OpenSharedResource1((HANDLE)(uintptr_t)g.share.handle[0], IID_PPV_ARGS(&g.eye_tex[0]));
    if (SUCCEEDED(hr))
        hr = device1->OpenSharedResource1((HANDLE)(uintptr_t)g.share.handle[1], IID_PPV_ARGS(&g.eye_tex[1]));
    device1->Release();
    if (FAILED(hr)) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    g.eye_tex[0]->QueryInterface(IID_PPV_ARGS(&g.mutex[0]));
    g.eye_tex[1]->QueryInterface(IID_PPV_ARGS(&g.mutex[1]));
    g.device = binding->device;
    g.device->AddRef();
    g.device->GetImmediateContext(&g.context);
    *session = reinterpret_cast<XrSession>(&g);
    push_state(XR_SESSION_STATE_IDLE);
    push_state(XR_SESSION_STATE_READY);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrDestroySession(XrSession session) {
    if (session != reinterpret_cast<XrSession>(&g)) return XR_ERROR_HANDLE_INVALID;
    g.begun = false;
    return XR_SUCCESS;
}

static bool session_ok(XrSession session) { return session == reinterpret_cast<XrSession>(&g); }

static XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession session, uint32_t capacity, uint32_t* count,
                                                      XrReferenceSpaceType* types) {
    if (!session_ok(session) || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 2 || types == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    types[0] = XR_REFERENCE_SPACE_TYPE_LOCAL;
    types[1] = XR_REFERENCE_SPACE_TYPE_VIEW;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* info,
                                                  XrSpace* space) {
    if (!session_ok(session) || info == nullptr || space == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (g.space_count >= 8) return XR_ERROR_LIMIT_REACHED;
    g.spaces[g.space_count].type = info->referenceSpaceType;
    *space = reinterpret_cast<XrSpace>(&g.spaces[g.space_count]);
    g.space_count += 1;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrDestroySpace(XrSpace) { return XR_SUCCESS; }

static XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession session, uint32_t capacity, uint32_t* count,
                                                       int64_t* formats) {
    if (!session_ok(session) || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || formats == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    formats[0] = (int64_t)g.share.format;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain) {
    if (!session_ok(session) || info == nullptr || swapchain == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (g.swap_count >= 2) return XR_ERROR_LIMIT_REACHED;
    if (info->format != (int64_t)g.share.format || info->sampleCount != 1) return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    if (info->width != g.share.width || info->height != g.share.height) return XR_ERROR_VALIDATION_FAILURE;
    Swapchain* swap = &g.swaps[g.swap_count];
    swap->eye = g.swap_count;
    swap->texture = g.eye_tex[g.swap_count];
    *swapchain = reinterpret_cast<XrSwapchain>(swap);
    g.swap_count += 1;
    return XR_SUCCESS;
}

static Swapchain* swap_of(XrSwapchain swapchain) {
    for (int i = 0; i < g.swap_count; ++i)
        if (reinterpret_cast<XrSwapchain>(&g.swaps[i]) == swapchain) return &g.swaps[i];
    return nullptr;
}

static XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain) { return XR_SUCCESS; }

static XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count,
                                                      XrSwapchainImageBaseHeader* images) {
    Swapchain* swap = swap_of(swapchain);
    if (swap == nullptr || count == nullptr) return XR_ERROR_HANDLE_INVALID;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || images == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    auto* image = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
    image->texture = swap->texture;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*,
                                                   uint32_t* index) {
    Swapchain* swap = swap_of(swapchain);
    if (swap == nullptr || index == nullptr) return XR_ERROR_HANDLE_INVALID;
    if (swap->acquired) return XR_ERROR_CALL_ORDER_INVALID;
    swap->acquired = true;
    *index = 0;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) {
    if (swap_of(swapchain) == nullptr) return XR_ERROR_HANDLE_INVALID;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
    Swapchain* swap = swap_of(swapchain);
    if (swap == nullptr) return XR_ERROR_HANDLE_INVALID;
    swap->acquired = false;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrPollEvent(XrInstance instance, XrEventDataBuffer* event) {
    if (!same_instance(instance) || event == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (g.events.empty()) return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged ev = g.events.front();
    g.events.erase(g.events.begin());
    std::memcpy(event, &ev, sizeof(ev));
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo*) {
    if (!session_ok(session)) return XR_ERROR_HANDLE_INVALID;
    if (g.state != XR_SESSION_STATE_READY) return XR_ERROR_SESSION_NOT_READY;
    g.begun = true;
    push_state(XR_SESSION_STATE_SYNCHRONIZED);
    push_state(XR_SESSION_STATE_VISIBLE);
    push_state(XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEndSession(XrSession session) {
    if (!session_ok(session)) return XR_ERROR_HANDLE_INVALID;
    g.begun = false;
    push_state(XR_SESSION_STATE_IDLE);
    if (g.exit_requested) push_state(XR_SESSION_STATE_EXITING);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrRequestExitSession(XrSession session) {
    if (!session_ok(session)) return XR_ERROR_HANDLE_INVALID;
    g.exit_requested = true;
    if (g.begun) push_state(XR_SESSION_STATE_STOPPING);
    return XR_SUCCESS;
}

static bool take_message() {
    QbMsgBuf msg{};
    if (!qb_read(g.pipe, &msg)) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_DATA || err == ERROR_PIPE_BUSY) return true;
        std::fprintf(stderr, "qb-runtime: pipe read failed (%lu)\n", err);
        return false;
    }
    if (msg.type == QB_MSG_VIEWS) qb_body(msg, &g.views);
    if (msg.type == QB_MSG_POSE && qb_body(msg, &g.pose)) g.have_pose = true;
    return true;
}

static XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* frame) {
    if (!session_ok(session) || frame == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (!g.begun) return XR_ERROR_SESSION_NOT_RUNNING;
    while (!g.have_pose) {
        if (!take_message()) return XR_ERROR_RUNTIME_FAILURE;
        if (!g.have_pose) Sleep(1);
    }
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(g.pipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
        QbMsgBuf msg{};
        if (!qb_read(g.pipe, &msg)) break;
        if (msg.type == QB_MSG_VIEWS) qb_body(msg, &g.views);
        if (msg.type == QB_MSG_POSE && qb_body(msg, &g.pose)) g.have_pose = true;
    }
    g.have_pose = false;
    frame->predictedDisplayTime = g.pose.display_time_ns;
    frame->predictedDisplayPeriod = 11111111;
    frame->shouldRender = XR_TRUE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
    if (!session_ok(session)) return XR_ERROR_HANDLE_INVALID;
    if (!g.begun) return XR_ERROR_SESSION_NOT_RUNNING;
    if (g.in_frame) return XR_ERROR_CALL_ORDER_INVALID;
    g.eyes_locked = false;
    if (g.mutex[0] != nullptr && g.mutex[1] != nullptr && g.mutex[0]->AcquireSync(0, 30) == S_OK) {
        if (g.mutex[1]->AcquireSync(0, 30) == S_OK) g.eyes_locked = true;
        else g.mutex[0]->ReleaseSync(0);
    }
    g.in_frame = true;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
    if (!session_ok(session)) return XR_ERROR_HANDLE_INVALID;
    if (!g.in_frame) return XR_ERROR_CALL_ORDER_INVALID;
    if (g.context != nullptr) g.context->Flush();
    if (g.eyes_locked) {
        g.mutex[1]->ReleaseSync(1);
        g.mutex[0]->ReleaseSync(1);
        g.eyes_locked = false;
    }
    QbFrame done{};
    done.width = g.share.width;
    done.height = g.share.height;
    done.format = g.share.format;
    /* Tell the host which pose this picture was drawn for. */
    if (info != nullptr && info->layerCount > 0 && info->layers != nullptr && info->layers[0] != nullptr &&
        info->layers[0]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
        auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        for (uint32_t i = 0; i < layer->viewCount && i < 2; ++i) {
            const XrCompositionLayerProjectionView& view = layer->views[i];
            done.view[i].px = view.pose.position.x;
            done.view[i].py = view.pose.position.y;
            done.view[i].pz = view.pose.position.z;
            done.view[i].qx = view.pose.orientation.x;
            done.view[i].qy = view.pose.orientation.y;
            done.view[i].qz = view.pose.orientation.z;
            done.view[i].qw = view.pose.orientation.w;
            done.fov[i][0] = view.fov.angleLeft;
            done.fov[i][1] = view.fov.angleRight;
            done.fov[i][2] = view.fov.angleUp;
            done.fov[i][3] = view.fov.angleDown;
        }
    }
    if (!qb_write(g.pipe, QB_MSG_FRAME, &done, sizeof(done)))
        std::fprintf(stderr, "qb-runtime: frame send failed (%lu)\n", GetLastError());
    g.in_frame = false;
    return XR_SUCCESS;
}

static void fill_pose(const QbXform& xform, XrPosef* pose) {
    pose->position = {xform.px, xform.py, xform.pz};
    pose->orientation = {xform.qx, xform.qy, xform.qz, xform.qw};
    if (pose->orientation.w == 0.f && pose->orientation.x == 0.f && pose->orientation.y == 0.f &&
        pose->orientation.z == 0.f)
        pose->orientation.w = 1.f;
}

static XrResult XRAPI_CALL xrLocateViews(XrSession session, const XrViewLocateInfo*, XrViewState* state, uint32_t capacity,
                                         uint32_t* count, XrView* views) {
    if (!session_ok(session) || state == nullptr || count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2;
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
                            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 2 || views == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    for (int i = 0; i < 2; ++i) {
        fill_pose(g.pose.view[i], &views[i].pose);
        views[i].fov.angleLeft = g.views.fov_left[i];
        views[i].fov.angleRight = g.views.fov_right[i];
        views[i].fov.angleUp = g.views.fov_up[i];
        views[i].fov.angleDown = g.views.fov_down[i];
    }
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime, XrSpaceLocation* location) {
    if (location == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    auto* stored = reinterpret_cast<Space*>(space);
    if (stored != nullptr && stored->type == XR_REFERENCE_SPACE_TYPE_VIEW)
        fill_pose(g.pose.head, &location->pose);
    else
        location->pose.orientation.w = 1.f;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    if (buffer == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XrResult %d", (int)value);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function);

#define QB_FUNCS(X)                                                                                                    \
    X(xrEnumerateInstanceExtensionProperties)                                                                          \
    X(xrCreateInstance)                                                                                                \
    X(xrDestroyInstance)                                                                                               \
    X(xrGetInstanceProperties)                                                                                         \
    X(xrGetSystem)                                                                                                     \
    X(xrGetSystemProperties)                                                                                           \
    X(xrEnumerateViewConfigurations)                                                                                   \
    X(xrGetViewConfigurationProperties)                                                                                \
    X(xrEnumerateViewConfigurationViews)                                                                               \
    X(xrEnumerateEnvironmentBlendModes)                                                                                \
    X(xrGetD3D11GraphicsRequirementsKHR)                                                                               \
    X(xrCreateSession)                                                                                                 \
    X(xrDestroySession)                                                                                                \
    X(xrEnumerateReferenceSpaces)                                                                                      \
    X(xrCreateReferenceSpace)                                                                                          \
    X(xrDestroySpace)                                                                                                  \
    X(xrEnumerateSwapchainFormats)                                                                                     \
    X(xrCreateSwapchain)                                                                                               \
    X(xrDestroySwapchain)                                                                                              \
    X(xrEnumerateSwapchainImages)                                                                                      \
    X(xrAcquireSwapchainImage)                                                                                         \
    X(xrWaitSwapchainImage)                                                                                            \
    X(xrReleaseSwapchainImage)                                                                                         \
    X(xrPollEvent)                                                                                                     \
    X(xrBeginSession)                                                                                                  \
    X(xrEndSession)                                                                                                    \
    X(xrRequestExitSession)                                                                                            \
    X(xrWaitFrame)                                                                                                     \
    X(xrBeginFrame)                                                                                                    \
    X(xrEndFrame)                                                                                                      \
    X(xrLocateViews)                                                                                                   \
    X(xrLocateSpace)                                                                                                   \
    X(xrResultToString)                                                                                                \
    X(xrGetInstanceProcAddr)

static XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function) {
    if (name == nullptr || function == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *function = nullptr;
#define MATCH(fn)                                                                                                      \
    if (std::strcmp(name, #fn) == 0) {                                                                                 \
        *function = reinterpret_cast<PFN_xrVoidFunction>(fn);                                                          \
        return XR_SUCCESS;                                                                                             \
    }
    QB_FUNCS(MATCH)
#undef MATCH
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest) {
    if (loaderInfo == nullptr || runtimeRequest == nullptr) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minApiVersion > XR_CURRENT_API_VERSION) return XR_ERROR_INITIALIZATION_FAILED;
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
    if (loaderInfo->maxApiVersion < runtimeRequest->runtimeApiVersion)
        runtimeRequest->runtimeApiVersion = loaderInfo->maxApiVersion;
    runtimeRequest->getInstanceProcAddr = xrGetInstanceProcAddr;
    return XR_SUCCESS;
}
