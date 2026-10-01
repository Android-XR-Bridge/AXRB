#include "layer_color_renderer.h"
#include "windows_gpu_receiver.h"
#include "gpu_frame_packet.h"
#include <cstdio>
#include <limits>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace axrb::protocol;
static bool test_shared_mono_quads(bool sourceSrgb, bool targetSrgb) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=sourceSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
    uint8_t raw[64];for(int i=0;i<64;i+=4){raw[i]=64;raw[i+1]=128;raw[i+2]=192;raw[i+3]=128;}
    D3D11_SUBRESOURCE_DATA init{raw,16,64};ComPtr<ID3D11Texture2D> source;
    if(FAILED(device->CreateTexture2D(&d,&init,&source)))return false;
    // Publish initialized pixels before opening a second alias of the resource,
    // matching the producer/receiver synchronization in the real transport.
    axrb::host::GpuCompletion sourceReady;
    if(!sourceReady.wait(device.Get(),context.Get()))return false;
    WindowsGpuFrame frame{0xc010000000000000ull+GetCurrentProcessId(),{sourceSrgb ? 43u : 37u,0}};
    wchar_t name[96];swprintf_s(name,L"Local\\AXRB_GPU_%016llx_0",frame.session);
    ComPtr<IDXGIResource1> resource;HANDLE handle=nullptr;
    if(FAILED(source.As(&resource)) || FAILED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,name,&handle)))return false;
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{handle};
    axrb::host::WindowsGpuReceiver receiver;axrb::host::LayerColorRenderer renderer;
    d.ArraySize=2;d.MiscFlags=0;d.BindFlags=0;
    d.Format=targetSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D11Texture2D> target,readback;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&target)))return false;
    d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&readback)))return false;
    for(int mode=0;mode<3;++mode) {
        ImageProjection p;p.view_count=kQuadCompositionBit|2;
        p.quads[0].layer_flags=p.quads[1].layer_flags=6;
        if(mode!=1){p.colors[0].scale[0]=.5f;p.colors[1].bias[1]=.1f;}
        if(!receiver.receive(device.Get(),context.Get(),frame,mode+1,4,4,d.Format,&renderer,&p)) {std::printf("mono receive failed mode=%d\n",mode);return false;}
        if(!receiver.copy_to(context.Get(),target.Get())) {std::printf("mono copy failed mode=%d\n",mode);return false;}
        context->CopyResource(readback.Get(),target.Get());
        for(UINT eye=0;eye<2;++eye) {
            D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(context->Map(readback.Get(),eye,D3D11_MAP_READ,0,&m)))return false;
            float expected[4];for(int i=0;i<4;++i)expected[i]=raw[i]/255.f;
            if(sourceSrgb)for(int i=0;i<3;++i)expected[i]=expected[i]<=.04045f ? expected[i]/12.92f : std::pow((expected[i]+.055f)/1.055f,2.4f);
            apply_layer_color(expected,p.colors[eye],6);
            if(targetSrgb)for(int i=0;i<3;++i)expected[i]=expected[i]<=.0031308f ? expected[i]*12.92f : 1.055f*std::pow(expected[i],1.f/2.4f)-.055f;
            bool ok=true;
            for(int y=0;y<4;++y)for(int x=0;x<4;++x)for(int ch=0;ch<4;++ch)
                if(std::abs(int(static_cast<uint8_t*>(m.pData)[y*m.RowPitch+x*4+ch])-int(expected[ch]*255+.5f))>1) {
                    if(x==0 && y==0)std::printf("mono mismatch mode=%d eye=%u ch=%d got=%u want=%d\n",mode,eye,ch,static_cast<uint8_t*>(m.pData)[ch],int(expected[ch]*255+.5f));ok=false;
                }
            context->Unmap(readback.Get(),eye);if(!ok)return false;
        }
    }
    return true;
}
int main() {
    LayerColor identity;
    if(!identity.identity() || !identity.valid()) return 1;
    LayerColor invalid;invalid.bias[1]=std::numeric_limits<float>::quiet_NaN();
    if(invalid.valid()) return 2;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) return 3;
    axrb::host::LayerColorRenderer renderer;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=4;desc.Height=4;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    uint8_t raw[64];for(int i=0;i<64;i+=4){raw[i]=64;raw[i+1]=128;raw[i+2]=192;raw[i+3]=128;}
    D3D11_SUBRESOURCE_DATA init{raw,16,64};ComPtr<ID3D11Texture2D> source,target,readback;
    if(FAILED(device->CreateTexture2D(&desc,&init,&source))) return 4;
    desc.ArraySize=2;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&target))) return 5;
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&readback))) return 6;
    for(bool targetSrgb : {false,true}) {
    desc.Format=targetSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
    target.Reset();readback.Reset();
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&target)))return 12;
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&readback)))return 13;
    for(uint32_t flags : {0u,2u,6u}) for(bool srgb : {false,true}) for(int mode=0;mode<3;++mode) {
        LayerColor c;
        if(mode==1) {c.scale[0]=c.scale[1]=c.scale[2]=0;}
        if(mode==2) {c.scale[0]=.5f;c.scale[3]=.75f;c.bias[1]=.1f;}
        if(!renderer.render(device.Get(),context.Get(),source.Get(),target.Get(),1,c,flags,srgb)) return 7;
        context->CopyResource(readback.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(readback.Get(),1,D3D11_MAP_READ,0,&mapped))) return 8;
        float expected[4];for(int i=0;i<4;++i)expected[i]=raw[i]/255.f;
        auto decode=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
        auto encode=[](float v){v=(std::max)(0.f,v);return v<=.0031308f?v*12.92f:1.055f*std::pow(v,1.f/2.4f)-.055f;};
        if(srgb)for(int i=0;i<3;++i)expected[i]=decode(expected[i]);
        apply_layer_color(expected,c,flags);
        if(targetSrgb)for(int i=0;i<3;++i)expected[i]=encode(expected[i]);
        bool ok=true;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)for(int ch=0;ch<4;++ch) {
            int want=int((std::max)(0.f,(std::min)(1.f,expected[ch]))*255+.5f);
            int actual=static_cast<uint8_t*>(mapped.pData)[y*mapped.RowPitch+x*4+ch];
            if(std::abs(actual-want)>1) {std::printf("flags=%u srgb=%d mode=%d ch=%d got=%d want=%d\n",flags,srgb,mode,ch,actual,want);ok=false;}
        }
        context->Unmap(readback.Get(),1);if(!ok)return 9;
    }
    }
    // Legacy packet decoding must supply identity instead of reading GPU bytes as color.
    uint8_t legacy[kLegacyGpuBatchPartBytes]{};
    ImageFrameHeader h;h.header_size=64+96;
    ImageProjection p;p.view_count=2;
    WindowsGpuFrame g{123,{37,37}};
    std::memcpy(legacy,&h,64);std::memcpy(legacy+64,&p,96);std::memcpy(legacy+160,&g,16);
    auto decoded=decode_gpu_batch_part(legacy,sizeof(legacy),0);
    if(decoded.gpu.session!=123 || !decoded.projection.colors[0].identity() || !decoded.projection.colors[1].identity())return 10;
    for(bool sourceSrgb : {false,true})for(bool targetSrgb : {false,true})
        if(!test_shared_mono_quads(sourceSrgb,targetSrgb))return 11;
    std::puts("PASS: GPU fade/identity/bias, stereo target slice, alpha modes, linear/sRGB, finite validation, legacy metadata, distinct colors on mono packed quads and identity transitions");
    return 0;
}
