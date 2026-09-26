/* OpenXR app that binds to qb_runtime.dll, not to the headset runtime.
   qb-host must already be running. The grid is drawn into the textures
   qb-host shared with the runtime. */

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char* kShader = R"(
cbuffer CB : register(b0) {
    float4 quat;
    float4 pos;
    float4 fov;
};
struct VSOut { float4 clip : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs(uint id : SV_VertexID) {
    float2 corner = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.uv = corner;
    o.clip = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
float3 qrot(float4 q, float3 v) {
    return v + 2.0 * cross(q.xyz, q.w * v + cross(q.xyz, v));
}
float4 ps(VSOut i) : SV_Target {
    float x = lerp(tan(fov.x), tan(fov.y), i.uv.x);
    float y = lerp(tan(fov.z), tan(fov.w), i.uv.y);
    float3 dir = qrot(quat, normalize(float3(x, y, -1.0)));
    float3 cam = pos.xyz;
    if (dir.y < -0.02) {
        float t = (cam.y - 1.6 - cam.y) / dir.y;
        float3 hit = cam + dir * t;
        float gx = abs(frac(hit.x) - 0.5);
        float gz = abs(frac(hit.z) - 0.5);
        float grid = saturate(1.0 - min(gx, gz) * 30.0);
        float checker = fmod(floor(hit.x) + floor(hit.z), 2.0);
        float3 base = checker > 0.5 ? float3(0.20, 0.16, 0.36) : float3(0.08, 0.07, 0.12);
        float3 col = lerp(base, float3(0.95, 0.85, 0.55), grid);
        col *= saturate(6.0 / (1.0 + t));
        return float4(col, 1.0);
    }
    float3 sky = lerp(float3(0.45, 0.22, 0.18), float3(0.03, 0.02, 0.04), saturate(dir.y * 1.4));
    return float4(sky, 1.0);
}
)";

struct CB {
    float quat[4];
    float pos[4];
    float fov[4];
};

static int g_frames = 300;

static void die(const char* what, XrResult result = XR_ERROR_RUNTIME_FAILURE) {
    std::fprintf(stderr, "qb-app: %s (%d)\n", what, (int)result);
    std::exit(1);
}

static void check(XrResult result, const char* what) {
    if (XR_FAILED(result)) die(what, result);
}

static void use_our_runtime() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) die("exe path");
    std::strcpy(slash + 1, "qb_runtime.json");
    SetEnvironmentVariableA("XR_RUNTIME_JSON", path);
    _putenv_s("XR_RUNTIME_JSON", path);
}

