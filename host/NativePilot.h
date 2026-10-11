#pragma once
#include "NativeLayout.h"
#include "GameBridge.h"
namespace witchcraft::native {
enum class PilotAck { Idle, Waiting, Confirmed, TimedOut };
// Engine movement may become visible on a later observer callback. Permit only
// one outstanding request and count it only after a subsequent position sample.
class PilotMotion {
    bool pending_=false;
    bool groundFollow_=false;
    float target_[3]{};
    std::uint64_t started_=0;
    double minimumError_=0;
public:
    void Reset() { pending_=false; }
    bool Begin(const float* target,std::uint64_t now,bool groundFollow=false) {
        if(pending_) return false;
        for(int i=0;i<3;++i) if(!std::isfinite(target[i])) return false;
        for(int i=0;i<3;++i) target_[i]=target[i];
        started_=now;pending_=true;groundFollow_=groundFollow;minimumError_=std::numeric_limits<double>::infinity();return true;
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
                if(std::abs(delta)>.5) {pending_=false;return PilotAck::TimedOut;}
            } else error+=delta*delta;
        }
        minimumError_=std::fmin(minimumError_,std::sqrt(error));
        if(now-started_>250) {pending_=false;return PilotAck::TimedOut;}
        if(error<=0.0025) {pending_=false;return PilotAck::Confirmed;}
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
// Play mode catches up using small acknowledged engine steps. Diagnostic calls
// retain their original exact-target bound above.
inline bool PilotFollowPosition(const float* current,const double* mc,float* out) {
    const double converted[]{mc[0],-mc[2],mc[1]};double distance=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(current[i])||!std::isfinite(converted[i])||std::abs(converted[i])>1000000) return false;
        distance+=std::pow(converted[i]-current[i],2);
    }
    if(!std::isfinite(distance)||distance>4) return false;
    const double scale=distance>.25?.5/std::sqrt(distance):1;
    for(int i=0;i<3;++i) out[i]=static_cast<float>(current[i]+(converted[i]-current[i])*scale);
    out[3]=1;return true;
}
void ApplyPilot(const Reader& reader,std::uintptr_t base,const Sample* sample,
    std::uint32_t world,std::uint32_t epoch,std::uint32_t relocation,void(*log)(const char*));
}
