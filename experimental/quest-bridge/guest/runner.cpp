/* Windows process that runs the arm64 Android guest.

   libgrid.so is Android arm64. Its OpenXR calls land back in this process,
   which is bound to qb_runtime.dll, so the swapchain images are the textures
   qb-host is presenting. */

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "cpu.h"
#include "glsl.h"
#include "loader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

/* A constant register per uniform, which is what packoffset(cN) gives us. */
static const int kMaxRegisters = 64;
static const int kMaxSamplers = 8;
static const int kMaxAttributes = 16;
/* A sampler is not a constant, so its location sits above every register. */
static const int kSamplerLocation = 1000;

struct Eye {
    XrSwapchain swap = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

/* One compiled guest program. The guest's GLSL ES is what got compiled. */
struct GlShader {
    bool fragment = false;
    std::string source;
};

/* A buffer the guest filled with glBufferData. One buffer can be used for
   vertices and for indices, so it is created for both. */
struct GlBuffer {
    ID3D11Buffer* buffer = nullptr;
    uint32_t size = 0;
};

/* One glVertexAttribPointer, which says where a slot reads from. */
struct GlAttribute {
    bool enabled = false;
    uint32_t buffer = 0;
    int components = 4;
    uint64_t type = 0x1406; /* GL_FLOAT */
    bool normalized = false;
    uint32_t stride = 0;
    uint32_t offset = 0;
};

/* A texture the guest made with glGenTextures and filled with glTexImage2D. */
struct GlTexture {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    bool point = false;
};

struct GlProgram {
    std::vector<uint32_t> shaders;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    std::vector<GlslUniform> uniforms;
    std::vector<GlslSampler> samplers;
    std::vector<GlslAttribute> attributes;
    std::vector<uint8_t> vertex_code; /* kept so an input layout can be made from it */
    std::unordered_map<uint64_t, ID3D11InputLayout*> layouts;
    int unit[kMaxSamplers]{};
    float value[kMaxRegisters][4]{};
    bool linked = false;
};

struct App : GuestImage {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Buffer* constants = nullptr;
    ID3D11Buffer* target = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    Eye eye[2]{};
    int eyes = 0;
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    std::unordered_map<uint32_t, GlBuffer> gl_buffer;
    GlAttribute attribute[kMaxAttributes]{};
    uint32_t array_buffer = 0;
    uint32_t index_buffer = 0;
    ID3D11RasterizerState* cull = nullptr;
    bool culling = false;
    std::unordered_map<uint32_t, GlTexture> gl_texture;
    uint32_t unit_texture[kMaxSamplers]{};
    int active_unit = 0;
    ID3D11SamplerState* linear = nullptr;
    ID3D11SamplerState* nearest = nullptr;
    std::unordered_map<uint32_t, GlShader> gl_shader;
    std::unordered_map<uint32_t, GlProgram> gl_program;
    std::unordered_map<uint32_t, ID3D11Texture2D*> gl_tex;
    std::unordered_map<uint32_t, ID3D11RenderTargetView*> gl_rtv;
    std::unordered_map<uint32_t, uint32_t> fbo_tex;
    uint32_t next_gl = 1;
    uint32_t bound_fbo = 0;
    uint32_t program = 0;
    int viewport[4]{};
};

static App* g_app = nullptr;

/* A float argument arrives in a vector register, which is what the normal
   arm64 calling convention says. */
static float farg(const GuestCpu& cpu, int n) {
    uint32_t bits = (uint32_t)cpu.q[n].lo;
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

static float bits_f(uint64_t bits) {
    float v = 0;
    uint32_t b = (uint32_t)bits;
    std::memcpy(&v, &b, 4);
    return v;
}

static void* host_ptr(uint64_t va) { return guest_ptr(g_app->mem, va, 1); }

static bool ensure_device(XrInstance instance, XrSystemId system) {
    if (g_app->device) return true;
    PFN_xrGetD3D11GraphicsRequirementsKHR get_reqs = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&get_reqs))))
        return false;
    XrGraphicsRequirementsD3D11KHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (XR_FAILED(get_reqs(instance, system, &reqs))) return false;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
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
    if (!adapter) return false;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                   &g_app->device, nullptr, &g_app->context);
    adapter->Release();
    if (FAILED(hr)) return false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = kMaxRegisters * 16;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_app->device->CreateBuffer(&bd, nullptr, &g_app->constants);
    bd.ByteWidth = 16;
    g_app->device->CreateBuffer(&bd, nullptr, &g_app->target);
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    g_app->device->CreateSamplerState(&sd, &g_app->linear);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    g_app->device->CreateSamplerState(&sd, &g_app->nearest);
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    g_app->device->CreateRasterizerState(&raster, &g_app->raster);
    /* GL counts a counter-clockwise face as the front one. */
    raster.CullMode = D3D11_CULL_BACK;
    raster.FrontCounterClockwise = TRUE;
    g_app->device->CreateRasterizerState(&raster, &g_app->cull);
    g_app->binding.device = g_app->device;
    return true;
}

/* A draw into the guest's framebuffer becomes one pass into the texture that
   framebuffer names, running the guest's own shaders. */
