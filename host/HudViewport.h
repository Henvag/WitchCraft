#pragma once
#include <algorithm>
#include <cstdint>
namespace witchcraft {
struct HudViewport {int width=0,height=0;};
inline HudViewport TransferViewport(int width,int height,bool fullResolution=false) {
    if(width<=0||height<=0) return {};
    if(fullResolution||(width<=1280&&height<=720)) return {width,height};
    const double scale=std::min(1280.0/width,720.0/height);
    return {std::max(1,static_cast<int>(width*scale)),std::max(1,static_cast<int>(height*scale))};
}
inline int HudCoordinate(int coordinate,int sourceExtent,int destinationExtent) {
    if(sourceExtent<=0||destinationExtent<=0) return 0;
    return static_cast<int>(static_cast<std::int64_t>(std::clamp(coordinate,0,sourceExtent-1))*destinationExtent/sourceExtent);
}
}
