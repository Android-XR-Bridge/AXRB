#include "windows_gpu_receiver.h"
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
// A layer that arrives as pixels lands in the receiver's host-owned image on
// the receive device and is then copied out on the presentation device, as
// the host does for every layer of a pixel-transfer stack.
int main() {
    ComPtr<ID3D11Device> receiveDevice, presentDevice;
    ComPtr<ID3D11DeviceContext> receiveContext, presentContext;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &receiveDevice, nullptr, &receiveContext)));
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &presentDevice, nullptr, &presentContext)));
    constexpr UINT width = 2, height = 2;
    // One mono RGBA layer, bottom row first (GL readback order).
    const uint8_t mono[width * height * 4] = {
        1, 2, 3, 4,  5, 6, 7, 8,          // bottom row
        9, 10, 11, 12,  13, 14, 15, 16,   // top row
    };
    axrb::host::WindowsGpuReceiver receiver;
    CHECK(receiver.receive_pixels(receiveDevice.Get(), receiveContext.Get(), mono, width, height, 1,
        DXGI_FORMAT_B8G8R8A8_UNORM));
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = width; desc.Height = height; desc.MipLevels = 1;
    desc.ArraySize = 2; desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ComPtr<ID3D11Texture2D> target, staging;
    CHECK(SUCCEEDED(presentDevice->CreateTexture2D(&desc, nullptr, &target)));
    CHECK(receiver.copy_to(presentContext.Get(), target.Get()));
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(presentDevice->CreateTexture2D(&desc, nullptr, &staging)));
    presentContext->CopyResource(staging.Get(), target.Get());
    for (UINT eye = 0; eye < 2; ++eye) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        CHECK(SUCCEEDED(presentContext->Map(staging.Get(), eye, D3D11_MAP_READ, 0, &mapped)));
        const auto* row0 = static_cast<const uint8_t*>(mapped.pData);
        const auto* row1 = row0 + mapped.RowPitch;
        // Top row first, red and blue swapped for the BGRA target.
        const bool ok = row0[0] == 11 && row0[1] == 10 && row0[2] == 9 && row0[3] == 12 &&
            row0[4] == 15 && row1[0] == 3 && row1[2] == 1 && row1[7] == 8;
        presentContext->Unmap(staging.Get(), eye);
        CHECK(ok);
    }
    // A later stereo layer of a new size reuses the same receiver.
    uint8_t stereo[4 * 2 * 4 * 2]{};
    stereo[4 * 2 * 4] = 200; // Right eye, bottom-left red.
    CHECK(receiver.receive_pixels(receiveDevice.Get(), receiveContext.Get(), stereo, 4, 2, 2,
        DXGI_FORMAT_R8G8B8A8_UNORM));
    desc = {}; desc.Width = 4; desc.Height = 2; desc.MipLevels = 1; desc.ArraySize = 2;
    desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    CHECK(SUCCEEDED(presentDevice->CreateTexture2D(&desc, nullptr, &target)));
    CHECK(receiver.copy_to(presentContext.Get(), target.Get()));
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(presentDevice->CreateTexture2D(&desc, nullptr, &staging)));
    presentContext->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(SUCCEEDED(presentContext->Map(staging.Get(), 1, D3D11_MAP_READ, 0, &mapped)));
    const auto* bottom = static_cast<const uint8_t*>(mapped.pData) + mapped.RowPitch;
    const bool right = bottom[0] == 200;
    presentContext->Unmap(staging.Get(), 1);
    CHECK(right);
    std::puts("PASS: pixel layers fill host-owned images with GL row order and target channel order");
    return 0;
}
