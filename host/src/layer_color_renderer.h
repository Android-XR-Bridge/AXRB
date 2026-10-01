#pragma once
#if defined(_WIN32)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include "layer_color.h"
namespace axrb::host {
// Receive-thread-only GPU transform. Identity layers take the existing copy path.
class LayerColorRenderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11RasterizerState> raster_;
    bool initialize(ID3D11Device* device) {
        if (ps_) return true;
        const char* code=R"(
cbuffer Constants : register(b0) { float4 scale; float4 bias; uint flags; uint srgb; uint2 padding; };
Texture2D<float4> source : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, 0.0, 1.0);
}
float3 decodeSRGB(float3 v) { return float3(v.x<=0.04045 ? v.x/12.92 : pow((v.x+0.055)/1.055,2.4),v.y<=0.04045 ? v.y/12.92 : pow((v.y+0.055)/1.055,2.4),v.z<=0.04045 ? v.z/12.92 : pow((v.z+0.055)/1.055,2.4)); }
float3 encodeSRGB(float3 v) { v=max(v,0); return float3(v.x<=0.0031308 ? v.x*12.92 : 1.055*pow(v.x,1.0/2.4)-0.055,v.y<=0.0031308 ? v.y*12.92 : 1.055*pow(v.y,1.0/2.4)-0.055,v.z<=0.0031308 ? v.z*12.92 : 1.055*pow(v.z,1.0/2.4)-0.055); }
float4 ps(float4 position : SV_Position) : SV_Target {
    float4 c=source.Load(int3(int2(position.xy),0));
    if((srgb&1)!=0) c.rgb=decodeSRGB(c.rgb);
    bool blend=(flags&2)!=0, premult=blend && (flags&4)==0;
    if(!blend) c.a=1.0;
    if(premult) c.rgb=c.a>0 ? c.rgb/c.a : 0;
    c=c*scale+bias;
    if(premult) c.rgb*=c.a;
    if((srgb&2)!=0) c.rgb=encodeSRGB(c.rgb);
    return c;
})";
        Ptr<ID3DBlob> vertex,pixel,error;
        if (FAILED(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error)) ||
            FAILED(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error))) return false;
        if (FAILED(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_)) ||
            FAILED(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_))) return false;
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=48; buffer.Usage=D3D11_USAGE_DEFAULT; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer,nullptr,&constants_))) { ps_.Reset(); return false; }
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        if (FAILED(device->CreateRasterizerState(&raster,&raster_))) { ps_.Reset(); return false; }
        return true;
    }
public:
    bool render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
                ID3D11Texture2D* target, UINT slice, const protocol::LayerColor& color, uint32_t flags, bool srgb) {
        if (!color.valid() || !initialize(device)) return false;
        D3D11_TEXTURE2D_DESC src{},dst{}; source->GetDesc(&src);target->GetDesc(&dst);
        if (slice>=dst.ArraySize || src.ArraySize!=1 || src.Width!=dst.Width || src.Height!=dst.Height) return false;
        // Typed sRGB views do their conversion in hardware; UNORM shared
        // resources carrying Vulkan sRGB bytes need explicit conversion.
        auto isSrgb=[](DXGI_FORMAT f) { return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; };
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=src.Format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=dst.Format;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.FirstArraySlice=slice;rtv.Texture2DArray.ArraySize=1;
        Ptr<ID3D11ShaderResourceView> input;Ptr<ID3D11RenderTargetView> output;
        if(FAILED(device->CreateShaderResourceView(source,&srv,&input)) || FAILED(device->CreateRenderTargetView(target,&rtv,&output))) return false;
        // Shader output is linear. An sRGB RTV encodes it automatically;
        // a UNORM RTV must retain linear values, regardless of source encoding.
        const uint32_t conversion=srgb && !isSrgb(src.Format) ? 1u:0u;
        struct Constants { protocol::LayerColor color; uint32_t flags,srgb,padding[2]; } values{color,flags,conversion,{}};
        context->UpdateSubresource(constants_.Get(),0,nullptr,&values,0,0);
        D3D11_VIEWPORT viewport{0,0,float(src.Width),float(src.Height),0,1};
        context->RSSetViewports(1,&viewport);context->RSSetState(raster_.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(ps_.Get(),nullptr,0);
        auto* cb=constants_.Get();context->PSSetConstantBuffers(0,1,&cb);
        auto* in=input.Get();context->PSSetShaderResources(0,1,&in);
        auto* out=output.Get();context->OMSetRenderTargets(1,&out,nullptr);context->OMSetBlendState(nullptr,nullptr,~0u);
        context->OMSetDepthStencilState(nullptr,0);context->Draw(3,0);
        in=nullptr;context->PSSetShaderResources(0,1,&in);context->OMSetRenderTargets(0,nullptr,nullptr);
        return true;
    }
};
}
#endif
