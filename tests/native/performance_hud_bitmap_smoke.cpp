#include "performance_hud_bitmap.h"
#include <fstream>
#include <cassert>
int main(int argc,char** argv) {
    axrb::host::PerformanceSummary stats;
    stats.fresh.fps=45;stats.fresh.averageMs=22.22;stats.fresh.worstMs=35;
    stats.fresh.low1Ready=true;stats.fresh.low1Fps=28;stats.fresh.ageMs=11;
    stats.received.fps=45;stats.host.fps=90;stats.repeats.fps=45;
    stats.width=2040;stats.height=2080;stats.targetHz=90;stats.gpuShared=true;
    stats.fresh.graph.assign(240,22.22);
    auto pixels=axrb::host::performance_hud_bitmap(stats,"ANDROID CPU 40% of 6 cores (2.4 cores busy) / 2s","THREADS (1 core=100%): UnityMain 93%  Thread-7 36%  Job.Worker 1 28%");
    assert(pixels.size()==axrb::host::performanceHudWidth*axrb::host::performanceHudHeight*4);
    size_t white=0;for(size_t i=0;i<pixels.size();i+=4){assert(pixels[i+3]==255);if(pixels[i]>220 && pixels[i+1]>220 && pixels[i+2]>220)++white;}
    assert(white>1000);
    if(argc>1) {
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=DWORD(file.bfOffBits+pixels.size());
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=axrb::host::performanceHudWidth;info.biHeight=-axrb::host::performanceHudHeight;info.biPlanes=1;info.biBitCount=32;
        std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
        assert(out.good());
    }
    std::puts("PASS: HUD bitmap size, opaque pixels and visible text");
}