static ID3D11Device* make_device(LUID luid, ID3D11DeviceContext** context) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) die("CreateDXGIFactory1");
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* candidate = nullptr;
        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (std::memcmp(&desc.AdapterLuid, &luid, sizeof(LUID)) == 0) {
            adapter = candidate;
            break;
        }
        candidate->Release();
    }
    factory->Release();
    if (adapter == nullptr) die("adapter from the runtime was not found");
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device,
                                   nullptr, context);
    adapter->Release();
    if (FAILED(hr)) die("D3D11CreateDevice");
    return device;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) g_frames = std::atoi(argv[++i]);
    std::printf("qb-app: start\n");
    std::fflush(stdout);
    use_our_runtime();

    XrInstance instance = XR_NULL_HANDLE;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::snprintf(info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Quest Bridge App");
    info.applicationInfo.applicationVersion = 1;
    std::snprintf(info.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "quest-bridge");
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    const char* extensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = extensions;
    check(xrCreateInstance(&info, &instance), "xrCreateInstance");

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    check(xrGetInstanceProperties(instance, &props), "xrGetInstanceProperties");
    std::printf("qb-app: runtime %s\n", props.runtimeName);
    std::fflush(stdout);

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    check(xrGetSystem(instance, &system_info, &system), "xrGetSystem");

    PFN_xrGetD3D11GraphicsRequirementsKHR get_reqs = nullptr;
    check(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR",
                                reinterpret_cast<PFN_xrVoidFunction*>(&get_reqs)),
          "xrGetD3D11GraphicsRequirementsKHR");
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    check(get_reqs(instance, system, &requirements), "xrGetD3D11GraphicsRequirementsKHR");

    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = make_device(requirements.adapterLuid, &context);

    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device;
    XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
    session_info.next = &binding;
    session_info.systemId = system;
    XrSession session = XR_NULL_HANDLE;
    check(xrCreateSession(instance, &session_info, &session), "xrCreateSession");

    XrViewConfigurationView configs[2]{};
    configs[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    configs[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    uint32_t view_count = 0;
    check(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count,
                                            configs),
          "xrEnumerateViewConfigurationViews");

    int64_t format = 0;
    uint32_t format_count = 0;
    check(xrEnumerateSwapchainFormats(session, 1, &format_count, &format), "xrEnumerateSwapchainFormats");

    XrSwapchain swapchain[2] = {};
    std::vector<ID3D11RenderTargetView*> rtvs[2];
    for (int eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo swap_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swap_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swap_info.format = format;
        swap_info.sampleCount = 1;
        swap_info.width = configs[eye].recommendedImageRectWidth;
        swap_info.height = configs[eye].recommendedImageRectHeight;
        swap_info.faceCount = 1;
        swap_info.arraySize = 1;
        swap_info.mipCount = 1;
        check(xrCreateSwapchain(session, &swap_info, &swapchain[eye]), "xrCreateSwapchain");
        XrSwapchainImageD3D11KHR image{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
        uint32_t image_count = 0;
        check(xrEnumerateSwapchainImages(swapchain[eye], 1, &image_count,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(&image)),
              "xrEnumerateSwapchainImages");
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = (DXGI_FORMAT)format;
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(device->CreateRenderTargetView(image.texture, &desc, &rtv))) die("CreateRenderTargetView");
        rtvs[eye].push_back(rtv);
    }

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), "grid.hlsl", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &code,
                          &errors))) {
        std::fprintf(stderr, "%s\n", errors ? (char*)errors->GetBufferPointer() : "vs");
        return 1;
    }
    ID3D11VertexShader* vs = nullptr;
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs);
    code->Release();
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), "grid.hlsl", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &code,
                          &errors))) {
        std::fprintf(stderr, "%s\n", errors ? (char*)errors->GetBufferPointer() : "ps");
        return 1;
    }
    ID3D11PixelShader* ps = nullptr;
    device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps);
    code->Release();

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(CB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* constants = nullptr;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &constants))) die("CreateBuffer");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    device->CreateRasterizerState(&raster, &rs);

    bool running = false;
    bool exit_sent = false;
    int presented = 0;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    while (state != XR_SESSION_STATE_EXITING) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult event_result = xrPollEvent(instance, &event);
        if (event_result == XR_SUCCESS && event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            state = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            if (state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                check(xrBeginSession(session, &begin), "xrBeginSession");
                running = true;
            } else if (state == XR_SESSION_STATE_STOPPING) {
                check(xrEndSession(session), "xrEndSession");
                running = false;
            }
            continue;
        }
        if (!running) {
            Sleep(10);
            continue;
        }

        XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        check(xrWaitFrame(session, &wait_info, &frame), "xrWaitFrame");
        XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
        check(xrBeginFrame(session, &begin_info), "xrBeginFrame");

        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame.predictedDisplayTime;
        locate.space = XR_NULL_HANDLE;
        XrViewState view_state{XR_TYPE_VIEW_STATE};
        XrView views[2]{};
        views[0].type = XR_TYPE_VIEW;
        views[1].type = XR_TYPE_VIEW;
        uint32_t located = 0;
        check(xrLocateViews(session, &locate, &view_state, 2, &located, views), "xrLocateViews");

        XrCompositionLayerProjectionView projection[2]{};
        for (int eye = 0; eye < 2; ++eye) {
            uint32_t index = 0;
            check(xrAcquireSwapchainImage(swapchain[eye], nullptr, &index), "xrAcquireSwapchainImage");
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            check(xrWaitSwapchainImage(swapchain[eye], &wait), "xrWaitSwapchainImage");

            D3D11_MAPPED_SUBRESOURCE mapped{};
            context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            auto* cb = static_cast<CB*>(mapped.pData);
            cb->quat[0] = views[eye].pose.orientation.x;
            cb->quat[1] = views[eye].pose.orientation.y;
            cb->quat[2] = views[eye].pose.orientation.z;
            cb->quat[3] = views[eye].pose.orientation.w;
            cb->pos[0] = views[eye].pose.position.x;
            cb->pos[1] = views[eye].pose.position.y;
            cb->pos[2] = views[eye].pose.position.z;
            cb->fov[0] = views[eye].fov.angleLeft;
            cb->fov[1] = views[eye].fov.angleRight;
            cb->fov[2] = views[eye].fov.angleUp;
            cb->fov[3] = views[eye].fov.angleDown;
            context->Unmap(constants, 0);

            D3D11_VIEWPORT viewport{};
            viewport.Width = (float)configs[eye].recommendedImageRectWidth;
            viewport.Height = (float)configs[eye].recommendedImageRectHeight;
            viewport.MaxDepth = 1.f;
            context->OMSetRenderTargets(1, &rtvs[eye][index], nullptr);
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rs);
            context->VSSetShader(vs, nullptr, 0);
            context->PSSetShader(ps, nullptr, 0);
            context->VSSetConstantBuffers(0, 1, &constants);
            context->PSSetConstantBuffers(0, 1, &constants);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(3, 0);

            check(xrReleaseSwapchainImage(swapchain[eye], nullptr), "xrReleaseSwapchainImage");
            projection[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection[eye].pose = views[eye].pose;
            projection[eye].fov = views[eye].fov;
            projection[eye].subImage.swapchain = swapchain[eye];
            projection[eye].subImage.imageRect.extent = {(int32_t)configs[eye].recommendedImageRectWidth,
                                                         (int32_t)configs[eye].recommendedImageRectHeight};
        }

        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.viewCount = 2;
        layer.views = projection;
        const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = frame.shouldRender ? 1 : 0;
        end.layers = end.layerCount ? layers : nullptr;
        check(xrEndFrame(session, &end), "xrEndFrame");
        if (frame.shouldRender) presented += 1;
        if (presented == 1 || presented % 207 == 0) {
            std::printf("qb-app: frame %d\n", presented);
            std::fflush(stdout);
        }
        if (presented >= g_frames && !exit_sent) {
            check(xrRequestExitSession(session), "xrRequestExitSession");
            exit_sent = true;
        }
    }
    std::printf("qb-app: presented %d\n", presented);
    xrDestroySession(session);
    xrDestroyInstance(instance);
    return presented > 0 ? 0 : 1;
}
