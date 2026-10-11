#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
namespace witchcraft {
inline bool ScaleHud(const std::vector<std::uint8_t>& source,int width,int height,
    bool bottomUp,int targetW,int targetH,std::vector<std::uint8_t>& output) {
    if(width<=0||height<=0||targetW<=0||targetH<=0||
        source.size()!=static_cast<std::size_t>(width)*height*4) {output.clear();return false;}
    output.resize(static_cast<std::size_t>(targetW)*targetH*4);
    std::vector<int> columns(targetW);
    for(int x=0;x<targetW;++x) columns[x]=static_cast<int>(static_cast<std::int64_t>(x)*width/targetW)*4;
    int previous=-1;
    for(int y=0;y<targetH;++y) {
        int sy=static_cast<int>(static_cast<std::int64_t>(y)*height/targetH);
        if(bottomUp) sy=height-1-sy;
        auto* dst=output.data()+static_cast<std::size_t>(y)*targetW*4;
        if(sy==previous) std::memcpy(dst,dst-static_cast<std::size_t>(targetW)*4,static_cast<std::size_t>(targetW)*4);
        else {
            const auto* row=source.data()+static_cast<std::size_t>(sy)*width*4;
            for(int x=0;x<targetW;++x) std::memcpy(dst+x*4,row+columns[x],4);
        }
        previous=sy;
    }
    return true;
}
}
