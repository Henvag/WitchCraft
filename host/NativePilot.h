#pragma once
#include "NativeLayout.h"
#include "GameBridge.h"
#include "RenderTiming.h"
namespace witchcraft::native {
inline constexpr std::uint16_t kPilotHideReason=0x10;
enum class PilotVisibility { Retain, Acquire, Reapply, GameOwned };
inline PilotVisibility PilotVisibilityFor(bool owned,std::uint16_t reasons) {
    if(owned) return reasons&kPilotHideReason?PilotVisibility::Retain:PilotVisibility::Reapply;
    return reasons?PilotVisibility::GameOwned:PilotVisibility::Acquire;
}
inline bool PilotCommandFresh(const PilotCommand& command,std::uint64_t now) {
    // Both camera and anchor use the same bounded grace period for a late
    // overlay frame. A stopped publisher must never keep driving the player.
    return command.publishedAt&&command.publishedAt<=now&&now-command.publishedAt<=1000;
}
class PilotRetryCadence {
    unsigned failures_=0;
    std::uint64_t next_=0;
public:
    void Reset() {failures_=0;next_=0;}
    bool Due(std::uint64_t now) const {return now>=next_;}
    void Completed(bool accepted,std::uint64_t now) {
        if(accepted) {Reset();return;}
        failures_=std::min(failures_+1,4u);
        next_=now+std::min(100u,16u<<(failures_-1));
    }
};
inline bool UseLatestFollowState(PilotCommand& command,const skycraft::proto::McState& latest) {
    using namespace skycraft::proto;
    if(!command.groundFollow||!command.interpolateFollow||
        !(latest.flags&kMcInWorld)||(latest.flags&kMcDead)||
        ((latest.flags&kMcFlying)&&!command.cameraOwned)||
        latest.teleportAck!=command.followState.teleportAck||
        latest.frameCounter<command.followState.frameCounter||
        latest.tickQpc<command.followState.tickQpc) return false;
    const double positions[]{latest.x,latest.y,latest.z,latest.prevX,latest.prevY,latest.prevZ,
        latest.curX,latest.curY,latest.curZ};
    for(const auto value:positions) if(!std::isfinite(value)||std::abs(value)>1000000) return false;
    command.followState=latest;
    return true;
}
inline void ResolvePilotFeet(const PilotCommand& command,std::int64_t now,
    std::int64_t frequency,double* target,double leadMs=0) {
    for(int i=0;i<3;++i) target[i]=command.feet[i];
    if(command.groundFollow&&command.interpolateFollow) {
        // Lead only within Minecraft's already collision-resolved tick segment.
        // RenderMcState clamps to the known endpoint, including during stops.
        const auto lead=std::isfinite(leadMs)&&leadMs>0&&leadMs<=20&&frequency>0?
            static_cast<std::int64_t>(frequency*(leadMs/1000)):0;
        const auto visual=RenderMcState(command.followState,now+lead,frequency);
        target[0]=visual.x;target[2]=visual.z;
    }
}
enum class PilotAck { Idle, Waiting, Confirmed, TimedOut };
inline double GroundSnapLimit(const PilotCommand& command) {
    // Creative flight owns camera altitude independently. Native ground/water
    // transitions can move the hidden anchor vertically without moving MC.
    return command.cameraOwned&&(command.followState.flags&skycraft::proto::kMcFlying)?4.0:.5;
}
// Engine movement may become visible on a later observer callback. Permit only
// one outstanding request and count it only after a subsequent position sample.
class PilotMotion {
    bool pending_=false;
    bool groundFollow_=false;
    float target_[3]{};
    std::uint64_t started_=0;
    double minimumError_=0;
    double groundSnapLimit_=.5;
    double positionTolerance_=.05;
public:
    void Reset() { pending_=false; }
    bool Begin(const float* target,std::uint64_t now,bool groundFollow=false,double groundSnapLimit=.5,double positionTolerance=.05) {
        if(pending_) return false;
        if(!std::isfinite(groundSnapLimit)||groundSnapLimit<.5||groundSnapLimit>4) return false;
        if(!std::isfinite(positionTolerance)||positionTolerance<.05||positionTolerance>.1) return false;
        for(int i=0;i<3;++i) if(!std::isfinite(target[i])) return false;
        for(int i=0;i<3;++i) target_[i]=target[i];
        started_=now;pending_=true;groundFollow_=groundFollow;groundSnapLimit_=groundSnapLimit;
        positionTolerance_=positionTolerance;
        minimumError_=std::numeric_limits<double>::infinity();return true;
    }
    double MinimumError() const {return minimumError_;}
    PilotAck Observe(const float* actual,std::uint64_t now) {
        if(!pending_) return PilotAck::Idle;
        double error=0;
        for(int i=0;i<3;++i) {
            if(!std::isfinite(actual[i])) {pending_=false;return PilotAck::TimedOut;}
            const double delta=actual[i]-target_[i];
            // Native axes X/Y are horizontal. Z is ground height, which may
            // legitimately change as the horizontal request crosses a slope.
            if(groundFollow_&&i==2) {
                if(std::abs(delta)>groundSnapLimit_) {pending_=false;return PilotAck::TimedOut;}
            } else error+=delta*delta;
        }
        minimumError_=std::fmin(minimumError_,std::sqrt(error));
        if(now-started_>250) {pending_=false;return PilotAck::TimedOut;}
        if(error<=positionTolerance_*positionTolerance_) {pending_=false;return PilotAck::Confirmed;}
        return PilotAck::Waiting;
    }
};
inline bool PilotPosition(const float* current,const double* mc,float* out) {
    const double converted[]{mc[0],-mc[2],mc[1]}; double delta=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(converted[i])||std::abs(converted[i])>1000000||!std::isfinite(current[i])) return false;
        delta+=std::pow(converted[i]-current[i],2); out[i]=static_cast<float>(converted[i]);
    }
    out[3]=1; return delta<=0.5625;
}
// Play mode catches up using bounded engine steps. Camera translation is owned
// by Minecraft, so an old native animation target does not gate the next step.
inline bool PilotFollowPosition(const float* current,const double* mc,float* out,double maximumDistance=2,double maximumStep=.5) {
    const double converted[]{mc[0],-mc[2],mc[1]};double distance=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(current[i])||!std::isfinite(converted[i])||std::abs(converted[i])>1000000) return false;
        distance+=std::pow(converted[i]-current[i],2);
    }
    if(!std::isfinite(maximumDistance)||maximumDistance<2||maximumDistance>32||!(maximumStep>=.5&&maximumStep<=2)||
        !std::isfinite(distance)||distance>maximumDistance*maximumDistance) return false;
    const double scale=distance>maximumStep*maximumStep?maximumStep/std::sqrt(distance):1;
    for(int i=0;i<3;++i) out[i]=static_cast<float>(current[i]+(converted[i]-current[i])*scale);
    out[3]=1;return true;
}
void ApplyPilot(const Reader& reader,std::uintptr_t base,const Sample* sample,
    std::uint32_t world,std::uint32_t epoch,std::uint32_t relocation,void(*log)(const char*));
void RequestPilotRecovery();
}
