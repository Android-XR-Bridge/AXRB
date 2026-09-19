#include "quad_renderer.h"
#if defined(_WIN32)
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
namespace axrb::host {
namespace {
const char* shader = R"(
cbuffer Params : register(b0) {
 float4 inverseCamera;
 float4 panelOrientation;
 float4 delta;
 float4 tangents;
 float4 dimensions;
 float4 boundsFlags;
};
Texture2DArray panel : register(t0);
SamplerState linearSampler : register(s0);
float3 rotate(float4 q, float3 v) { return v + 2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vsMain(uint id : SV_VertexID) {
 const float2 uv[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
 Vertex o; o.uv=uv[id];
 float3 p=rotate(inverseCamera,rotate(panelOrientation,float3((o.uv*float2(2,-2)+float2(-1,1))*dimensions.xy*.5,0))+delta.xyz);
 o.position=float4((2*p.x+(tangents.x+tangents.y)*p.z)/(tangents.y-tangents.x),
                   (2*p.y+(tangents.z+tangents.w)*p.z)/(tangents.z-tangents.w),-p.z-.01,-p.z);
 return o;
}
float4 psMain(Vertex v) : SV_Target {
 float2 uv=clamp(v.uv*dimensions.zw,boundsFlags.xy,dimensions.zw-boundsFlags.xy);
 float4 color=panel.Sample(linearSampler,float3(uv,0));
 if(boundsFlags.z==0) color.a=1;
 else if(boundsFlags.w!=0) color.rgb*=color.a;
 return color;
}
)";
const char* projectionShader = R"(
cbuffer Params : register(b0) {
 float4 targetOrientation;
 float4 inverseSourceOrientation;
 float4 targetTangents;
 float4 sourceTangents;
 float4 textureScale;
 float4 flags;
};
Texture2DArray scene : register(t0);
SamplerState linearSampler : register(s0);
float3 rotate(float4 q, float3 v) { return v + 2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex projectionVsMain(uint id : SV_VertexID) {
 Vertex v; v.uv=float2((id<<1)&2,id&2); v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 projectionPsMain(Vertex input) : SV_Target {
 float3 ray=float3(lerp(targetTangents.x,targetTangents.y,input.uv.x),
                  lerp(targetTangents.z,targetTangents.w,input.uv.y),-1);
 ray=rotate(inverseSourceOrientation,rotate(targetOrientation,ray));
 if(ray.z>=0) discard;
 float2 tangent=ray.xy/-ray.z;
 if(tangent.x<sourceTangents.x || tangent.x>sourceTangents.y ||
    tangent.y>sourceTangents.z || tangent.y<sourceTangents.w) discard;
 float2 uv=float2((tangent.x-sourceTangents.x)/(sourceTangents.y-sourceTangents.x),
                  (sourceTangents.z-tangent.y)/(sourceTangents.z-sourceTangents.w));
 uv=clamp(uv*textureScale.xy,textureScale.zw,textureScale.xy-textureScale.zw);
 float4 color=scene.Sample(linearSampler,float3(uv,0));
 if(flags.x==0) color.a=1;
 else if(flags.x==2) color.rgb*=color.a;
 return color;
}
)";
}

bool QuadRenderer::initialize_common(ID3D11Device* device) {
    if (!sampler_) {
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler,&sampler_))) return false;
    }
    if (!rasterizer_) {
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        if (FAILED(device->CreateRasterizerState(&raster,&rasterizer_))) return false;
    }
    if (!blend_) {
        D3D11_BLEND_DESC blend{}; auto& b=blend.RenderTarget[0]; b.BlendEnable=TRUE;
        b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE;
        b.DestBlend=b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend,&blend_))) return false;
    }
    return true;
}

namespace {
void bind_target(ID3D11DeviceContext* context, ID3D11RenderTargetView* output,
                 ID3D11RasterizerState* rasterizer, ID3D11BlendState* blend,
                 uint32_t width, uint32_t height) {
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1}; context->RSSetViewports(1,&viewport);
    context->RSSetState(rasterizer); context->OMSetRenderTargets(1,&output,nullptr);
    context->OMSetBlendState(blend,nullptr,~0u); context->OMSetDepthStencilState(nullptr,0);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void unbind_target(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* empty=nullptr; context->PSSetShaderResources(0,1,&empty);
    context->OMSetRenderTargets(0,nullptr,nullptr);
}
}

