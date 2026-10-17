#pragma once
#include <cstdint>
#include <algorithm>
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
// Per source row, the [lo, hi) pixel range with any coverage (lo >= hi: empty).
inline bool HudRowSpans(const std::vector<std::uint8_t>& source,int width,int height,
    std::vector<int>& lo,std::vector<int>& hi) {
    if(width<=0||height<=0||source.size()!=static_cast<std::size_t>(width)*height*4) return false;
    lo.assign(height,0);hi.assign(height,0);
    for(int y=0;y<height;++y) {
        const auto* row=source.data()+static_cast<std::size_t>(y)*width*4;
        int first=-1,last=-1;
        for(int x=0;x<width;++x) if(row[x*4+3]) {if(first<0) first=x;last=x;}
        if(first>=0) {lo[y]=first;hi[y]=last+1;}
    }
    return true;
}
// Same pixels as ScaleHud followed by BlendHudRow, but sampling the small HUD
// directly and visiting only covered spans (the HUD is mostly transparent).
inline void BlendScaledHud(std::uint8_t* dst,int targetW,int targetH,const std::vector<std::uint8_t>& source,
    int width,int height,bool bottomUp,const std::vector<int>& lo,const std::vector<int>& hi) {
    if(width<=0||height<=0||targetW<=0||targetH<=0||lo.size()!=static_cast<std::size_t>(height)||hi.size()!=lo.size()) return;
    for(int y=0;y<targetH;++y) {
        int sy=static_cast<int>(static_cast<std::int64_t>(y)*height/targetH);
        if(bottomUp) sy=height-1-sy;
        if(lo[sy]>=hi[sy]) continue;
        const auto* row=source.data()+static_cast<std::size_t>(sy)*width*4;
        auto* out=dst+static_cast<std::size_t>(y)*targetW*4;
        // x samples sx = x * width / targetW; cover every x whose sx lies in the span.
        const int x0=static_cast<int>((static_cast<std::int64_t>(lo[sy])*targetW+width-1)/width);
        const int x1=static_cast<int>(std::min<std::int64_t>(targetW,(static_cast<std::int64_t>(hi[sy])*targetW+width-1)/width));
        for(int x=x0;x<x1;++x) {
            const auto* p=row+static_cast<std::size_t>(static_cast<std::int64_t>(x)*width/targetW)*4;
            if(!p[3]) continue;
            auto* d=out+static_cast<std::size_t>(x)*4;const int inv=255-p[3];
            d[0]=static_cast<std::uint8_t>(p[2]+d[0]*inv/255);
            d[1]=static_cast<std::uint8_t>(p[1]+d[1]*inv/255);
            d[2]=static_cast<std::uint8_t>(p[0]+d[2]*inv/255);
            d[3]=static_cast<std::uint8_t>(p[3]+d[3]*inv/255);
        }
    }
}}