static ID3D11RenderTargetView* rtv_for(uint32_t tex) {
    auto found = g_app->gl_rtv.find(tex);
    if (found != g_app->gl_rtv.end()) return found->second;
    ID3D11Texture2D* texture = g_app->gl_tex.count(tex) ? g_app->gl_tex[tex] : nullptr;
    if (!texture) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : td.Format;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_app->device->CreateRenderTargetView(texture, &desc, &rtv))) return nullptr;
    g_app->gl_rtv[tex] = rtv;
    return rtv;
}

/* glTexImage2D. GL's first row is the bottom of the picture and D3D's is the
   top, so the rows go in backwards and a sampled v of 0 means the same thing
   in both. */
static bool gl_upload(uint32_t id, uint64_t format, uint64_t type, int width, int height, uint64_t pixels_va) {
    if (!g_app->device || width <= 0 || height <= 0) return false;
    int source_channels = 0;
    if (format == 0x1908) source_channels = 4;      /* GL_RGBA */
    else if (format == 0x1907) source_channels = 3; /* GL_RGB */
    else if (format == 0x1909 || format == 0x1903 || format == 0x1906) source_channels = 1;
    if (!source_channels || type != 0x1401) {       /* GL_UNSIGNED_BYTE */
        std::fprintf(stderr, "qb-guest: glTexImage2D format %#llx type %#llx is not wired through yet\n",
                     (unsigned long long)format, (unsigned long long)type);
        return false;
    }
    const uint8_t* source = pixels_va ? guest_ptr(g_app->mem, pixels_va,
                                                  (uint64_t)width * height * source_channels)
                                      : nullptr;
    std::vector<uint8_t> rgba((size_t)width * height * 4, 0xff);
    for (int y = 0; y < height && source; ++y) {
        const uint8_t* in = source + (size_t)(height - 1 - y) * width * source_channels;
        uint8_t* out = rgba.data() + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = in + (size_t)x * source_channels;
            uint8_t* q = out + (size_t)x * 4;
            q[0] = p[0];
            q[1] = source_channels == 1 ? p[0] : p[1];
            q[2] = source_channels == 1 ? p[0] : p[2];
            q[3] = source_channels == 4 ? p[3] : 0xff;
        }
    }
    GlTexture& texture = g_app->gl_texture[id];
    if (texture.view) texture.view->Release();
    if (texture.texture) texture.texture->Release();
    texture.view = nullptr;
    texture.texture = nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = rgba.data();
    data.SysMemPitch = (UINT)width * 4;
    if (FAILED(g_app->device->CreateTexture2D(&td, &data, &texture.texture))) return false;
    if (FAILED(g_app->device->CreateShaderResourceView(texture.texture, nullptr, &texture.view))) return false;
    /* The guest's first row is the bottom of the picture, so it has to come out
       as the last row stored. These two should not match. */
    std::printf("qb-guest: texture %u is %dx%d, %d channels in, guest row 0 starts %d %d %d,"
                " stored top row starts %d %d %d\n",
                id, width, height, source_channels, source ? source[0] : 0, source ? source[1] : 0,
                source ? source[2] : 0, rgba[0], rgba[1], rgba[2]);
    std::fflush(stdout);
    return true;
}

