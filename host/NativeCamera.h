#pragma once
#include "NativeLayout.h"
#include "skycraft_protocol.h"
namespace witchcraft::native {
// Camera data uses Witcher's X/Y horizontal, Z up coordinates. Preserve its
// rotation and lens; translation is changed before the director rebuilds its
// render transform, rather than writing only the reported camera position.
inline bool CameraPosition(const skycraft::proto::McState& pose,const float* ground,
    const float* original,float* result) {
    for(int i=0;i<3;++i) if(!std::isfinite(original[i])||!std::isfinite(ground[i])) return false;
    if(!std::isfinite(pose.x)||!std::isfinite(pose.y)||!std::isfinite(pose.z)||
        pose.cameraMode>2) return false;
    if(pose.cameraMode==0&&(!std::isfinite(pose.eyeHeight)||pose.eyeHeight<0||pose.eyeHeight>3)) return false;
    for(int i=0;i<3;++i) result[i]=original[i];
    if(pose.cameraMode==0) {
        result[0]=static_cast<float>(pose.x);result[1]=static_cast<float>(-pose.z);
        result[2]=static_cast<float>(pose.y+pose.eyeHeight);
    } else if(pose.flags&skycraft::proto::kMcCanFly) {
        result[2]+=static_cast<float>(pose.y-ground[2]);
    }
    for(int i=0;i<3;++i) if(!std::isfinite(result[i])||std::abs(result[i])>1000000) return false;
    return std::abs(pose.y-ground[2])<=256;
}
bool CameraReady();
void PrepareCamera(const Reader&,std::uintptr_t,const Sample*,void(*)(const char*));
}
