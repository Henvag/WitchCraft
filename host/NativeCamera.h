#pragma once
#include "NativeLayout.h"
#include "skycraft_protocol.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    } else {
        result[0]+=static_cast<float>(pose.x-ground[0]);
        result[1]+=static_cast<float>(-pose.z-ground[1]);
        result[2]+=static_cast<float>(pose.y-ground[2]);
    }
    for(int i=0;i<3;++i) if(!std::isfinite(result[i])||std::abs(result[i])>1000000) return false;
    return std::abs(pose.y-ground[2])<=256;
}
// ICamera data returned by CCustomCamera slot 0x10 (RVA 0x1e0f290): +0 position,
// +0x10 three rotation floats, +0x1c has-focus, +0x20 focus, +0x30 FOV, +0x34 near,
// +0x38 far (1000), +0x3c force near plane. Which rotation float is pitch/yaw and
// its sign/zero heading are not verified statically, so AngleLayoutProbe learns
// them from the director's built forward vector before front view is enabled.
inline constexpr std::size_t kCameraAngles=0x10/4;
struct AngleLayout {
    int pitch=-1,yaw=-1;
    float pitchSign=1,yawSign=1,yawOffset=0; // degrees
    bool Valid() const {return pitch>=0&&yaw>=0&&pitch!=yaw;}
};
inline bool AngleForward(const float* angles,const AngleLayout& layout,float* out) {
    if(!layout.Valid()) return false;
    constexpr float radians=0.01745329252f;
    const float pitch=angles[layout.pitch],yaw=angles[layout.yaw];
    if(!std::isfinite(pitch)||!std::isfinite(yaw)||std::abs(pitch)>720||std::abs(yaw)>100000) return false;
    const float p=pitch*layout.pitchSign*radians,y=(yaw*layout.yawSign+layout.yawOffset)*radians;
    out[0]=-std::sin(y)*std::cos(p);out[1]=std::cos(y)*std::cos(p);out[2]=std::sin(p);
    return true;
}
// Locks a layout only after one hypothesis alone explains many varied frames.
class AngleLayoutProbe {
    AngleLayout candidate_{},locked_{};
    unsigned streak_=0;
    float firstYaw_=0;bool yawVaried_=false;
public:
    static constexpr unsigned kRequired=30;
    const AngleLayout& Locked() const {return locked_;}
    bool Ready() const {return locked_.Valid();}
    unsigned Streak() const {return streak_;}
    bool Observe(const float* angles,const float* forward) {
        if(Ready()) return true;
        for(int i=0;i<3;++i) if(!std::isfinite(angles[i])||!std::isfinite(forward[i])) return false;
        const float length=std::hypot(forward[0],forward[1],forward[2]);
        // Level views cannot distinguish pitch from roll; vertical views lack heading.
        if(std::abs(length-1)>.01f||std::abs(forward[2])<.08f||std::hypot(forward[0],forward[1])<.3f) return false;
        AngleLayout match{};unsigned matches=0;
        for(int pitch=0;pitch<3;++pitch) for(int yaw=0;yaw<3;++yaw) if(pitch!=yaw)
        for(float pitchSign:{1.f,-1.f}) for(float yawSign:{1.f,-1.f}) for(float offset:{0.f,90.f,180.f,270.f}) {
            const AngleLayout layout{pitch,yaw,pitchSign,yawSign,offset};float model[3]{};
            if(!AngleForward(angles,layout,model)) continue;
            if(model[0]*forward[0]+model[1]*forward[1]+model[2]*forward[2]>.999f) {match=layout;++matches;}
        }
        if(matches!=1) return false; // Ambiguous or mid-turn frames are not evidence.
        const bool same=streak_&&match.pitch==candidate_.pitch&&match.yaw==candidate_.yaw&&
            match.pitchSign==candidate_.pitchSign&&match.yawSign==candidate_.yawSign&&match.yawOffset==candidate_.yawOffset;
        if(!same) {candidate_=match;streak_=0;firstYaw_=angles[match.yaw];yawVaried_=false;}
        ++streak_;
        if(std::abs(angles[match.yaw]-firstYaw_)>10) yawVaried_=true;
        if(streak_>=kRequired&&yawVaried_) locked_=candidate_;
        return Ready();
    }
};
// Minecraft's front F5 view: from the eye, Minecraft's camera distance along the
// look direction, turned to face back at the player (yaw + 180, pitch mirrored).
inline bool FrontCamera(const skycraft::proto::McState& pose,const float* angles,const AngleLayout& layout,
    float* position,float* outAngles,float* lookForward) {
    if(!std::isfinite(pose.x)||!std::isfinite(pose.y)||!std::isfinite(pose.z)||
        !std::isfinite(pose.eyeHeight)||pose.eyeHeight<0||pose.eyeHeight>3) return false;
    if(!AngleForward(angles,layout,lookForward)) return false;
    const float distance=std::isfinite(pose.cameraDistance)&&pose.cameraDistance>.3f&&pose.cameraDistance<=8?pose.cameraDistance:4;
    const float eye[]{static_cast<float>(pose.x),static_cast<float>(-pose.z),static_cast<float>(pose.y+pose.eyeHeight)};
    for(int i=0;i<3;++i) {
        position[i]=eye[i]+lookForward[i]*distance;outAngles[i]=angles[i];
        if(!std::isfinite(position[i])||std::abs(position[i])>1000000) return false;
    }
    outAngles[layout.yaw]+=180;outAngles[layout.pitch]=-outAngles[layout.pitch];
    return true;
}
bool CameraReady();
// True once the rotation layout is verified, so Minecraft's front view can be shown.
bool FrontViewReady();
// The flipped camera forward (Witcher coordinates) most recently produced by the
// front view, if one was produced within maxAgeMs.
bool FrontViewForward(float* forward,std::uint64_t now,std::uint64_t maxAgeMs=250);
void PrepareCamera(const Reader&,std::uintptr_t,const Sample*,void(*)(const char*));
}