/* What glVertexAttribPointer described, as a D3D format. */
static DXGI_FORMAT attribute_format(const GlAttribute& attribute) {
    static const DXGI_FORMAT floats[5] = {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT,
                                          DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    int n = attribute.components;
    if (n < 1 || n > 4) return DXGI_FORMAT_UNKNOWN;
    switch (attribute.type) {
        case 0x1406: return floats[n]; /* GL_FLOAT */
        case 0x1401:                   /* GL_UNSIGNED_BYTE */
            if (n == 4) return attribute.normalized ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UINT;
            return DXGI_FORMAT_UNKNOWN;
        case 0x1403: /* GL_UNSIGNED_SHORT */
            if (n == 2) return attribute.normalized ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16G16_UINT;
            if (n == 4) return attribute.normalized ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_R16G16B16A16_UINT;
            return DXGI_FORMAT_UNKNOWN;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

/* One input layout per program per set of enabled attributes. Each attribute
   reads from its own slot, so a shared buffer and separate buffers both work. */
static ID3D11InputLayout* layout_for(GlProgram& program, uint64_t& key) {
    key = 0;
    for (const GlslAttribute& attribute : program.attributes) {
        if (attribute.location >= kMaxAttributes) continue;
        const GlAttribute& state = g_app->attribute[attribute.location];
        if (state.enabled) key |= 1ull << attribute.location;
    }
    if (!key) return nullptr;
    auto found = program.layouts.find(key);
    if (found != program.layouts.end()) return found->second;
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    for (const GlslAttribute& attribute : program.attributes) {
        if (attribute.location >= kMaxAttributes) continue;
        const GlAttribute& state = g_app->attribute[attribute.location];
        if (!state.enabled) continue;
        D3D11_INPUT_ELEMENT_DESC element{};
        element.SemanticName = "TEXCOORD";
        element.SemanticIndex = (UINT)attribute.location;
        element.Format = attribute_format(state);
        element.InputSlot = (UINT)attribute.location;
        element.AlignedByteOffset = 0; /* the offset rides on the buffer binding */
        element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        if (element.Format == DXGI_FORMAT_UNKNOWN) {
            std::fprintf(stderr, "qb-guest: attribute %s is %d of type %#llx, which is not wired through yet\n",
                         attribute.name.c_str(), state.components, (unsigned long long)state.type);
            return nullptr;
        }
        elements.push_back(element);
    }
    ID3D11InputLayout* layout = nullptr;
    if (FAILED(g_app->device->CreateInputLayout(elements.data(), (UINT)elements.size(), program.vertex_code.data(),
                                                program.vertex_code.size(), &layout))) {
        std::fprintf(stderr, "qb-guest: could not make an input layout for program\n");
        return nullptr;
    }
    program.layouts[key] = layout;
    return layout;
}

static D3D11_PRIMITIVE_TOPOLOGY topology_of(uint64_t mode) {
    switch (mode) {
        case 0: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        case 1: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case 3: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case 5: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

/* glLinkProgram is where the guest's GLSL ES becomes a pipeline. */
static bool gl_link(uint32_t id) {
    GlProgram& program = g_app->gl_program[id];
    program.linked = false;
    if (!g_app->device) return false;
    std::string vertex;
    std::string fragment;
    for (uint32_t shader : program.shaders) {
        auto found = g_app->gl_shader.find(shader);
        if (found == g_app->gl_shader.end()) continue;
        (found->second.fragment ? fragment : vertex) = found->second.source;
    }
    if (vertex.empty() || fragment.empty()) {
        std::fprintf(stderr, "qb-guest: program %u is missing a stage\n", id);
        return false;
    }
    GlslProgram translated;
    if (!glsl_translate(vertex, fragment, translated)) {
        std::fprintf(stderr, "qb-guest: %s\n", translated.error.c_str());
        return false;
    }
    if (translated.registers > kMaxRegisters) {
        std::fprintf(stderr, "qb-guest: program %u wants %d uniform registers\n", id, translated.registers);
        return false;
    }
    struct Build {
        const std::string& source;
        const char* entry;
        const char* profile;
    } builds[2] = {{translated.vertex, "qb_vs", "vs_5_0"}, {translated.fragment, "qb_ps", "ps_5_0"}};
    ID3DBlob* code[2]{};
    for (int stage = 0; stage < 2; ++stage) {
        ID3DBlob* errors = nullptr;
        if (FAILED(D3DCompile(builds[stage].source.data(), builds[stage].source.size(), "guest.hlsl", nullptr, nullptr,
                              builds[stage].entry, builds[stage].profile, 0, 0, &code[stage], &errors))) {
            std::fprintf(stderr, "qb-guest: %s from the guest's %s shader\n",
                         errors ? (const char*)errors->GetBufferPointer() : "compile failed",
                         stage ? "fragment" : "vertex");
            if (errors) errors->Release();
            if (code[0]) code[0]->Release();
            return false;
        }
        if (errors) errors->Release();
    }
    if (program.vs) program.vs->Release();
    if (program.ps) program.ps->Release();
    program.vs = nullptr;
    program.ps = nullptr;
    g_app->device->CreateVertexShader(code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, &program.vs);
    g_app->device->CreatePixelShader(code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, &program.ps);
    /* The input layout is made from this bytecode later, so it is kept. */
    program.vertex_code.assign((const uint8_t*)code[0]->GetBufferPointer(),
                               (const uint8_t*)code[0]->GetBufferPointer() + code[0]->GetBufferSize());
    code[0]->Release();
    code[1]->Release();
    program.attributes = translated.attributes;
    program.uniforms = translated.uniforms;
    program.samplers = translated.samplers;
    for (size_t i = 0; i < program.samplers.size() && i < kMaxSamplers; ++i) program.unit[i] = (int)i;
    program.linked = program.vs && program.ps;
    if (program.linked)
        std::printf("qb-guest: program %u linked, %d uniform registers, %d samplers\n", id, translated.registers,
                    (int)translated.samplers.size());
    std::fflush(stdout);
    return program.linked;
}

static void gl_draw(uint64_t mode, uint32_t first, uint32_t count, uint64_t index_type,
                    uint64_t index_offset) {
    /* The first few draws say what they did, because a draw that quietly does
       nothing looks exactly like a frozen picture in the headset. */
    static int reported = 0;
    bool say = reported < 4;
    if (say) ++reported;
    if (!g_app->device) {
        if (say) std::printf("qb-guest: draw with no device\n");
        return;
    }
    auto found = g_app->gl_program.find(g_app->program);
    if (found == g_app->gl_program.end() || !found->second.linked) {
        if (say) std::printf("qb-guest: draw with program %u, which is not linked\n", g_app->program);
        return;
    }
    GlProgram& program = found->second;
    uint32_t tex = g_app->fbo_tex.count(g_app->bound_fbo) ? g_app->fbo_tex[g_app->bound_fbo] : 0;
    ID3D11RenderTargetView* rtv = rtv_for(tex);
    if (!rtv) {
        if (say)
            std::printf("qb-guest: draw into framebuffer %u, texture %u, which has no target\n", g_app->bound_fbo,
                        tex);
        return;
    }
    if (say) {
        std::printf("qb-guest: draw %u verts into texture %u, viewport %d %d %d %d, quat %.2f %.2f %.2f %.2f,"
                    " fov %.2f %.2f %.2f %.2f\n",
                    count, tex, g_app->viewport[0], g_app->viewport[1], g_app->viewport[2], g_app->viewport[3],
                    program.value[0][0], program.value[0][1], program.value[0][2], program.value[0][3],
                    program.value[3][0], program.value[3][1], program.value[3][2], program.value[3][3]);
        std::fflush(stdout);
    }
    ID3D11Texture2D* texture = g_app->gl_tex[tex];
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_app->context->Map(g_app->constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, program.value, sizeof(program.value));
    g_app->context->Unmap(g_app->constants, 0);
    /* The target size, so the translated shader can put gl_FragCoord.y back on the bottom. */
    if (SUCCEEDED(g_app->context->Map(g_app->target, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        float size[4] = {(float)td.Width, (float)td.Height, 0, 0};
        std::memcpy(mapped.pData, size, sizeof(size));
        g_app->context->Unmap(g_app->target, 0);
    }
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = (float)g_app->viewport[0];
    viewport.TopLeftY = (float)g_app->viewport[1];
    viewport.Width = (float)g_app->viewport[2];
    viewport.Height = (float)g_app->viewport[3];
    viewport.MaxDepth = 1.f;
    ID3D11Buffer* buffers[2] = {g_app->constants, g_app->target};
    g_app->context->OMSetRenderTargets(1, &rtv, nullptr);
    g_app->context->RSSetViewports(1, &viewport);
    g_app->context->RSSetState(g_app->culling ? g_app->cull : g_app->raster);
    g_app->context->VSSetShader(program.vs, nullptr, 0);
    g_app->context->PSSetShader(program.ps, nullptr, 0);
    g_app->context->VSSetConstantBuffers(0, 2, buffers);
    g_app->context->PSSetConstantBuffers(0, 2, buffers);
    for (const GlslSampler& sampler : program.samplers) {
        if (sampler.slot >= kMaxSamplers) continue;
        int unit = program.unit[sampler.slot];
        uint32_t bound = unit >= 0 && unit < kMaxSamplers ? g_app->unit_texture[unit] : 0;
        auto texture = g_app->gl_texture.find(bound);
        ID3D11ShaderResourceView* view = texture == g_app->gl_texture.end() ? nullptr : texture->second.view;
        ID3D11SamplerState* state = texture != g_app->gl_texture.end() && texture->second.point ? g_app->nearest
                                                                                                : g_app->linear;
        g_app->context->PSSetShaderResources((UINT)sampler.slot, 1, &view);
        g_app->context->PSSetSamplers((UINT)sampler.slot, 1, &state);
        g_app->context->VSSetShaderResources((UINT)sampler.slot, 1, &view);
        g_app->context->VSSetSamplers((UINT)sampler.slot, 1, &state);
    }
    /* Bind whatever the guest attached to each attribute slot. */
    uint64_t key = 0;
    ID3D11InputLayout* layout = layout_for(program, key);
    g_app->context->IASetInputLayout(layout);
    for (const GlslAttribute& attribute : program.attributes) {
        if (attribute.location >= kMaxAttributes) continue;
        const GlAttribute& state = g_app->attribute[attribute.location];
        if (!state.enabled) continue;
        auto buffer = g_app->gl_buffer.find(state.buffer);
        ID3D11Buffer* vb = buffer == g_app->gl_buffer.end() ? nullptr : buffer->second.buffer;
        UINT stride = state.stride ? state.stride : (UINT)state.components * 4;
        UINT offset = state.offset;
        g_app->context->IASetVertexBuffers((UINT)attribute.location, 1, &vb, &stride, &offset);
    }
    g_app->context->IASetPrimitiveTopology(topology_of(mode));
    if (index_type) {
        auto buffer = g_app->gl_buffer.find(g_app->index_buffer);
        if (buffer == g_app->gl_buffer.end()) return;
        DXGI_FORMAT format = index_type == 0x1405 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        g_app->context->IASetIndexBuffer(buffer->second.buffer, format, (UINT)index_offset);
        g_app->context->DrawIndexed(count, 0, 0);
    } else {
        g_app->context->Draw(count, first);
    }
    if (say) {
        /* Read four pixels back, so a draw that wrote nothing can be told from
           one that wrote something. */
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = 2;
        sd.Height = 2;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = td.Format;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging = nullptr;
        if (SUCCEEDED(g_app->device->CreateTexture2D(&sd, nullptr, &staging))) {
            D3D11_BOX box{};
            box.left = td.Width / 2;
            box.right = box.left + 2;
            box.top = td.Height / 2;
            box.bottom = box.top + 2;
            box.back = 1;
            g_app->context->CopySubresourceRegion(staging, 0, 0, 0, 0, texture, 0, &box);
            D3D11_MAPPED_SUBRESOURCE read{};
            if (SUCCEEDED(g_app->context->Map(staging, 0, D3D11_MAP_READ, 0, &read))) {
                const uint8_t* p = static_cast<const uint8_t*>(read.pData);
                std::printf("qb-guest: centre pixel of texture %u after the draw: %d %d %d %d\n", tex, p[0], p[1],
                            p[2], p[3]);
                g_app->context->Unmap(staging, 0);
            }
            staging->Release();
        }
        std::fflush(stdout);
    }
}

static XrResult call_create_instance(uint64_t info_va, uint64_t out_va) {
    auto* info = static_cast<XrInstanceCreateInfo*>(host_ptr(info_va));
    XrInstanceCreateInfo host = *info;
    /* The guest asks for OpenGL ES. The headset runtime behind us is D3D11. */
    const char* ext = "XR_KHR_D3D11_enable";
    host.enabledExtensionCount = 1;
    host.enabledExtensionNames = &ext;
    (void)info;
    std::snprintf(host.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Quest Bridge Guest");
    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&host, &instance);
    std::memcpy(host_ptr(out_va), &instance, sizeof(instance));
    return result;
}

static XrResult call_create_session(uint64_t instance, uint64_t info_va, uint64_t out_va) {
    auto* info = static_cast<XrSessionCreateInfo*>(host_ptr(info_va));
    if (!ensure_device((XrInstance)instance, info->systemId)) return XR_ERROR_RUNTIME_FAILURE;
    XrSessionCreateInfo host = *info;
    host.next = &g_app->binding;
    XrSession session = XR_NULL_HANDLE;
    XrResult result = xrCreateSession((XrInstance)instance, &host, &session);
    std::memcpy(host_ptr(out_va), &session, sizeof(session));
    return result;
}

static XrResult call_end_frame(uint64_t session, uint64_t info_va) {
    auto* info = static_cast<XrFrameEndInfo*>(host_ptr(info_va));
    XrFrameEndInfo host = *info;
    XrCompositionLayerProjection projection{};
    const XrCompositionLayerBaseHeader* layers[1]{};
    if (host.layerCount > 0 && info->layers) {
        uint64_t layers_va = (uint64_t)info->layers;
        uint64_t layer_va = *static_cast<uint64_t*>(host_ptr(layers_va));
        auto* guest_layer = static_cast<XrCompositionLayerProjection*>(host_ptr(layer_va));
        projection = *guest_layer;
        uint64_t views_va = (uint64_t)guest_layer->views;
        projection.views = static_cast<XrCompositionLayerProjectionView*>(host_ptr(views_va));
        layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        host.layers = layers;
    }
    return xrEndFrame((XrSession)session, &host);
}

/* The guest was built with no libc. These are the entry points it imports. */
static uint64_t call_memset(uint64_t dst_va, uint64_t value, uint64_t bytes) {
    uint8_t* p = guest_ptr(g_app->mem, dst_va, bytes);
    if (p) std::memset(p, (int)(value & 0xff), (size_t)bytes);
    return dst_va;
}

static const char* guest_string(uint64_t va) {
    const char* p = reinterpret_cast<const char*>(guest_ptr(g_app->mem, va, 1));
    return p ? p : "";
}

/* The guest asks for its swapchain images as GLES texture names. Each one
   stands for the D3D11 texture qb-host is presenting. */
static XrResult call_enumerate_images(uint64_t swapchain, uint64_t capacity, uint64_t count_va, uint64_t images_va) {
    XrSwapchainImageD3D11KHR host{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    uint32_t count = 0;
    XrResult result = xrEnumerateSwapchainImages((XrSwapchain)swapchain, (uint32_t)capacity, &count,
                                                 capacity ? reinterpret_cast<XrSwapchainImageBaseHeader*>(&host)
                                                          : nullptr);
    if (count_va) std::memcpy(host_ptr(count_va), &count, 4);
    if (XR_FAILED(result) || capacity == 0 || !images_va) return result;
    uint32_t name = 0;
    for (auto& pair : g_app->gl_tex)
        if (pair.second == host.texture) name = pair.first;
    if (!name) {
        name = g_app->next_gl++;
        g_app->gl_tex[name] = host.texture;
    }
    std::memcpy(host_ptr(images_va + 16), &name, 4);
    return result;
}

/* The OpenGL ES calls the guest makes. State here, and one D3D11 pass on a draw. */
static bool gl_call(const std::string& name, GuestCpu& cpu) {
    auto arg = [&](int n) { return cpu.x[n]; };
    if (name == "glCreateShader") {
        uint32_t id = g_app->next_gl++;
        g_app->gl_shader[id].fragment = arg(0) == 0x8b30; /* GL_FRAGMENT_SHADER */
        cpu.x[0] = id;
    } else if (name == "glCreateProgram") {
        uint32_t id = g_app->next_gl++;
        g_app->gl_program[id];
        cpu.x[0] = id;
    } else if (name == "glShaderSource") {
        uint64_t first = 0;
        std::memcpy(&first, guest_ptr(g_app->mem, arg(2), 8), 8);
        g_app->gl_shader[(uint32_t)arg(0)].source = guest_string(first);
    } else if (name == "glAttachShader") {
        g_app->gl_program[(uint32_t)arg(0)].shaders.push_back((uint32_t)arg(1));
    } else if (name == "glLinkProgram") {
        gl_link((uint32_t)arg(0));
    } else if (name == "glGetUniformLocation") {
        const char* wanted = guest_string(arg(1));
        const GlProgram& program = g_app->gl_program[(uint32_t)arg(0)];
        int slot = -1;
        for (const GlslUniform& uniform : program.uniforms)
            if (uniform.name == wanted) slot = uniform.reg;
        for (const GlslSampler& sampler : program.samplers)
            if (sampler.name == wanted) slot = kSamplerLocation + sampler.slot;
        cpu.x[0] = (uint64_t)(int64_t)slot;
    } else if (name == "glUseProgram") {
        g_app->program = (uint32_t)arg(0);
    } else if (name == "glGenFramebuffers") {
        for (uint32_t i = 0; i < (uint32_t)arg(0); ++i) {
            uint32_t id = g_app->next_gl++;
            std::memcpy(host_ptr(arg(1) + i * 4ull), &id, 4);
        }
    } else if (name == "glBindFramebuffer") {
        g_app->bound_fbo = (uint32_t)arg(1);
    } else if (name == "glFramebufferTexture2D") {
        g_app->fbo_tex[g_app->bound_fbo] = (uint32_t)arg(3);
    } else if (name == "glViewport") {
        for (int i = 0; i < 4; ++i) g_app->viewport[i] = (int)(int32_t)arg(i);
    } else if (name.compare(0, 9, "glUniform") == 0 && name.size() >= 11) {
        /* glUniform{1,2,3,4}{f,i}. The guest has no FP registers, so a float
           argument arrives as its bits in a general one. */
        int count = name[9] - '0';
        bool integer = name[10] == 'i';
        int slot = (int)(int32_t)arg(0);
        auto& program = g_app->gl_program[g_app->program];
        if (slot >= kSamplerLocation) {
            int sampler = slot - kSamplerLocation;
            if (sampler < kMaxSamplers) program.unit[sampler] = (int)(int32_t)arg(1);
            return true;
        }
        if (slot < 0 || slot >= kMaxRegisters || count < 1 || count > 4) return true;
        /* An int uniform is read by the shader as an integer, so its bits go in
           as they are. Writing it as a float would hand the shader the float's
           bit pattern as a huge number. */
        bool as_integer = false;
        for (const GlslUniform& uniform : program.uniforms)
            if (uniform.reg == slot) as_integer = uniform.integer;
        for (int i = 0; i < count; ++i) {
            if (as_integer) {
                int32_t value = integer ? (int32_t)arg(1 + i) : (int32_t)farg(cpu, i);
                std::memcpy(&program.value[slot][i], &value, 4);
            } else {
                program.value[slot][i] = integer ? (float)(int32_t)arg(1 + i) : farg(cpu, i);
            }
        }
    } else if (name == "glGenTextures") {
        for (uint32_t i = 0; i < (uint32_t)arg(0); ++i) {
            uint32_t id = g_app->next_gl++;
            g_app->gl_texture[id];
            std::memcpy(host_ptr(arg(1) + i * 4ull), &id, 4);
        }
    } else if (name == "glActiveTexture") {
        int unit = (int)(arg(0) - 0x84c0); /* GL_TEXTURE0 */
        if (unit >= 0 && unit < kMaxSamplers) g_app->active_unit = unit;
    } else if (name == "glBindTexture") {
        g_app->unit_texture[g_app->active_unit] = (uint32_t)arg(1);
    } else if (name == "glTexImage2D") {
        uint64_t pixels = 0;
        std::memcpy(&pixels, guest_ptr(g_app->mem, cpu.sp, 8), 8); /* the ninth argument is on the stack */
        gl_upload(g_app->unit_texture[g_app->active_unit], arg(6), arg(7), (int)(int32_t)arg(3),
                  (int)(int32_t)arg(4), pixels);
    } else if (name == "glTexParameteri") {
        bool filter = arg(1) == 0x2800 || arg(1) == 0x2801; /* MAG_FILTER, MIN_FILTER */
        if (filter && g_app->gl_texture.count(g_app->unit_texture[g_app->active_unit]))
            g_app->gl_texture[g_app->unit_texture[g_app->active_unit]].point = arg(2) == 0x2600; /* GL_NEAREST */
    } else if (name == "glGenBuffers" || name == "glGenVertexArrays") {
        for (uint32_t i = 0; i < (uint32_t)arg(0); ++i) {
            uint32_t id = g_app->next_gl++;
            if (name[5] == 'B') g_app->gl_buffer[id];
            std::memcpy(host_ptr(arg(1) + i * 4ull), &id, 4);
        }
    } else if (name == "glBindBuffer") {
        if (arg(0) == 0x8893) g_app->index_buffer = (uint32_t)arg(1); /* GL_ELEMENT_ARRAY_BUFFER */
        else g_app->array_buffer = (uint32_t)arg(1);
    } else if (name == "glBufferData") {
        uint32_t id = arg(0) == 0x8893 ? g_app->index_buffer : g_app->array_buffer;
        GlBuffer& target = g_app->gl_buffer[id];
        if (target.buffer) target.buffer->Release();
        target.buffer = nullptr;
        target.size = (uint32_t)arg(1);
        const uint8_t* source = arg(2) ? guest_ptr(g_app->mem, arg(2), arg(1)) : nullptr;
        std::vector<uint8_t> zero(target.size, 0);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (target.size + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = source ? source : zero.data();
        if (g_app->device) g_app->device->CreateBuffer(&bd, &data, &target.buffer);
        std::printf("qb-guest: buffer %u holds %u bytes\n", id, target.size);
        std::fflush(stdout);
    } else if (name == "glVertexAttribPointer") {
        int index = (int)arg(0);
        if (index >= 0 && index < kMaxAttributes) {
            GlAttribute& attribute = g_app->attribute[index];
            attribute.buffer = g_app->array_buffer;
            attribute.components = (int)arg(1);
            attribute.type = arg(2);
            attribute.normalized = arg(3) != 0;
            attribute.stride = (uint32_t)arg(4);
            attribute.offset = (uint32_t)arg(5);
        }
    } else if (name == "glEnableVertexAttribArray" || name == "glDisableVertexAttribArray") {
        int index = (int)arg(0);
        if (index >= 0 && index < kMaxAttributes) g_app->attribute[index].enabled = name[2] == 'E';
    } else if (name == "glGetAttribLocation") {
        const char* wanted = guest_string(arg(1));
        int slot = -1;
        for (const GlslAttribute& attribute : g_app->gl_program[(uint32_t)arg(0)].attributes)
            if (attribute.name == wanted) slot = attribute.location;
        cpu.x[0] = (uint64_t)(int64_t)slot;
    } else if (name == "glDrawElements") {
        gl_draw(arg(0), 0, (uint32_t)arg(1), arg(2) ? arg(2) : 0x1403, arg(3));
    } else if (name == "glDrawArrays") {
        gl_draw(arg(0), (uint32_t)arg(1), (uint32_t)arg(2), 0, 0);
    } else if (name == "glEnable" || name == "glDisable") {
        if (arg(0) == 0x0b44) g_app->culling = name[2] == 'E'; /* GL_CULL_FACE */
    } else if (name == "glGetError") {
        cpu.x[0] = 0;
    } else if (name == "glCompileShader" || name == "glDeleteShader" || name == "glDeleteProgram" ||
               name == "glDeleteFramebuffers" || name == "glClear" || name == "glClearColor" ||
               name == "glFlush" || name == "glFinish" ||
               name == "glDeleteTextures" || name == "glGenerateMipmap" || name == "glPixelStorei" ||
               name == "glDeleteBuffers" || name == "glDeleteVertexArrays" || name == "glBindVertexArray" ||
               name == "glCullFace" || name == "glFrontFace" || name == "glDepthMask" || name == "glColorMask") {
        cpu.x[0] = 0;
    } else {
        return false;
    }
    return true;
}

/* Where the guest's frame time actually goes: interpreting its own code, or
   sitting inside one of our calls waiting on the host and the GPU. */
static double g_thunk_ms = 0;
static unsigned long long g_frame_steps = 0;
static double g_frame_start = 0;

static double now_ms() {
    LARGE_INTEGER t{}, f{};
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return 1000.0 * (double)t.QuadPart / (double)f.QuadPart;
}

static void thunk(GuestCpu& cpu, GuestMem&, int index, void*) {
    const std::string& name = g_app->imports()[index];
    double entered = now_ms();
    static double per_call[64] = {};
    struct Timed {
        double entered;
        int index;
        ~Timed() {
            double spent = now_ms() - entered;
            g_thunk_ms += spent;
            if (index < 64) per_call[index] += spent;
        }
    } timed{entered, index};
    if (name == "xrWaitFrame") {
        static int frames = 0;
        if (g_frame_start != 0 && ++frames % 120 == 0) {
            double total = entered - g_frame_start;
            unsigned long long steps = guest_steps() - g_frame_steps;
            std::printf("qb-guest: last frame %.2fms, %llu instructions, %.2fms in host calls, %.2fms interpreting\n",
                        total, steps, g_thunk_ms, total - g_thunk_ms);
            for (size_t i = 0; i < g_app->imports().size() && i < 64; ++i) {
                if (per_call[i] < 1.0) continue;
                std::printf("qb-guest:   %-26s %6.1fms over 120 frames\n", g_app->imports()[i].c_str(), per_call[i]);
                per_call[i] = 0;
            }
            std::fflush(stdout);
        }
        g_frame_start = entered;
        g_frame_steps = guest_steps();
        g_thunk_ms = 0;
    }
    auto arg = [&](int n) { return n < 8 ? cpu.x[n] : 0ull; };
    auto stack_u32 = [&](int slot) {
        uint64_t v = 0;
        std::memcpy(&v, guest_ptr(g_app->mem, cpu.sp + slot * 8ull, 8), 8);
        return v;
    };
    if (name == "memset") {
        cpu.x[0] = call_memset(arg(0), arg(1), arg(2));
        return;
    }
    if (name.compare(0, 2, "gl") == 0 && gl_call(name, cpu)) return;
    XrResult result = XR_SUCCESS;
    if (name == "xrCreateInstance") result = call_create_instance(arg(0), arg(1));
    else if (name == "xrGetSystem") {
        auto* info = static_cast<const XrSystemGetInfo*>(host_ptr(arg(1)));
        result = xrGetSystem((XrInstance)arg(0), info, static_cast<XrSystemId*>(host_ptr(arg(2))));
    }
    else if (name == "xrCreateSession") result = call_create_session(arg(0), arg(1), arg(2));
    else if (name == "xrEnumerateViewConfigurationViews")
        result = xrEnumerateViewConfigurationViews(
            (XrInstance)arg(0), (XrSystemId)arg(1), (XrViewConfigurationType)arg(2), (uint32_t)arg(3),
            static_cast<uint32_t*>(host_ptr(arg(4))), static_cast<XrViewConfigurationView*>(host_ptr(arg(5))));
    else if (name == "xrEnumerateSwapchainFormats")
        result = xrEnumerateSwapchainFormats((XrSession)arg(0), (uint32_t)arg(1), static_cast<uint32_t*>(host_ptr(arg(2))),
                                             static_cast<int64_t*>(host_ptr(arg(3))));
    else if (name == "xrCreateSwapchain") {
        auto* info = static_cast<const XrSwapchainCreateInfo*>(host_ptr(arg(1)));
        XrSwapchain swap = XR_NULL_HANDLE;
        result = xrCreateSwapchain((XrSession)arg(0), info, &swap);
        std::memcpy(host_ptr(arg(2)), &swap, sizeof(swap));
        if (XR_SUCCEEDED(result) && g_app->eyes < 2) {
            g_app->eye[g_app->eyes].swap = swap;
            g_app->eye[g_app->eyes].width = info->width;
            g_app->eye[g_app->eyes].height = info->height;
            g_app->eyes += 1;
        }
    } else if (name == "xrPollEvent")
        result = xrPollEvent((XrInstance)arg(0), static_cast<XrEventDataBuffer*>(host_ptr(arg(1))));
    else if (name == "xrBeginSession")
        result = xrBeginSession((XrSession)arg(0), static_cast<const XrSessionBeginInfo*>(host_ptr(arg(1))));
    else if (name == "xrEndSession") result = xrEndSession((XrSession)arg(0));
    else if (name == "xrWaitFrame")
        result = xrWaitFrame((XrSession)arg(0), static_cast<const XrFrameWaitInfo*>(host_ptr(arg(1))),
                             static_cast<XrFrameState*>(host_ptr(arg(2))));
    else if (name == "xrBeginFrame")
        result = xrBeginFrame((XrSession)arg(0), static_cast<const XrFrameBeginInfo*>(host_ptr(arg(1))));
    else if (name == "xrLocateViews")
        result = xrLocateViews((XrSession)arg(0), static_cast<const XrViewLocateInfo*>(host_ptr(arg(1))),
                               static_cast<XrViewState*>(host_ptr(arg(2))), (uint32_t)arg(3),
                               static_cast<uint32_t*>(host_ptr(arg(4))), static_cast<XrView*>(host_ptr(arg(5))));
    else if (name == "xrAcquireSwapchainImage")
        result = xrAcquireSwapchainImage((XrSwapchain)arg(0), nullptr, static_cast<uint32_t*>(host_ptr(arg(2))));
    else if (name == "xrWaitSwapchainImage")
        result = xrWaitSwapchainImage((XrSwapchain)arg(0), static_cast<const XrSwapchainImageWaitInfo*>(host_ptr(arg(1))));
    else if (name == "xrReleaseSwapchainImage") result = xrReleaseSwapchainImage((XrSwapchain)arg(0), nullptr);
    else if (name == "xrEnumerateSwapchainImages")
        result = call_enumerate_images(arg(0), arg(1), arg(2), arg(3));
    else if (name == "xrEndFrame") result = call_end_frame(arg(0), arg(1));
    else if (name == "xrRequestExitSession") result = xrRequestExitSession((XrSession)arg(0));
    else if (name == "xrDestroySession") result = xrDestroySession((XrSession)arg(0));
    else if (name == "xrDestroyInstance") result = xrDestroyInstance((XrInstance)arg(0));
    else {
        std::fprintf(stderr, "qb-guest: unhandled import %s\n", name.c_str());
        result = XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    cpu.x[0] = (uint64_t)(int64_t)result;
}

static void use_runtime() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = std::strrchr(path, '\\');
    if (!slash) return;
    std::strcpy(slash + 1, "qb_runtime.json");
    SetEnvironmentVariableA("XR_RUNTIME_JSON", path);
    _putenv_s("XR_RUNTIME_JSON", path);
}

int main() {
    use_runtime();
    App app;
    g_app = &app;
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = std::strrchr(path, '\\');
    std::strcpy(slash + 1, "libgrid.so");
    if (!guest_load(app, path, "qb_guest_main")) return 1;
    std::printf("qb-guest: running arm64 main\n");
    std::fflush(stdout);
    bool ok = guest_run(app.cpu, app.mem, thunk, nullptr, 0);
    std::printf("qb-guest: %s\n", ok ? "done" : "stopped");
    return ok ? 0 : 1;
}
