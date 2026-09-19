// Diagnostic snapshot of AXRB's own exported GPU texture; never changes it.
#include <d3d11_1.h>
#include <wrl/client.h>
#include <fstream>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(result))return 3;
    ComPtr<ID3D11Device1> d1;device.As(&d1);ComPtr<ID3D11Texture2D> source,stage;
    result=d1->OpenSharedResourceByName(argv[1],DXGI_SHARED_RESOURCE_READ,IID_PPV_ARGS(&source));
    if(FAILED(result)){std::printf("open %lx\n",result);return 4;}
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    std::printf("%ux%u format=%u\n",desc.Width,desc.Height,desc.Format);
    if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)return 5;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&stage)))return 6;
    context->CopyResource(stage.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped)))return 7;
    std::ofstream output(argv[2],std::ios::binary);output<<"P6\n"<<desc.Width<<" "<<desc.Height<<"\n255\n";
    unsigned long long transparent=0,opaque=0;
    unsigned minAlpha=255,maxAlpha=0;
    for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){
        const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
        output.write(reinterpret_cast<const char*>(pixel),3);
        const unsigned alpha=pixel[3];transparent+=alpha==0;opaque+=alpha==255;
        if(alpha<minAlpha)minAlpha=alpha;if(alpha>maxAlpha)maxAlpha=alpha;
    }
    std::printf("alpha min=%u max=%u transparent=%llu opaque=%llu total=%llu\n",minAlpha,maxAlpha,transparent,opaque,static_cast<unsigned long long>(desc.Width)*desc.Height);
    context->Unmap(stage.Get(),0);return output?0:8;
}