bool QuadRenderer::render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
    uint32_t slice, XrExtent2Di extent, DXGI_FORMAT format, const protocol::ImageQuad& quad,
    const XrView& view, ID3D11Texture2D* target, uint32_t eye, uint32_t width, uint32_t height) {
    using Microsoft::WRL::ComPtr;
    if ((quad.eye_visibility==1 && eye==1) || (quad.eye_visibility==2 && eye==0)) return true;
    if (!vertex_) {
        ComPtr<ID3DBlob> vs, ps;
        if (FAILED(D3DCompile(shader,std::strlen(shader),"quad",nullptr,nullptr,"vsMain","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,nullptr)) ||
            FAILED(D3DCompile(shader,std::strlen(shader),"quad",nullptr,nullptr,"psMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,nullptr))) return false;
        if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_))) return false;
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=96; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer,nullptr,&constants_))) return false;
        if (!initialize_common(device)) return false;
        if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_))) return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels=1; srv.Texture2DArray.FirstArraySlice=slice; srv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11ShaderResourceView> input; if (FAILED(device->CreateShaderResourceView(source,&srv,&input))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format=format; rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.FirstArraySlice=eye; rtv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11RenderTargetView> output; if (FAILED(device->CreateRenderTargetView(target,&rtv,&output))) return false;
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    const auto& q=quad.pose; const auto& c=view.pose;
    float values[24]={-c.orientation.x,-c.orientation.y,-c.orientation.z,c.orientation.w,
        q.qx,q.qy,q.qz,q.qw,q.x-c.position.x,q.y-c.position.y,q.z-c.position.z,0,
        std::tan(view.fov.angleLeft),std::tan(view.fov.angleRight),std::tan(view.fov.angleUp),std::tan(view.fov.angleDown),
        quad.width,quad.height,float(extent.width)/desc.Width,float(extent.height)/desc.Height,
        .5f/desc.Width,.5f/desc.Height,(quad.layer_flags&2u)?1.f:0.f,(quad.layer_flags&4u)?1.f:0.f};
    context->UpdateSubresource(constants_.Get(),0,nullptr,values,0,0);
    bind_target(context, output.Get(), rasterizer_.Get(), blend_.Get(), width, height);
    context->VSSetShader(vertex_.Get(),nullptr,0); context->PSSetShader(pixel_.Get(),nullptr,0);
    context->VSSetConstantBuffers(0,1,constants_.GetAddressOf()); context->PSSetConstantBuffers(0,1,constants_.GetAddressOf());
    context->PSSetSamplers(0,1,sampler_.GetAddressOf()); context->PSSetShaderResources(0,1,input.GetAddressOf());
    context->Draw(6,0);
    unbind_target(context);
    return true;
}

bool QuadRenderer::render_projection(
    ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
    uint32_t slice, XrExtent2Di extent, DXGI_FORMAT format,
    const protocol::ImageProjectionView& sourceView, uint32_t layerFlags,
    const XrView& targetView, ID3D11Texture2D* target, uint32_t eye,
    uint32_t width, uint32_t height) {
    using Microsoft::WRL::ComPtr;
    if (!projectionVertex_) {
        ComPtr<ID3DBlob> vs, ps;
        if (FAILED(D3DCompile(projectionShader,std::strlen(projectionShader),"projection",nullptr,nullptr,
                "projectionVsMain","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,nullptr)) ||
            FAILED(D3DCompile(projectionShader,std::strlen(projectionShader),"projection",nullptr,nullptr,
                "projectionPsMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,nullptr))) return false;
        if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&projectionPixel_))) return false;
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=96; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer,nullptr,&projectionConstants_)) ||
            !initialize_common(device) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&projectionVertex_))) return false;
    }
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels=1; srv.Texture2DArray.FirstArraySlice=desc.ArraySize==1 ? 0 : slice; srv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11ShaderResourceView> input;
    if (FAILED(device->CreateShaderResourceView(source,&srv,&input))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format=format; rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.FirstArraySlice=eye; rtv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11RenderTargetView> output;
    if (FAILED(device->CreateRenderTargetView(target,&rtv,&output))) return false;
    const auto& sourcePose=sourceView.pose;
    const auto& targetPose=targetView.pose;
    const float alphaMode=(layerFlags&2u) ? ((layerFlags&4u) ? 2.f : 1.f) : 0.f;
    float values[24]={
        targetPose.orientation.x,targetPose.orientation.y,targetPose.orientation.z,targetPose.orientation.w,
        -sourcePose.qx,-sourcePose.qy,-sourcePose.qz,sourcePose.qw,
        std::tan(targetView.fov.angleLeft),std::tan(targetView.fov.angleRight),
        std::tan(targetView.fov.angleUp),std::tan(targetView.fov.angleDown),
        std::tan(sourceView.angle_left),std::tan(sourceView.angle_right),
        std::tan(sourceView.angle_up),std::tan(sourceView.angle_down),
        float(extent.width)/desc.Width,float(extent.height)/desc.Height,
        .5f/desc.Width,.5f/desc.Height,
        alphaMode,0,0,0};
    context->UpdateSubresource(projectionConstants_.Get(),0,nullptr,values,0,0);
    bind_target(context,output.Get(),rasterizer_.Get(),blend_.Get(),width,height);
    context->VSSetShader(projectionVertex_.Get(),nullptr,0); context->PSSetShader(projectionPixel_.Get(),nullptr,0);
    context->VSSetConstantBuffers(0,1,projectionConstants_.GetAddressOf());
    context->PSSetConstantBuffers(0,1,projectionConstants_.GetAddressOf());
    context->PSSetSamplers(0,1,sampler_.GetAddressOf()); context->PSSetShaderResources(0,1,input.GetAddressOf());
    context->Draw(3,0);
    unbind_target(context);
    return true;
}
}
#endif
