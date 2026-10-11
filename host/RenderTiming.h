#pragma once
#include "skycraft_protocol.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace witchcraft {
// Same previous/current physics-tick interpolation SkyCraft supplies for Skyrim.
// Only the rendered pose changes; control always uses authoritative raw ticks.
inline skycraft::proto::McState RenderMcState(const skycraft::proto::McState& mc,
    std::int64_t now,std::int64_t frequency) {
    auto result=mc;
    if(frequency<=0||mc.tickQpc<=0||!std::isfinite(mc.tickMs)||mc.tickMs<1||mc.tickMs>1000||
        (mc.flags&skycraft::proto::kMcScreenOpen)) return result;
    const double elapsed=(static_cast<double>(now)-static_cast<double>(mc.tickQpc))*1000/frequency;
    if(!std::isfinite(elapsed)||elapsed>mc.tickMs*2) return result;
    const double previous[]{mc.prevX,mc.prevY,mc.prevZ},current[]{mc.curX,mc.curY,mc.curZ};
    double delta=0;
    for(unsigned i=0;i<3;++i) {
        if(!std::isfinite(previous[i])||!std::isfinite(current[i])) return result;
        delta+=std::pow(current[i]-previous[i],2);
    }
    if(delta>16) return result; // Teleports are discontinuities, never tweened.
    const double alpha=std::clamp(elapsed/mc.tickMs,0.0,1.0);
    result.x=mc.prevX+(mc.curX-mc.prevX)*alpha;
    result.y=mc.prevY+(mc.curY-mc.prevY)*alpha;
    result.z=mc.prevZ+(mc.curZ-mc.prevZ)*alpha;
    return result;
}
}
