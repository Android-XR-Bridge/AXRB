/* Draws both eyes into the host's shared textures and reads QbPose back.
   The image stays on the GPU: this process never maps the texture for readback. */

#include "bridge.h"
#include "wire.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    float floor_y = cam.y - 1.6;
    if (dir.y < -0.02) {
        float t = (floor_y - cam.y) / dir.y;
        float3 hit = cam + dir * t;
        float gx = abs(frac(hit.x) - 0.5);
        float gz = abs(frac(hit.z) - 0.5);
        float grid = saturate(1.0 - min(gx, gz) * 30.0);
        float checker = fmod(floor(hit.x) + floor(hit.z), 2.0);
        float3 base = checker > 0.5 ? float3(0.16, 0.28, 0.20) : float3(0.07, 0.09, 0.08);
        float3 col = lerp(base, float3(0.92, 0.95, 0.85), grid);
        col *= saturate(6.0 / (1.0 + t));
        return float4(col, 1.0);
    }
    float3 sky = lerp(float3(0.22, 0.40, 0.72), float3(0.02, 0.03, 0.06), saturate(dir.y * 1.4));
    return float4(sky, 1.0);
}
)";

struct CB {
    float quat[4];
    float pos[4];
    float fov[4];
};

static void die(const char* what) {
    std::fprintf(stderr, "qb-source: %s\n", what);
    std::exit(1);
}

static HANDLE connect_pipe() {
    for (int i = 0; i < 200; ++i) {
        HANDLE pipe = CreateFileA(QB_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) die("SetNamedPipeHandleState failed");
            return pipe;
        }
        Sleep(50);
    }
    die("timed out waiting for qb-host");
    return INVALID_HANDLE_VALUE;
}

static ID3D11Device* open_device(HANDLE shared, ID3D11Texture2D** texture) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) die("CreateDXGIFactory1 failed");
    ID3D11Device* found = nullptr;
    for (UINT i = 0; !found; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                        &device, nullptr, &context);
        adapter->Release();
        if (FAILED(hr)) continue;
        ID3D11Device1* device1 = nullptr;
        device->QueryInterface(IID_PPV_ARGS(&device1));
        hr = device1->OpenSharedResource1(shared, IID_PPV_ARGS(texture));
        device1->Release();
        if (SUCCEEDED(hr)) {
            found = device;
            context->Release();
            break;
        }
        context->Release();
        device->Release();
    }
    factory->Release();
    if (!found) die("OpenSharedResource1 failed on every adapter");
    return found;
}

int main() {
    static_assert(sizeof(QbPose) == 152, "QbPose layout");
    static_assert(sizeof(QbShare) == 32, "QbShare layout");

    HANDLE pipe = connect_pipe();
    std::printf("qb-source: connected\n");
    std::fflush(stdout);

    QbShare share{};
    QbViews views{};
    bool have_share = false;
    bool have_views = false;
    QbMsgBuf msg{};
    while (!have_share || !have_views) {
        if (!qb_read(pipe, &msg)) die("pipe closed before setup");
        if (msg.type == QB_MSG_SHARE) have_share = qb_body(msg, &share);
        if (msg.type == QB_MSG_VIEWS) have_views = qb_body(msg, &views);
    }
    std::printf("qb-source: eyes %ux%u\n", share.width, share.height);
    std::fflush(stdout);

    ID3D11Texture2D* texture[2] = {};
    ID3D11Device* device = open_device((HANDLE)(uintptr_t)share.handle[0], &texture[0]);
    ID3D11Device1* device1 = nullptr;
    device->QueryInterface(IID_PPV_ARGS(&device1));
    if (FAILED(device1->OpenSharedResource1((HANDLE)(uintptr_t)share.handle[1], IID_PPV_ARGS(&texture[1]))))
        die("failed to open the right eye");
    device1->Release();
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);

    IDXGIKeyedMutex* mutex[2] = {};
    if (FAILED(texture[0]->QueryInterface(IID_PPV_ARGS(&mutex[0])))) die("keyed mutex missing");
    if (FAILED(texture[1]->QueryInterface(IID_PPV_ARGS(&mutex[1])))) die("keyed mutex missing");

    ID3D11RenderTargetView* rtv[2] = {};
    for (int i = 0; i < 2; ++i) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = (DXGI_FORMAT)share.format;
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateRenderTargetView(texture[i], &desc, &rtv[i]))) die("CreateRenderTargetView failed");
    }

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShader, std::strlen(kShader), "grid.hlsl", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &code,
                            &errors);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s\n", errors ? (char*)errors->GetBufferPointer() : "vs compile failed");
        return 1;
    }
    ID3D11VertexShader* vs = nullptr;
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs);
    code->Release();
    hr = D3DCompile(kShader, std::strlen(kShader), "grid.hlsl", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &code, &errors);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s\n", errors ? (char*)errors->GetBufferPointer() : "ps compile failed");
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
    ID3D11Buffer* cb = nullptr;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &cb))) die("CreateBuffer failed");

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    device->CreateRasterizerState(&raster, &rs);

    D3D11_VIEWPORT viewport{};
    viewport.Width = (float)share.width;
    viewport.Height = (float)share.height;
    viewport.MaxDepth = 1.f;

    int drawn = 0;
    while (qb_read(pipe, &msg)) {
        if (msg.type == QB_MSG_VIEWS) {
            qb_body(msg, &views);
            continue;
        }
        if (msg.type != QB_MSG_POSE) continue;
        QbPose pose{};
        if (!qb_body(msg, &pose)) continue;

        if (mutex[0]->AcquireSync(0, 5) != S_OK) continue;
        if (mutex[1]->AcquireSync(0, 5) != S_OK) {
            mutex[0]->ReleaseSync(0);
            continue;
        }
        for (int eye = 0; eye < 2; ++eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
            auto* constants = static_cast<CB*>(mapped.pData);
            const QbXform& view = pose.view[eye];
            constants->quat[0] = view.qx;
            constants->quat[1] = view.qy;
            constants->quat[2] = view.qz;
            constants->quat[3] = view.qw;
            constants->pos[0] = view.px;
            constants->pos[1] = view.py;
            constants->pos[2] = view.pz;
            constants->fov[0] = views.fov_left[eye];
            constants->fov[1] = views.fov_right[eye];
            constants->fov[2] = views.fov_up[eye];
            constants->fov[3] = views.fov_down[eye];
            context->Unmap(cb, 0);

            context->OMSetRenderTargets(1, &rtv[eye], nullptr);
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rs);
            context->VSSetShader(vs, nullptr, 0);
            context->PSSetShader(ps, nullptr, 0);
            context->VSSetConstantBuffers(0, 1, &cb);
            context->PSSetConstantBuffers(0, 1, &cb);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(3, 0);
        }
        context->Flush();
        mutex[1]->ReleaseSync(1);
        mutex[0]->ReleaseSync(1);
        QbFrame frame{};
        frame.width = share.width;
        frame.height = share.height;
        frame.format = share.format;
        if (!qb_write(pipe, QB_MSG_FRAME, &frame, sizeof(frame))) break;
        drawn += 1;
        if (drawn == 1) {
            std::printf("qb-source: drawing\n");
            std::fflush(stdout);
        }
    }
    std::printf("qb-source: drew %d frames\n", drawn);
    return drawn > 0 ? 0 : 1;
}
